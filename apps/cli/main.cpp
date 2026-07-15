// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include <mcdf/mcdf.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace nl = nlohmann;

namespace {

void print_usage() {
  std::cout <<
      "mcdf - Markdown Container Document Format tool\n"
      "\n"
      "Usage:\n"
      "  mcdf <command> [options]\n"
      "\n"
      "Commands:\n"
      "  inspect  <container> [--json]         Summarize a container\n"
      "  manifest <container> [--verify]       Build, or --verify, the manifest\n"
      "  validate <container> [--profile P]    Validate (P: core|integrity|\n"
      "                                        signed|encrypted|render)\n"
      "  keygen   --out <key.pem> [--type T]   Generate a key (T: ed25519|x25519)\n"
      "  sign     <container> --key <pem>      Sign the manifest (--name <n>)\n"
      "  verify   <container>                  Verify manifest + signatures\n"
      "  pack     <container> -o <file.mcdf>   Pack into a single-file archive\n"
      "  unpack   <file.mcdf> -o <directory>   Unpack an archive to a directory\n"
      "  encrypt  <container> --recipient <did>  Encrypt files (--file <p>)\n"
      "  decrypt  <container> --key <x25519.pem> Decrypt files\n"
      "\n"
      "(inspect/validate/verify/manifest also accept a .mcdf file)\n"
      "\n"
      "Global:\n"
      "  -h, --help                            Show this help\n"
      "  -v, --version                         Show version\n"
      "\n"
      "Planned: audit render\n";
}

void print_human(const std::string& path, const std::string& format,
                 const mcdf::Container& c, const mcdf::Document& d) {
  std::cout << "MCDF container: " << path << "\n";
  std::cout << "  format: " << format << "\n";
  if (auto files = c.list()) {
    std::cout << "  files:  " << files->size() << "\n";
    for (const auto& f : *files) std::cout << "    - " << f << "\n";
  }

  std::cout << "\nMetadata:\n";
  if (d.has_metadata) {
    std::cout << "  title:          " << d.metadata.title << "\n";
    std::cout << "  version:        " << d.metadata.version << "\n";
    std::cout << "  classification: " << d.metadata.classification << "\n";
    std::cout << "  language:       " << d.metadata.language << "\n";
    if (!d.metadata.authors.empty()) {
      std::cout << "  authors:        ";
      for (std::size_t i = 0; i < d.metadata.authors.size(); ++i) {
        if (i) std::cout << ", ";
        std::cout << d.metadata.authors[i].name;
        if (!d.metadata.authors[i].id.empty())
          std::cout << " <" << d.metadata.authors[i].id << ">";
      }
      std::cout << "\n";
    }
    if (d.metadata.generated_by)
      std::cout << "  generated_by:   " << *d.metadata.generated_by << "\n";
  } else {
    std::cout << "  (no metadata.yaml)\n";
  }

  std::cout << "\nSchema:\n";
  if (d.has_schema) {
    std::cout << "  document_type: " << d.schema.document_type << "\n";
    std::cout << "  sections (" << d.schema.sections.size() << "):\n";
    for (const auto& s : d.schema.sections) {
      std::cout << "    - " << (s.required ? "[required] " : "           ")
                << s.id;
      if (!s.title.empty()) std::cout << " - " << s.title;
      std::cout << "\n";
    }
  } else {
    std::cout << "  (no schema.yaml)\n";
  }

  std::cout << "\nContent headings (" << d.headings.size() << "):\n";
  for (const auto& h : d.headings) {
    std::cout << "  " << std::string(static_cast<std::size_t>(h.level), '#')
              << " " << h.text;
    if (!h.id.empty()) std::cout << "  {#" << h.id << "}";
    std::cout << "\n";
  }

  std::cout << "\nManifest:\n";
  if (d.has_manifest) {
    std::cout << "  mcdf_version:   " << d.manifest.mcdf_version << "\n";
    std::cout << "  hash_algorithm: " << d.manifest.hash_algorithm << "\n";
    std::cout << "  files:          " << d.manifest.files.size() << "\n";
  } else {
    std::cout << "  (no manifest.json)\n";
  }
}

void print_json(const std::string& format, const mcdf::Container& c,
                const mcdf::Document& d) {
  nl::json j;
  j["format"] = format;
  if (auto files = c.list()) j["files"] = *files;

  if (d.has_metadata) {
    j["metadata"] = {
        {"title", d.metadata.title},
        {"version", d.metadata.version},
        {"classification", d.metadata.classification},
        {"language", d.metadata.language},
        {"created_at", d.metadata.created_at},
    };
    for (const auto& a : d.metadata.authors)
      j["metadata"]["authors"].push_back({{"name", a.name}, {"id", a.id}});
    if (d.metadata.generated_by)
      j["metadata"]["generated_by"] = *d.metadata.generated_by;
  }

  if (d.has_schema) {
    j["schema"]["document_type"] = d.schema.document_type;
    for (const auto& s : d.schema.sections)
      j["schema"]["sections"].push_back(
          {{"id", s.id}, {"title", s.title}, {"required", s.required}});
  }

  for (const auto& h : d.headings)
    j["headings"].push_back(
        {{"level", h.level}, {"text", h.text}, {"id", h.id}});

  if (d.has_manifest) {
    j["manifest"] = {
        {"mcdf_version", d.manifest.mcdf_version},
        {"hash_algorithm", d.manifest.hash_algorithm},
        {"file_count", d.manifest.files.size()},
    };
  }

  std::cout << j.dump(2) << "\n";
}

int cmd_inspect(const std::vector<std::string>& args) {
  std::string path;
  bool json = false;
  for (std::size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "--json") {
      json = true;
    } else if (!args[i].empty() && args[i][0] == '-') {
      std::cerr << "unknown option: " << args[i] << "\n";
      return 2;
    } else if (path.empty()) {
      path = args[i];
    }
  }
  if (path.empty()) {
    std::cerr << "usage: mcdf inspect <container> [--json]\n";
    return 2;
  }

