// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "core/studio_core.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace studio {

// ---- filesystem helpers --------------------------------------------------------

std::string read_file_bytes(const std::string& path, bool& ok) {
  std::ifstream in(path, std::ios::binary);
  ok = static_cast<bool>(in);
  if (!ok) return {};
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

bool write_file_bytes(const std::string& path, std::string_view bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(out);
}

fs::path make_workdir() {
  static int counter = 0;
  std::error_code ec;
  const fs::path dir = fs::temp_directory_path(ec) / "mcdf-studio" /
                       ("work-" + std::to_string(++counter));
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  return dir;
}

std::string iso8601_now_utc() {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

// ---- keystore ------------------------------------------------------------------

bool add_key_from_file(Keystore& ks, const std::string& path, bool select) {
  for (int i = 0; i < static_cast<int>(ks.keys.size()); ++i) {
    if (ks.keys[i].path == path) {
      if (select) ks.key_idx = i;
      return true;
    }
  }
  for (int i = 0; i < static_cast<int>(ks.enc_keys.size()); ++i) {
    if (ks.enc_keys[i].path == path) {
      if (select) ks.enc_key_idx = i;
      return true;
    }
  }
  bool ok = false;
  const std::string pem = read_file_bytes(path, ok);
  if (!ok) return false;
  if (auto key = mcdf::PrivateKey::from_pem(pem)) {
    const std::string alg = key->jws_algorithm();
    if (!alg.empty()) {
      auto did = key->did_key();
      if (!did) return false;
      ks.keys.push_back(
          {fs::path(path).stem().string(), path, *did, alg, std::move(*key)});
      if (select || ks.key_idx < 0)
        ks.key_idx = static_cast<int>(ks.keys.size()) - 1;
      return true;
    }
  }
  if (auto key = mcdf::EncPrivateKey::from_pem(pem)) {
    auto did = key->did_key();
    if (!did) return false;
    ks.enc_keys.push_back(
        {fs::path(path).stem().string(), path, *did, std::move(*key)});
    if (select || ks.enc_key_idx < 0)
      ks.enc_key_idx = static_cast<int>(ks.enc_keys.size()) - 1;
    return true;
  }
  return false;
}

void scan_keys(Keystore& ks) {
  std::error_code ec;
  fs::create_directories(ks.dir, ec);
  for (const auto& e : fs::directory_iterator(ks.dir, ec)) {
    if (!e.is_regular_file() || e.path().extension() != ".pem") continue;
    add_key_from_file(ks, e.path().string(), false);
  }
}

namespace {

fs::path free_key_path(const fs::path& dir, const char* prefix) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  for (int i = 1;; ++i) {
    fs::path dest = dir / (std::string(prefix) + "-" + std::to_string(i) + ".pem");
    if (!fs::exists(dest, ec)) return dest;
  }
}

}  // namespace

void generate_signing_key(Keystore& ks) {
  auto key = mcdf::PrivateKey::generate_ed25519();
  if (!key) return;
  auto pem = key->to_pem();
  if (!pem) return;
  const fs::path dest = free_key_path(ks.dir, "key");
  if (!write_file_bytes(dest.string(), *pem)) return;
  add_key_from_file(ks, dest.string(), true);
}

void generate_enc_key(Keystore& ks) {
  auto key = mcdf::EncPrivateKey::generate_x25519();
  if (!key) return;
  auto pem = key->to_pem();
  if (!pem) return;
  const fs::path dest = free_key_path(ks.dir, "enc");
  if (!write_file_bytes(dest.string(), *pem)) return;
  add_key_from_file(ks, dest.string(), true);
}

const SigningKey* selected_signing_key(const Keystore& ks) {
  if (ks.key_idx < 0 || ks.key_idx >= static_cast<int>(ks.keys.size()))
    return nullptr;
  const SigningKey& k = ks.keys[ks.key_idx];
  return k.key ? &k : nullptr;
}

const EncKeyEntry* selected_enc_key(const Keystore& ks) {
  if (ks.enc_key_idx < 0 ||
      ks.enc_key_idx >= static_cast<int>(ks.enc_keys.size()))
    return nullptr;
  const EncKeyEntry& k = ks.enc_keys[ks.enc_key_idx];
  return k.key ? &k : nullptr;
}

// ---- integrity (declared early: lifecycle uses reload_manifest) ----------------

void reload_manifest(Doc& d) {
  d.manifest = {};
  d.has_manifest = false;
  d.container_hash.clear();
  d.cur_hash.clear();
  d.extra_files.clear();
  d.mh_disk_rev = -1;
  ++d.disk_rev;
  if (d.workdir.empty()) return;
  auto c = mcdf::open_container(d.workdir);
  if (!c) return;
  if (auto files = (*c)->list()) d.files = *files;  // keep member views fresh
  if (!(*c)->contains("manifest.json")) return;
  if (auto raw = (*c)->read("manifest.json")) {
    if (auto m = mcdf::parse_manifest_json(*raw)) {
      d.manifest = *m;
      d.has_manifest = true;
      d.container_hash = mcdf::sha256_hex(*raw);
    }
  }
}

// ---- lifecycle -----------------------------------------------------------------

void load_from_workdir(Doc& d) {
  auto container = mcdf::open_container(d.workdir);
  if (!container) {
    d.status = "open failed: " + container.error().message;
    d.content.clear();
    d.content_reloaded = true;
    return;
  }
  mcdf::Container& c = **container;
  if (auto files = c.list()) d.files = *files;

  d.content.clear();
  if (c.contains("content.md")) {
    if (auto raw = c.read("content.md")) {
      d.content = *raw;
      d.has_content = true;
    } else {
      d.status = "read content.md failed: " + raw.error().message;
    }
  }
  d.meta = {};
  d.schema = {};
  d.write_meta = d.write_schema = false;
  if (auto doc = mcdf::load_document(c)) {
    if (doc->has_metadata) {
      d.meta = doc->metadata;
      d.title = doc->metadata.title;
    }
    d.write_meta = doc->has_metadata;
    if (doc->has_schema) {
      d.schema = doc->schema;
      d.doc_type = doc->schema.document_type;
    }
    d.write_schema = doc->has_schema;
    d.heading_count = static_cast<int>(doc->headings.size());
  }
  d.policy.reset();
  d.content_encrypted = false;
  if (c.contains("encryption/policy.yaml")) {
    if (auto raw = c.read("encryption/policy.yaml"))
      if (auto p = mcdf::parse_encryption_policy_yaml(*raw)) {
        d.policy = *p;
        for (const auto& f : p->encrypted_files)
          if (f == "content.md") d.content_encrypted = true;
      }
  }
  d.content_reloaded = true;
  d.verify_summary.clear();
  d.verify_ok = false;
  reload_manifest(d);
  if (d.status.empty()) d.status = "opened " + d.path;
}

namespace {

// Reset everything derived from a previous container.
void reset_views(Doc& d) {
  d.files.clear();
  d.has_content = false;
  d.title.clear();
  d.doc_type.clear();
  d.heading_count = 0;
  d.meta = {};
  d.schema = {};
  d.write_meta = d.write_schema = false;
  d.manifest = {};
  d.has_manifest = false;
  d.container_hash.clear();
  d.verify_summary.clear();
  d.verify_ok = false;
  d.cur_hash.clear();
  d.extra_files.clear();
  d.mh_disk_rev = -1;
  ++d.disk_rev;
  d.sigs.clear();
  d.sig_disk_rev = -1;
  d.policy.reset();
  d.content_encrypted = false;
  d.audit.clear();
  d.audit_chain = {};
  d.audit_cp = {};
  d.audit_disk_rev = -1;
  d.conf_reports.clear();
  d.conf_disk_rev = -1;
  d.status.clear();
}

}  // namespace

void open_archive_into(Doc& d, const std::string& path) {
  std::error_code ec;
  if (d.workdir_is_temp && !d.workdir.empty()) fs::remove_all(d.workdir, ec);
  d.path = path;
  d.is_archive = true;
  d.archive_path = path;
  reset_views(d);

  bool ok = false;
  const std::string bytes = read_file_bytes(path, ok);
  if (!ok) {
    d.workdir.clear();
    d.workdir_is_temp = false;
    d.status = "cannot read " + path;
    d.content.clear();
    d.content_reloaded = true;
    return;
  }
  const fs::path work = make_workdir();
  if (auto un = mcdf::unpack_archive(bytes, work); !un) {
    fs::remove_all(work, ec);
    d.workdir.clear();
    d.workdir_is_temp = false;
    d.status = "unpack failed: " + un.error().message;
    d.content.clear();
    d.content_reloaded = true;
    return;
  }
  d.workdir = work;
  d.workdir_is_temp = true;
  load_from_workdir(d);
}

void open_folder_into(Doc& d, const std::string& path) {
  d.path = path;
  d.is_archive = false;
  d.archive_path.clear();
  reset_views(d);
  d.workdir = fs::path(path);
  d.workdir_is_temp = false;
  load_from_workdir(d);
}

void init_blank(Doc& d) {
  reset_views(d);
  const fs::path work = make_workdir();
  if (auto dir = mcdf::DirectoryContainer::open(work))
    (void)(*dir)->write("content.md", "");
  // Seed a manifest so the first Save yields a well-formed .mcdf.
  if (auto c = mcdf::open_container(work))
    if (auto m = mcdf::build_manifest(**c))
      if (auto j = mcdf::manifest_to_canonical_json(*m))
        if (auto dir = mcdf::DirectoryContainer::open(work))
          (void)(*dir)->write("manifest.json", *j);
  d.workdir = work;
  d.workdir_is_temp = true;
  d.is_archive = false;
  d.archive_path.clear();
  d.path.clear();  // untitled
  d.has_content = true;
  d.content.clear();
  d.content_reloaded = true;
  reload_manifest(d);
  d.status = "new document - Save to choose a file";
}

void cleanup(Doc& d) {
  if (d.workdir_is_temp && !d.workdir.empty()) {
    std::error_code ec;
    fs::remove_all(d.workdir, ec);
  }
}

// ---- save pipeline -------------------------------------------------------------

bool write_members(Doc& d, const std::string& content, const fs::path& where,
                   std::string& err, bool ensure_manifest) {
  auto dir = mcdf::DirectoryContainer::open(where);
  if (!dir) {
    err = dir.error().message;
    return false;
  }
  // Members encrypted at rest must never be overwritten from the (stale or
  // ciphertext-holding) UI models - decrypt first.
  const auto is_encrypted = [&d](std::string_view f) {
    if (!d.policy) return false;
    for (const auto& e : d.policy->encrypted_files)
      if (e == f) return true;
    return false;
  };
  if (!is_encrypted("content.md")) {
    if (auto w = (*dir)->write("content.md", content); !w) {
      err = w.error().message;
      return false;
    }
  }
  if (d.write_meta && !is_encrypted("metadata.yaml")) {
    if (auto w = (*dir)->write("metadata.yaml", mcdf::metadata_to_yaml(d.meta)); !w) {
      err = w.error().message;
      return false;
    }
  }
  if (d.write_schema && !is_encrypted("schema.yaml")) {
    if (auto w = (*dir)->write("schema.yaml", mcdf::schema_to_yaml(d.schema)); !w) {
      err = w.error().message;
      return false;
    }
  }
  if (ensure_manifest || (*dir)->contains("manifest.json")) {
    if (auto container = mcdf::open_container(where))
      if (auto m = mcdf::build_manifest(**container))
        if (auto json = mcdf::manifest_to_canonical_json(*m))
          (void)(*dir)->write("manifest.json", *json);
  }
  return true;
}

bool flush_working_copy(Doc& d, const std::string& content,
                        bool ensure_manifest) {
  std::string err;
  if (!write_members(d, content, d.workdir, err, ensure_manifest)) {
    d.status = "save failed: " + err;
    return false;
  }
  reload_manifest(d);
  return true;
}

SaveResult save(Doc& d, const std::string& content) {
  if (d.workdir.empty()) return SaveResult::kFailed;
  if (d.path.empty()) return SaveResult::kNeedsPath;
  if (!flush_working_copy(d, content)) return SaveResult::kFailed;

  if (d.is_archive && !d.archive_path.empty()) {
    auto container = mcdf::open_container(d.workdir);
    if (!container) {
      d.status = "save failed: " + container.error().message;
      return SaveResult::kFailed;
    }
    auto archive = mcdf::pack_container(**container);
    if (!archive) {
      d.status = "save failed: " + archive.error().message;
      return SaveResult::kFailed;
    }
    if (!write_file_bytes(d.archive_path, *archive)) {
      d.status = "save failed: cannot write " + d.archive_path;
      return SaveResult::kFailed;
    }
    d.status = "saved " + d.archive_path + " (" +
               std::to_string(archive->size()) + " bytes)";
  } else {
    d.status = "saved content.md";
  }
  return SaveResult::kSaved;
}

bool save_as(Doc& d, const std::string& content, std::string path) {
  if (d.workdir.empty()) return false;
  if (path.size() < 5 || path.substr(path.size() - 5) != ".mcdf")
    path += ".mcdf";

  // Stage into a temp copy: in folder mode d.workdir is the user's real
  // folder, which Save As must not modify.
  std::error_code ec;
  const fs::path tmp = make_workdir();
  fs::copy(d.workdir, tmp,
           fs::copy_options::recursive | fs::copy_options::overwrite_existing,
           ec);
  std::string err;
  if (!write_members(d, content, tmp, err)) {
    fs::remove_all(tmp, ec);
    d.status = "save-as failed: " + err;
    return false;
  }
  bool ok = false;
  if (auto c = mcdf::open_container(tmp))
    if (auto archive = mcdf::pack_container(**c))
      ok = write_file_bytes(path, *archive);
  fs::remove_all(tmp, ec);

  if (!ok) {
    d.status = "save-as failed: " + path;
    return false;
  }
  open_archive_into(d, path);  // this document now points at the new .mcdf
  return true;
}

// ---- integrity -----------------------------------------------------------------

bool refresh_disk_hashes(Doc& d) {
  if (!d.has_manifest || d.mh_disk_rev == d.disk_rev) return false;
  d.mh_disk_rev = d.disk_rev;
  d.cur_hash.clear();
  d.extra_files.clear();
  const std::string alg =
      d.manifest.hash_algorithm.empty() ? "sha256" : d.manifest.hash_algorithm;
  if (auto c = mcdf::open_container(d.workdir)) {
    if (auto files = (*c)->list()) {
      for (const auto& f : *files) {
        if (mcdf::is_manifest_excluded(f)) continue;
        if (auto raw = (*c)->read(f))
          if (auto h = mcdf::hash_hex(alg, *raw)) d.cur_hash[f] = *h;
        if (!d.manifest.files.contains(f)) d.extra_files.push_back(f);
      }
    }
  }
  return true;
}

void update_live_content_hash(Doc& d, const std::string& content) {
  if (!d.has_manifest || d.content_encrypted) return;
  const std::string alg =
      d.manifest.hash_algorithm.empty() ? "sha256" : d.manifest.hash_algorithm;
  if (auto h = mcdf::hash_hex(alg, content)) d.cur_hash["content.md"] = *h;
}

void update_live_props_hash(Doc& d) {
  if (!d.has_manifest) return;
  const std::string alg =
      d.manifest.hash_algorithm.empty() ? "sha256" : d.manifest.hash_algorithm;
  if (d.write_meta)
    if (auto h = mcdf::hash_hex(alg, mcdf::metadata_to_yaml(d.meta)))
      d.cur_hash["metadata.yaml"] = *h;
  if (d.write_schema)
    if (auto h = mcdf::hash_hex(alg, mcdf::schema_to_yaml(d.schema)))
      d.cur_hash["schema.yaml"] = *h;
}

DriftCounts manifest_drift(const Doc& d) {
  DriftCounts c;
  for (const auto& [path, hash] : d.manifest.files) {
    const auto it = d.cur_hash.find(path);
    if (it == d.cur_hash.end()) ++c.missing;
    else if (it->second != hash) ++c.modified;
  }
  c.extra = static_cast<int>(d.extra_files.size());
  return c;
}

bool rebuild_manifest(Doc& d, const std::string& content) {
  if (d.workdir.empty()) return false;
  if (!flush_working_copy(d, content, /*ensure_manifest=*/true)) return false;
  d.verify_summary.clear();
  d.status = d.is_archive ? "manifest rebuilt (Save to write the .mcdf)"
                          : "manifest rebuilt";
  return true;
}

void run_verify(Doc& d) {
  d.verify_ok = false;
  auto c = mcdf::open_container(d.workdir);
  if (!c) {
    d.verify_summary = "verify failed: " + c.error().message;
    return;
  }
  auto v = mcdf::verify_manifest(**c, d.manifest);
  if (!v) {
    d.verify_summary = "verify failed: " + v.error().message;
    return;
  }
  if (v->ok) {
    d.verify_ok = true;
    d.verify_summary =
        "OK - " + std::to_string(d.manifest.files.size()) + " file(s) match";
  } else {
    std::string s;
    const auto part = [&s](std::size_t n, const char* what) {
      if (!n) return;
      if (!s.empty()) s += ", ";
      s += std::to_string(n) + " " + what;
    };
    part(v->mismatched.size(), "mismatched");
    part(v->missing.size(), "missing");
    part(v->extra.size(), "unlisted");
    d.verify_summary = "FAILED - " + s;
  }
}

// ---- trust ---------------------------------------------------------------------

void refresh_signatures(Doc& d) {
  if (d.sig_disk_rev == d.disk_rev) return;
  d.sig_disk_rev = d.disk_rev;
  d.sigs.clear();
  if (d.workdir.empty()) return;
  if (auto c = mcdf::open_container(d.workdir))
    if (auto checks = mcdf::verify_container(**c)) d.sigs = std::move(*checks);
}

bool sign_document(Doc& d, const std::string& content, const SigningKey& k,
                   const std::string& sig_name) {
  if (!k.key || d.workdir.empty()) return false;
  if (!flush_working_copy(d, content, /*ensure_manifest=*/true)) return false;

  auto c = mcdf::open_container(d.workdir);
  if (!c) {
    d.status = "sign failed: " + c.error().message;
    return false;
  }
  auto jws = mcdf::sign_container(**c, *k.key, k.did);
  if (!jws) {
    d.status = "sign failed: " + jws.error().message;
    return false;
  }
  std::string name;
  for (char ch : sig_name)
    if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_')
      name += ch;
  if (name.empty()) name = "author";
  auto dir = mcdf::DirectoryContainer::open(d.workdir);
  if (!dir) {
    d.status = "sign failed: " + dir.error().message;
    return false;
  }
  const std::string sig_path = "signatures/" + name + ".sig";
  if (auto w = (*dir)->write(sig_path, *jws); !w) {
    d.status = "sign failed: " + w.error().message;
    return false;
  }
  reload_manifest(d);  // new signature on disk -> refresh members + re-verify
  d.verify_summary.clear();
  d.status = "signed by " + k.did + " -> " + sig_path +
             (d.is_archive ? " (Save to write the .mcdf)" : "");
  return true;
}

void remove_signature(Doc& d, const std::string& file) {
  auto dir = mcdf::DirectoryContainer::open(d.workdir);
  if (!dir) return;
  if (auto r = (*dir)->remove(file); !r) {
    d.status = "remove failed: " + r.error().message;
    return;
  }
  reload_manifest(d);
  d.status = "removed " + file;
}

// ---- encryption ----------------------------------------------------------------

bool encrypt_document(Doc& d, const std::string& content,
                      const std::vector<std::string>& recipient_dids,
                      std::vector<std::string> files) {
  if (recipient_dids.empty()) {
    d.status = "encrypt: add at least one recipient DID";
    return false;
  }
  std::vector<mcdf::EncPublicKey> recipients;
  for (const auto& did : recipient_dids) {
    auto pk = mcdf::EncPublicKey::from_did_key(did);
    if (!pk) {
      d.status = "encrypt failed: " + pk.error().message;
      return false;
    }
    recipients.push_back(*pk);
  }
  if (!flush_working_copy(d, content, /*ensure_manifest=*/true)) return false;
  auto dir = mcdf::DirectoryContainer::open(d.workdir);
  if (!dir) {
    d.status = "encrypt failed: " + dir.error().message;
    return false;
  }
  if (files.empty()) files.push_back("content.md");
  if (auto r = mcdf::encrypt_container(**dir, files, recipients); !r) {
    d.status = "encrypt failed: " + r.error().message;
    return false;
  }
  const std::string note = d.is_archive ? " (Save to write the .mcdf)" : "";
  load_from_workdir(d);  // ciphertext on disk, policy present
  d.status = "encrypted " + std::to_string(files.size()) + " file(s) for " +
             std::to_string(recipients.size()) + " recipient(s)" + note;
  return true;
}

bool decrypt_document(Doc& d, const EncKeyEntry& k) {
  if (!k.key) return false;
  auto dir = mcdf::DirectoryContainer::open(d.workdir);
  if (!dir) {
    d.status = "decrypt failed: " + dir.error().message;
    return false;
  }
  if (auto r = mcdf::decrypt_container(**dir, *k.key); !r) {
    d.status = "decrypt failed: " + r.error().message;
    return false;
  }
  const std::string note = d.is_archive ? " (Save to write the .mcdf)" : "";
  load_from_workdir(d);
  d.status = "decrypted" + note;
  return true;
}

// ---- audit ---------------------------------------------------------------------

void refresh_audit(Doc& d) {
  if (d.audit_disk_rev == d.disk_rev) return;
  d.audit_disk_rev = d.disk_rev;
  d.audit.clear();
  d.audit_chain = {};
  d.audit_cp = {};
  if (d.workdir.empty()) return;
  auto c = mcdf::open_container(d.workdir);
  if (!c) return;
  if (auto e = mcdf::read_audit_log(**c)) d.audit = std::move(*e);
  if (auto v = mcdf::audit_verify(**c)) d.audit_chain = *v;
  if (auto cp = mcdf::audit_verify_checkpoint(**c)) d.audit_cp = *cp;
}

void audit_append_entry(Doc& d, const std::string& action,
                        const std::string& actor) {
  auto dir = mcdf::DirectoryContainer::open(d.workdir);
  if (!dir) return;
  if (auto r = mcdf::audit_append(**dir, action, actor, iso8601_now_utc());
      !r) {
    d.status = "audit append failed: " + r.error().message;
    return;
  }
  reload_manifest(d);
  d.status = "audit: appended " + action;
}

void audit_make_checkpoint(Doc& d, const SigningKey& k) {
  if (!k.key) return;
  auto dir = mcdf::DirectoryContainer::open(d.workdir);
  if (!dir) return;
  if (auto r = mcdf::audit_checkpoint(**dir, *k.key, k.did); !r) {
    d.status = "checkpoint failed: " + r.error().message;
    return;
  }
  reload_manifest(d);
  d.status = "audit checkpoint signed by " + k.did;
}

// ---- conformance + render ------------------------------------------------------

void refresh_conformance(Doc& d) {
  if (d.conf_disk_rev == d.disk_rev) return;
  d.conf_disk_rev = d.disk_rev;
  d.conf_reports.clear();
  if (d.workdir.empty()) return;
  auto c = mcdf::open_container(d.workdir);
  if (!c) return;
  auto doc = mcdf::load_document(**c);
  if (!doc) return;
  using P = mcdf::Profile;
  for (P p : {P::kCore, P::kIntegrity, P::kSigned, P::kEncrypted, P::kRender})
    if (auto r = mcdf::validate(**c, *doc, p)) d.conf_reports.push_back(*r);
}

bool export_render(Doc& d, const std::string& content, mcdf::RenderFormat fmt,
                   std::string path) {
  if (!flush_working_copy(d, content)) return false;  // render what the user sees
  auto c = mcdf::open_container(d.workdir);
  if (!c) {
    d.status = "render failed: " + c.error().message;
    return false;
  }
  auto out = mcdf::render(**c, fmt);
  if (!out) {
    d.status = "render failed: " + out.error().message;
    return false;
  }
  const std::string ext = fmt == mcdf::RenderFormat::kHtml ? ".html" : ".txt";
  if (path.size() < ext.size() ||
      path.compare(path.size() - ext.size(), ext.size(), ext) != 0)
    path += ext;
  if (!write_file_bytes(path, *out)) {
    d.status = "render failed: cannot write " + path;
    return false;
  }
  d.status = "canonical render -> " + path;
  return true;
}

}  // namespace studio
