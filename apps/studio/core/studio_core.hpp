// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
//
// MCDF Studio's document engine, free of any UI dependency. Everything the
// app does to a document - open/unpack, flush members, rebuild the manifest,
// sign/verify, encrypt/decrypt, audit, conformance, canonical render - lives
// here so it can be driven headlessly by tests (plan 04 §10) and, later, from
// a worker pool. The ImGui layer owns only view state (editor buffer, caches,
// panel visibility) and passes the live content in.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <mcdf/mcdf.hpp>

namespace studio {

namespace fs = std::filesystem;

// ---- filesystem helpers --------------------------------------------------------

std::string read_file_bytes(const std::string& path, bool& ok);
bool write_file_bytes(const std::string& path, std::string_view bytes);
fs::path make_workdir();               // fresh temp working-copy directory
std::string iso8601_now_utc();         // RFC 3339 "now" (UTC, seconds)

// ---- document state ------------------------------------------------------------

// A document's non-UI state. `content` is content.md as last read from the
// working copy; ops that change it on disk set `content_reloaded` so the UI
// knows to re-seed its editor.
struct Doc {
  std::string path;         // .mcdf file or folder shown to the user ("" = untitled)
  std::string status;
  std::vector<std::string> files;
  bool has_content = false;
  bool is_archive = false;  // opened from a .mcdf file
  std::string title;        // metadata.title
  std::string doc_type;     // schema.document_type
  int heading_count = 0;

  std::string content;           // content.md as on disk
  bool content_reloaded = false; // UI must re-seed the editor, then clear

  fs::path workdir;
  bool workdir_is_temp = false;
  std::string archive_path;  // .mcdf to save back to (empty in folder mode)

  // Editable metadata.yaml / schema.yaml models.
  mcdf::Metadata meta;
  mcdf::Schema schema;
  bool write_meta = false;    // member exists or was edited -> written on save
  bool write_schema = false;

  // Manifest view: manifest.json as stored + current hashes (disk members
  // cached per disk_rev; live members overlaid via the update_live_* calls).
  mcdf::Manifest manifest;
  bool has_manifest = false;
  std::string container_hash;  // sha256 of manifest.json as stored
  std::string verify_summary;  // last engine verify_manifest result
  bool verify_ok = false;
  int disk_rev = 0;            // bumped whenever the working copy changes
  int mh_disk_rev = -1;
  std::unordered_map<std::string, std::string> cur_hash;
  std::vector<std::string> extra_files;

  // Signature checks over the working copy.
  std::vector<mcdf::SignatureCheck> sigs;
  int sig_disk_rev = -1;

  // Encryption state (encryption/policy.yaml).
  std::optional<mcdf::EncryptionPolicy> policy;
  bool content_encrypted = false;

  // Audit timeline + chain/checkpoint status.
  std::vector<mcdf::AuditEntry> audit;
  mcdf::AuditVerification audit_chain;
  mcdf::CheckpointResult audit_cp;
  int audit_disk_rev = -1;