  auto container = mcdf::open_container(path);
  if (!container) {
    std::cerr << "error: " << container.error().message << "\n";
    return 1;
  }
  auto doc = mcdf::load_document(**container);
  if (!doc) {
    std::cerr << "error: " << doc.error().message << "\n";
    return 1;
  }

  const std::string format =
      std::filesystem::is_directory(path) ? "directory" : "archive";
  if (json)
    print_json(format, **container, *doc);
  else
    print_human(path, format, **container, *doc);
  return 0;
}

int cmd_manifest(const std::vector<std::string>& args) {
  std::string path;
  bool verify = false;
  for (std::size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "--verify") {
      verify = true;
    } else if (!args[i].empty() && args[i][0] == '-') {
      std::cerr << "unknown option: " << args[i] << "\n";
      return 2;
    } else if (path.empty()) {
      path = args[i];
    }
  }
  if (path.empty()) {
    std::cerr << "usage: mcdf manifest <container> [--verify]\n";
    return 2;
  }

  auto container = mcdf::open_container(path);
  if (!container) {
    std::cerr << "error: " << container.error().message << "\n";
    return 1;
  }

  if (!verify) {
    auto manifest = mcdf::build_manifest(**container);
    if (!manifest) {
      std::cerr << "error: " << manifest.error().message << "\n";
      return 1;
    }
    auto canonical = mcdf::manifest_to_canonical_json(*manifest);
    if (!canonical) {
      std::cerr << "error: " << canonical.error().message << "\n";
      return 1;
    }
    std::cout << *canonical << "\n";
    return 0;
  }

  if (!(*container)->contains("manifest.json")) {
    std::cerr << "error: manifest.json not found in container\n";
    return 1;
  }
  auto raw = (*container)->read("manifest.json");
  if (!raw) {
    std::cerr << "error: " << raw.error().message << "\n";
    return 1;
  }
  auto manifest = mcdf::parse_manifest_json(*raw);
  if (!manifest) {
    std::cerr << "error: " << manifest.error().message << "\n";
    return 1;
  }
  auto result = mcdf::verify_manifest(**container, *manifest);
  if (!result) {
    std::cerr << "error: " << result.error().message << "\n";
    return 1;
  }
  if (result->ok) {
    std::cout << "manifest OK (" << manifest->files.size() << " files, "
              << manifest->hash_algorithm << ")\n";
    return 0;
  }
  std::cout << "manifest FAILED\n";
  for (const auto& p : result->mismatched) std::cout << "  mismatch: " << p << "\n";
  for (const auto& p : result->missing)    std::cout << "  missing:  " << p << "\n";
  for (const auto& p : result->extra)      std::cout << "  extra:    " << p << "\n";
  return 1;
}

int cmd_validate(const std::vector<std::string>& args) {
  std::string path;
  std::string profile_name = "integrity";
  for (std::size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "--profile") {
      if (i + 1 >= args.size()) {
        std::cerr << "--profile requires a value\n";
        return 2;
      }
      profile_name = args[++i];
    } else if (!args[i].empty() && args[i][0] == '-') {
      std::cerr << "unknown option: " << args[i] << "\n";
      return 2;
    } else if (path.empty()) {
      path = args[i];
    }
  }
  if (path.empty()) {
    std::cerr << "usage: mcdf validate <container> "
                 "[--profile core|integrity|signed|encrypted|render]\n";
    return 2;
  }

  auto profile = mcdf::parse_profile(profile_name);
  if (!profile) {
    std::cerr << "error: " << profile.error().message << "\n";
    return 2;
  }
  auto container = mcdf::open_container(path);
  if (!container) {
    std::cerr << "error: " << container.error().message << "\n";
    return 1;
  }
  auto doc = mcdf::load_document(**container);
  if (!doc) {
    std::cerr << "error: " << doc.error().message << "\n";
    return 1;
  }
  auto report = mcdf::validate(**container, *doc, *profile);
  if (!report) {
    std::cerr << "error: " << report.error().message << "\n";
    return 1;
  }

  if (report->ok) {
    std::cout << "validate [" << mcdf::to_string(report->profile) << "]: OK\n";
    return 0;
  }
  std::cout << "validate [" << mcdf::to_string(report->profile) << "]: "
            << report->issues.size() << " issue(s)\n";
  for (const auto& issue : report->issues)
    std::cout << "  " << issue.code << ": " << issue.message << "\n";
  return 1;
}

std::string read_file(const std::string& path, bool& ok) {
  std::ifstream in(path, std::ios::binary);
  ok = static_cast<bool>(in);
  if (!ok) return {};
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

int cmd_keygen(const std::vector<std::string>& args) {
  std::string out, type = "ed25519";
  for (std::size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "--out") {
      if (i + 1 >= args.size()) { std::cerr << "--out requires a value\n"; return 2; }
      out = args[++i];
    } else if (args[i] == "--type") {
      if (i + 1 >= args.size()) { std::cerr << "--type requires a value\n"; return 2; }
      type = args[++i];
    } else if (!args[i].empty() && args[i][0] == '-') {
      std::cerr << "unknown option: " << args[i] << "\n";
      return 2;
    }
  }
  if (out.empty()) {
    std::cerr << "usage: mcdf keygen --out <key.pem> [--type ed25519|x25519]\n";
    return 2;
  }

  std::string pem, did;
  if (type == "ed25519") {  // signing key
    auto key = mcdf::PrivateKey::generate_ed25519();
    if (!key) { std::cerr << "error: " << key.error().message << "\n"; return 1; }
    auto p = key->to_pem();
    auto d = key->did_key();
    if (!p) { std::cerr << "error: " << p.error().message << "\n"; return 1; }
    if (!d) { std::cerr << "error: " << d.error().message << "\n"; return 1; }
    pem = *p;
    did = *d;
  } else if (type == "x25519") {  // encryption key
    auto key = mcdf::EncPrivateKey::generate_x25519();
    if (!key) { std::cerr << "error: " << key.error().message << "\n"; return 1; }
    auto p = key->to_pem();
    auto d = key->did_key();
    if (!p) { std::cerr << "error: " << p.error().message << "\n"; return 1; }
    if (!d) { std::cerr << "error: " << d.error().message << "\n"; return 1; }
    pem = *p;
    did = *d;
  } else {
    std::cerr << "error: unknown key type '" << type << "' (ed25519|x25519)\n";
    return 2;
  }

  std::ofstream f(out, std::ios::binary | std::ios::trunc);
  if (!f) { std::cerr << "error: cannot write " << out << "\n"; return 1; }
  f << pem;
  f.close();
  std::cout << did << "\n";
  return 0;
}