  // Conformance reports, one per profile.
  std::vector<mcdf::ValidationReport> conf_reports;
  int conf_disk_rev = -1;
};

// ---- keystore ------------------------------------------------------------------

struct SigningKey {
  std::string name;  // display name (file stem)
  std::string path;  // PEM file path
  std::string did;   // did:key of the public half
  std::string alg;   // "EdDSA" | "ES256"
  std::optional<mcdf::PrivateKey> key;
};

struct EncKeyEntry {
  std::string name;
  std::string path;
  std::string did;  // did:key of the public half (the recipient id)
  std::optional<mcdf::EncPrivateKey> key;
};

struct Keystore {
  fs::path dir;  // where generated PEMs go and scan() looks
  std::vector<SigningKey> keys;
  int key_idx = -1;
  std::vector<EncKeyEntry> enc_keys;
  int enc_key_idx = -1;
};

// Loads a PEM into the right list (Ed25519/P-256 -> signing, X25519 -> HPKE),
// deduped by path. Returns false for unreadable/unusable files.
bool add_key_from_file(Keystore& ks, const std::string& path, bool select);
void scan_keys(Keystore& ks);
void generate_signing_key(Keystore& ks);  // Ed25519 -> key-<n>.pem, selected
void generate_enc_key(Keystore& ks);      // X25519 -> enc-<n>.pem, selected
const SigningKey* selected_signing_key(const Keystore& ks);
const EncKeyEntry* selected_enc_key(const Keystore& ks);

// ---- lifecycle -----------------------------------------------------------------

// Re-reads every member view (content, metadata/schema models, policy,
// manifest, member list) from d.workdir. Sets content_reloaded.
void load_from_workdir(Doc& d);

// Point the document at a .mcdf file: unpack to a fresh temp working copy.
void open_archive_into(Doc& d, const std::string& path);

// Open an unpacked folder in place (the folder IS the working copy).
void open_folder_into(Doc& d, const std::string& path);

// A new, untitled document backed by a temp working copy with an empty
// content.md and a seed manifest.
void init_blank(Doc& d);

// Removes the temp working copy (call when the document closes).
void cleanup(Doc& d);

// ---- save pipeline -------------------------------------------------------------

// Writes the editable members (content.md unless encrypted, metadata.yaml,
// schema.yaml) into `where` and keeps manifest.json in sync (created when
// ensure_manifest). YAML bytes come from libmcdf's writers.
bool write_members(Doc& d, const std::string& content, const fs::path& where,
                   std::string& err, bool ensure_manifest = false);

// write_members into d.workdir + refresh the manifest view. Sets d.status on
// failure.
bool flush_working_copy(Doc& d, const std::string& content,
                        bool ensure_manifest = false);

enum class SaveResult { kSaved, kNeedsPath, kFailed };

// Flush, and in archive mode re-pack to d.archive_path. kNeedsPath for an
// untitled document (caller runs a Save As flow).
SaveResult save(Doc& d, const std::string& content);

// Stage into a temp copy (folder mode must not mutate the user's folder),
// pack to `path` (".mcdf" appended if missing), then re-point d at the new
// archive. Returns false on failure (d untouched).
bool save_as(Doc& d, const std::string& content, std::string path);

// ---- integrity -----------------------------------------------------------------

struct DriftCounts {
  int modified = 0, missing = 0, extra = 0;
  bool any() const { return modified || missing || extra; }
};

// Re-read manifest.json + the member list; invalidates the hash caches and
// bumps disk_rev. Called by every op that touches the working copy.
void reload_manifest(Doc& d);

// Hash the on-disk members once per disk change. Returns true if it rescanned
// (the caller should re-apply its live overlays).
bool refresh_disk_hashes(Doc& d);

// Overlay live (unsaved) state onto cur_hash: the editor buffer for
// content.md; the form models for metadata.yaml / schema.yaml.
void update_live_content_hash(Doc& d, const std::string& content);
void update_live_props_hash(Doc& d);

// Divergence between the manifest and the overlaid current hashes.
DriftCounts manifest_drift(const Doc& d);

// Flush + rebuild manifest.json in the working copy (created if absent).
// Does NOT repack an archive - save() does that.
bool rebuild_manifest(Doc& d, const std::string& content);

// Engine verify_manifest over the working copy on disk -> verify_summary.
void run_verify(Doc& d);

// ---- trust ---------------------------------------------------------------------

void refresh_signatures(Doc& d);  // verify_container, cached per disk state

// Flush + rebuild manifest, sign its canonical bytes with `key` ->
// signatures/<sig_name>.sig (name sanitized; "author" if empty).
bool sign_document(Doc& d, const std::string& content, const SigningKey& key,
                   const std::string& sig_name);

void remove_signature(Doc& d, const std::string& file);

// ---- encryption ----------------------------------------------------------------

// Flush, then AES-256-GCM the files in place with the CEK HPKE-wrapped to
// each recipient DID; policy.yaml written, manifest rebuilt. Reloads on
// success (content becomes ciphertext; content_reloaded set).
bool encrypt_document(Doc& d, const std::string& content,
                      const std::vector<std::string>& recipient_dids,
                      std::vector<std::string> files);

// Reverse with a recipient's X25519 key; reloads on success.
bool decrypt_document(Doc& d, const EncKeyEntry& key);

// ---- audit ---------------------------------------------------------------------

void refresh_audit(Doc& d);  // entries + chain + checkpoint, cached per disk state
void audit_append_entry(Doc& d, const std::string& action,
                        const std::string& actor);
void audit_make_checkpoint(Doc& d, const SigningKey& key);

// ---- conformance + render ------------------------------------------------------

void refresh_conformance(Doc& d);  // all profiles, cached per disk state

// Flush, canonical engine render (provenance stamp included), write to
// `path` (extension appended if missing).
bool export_render(Doc& d, const std::string& content, mcdf::RenderFormat fmt,
                   std::string path);

}  // namespace studio