int cmd_encrypt(const std::vector<std::string>& args) {
  std::string path;
  std::vector<std::string> recipient_dids, files;
  for (std::size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "--recipient") {
      if (i + 1 >= args.size()) { std::cerr << "--recipient requires a value\n"; return 2; }
      recipient_dids.push_back(args[++i]);
    } else if (args[i] == "--file") {
      if (i + 1 >= args.size()) { std::cerr << "--file requires a value\n"; return 2; }
      files.push_back(args[++i]);
    } else if (!args[i].empty() && args[i][0] == '-') {
      std::cerr << "unknown option: " << args[i] << "\n";
      return 2;
    } else if (path.empty()) {
      path = args[i];
    }
  }
  if (path.empty() || recipient_dids.empty()) {
    std::cerr << "usage: mcdf encrypt <container> --recipient <did> [--file <path>]...\n";
    return 2;
  }
  if (files.empty()) files.push_back("content.md");

  auto container = mcdf::DirectoryContainer::open(path);
  if (!container) { std::cerr << "error: " << container.error().message << "\n"; return 1; }

  std::vector<mcdf::EncPublicKey> recipients;
  for (const auto& did : recipient_dids) {
    auto pk = mcdf::EncPublicKey::from_did_key(did);
    if (!pk) { std::cerr << "error: " << pk.error().message << "\n"; return 1; }
    recipients.push_back(*pk);
  }

  auto result = mcdf::encrypt_container(**container, files, recipients);
  if (!result) { std::cerr << "error: " << result.error().message << "\n"; return 1; }
  std::cout << "encrypted " << files.size() << " file(s) for "
            << recipients.size() << " recipient(s)\n";
  return 0;
}

int cmd_decrypt(const std::vector<std::string>& args) {
  std::string path, keyfile;
  for (std::size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "--key") {
      if (i + 1 >= args.size()) { std::cerr << "--key requires a value\n"; return 2; }
      keyfile = args[++i];
    } else if (!args[i].empty() && args[i][0] == '-') {
      std::cerr << "unknown option: " << args[i] << "\n";
      return 2;
    } else if (path.empty()) {
      path = args[i];
    }
  }
  if (path.empty() || keyfile.empty()) {
    std::cerr << "usage: mcdf decrypt <container> --key <x25519-key.pem>\n";
    return 2;
  }

  auto container = mcdf::DirectoryContainer::open(path);
  if (!container) { std::cerr << "error: " << container.error().message << "\n"; return 1; }

  bool ok = false;
  const std::string pem = read_file(keyfile, ok);
  if (!ok) { std::cerr << "error: cannot read key " << keyfile << "\n"; return 1; }
  auto key = mcdf::EncPrivateKey::from_pem(pem);
  if (!key) { std::cerr << "error: " << key.error().message << "\n"; return 1; }

  auto result = mcdf::decrypt_container(**container, *key);
  if (!result) { std::cerr << "error: " << result.error().message << "\n"; return 1; }
  std::cout << "decrypted\n";
  return 0;
}

int cmd_sign(const std::vector<std::string>& args) {
  std::string path, keyfile, name = "author";
  for (std::size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "--key") {
      if (i + 1 >= args.size()) { std::cerr << "--key requires a value\n"; return 2; }
      keyfile = args[++i];
    } else if (args[i] == "--name") {
      if (i + 1 >= args.size()) { std::cerr << "--name requires a value\n"; return 2; }
      name = args[++i];
    } else if (!args[i].empty() && args[i][0] == '-') {
      std::cerr << "unknown option: " << args[i] << "\n";
      return 2;
    } else if (path.empty()) {
      path = args[i];
    }
  }
  if (path.empty() || keyfile.empty()) {
    std::cerr << "usage: mcdf sign <container> --key <pem> [--name <n>]\n";
    return 2;
  }

  // Signing writes a signature file, so a directory container is required.
  auto container = mcdf::DirectoryContainer::open(path);
  if (!container) { std::cerr << "error: " << container.error().message << "\n"; return 1; }

  // Only sign a container whose manifest matches its content.
  if (!(*container)->contains("manifest.json")) {
    std::cerr << "error: manifest.json not found; run 'mcdf manifest' first\n";
    return 1;
  }
  auto raw = (*container)->read("manifest.json");
  if (!raw) { std::cerr << "error: " << raw.error().message << "\n"; return 1; }
  auto manifest = mcdf::parse_manifest_json(*raw);
  if (!manifest) { std::cerr << "error: " << manifest.error().message << "\n"; return 1; }
  auto integrity = mcdf::verify_manifest(**container, *manifest);
  if (!integrity) { std::cerr << "error: " << integrity.error().message << "\n"; return 1; }
  if (!integrity->ok) {
    std::cerr << "error: manifest does not match content; re-run 'mcdf manifest'\n";
    return 1;
  }

  bool ok = false;
  const std::string pem = read_file(keyfile, ok);
  if (!ok) { std::cerr << "error: cannot read key " << keyfile << "\n"; return 1; }
  auto key = mcdf::PrivateKey::from_pem(pem);
  if (!key) { std::cerr << "error: " << key.error().message << "\n"; return 1; }
  auto kid = key->did_key();
  if (!kid) { std::cerr << "error: " << kid.error().message << "\n"; return 1; }

  auto jws = mcdf::sign_container(**container, *key, *kid);
  if (!jws) { std::cerr << "error: " << jws.error().message << "\n"; return 1; }

  const std::string sig_path = "signatures/" + name + ".sig";
  auto written = (*container)->write(sig_path, *jws);
  if (!written) { std::cerr << "error: " << written.error().message << "\n"; return 1; }

  std::cout << "signed by " << *kid << "\n  -> " << sig_path << "\n";
  return 0;
}

int cmd_verify(const std::vector<std::string>& args) {
  std::string path;
  for (std::size_t i = 1; i < args.size(); ++i) {
    if (!args[i].empty() && args[i][0] == '-') {
      std::cerr << "unknown option: " << args[i] << "\n";
      return 2;
    } else if (path.empty()) {
      path = args[i];
    }
  }
  if (path.empty()) { std::cerr << "usage: mcdf verify <container>\n"; return 2; }

  auto container = mcdf::open_container(path);
  if (!container) { std::cerr << "error: " << container.error().message << "\n"; return 1; }

  bool ok = true;

  if (!(*container)->contains("manifest.json")) {
    std::cout << "manifest: MISSING\n";
    ok = false;
  } else {
    auto raw = (*container)->read("manifest.json");
    if (!raw) { std::cerr << "error: " << raw.error().message << "\n"; return 1; }
    auto manifest = mcdf::parse_manifest_json(*raw);
    if (!manifest) { std::cerr << "error: " << manifest.error().message << "\n"; return 1; }
    auto integrity = mcdf::verify_manifest(**container, *manifest);
    if (!integrity) { std::cerr << "error: " << integrity.error().message << "\n"; return 1; }
    if (integrity->ok) {
      std::cout << "manifest: OK (" << manifest->files.size() << " files)\n";
    } else {
      std::cout << "manifest: FAILED\n";
      ok = false;
      for (const auto& p : integrity->mismatched) std::cout << "  mismatch: " << p << "\n";
      for (const auto& p : integrity->missing)    std::cout << "  missing:  " << p << "\n";
      for (const auto& p : integrity->extra)      std::cout << "  extra:    " << p << "\n";
    }
  }

  auto checks = mcdf::verify_container(**container);
  if (!checks) { std::cerr << "error: " << checks.error().message << "\n"; return 1; }
  if (checks->empty()) {
    std::cout << "signatures: NONE\n";
    ok = false;
  } else {
    for (const auto& c : *checks) {
      if (c.valid) {
        std::cout << "signature " << c.file << ": VALID (" << c.alg << ", "
                  << c.kid << ")\n";
      } else {
        std::cout << "signature " << c.file << ": INVALID";
        if (!c.error.empty()) std::cout << " (" << c.error << ")";
        std::cout << "\n";
        ok = false;
      }
    }
  }

  std::cout << (ok ? "verify: OK\n" : "verify: FAILED\n");
  return ok ? 0 : 1;
}

int cmd_pack(const std::vector<std::string>& args) {
  std::string path, out;
  for (std::size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "-o" || args[i] == "--output") {
      if (i + 1 >= args.size()) { std::cerr << "-o requires a value\n"; return 2; }
      out = args[++i];
    } else if (!args[i].empty() && args[i][0] == '-') {
      std::cerr << "unknown option: " << args[i] << "\n";
      return 2;
    } else if (path.empty()) {
      path = args[i];
    }
  }
  if (path.empty() || out.empty()) {
    std::cerr << "usage: mcdf pack <container> -o <file.mcdf>\n";
    return 2;
  }

  auto container = mcdf::open_container(path);
  if (!container) { std::cerr << "error: " << container.error().message << "\n"; return 1; }
  auto archive = mcdf::pack_container(**container);
  if (!archive) { std::cerr << "error: " << archive.error().message << "\n"; return 1; }

  std::ofstream f(out, std::ios::binary | std::ios::trunc);
  if (!f) { std::cerr << "error: cannot write " << out << "\n"; return 1; }
  f.write(archive->data(), static_cast<std::streamsize>(archive->size()));
  std::cout << "packed -> " << out << " (" << archive->size() << " bytes)\n";
  return 0;
}

int cmd_unpack(const std::vector<std::string>& args) {
  std::string file, out;
  for (std::size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "-o" || args[i] == "--output") {
      if (i + 1 >= args.size()) { std::cerr << "-o requires a value\n"; return 2; }
      out = args[++i];
    } else if (!args[i].empty() && args[i][0] == '-') {
      std::cerr << "unknown option: " << args[i] << "\n";
      return 2;
    } else if (file.empty()) {
      file = args[i];
    }
  }
  if (file.empty() || out.empty()) {
    std::cerr << "usage: mcdf unpack <file.mcdf> -o <directory>\n";
    return 2;
  }

  bool ok = false;
  const std::string bytes = read_file(file, ok);
  if (!ok) { std::cerr << "error: cannot read " << file << "\n"; return 1; }
  auto result = mcdf::unpack_archive(bytes, out);
  if (!result) { std::cerr << "error: " << result.error().message << "\n"; return 1; }
  std::cout << "unpacked -> " << out << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<std::string> args(argv + 1, argv + argc);

  if (args.empty() || args[0] == "-h" || args[0] == "--help") {
    print_usage();
    return 0;
  }
  if (args[0] == "-v" || args[0] == "--version") {
    std::cout << "mcdf " << mcdf::version_string() << "\n";
    return 0;
  }
  if (args[0] == "inspect") {
    return cmd_inspect(args);
  }
  if (args[0] == "manifest") {
    return cmd_manifest(args);
  }
  if (args[0] == "validate") {
    return cmd_validate(args);
  }
  if (args[0] == "keygen") {
    return cmd_keygen(args);
  }
  if (args[0] == "sign") {
    return cmd_sign(args);
  }
  if (args[0] == "verify") {
    return cmd_verify(args);
  }
  if (args[0] == "pack") {
    return cmd_pack(args);
  }
  if (args[0] == "unpack") {
    return cmd_unpack(args);
  }
  if (args[0] == "encrypt") {
    return cmd_encrypt(args);
  }
  if (args[0] == "decrypt") {
    return cmd_decrypt(args);
  }

  std::cerr << "unknown command: " << args[0] << "\n\n";
  print_usage();
  return 2;
}
