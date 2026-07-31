// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#include <mcdf/mcdf.hpp>

#include <cxxopts.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace nl = nlohmann;

namespace {

namespace fs = std::filesystem;

// cxxopts treats argv[0] as the program name and begins parsing at argv[1].
// Our args vector carries the verb at [0], which is exactly where the previous
// hand-rolled loops started (`for i = 1`), so the two shapes line up with no
// adjustment. Parse failures throw; main() maps them to exit code 2.
cxxopts::ParseResult parse_args(cxxopts::Options& opts,
                                const std::vector<std::string>& args) {
  std::vector<const char*> argv;
  argv.reserve(args.size());
  for (const auto& a : args) argv.push_back(a.c_str());
  return opts.parse(static_cast<int>(argv.size()), argv.data());
}

// cxxopts cannot mark a positional as required, so each verb checks its own
// and lands here. Exit code 2 = usage error, as before.
int usage_error(cxxopts::Options& opts, std::string_view message) {
  fmt::print(stderr, "error: {}\n\n{}", message, opts.help());
  return 2;
}

// Every verb accepts -h/--help; this reports whether it was asked for.
bool wants_help(const cxxopts::ParseResult& r, cxxopts::Options& opts) {
  if (!r.count("help")) return false;
  fmt::print("{}", opts.help());
  return true;
}

void print_usage() {
  fmt::print(
      "mcdf - Markdown Container Document Format tool\n"
      "\n"
      "Usage:\n"
      "  mcdf <command> [options]\n"
      "\n"
      "Commands:\n"
      "  create   <dir|out.mcdf> --from <d.md> Create a document from Markdown\n"
      "                                        [--assets <dir>] [--title <t>]\n"
      "  import-epub <book.epub> -o <out>      Convert an EPUB to a document\n"
      "  import-html <page.html> -o <out>      Convert an HTML page to a document\n"
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
      "  audit    <container> [--append <ACTION>] Verify or append to audit.log\n"
      "  render   <html|text> <container> [-o]  Render content to HTML or text\n"
      "\n"
      "(inspect/validate/verify/manifest/audit/render also accept a .mcdf file)\n"
      "\n"
      "Global:\n"
      "  -h, --help                            Show this help\n"
      "  -v, --version                         Show version\n"
      "\n"
      "Every command also accepts --help for its own options.\n"
      "\n"
      "Rendering to PDF/DOCX is planned for a future release.\n");
}

void print_human(const std::string& path, const std::string& format,
                 const mcdf::Container& c, const mcdf::Document& d) {
  fmt::print("MCDF container: {}\n", path);
  fmt::print("  format: {}\n", format);
  if (auto files = c.list()) {
    fmt::print("  files:  {}\n", files->size());
    for (const auto& f : *files) fmt::print("    - {}\n", f);
  }

  fmt::print("\nMetadata:\n");
  if (d.has_metadata) {
    fmt::print("  title:          {}\n", d.metadata.title);
    fmt::print("  version:        {}\n", d.metadata.version);
    fmt::print("  classification: {}\n", d.metadata.classification);
    fmt::print("  language:       {}\n", d.metadata.language);
    if (!d.metadata.authors.empty()) {
      fmt::print("  authors:        ");
      for (std::size_t i = 0; i < d.metadata.authors.size(); ++i) {
        if (i) fmt::print(", ");
        fmt::print("{}", d.metadata.authors[i].name);
        if (!d.metadata.authors[i].id.empty())
          fmt::print(" <{}>", d.metadata.authors[i].id);
      }
      fmt::print("\n");
    }
    if (d.metadata.generated_by)
      fmt::print("  generated_by:   {}\n", *d.metadata.generated_by);
  } else {
    fmt::print("  (no metadata.yaml)\n");
  }

  fmt::print("\nSchema:\n");
  if (d.has_schema) {
    fmt::print("  document_type: {}\n", d.schema.document_type);
    fmt::print("  sections ({}):\n", d.schema.sections.size());
    for (const auto& s : d.schema.sections) {
      fmt::print("    - {}{}", s.required ? "[required] " : "           ", s.id);
      if (!s.title.empty()) fmt::print(" - {}", s.title);
      fmt::print("\n");
    }
  } else {
    fmt::print("  (no schema.yaml)\n");
  }

  fmt::print("\nContent headings ({}):\n", d.headings.size());
  for (const auto& h : d.headings) {
    fmt::print("  {} {}", std::string(static_cast<std::size_t>(h.level), '#'),
               h.text);
    if (!h.id.empty()) fmt::print("  {{#{}}}", h.id);
    fmt::print("\n");
  }

  fmt::print("\nManifest:\n");
  if (d.has_manifest) {
    fmt::print("  mcdf_version:   {}\n", d.manifest.mcdf_version);
    fmt::print("  hash_algorithm: {}\n", d.manifest.hash_algorithm);
    fmt::print("  files:          {}\n", d.manifest.files.size());
  } else {
    fmt::print("  (no manifest.json)\n");
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

  fmt::print("{}\n", j.dump(2));
}

int cmd_inspect(const std::vector<std::string>& args) {
  cxxopts::Options opts("mcdf inspect", "Summarize a container");
  opts.add_options()
      ("json", "Emit machine-readable JSON instead of text")
      ("container", "Container directory or .mcdf archive",
       cxxopts::value<std::string>())
      ("h,help", "Show this help");
  opts.parse_positional({"container"});
  opts.positional_help("<container>");

  const auto r = parse_args(opts, args);
  if (wants_help(r, opts)) return 0;
  if (!r.count("container")) return usage_error(opts, "a container is required");
  const auto path = r["container"].as<std::string>();

  auto container = mcdf::open_container(path);
  if (!container) {
    fmt::print(stderr, "error: {}\n", container.error().message);
    return 1;
  }
  auto doc = mcdf::load_document(**container);
  if (!doc) {
    fmt::print(stderr, "error: {}\n", doc.error().message);
    return 1;
  }

  const std::string format =
      fs::is_directory(path) ? "directory" : "archive";
  if (r.count("json"))
    print_json(format, **container, *doc);
  else
    print_human(path, format, **container, *doc);
  return 0;
}

int cmd_manifest(const std::vector<std::string>& args) {
  cxxopts::Options opts("mcdf manifest",
                        "Build the canonical manifest, or verify the stored one");
  opts.add_options()
      ("verify", "Verify the stored manifest instead of building one")
      ("container", "Container directory or .mcdf archive",
       cxxopts::value<std::string>())
      ("h,help", "Show this help");
  opts.parse_positional({"container"});
  opts.positional_help("<container>");

  const auto r = parse_args(opts, args);
  if (wants_help(r, opts)) return 0;
  if (!r.count("container")) return usage_error(opts, "a container is required");
  const auto path = r["container"].as<std::string>();

  auto container = mcdf::open_container(path);
  if (!container) {
    fmt::print(stderr, "error: {}\n", container.error().message);
    return 1;
  }

  if (!r.count("verify")) {
    auto manifest = mcdf::build_manifest(**container);
    if (!manifest) {
      fmt::print(stderr, "error: {}\n", manifest.error().message);
      return 1;
    }
    auto canonical = mcdf::manifest_to_canonical_json(*manifest);
    if (!canonical) {
      fmt::print(stderr, "error: {}\n", canonical.error().message);
      return 1;
    }
    // The conformance kit diffs this against expected/manifest.json, so stdout
    // carries the canonical bytes and nothing else.
    fmt::print("{}\n", *canonical);
    return 0;
  }

  if (!(*container)->contains("manifest.json")) {
    fmt::print(stderr, "error: manifest.json not found in container\n");
    return 1;
  }
  auto raw = (*container)->read("manifest.json");
  if (!raw) {
    fmt::print(stderr, "error: {}\n", raw.error().message);
    return 1;
  }
  auto manifest = mcdf::parse_manifest_json(*raw);
  if (!manifest) {
    fmt::print(stderr, "error: {}\n", manifest.error().message);
    return 1;
  }
  auto result = mcdf::verify_manifest(**container, *manifest);
  if (!result) {
    fmt::print(stderr, "error: {}\n", result.error().message);
    return 1;
  }
  if (result->ok) {
    fmt::print("manifest OK ({} files, {})\n", manifest->files.size(),
               manifest->hash_algorithm);
    return 0;
  }
  fmt::print("manifest FAILED\n");
  for (const auto& p : result->mismatched) fmt::print("  mismatch: {}\n", p);
  for (const auto& p : result->missing)    fmt::print("  missing:  {}\n", p);
  for (const auto& p : result->extra)      fmt::print("  extra:    {}\n", p);
  return 1;
}

int cmd_validate(const std::vector<std::string>& args) {
  cxxopts::Options opts("mcdf validate", "Validate a container against a profile");
  opts.add_options()
      ("profile", "core|integrity|signed|encrypted|render",
       cxxopts::value<std::string>()->default_value("integrity"))
      ("container", "Container directory or .mcdf archive",
       cxxopts::value<std::string>())
      ("h,help", "Show this help");
  opts.parse_positional({"container"});
  opts.positional_help("<container>");

  const auto r = parse_args(opts, args);
  if (wants_help(r, opts)) return 0;
  if (!r.count("container")) return usage_error(opts, "a container is required");
  const auto path = r["container"].as<std::string>();

  auto profile = mcdf::parse_profile(r["profile"].as<std::string>());
  if (!profile) {
    fmt::print(stderr, "error: {}\n", profile.error().message);
    return 2;
  }
  auto container = mcdf::open_container(path);
  if (!container) {
    fmt::print(stderr, "error: {}\n", container.error().message);
    return 1;
  }
  auto doc = mcdf::load_document(**container);
  if (!doc) {
    fmt::print(stderr, "error: {}\n", doc.error().message);
    return 1;
  }
  auto report = mcdf::validate(**container, *doc, *profile);
  if (!report) {
    fmt::print(stderr, "error: {}\n", report.error().message);
    return 1;
  }

  if (report->ok) {
    fmt::print("validate [{}]: OK\n", mcdf::to_string(report->profile));
    return 0;
  }
  // run.sh greps the combined output for the expected E_* code, so the issue
  // codes must keep appearing here verbatim.
  fmt::print("validate [{}]: {} issue(s)\n", mcdf::to_string(report->profile),
             report->issues.size());
  for (const auto& issue : report->issues)
    fmt::print("  {}: {}\n", issue.code, issue.message);
  return 1;
}

std::string read_file(const std::string& path, bool& ok) {
  std::ifstream in(path, std::ios::binary);
  ok = static_cast<bool>(in);
  if (!ok) return {};
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

std::string now_rfc3339() {
  const std::time_t t = std::time(nullptr);
  const std::tm* tm = std::gmtime(&t);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tm);
  return buf;
}

// Pack a staged directory into a .mcdf archive and remove the staging tree.
// Shared by create/import-epub/import-html, which all offer the same
// "<dir> or <file.mcdf>" output choice.
int finish_archive(const fs::path& workdir, const std::string& out,
                   std::string_view label) {
  auto container = mcdf::open_container(workdir);
  if (!container) {
    fmt::print(stderr, "error: {}\n", container.error().message);
    return 1;
  }
  auto archive = mcdf::pack_container(**container);
  if (!archive) {
    fmt::print(stderr, "error: {}\n", archive.error().message);
    return 1;
  }
  std::ofstream f(out, std::ios::binary | std::ios::trunc);
  if (!f) {
    fmt::print(stderr, "error: cannot write {}\n", out);
    return 1;
  }
  f.write(archive->data(), static_cast<std::streamsize>(archive->size()));
  f.close();
  std::error_code ec;
  fs::remove_all(workdir, ec);
  fmt::print("{}{} ({} bytes)\n", label, out, archive->size());
  return 0;
}

bool wants_archive(const std::string& out) {
  return out.size() >= 5 && out.compare(out.size() - 5, 5, ".mcdf") == 0;
}

// Wrap a Markdown file (plus optional assets) into a new container. Markdown
// is the native content format, so this is core authoring, not format
// conversion.
int cmd_create(const std::vector<std::string>& args) {
  cxxopts::Options opts("mcdf create", "Create a document from a Markdown file");
  opts.add_options()
      ("from", "Source Markdown file", cxxopts::value<std::string>())
      ("assets", "Directory of assets to copy in", cxxopts::value<std::string>())
      ("title", "Document title", cxxopts::value<std::string>())
      ("output", "Output directory, or a path ending in .mcdf",
       cxxopts::value<std::string>())
      ("h,help", "Show this help");
  opts.parse_positional({"output"});
  opts.positional_help("<dir|out.mcdf>");

  const auto r = parse_args(opts, args);
  if (wants_help(r, opts)) return 0;
  if (!r.count("output") || !r.count("from"))
    return usage_error(opts, "an output path and --from <doc.md> are required");
  const auto out = r["output"].as<std::string>();
  const auto from = r["from"].as<std::string>();

  bool ok = false;
  const std::string markdown = read_file(from, ok);
  if (!ok) {
    fmt::print(stderr, "error: cannot read {}\n", from);
    return 1;
  }

  const bool to_archive = wants_archive(out);
  std::error_code ec;
  const fs::path workdir = to_archive ? fs::path(out + ".staging") : fs::path(out);
  if (to_archive) fs::remove_all(workdir, ec);
  fs::create_directories(workdir, ec);

  if (r.count("assets")) {
    fs::copy(r["assets"].as<std::string>(), workdir / "assets",
             fs::copy_options::recursive | fs::copy_options::overwrite_existing,
             ec);
    if (ec) {
      fmt::print(stderr, "error: copying assets: {}\n", ec.message());
      return 1;
    }
  }

  auto dir = mcdf::DirectoryContainer::open(workdir);
  if (!dir) {
    fmt::print(stderr, "error: {}\n", dir.error().message);
    return 1;
  }
  mcdf::Metadata meta;
  meta.title = r.count("title") ? r["title"].as<std::string>() : std::string{};
  meta.created_at = now_rfc3339();
  if (auto res = mcdf::create_document(**dir, markdown, meta); !res) {
    fmt::print(stderr, "error: {}\n", res.error().message);
    return 1;
  }
  (void)mcdf::audit_append(**dir, "CREATED", "mcdf-cli", now_rfc3339());

  if (to_archive) return finish_archive(workdir, out, "created -> ");
  fmt::print("created -> {}\n", out);
  return 0;
}

// Convert an EPUB into a container. EPUB is license-clean to parse
// (in-house ZIP + pugixml), so this import lives in-repo.
int cmd_import_epub(const std::vector<std::string>& args) {
  cxxopts::Options opts("mcdf import-epub", "Convert an EPUB into a document");
  opts.add_options()
      ("o,output", "Output directory, or a path ending in .mcdf",
       cxxopts::value<std::string>())
      ("input", "Source .epub file", cxxopts::value<std::string>())
      ("h,help", "Show this help");
  opts.parse_positional({"input"});
  opts.positional_help("<book.epub>");

  const auto r = parse_args(opts, args);
  if (wants_help(r, opts)) return 0;
  if (!r.count("input") || !r.count("output"))
    return usage_error(opts, "an input .epub and -o <output> are required");
  const auto from = r["input"].as<std::string>();
  const auto out = r["output"].as<std::string>();

  bool ok = false;
  const std::string bytes = read_file(from, ok);
  if (!ok) {
    fmt::print(stderr, "error: cannot read {}\n", from);
    return 1;
  }

  const bool to_archive = wants_archive(out);
  std::error_code ec;
  const fs::path workdir = to_archive ? fs::path(out + ".staging") : fs::path(out);
  if (to_archive) fs::remove_all(workdir, ec);
  fs::create_directories(workdir, ec);

  auto dir = mcdf::DirectoryContainer::open(workdir);
  if (!dir) {
    fmt::print(stderr, "error: {}\n", dir.error().message);
    return 1;
  }
  auto report = mcdf::import_epub(**dir, bytes);
  if (!report) {
    fmt::print(stderr, "error: {}\n", report.error().message);
    if (to_archive) fs::remove_all(workdir, ec);
    return 1;
  }
  (void)mcdf::audit_append(**dir, "IMPORTED", "mcdf-cli", now_rfc3339());

  fmt::print("imported \"{}\"\n  chapters: {}\n  images:   {}\n", report->title,
             report->chapters, report->images);
  if (!report->authors.empty()) {
    fmt::print("  authors:  ");
    for (std::size_t i = 0; i < report->authors.size(); ++i)
      fmt::print("{}{}", i ? ", " : "", report->authors[i]);
    fmt::print("\n");
  }
  fmt::print("conversion is best-effort:\n");
  for (const auto& note : report->notes) fmt::print("  - {}\n", note);

  if (to_archive) return finish_archive(workdir, out, "-> ");
  fmt::print("-> {}\n", out);
  return 0;
}

// Convert an HTML page into a container. Assets are read from the page's own
// directory (relative references only) - nothing is fetched from the network.
int cmd_import_html(const std::vector<std::string>& args) {
  cxxopts::Options opts("mcdf import-html", "Convert an HTML page into a document");
  opts.add_options()
      ("o,output", "Output directory, or a path ending in .mcdf",
       cxxopts::value<std::string>())
      ("input", "Source .html file", cxxopts::value<std::string>())
      ("h,help", "Show this help");
  opts.parse_positional({"input"});
  opts.positional_help("<page.html>");

  const auto r = parse_args(opts, args);
  if (wants_help(r, opts)) return 0;
  if (!r.count("input") || !r.count("output"))
    return usage_error(opts, "an input .html and -o <output> are required");
  const auto from = r["input"].as<std::string>();
  const auto out = r["output"].as<std::string>();

  bool ok = false;
  const std::string html = read_file(from, ok);
  if (!ok) {
    fmt::print(stderr, "error: cannot read {}\n", from);
    return 1;
  }

  const fs::path source_dir = fs::path(from).parent_path();
  const bool to_archive = wants_archive(out);
  std::error_code ec;
  const fs::path workdir = to_archive ? fs::path(out + ".staging") : fs::path(out);
  if (to_archive) fs::remove_all(workdir, ec);
  fs::create_directories(workdir, ec);

  auto dir = mcdf::DirectoryContainer::open(workdir);
  if (!dir) {
    fmt::print(stderr, "error: {}\n", dir.error().message);
    return 1;
  }

  // Resolve assets from the page's directory, refusing to escape it.
  const auto resolver =
      [&source_dir](std::string_view rel) -> std::optional<std::string> {
    const fs::path candidate = source_dir / fs::path(std::string(rel));
    std::error_code err;
    const fs::path base = fs::weakly_canonical(source_dir, err);
    const fs::path real = fs::weakly_canonical(candidate, err);
    if (err) return std::nullopt;
    const auto rel_to_base = fs::relative(real, base, err);
    if (err || rel_to_base.empty() || *rel_to_base.begin() == "..")
      return std::nullopt;
    bool read_ok = false;
    std::string bytes = read_file(real.string(), read_ok);
    if (!read_ok) return std::nullopt;
    return bytes;
  };

  auto report = mcdf::import_html(**dir, html, resolver);
  if (!report) {
    fmt::print(stderr, "error: {}\n", report.error().message);
    if (to_archive) fs::remove_all(workdir, ec);
    return 1;
  }
  (void)mcdf::audit_append(**dir, "IMPORTED", "mcdf-cli", now_rfc3339());

  fmt::print("imported \"{}\"\n  images: {}\nconversion is best-effort:\n",
             report->title, report->images);
  for (const auto& note : report->notes) fmt::print("  - {}\n", note);

  if (to_archive) return finish_archive(workdir, out, "-> ");
  fmt::print("-> {}\n", out);
  return 0;
}

int cmd_keygen(const std::vector<std::string>& args) {
  cxxopts::Options opts("mcdf keygen", "Generate a key pair and print its did:key");
  opts.add_options()
      ("out", "Destination PEM file", cxxopts::value<std::string>())
      ("type", "ed25519|ecdsa-p256|x25519",
       cxxopts::value<std::string>()->default_value("ed25519"))
      ("h,help", "Show this help");

  const auto r = parse_args(opts, args);
  if (wants_help(r, opts)) return 0;
  if (!r.count("out")) return usage_error(opts, "--out <key.pem> is required");
  const auto out = r["out"].as<std::string>();
  const auto type = r["type"].as<std::string>();

  std::string pem, did;
  if (type == "ed25519" || type == "ecdsa-p256") {  // signing keys
    auto key = (type == "ed25519") ? mcdf::PrivateKey::generate_ed25519()
                                   : mcdf::PrivateKey::generate_ecdsa_p256();
    if (!key) {
      fmt::print(stderr, "error: {}\n", key.error().message);
      return 1;
    }
    auto p = key->to_pem();
    auto d = key->did_key();
    if (!p) {
      fmt::print(stderr, "error: {}\n", p.error().message);
      return 1;
    }
    if (!d) {
      fmt::print(stderr, "error: {}\n", d.error().message);
      return 1;
    }
    pem = *p;
    did = *d;
  } else if (type == "x25519") {  // encryption key
    auto key = mcdf::EncPrivateKey::generate_x25519();
    if (!key) {
      fmt::print(stderr, "error: {}\n", key.error().message);
      return 1;
    }
    auto p = key->to_pem();
    auto d = key->did_key();
    if (!p) {
      fmt::print(stderr, "error: {}\n", p.error().message);
      return 1;
    }
    if (!d) {
      fmt::print(stderr, "error: {}\n", d.error().message);
      return 1;
    }
    pem = *p;
    did = *d;
  } else {
    fmt::print(stderr, "error: unknown key type '{}' (ed25519|ecdsa-p256|x25519)\n",
               type);
    return 2;
  }

  std::ofstream f(out, std::ios::binary | std::ios::trunc);
  if (!f) {
    fmt::print(stderr, "error: cannot write {}\n", out);
    return 1;
  }
  f << pem;
  f.close();
  fmt::print("{}\n", did);
  return 0;
}

int cmd_encrypt(const std::vector<std::string>& args) {
  cxxopts::Options opts("mcdf encrypt", "Encrypt files in a container for recipients");
  opts.add_options()
      ("recipient", "Recipient did:key (repeatable)",
       cxxopts::value<std::vector<std::string>>())
      ("file", "Container-relative file to encrypt (repeatable, default content.md)",
       cxxopts::value<std::vector<std::string>>())
      ("container", "Container directory", cxxopts::value<std::string>())
      ("h,help", "Show this help");
  opts.parse_positional({"container"});
  opts.positional_help("<container>");

  const auto r = parse_args(opts, args);
  if (wants_help(r, opts)) return 0;
  if (!r.count("container") || !r.count("recipient"))
    return usage_error(opts, "a container and at least one --recipient are required");
  const auto path = r["container"].as<std::string>();
  const auto recipient_dids = r["recipient"].as<std::vector<std::string>>();

  std::vector<std::string> files;
  if (r.count("file")) files = r["file"].as<std::vector<std::string>>();
  if (files.empty()) files.push_back("content.md");

  auto container = mcdf::DirectoryContainer::open(path);
  if (!container) {
    fmt::print(stderr, "error: {}\n", container.error().message);
    return 1;
  }

  std::vector<mcdf::EncPublicKey> recipients;
  for (const auto& did : recipient_dids) {
    auto pk = mcdf::EncPublicKey::from_did_key(did);
    if (!pk) {
      fmt::print(stderr, "error: {}\n", pk.error().message);
      return 1;
    }
    recipients.push_back(*pk);
  }

  auto result = mcdf::encrypt_container(**container, files, recipients);
  if (!result) {
    fmt::print(stderr, "error: {}\n", result.error().message);
    return 1;
  }
  fmt::print("encrypted {} file(s) for {} recipient(s)\n", files.size(),
             recipients.size());
  return 0;
}

int cmd_decrypt(const std::vector<std::string>& args) {
  cxxopts::Options opts("mcdf decrypt", "Decrypt a container with an X25519 key");
  opts.add_options()
      ("key", "X25519 private key PEM", cxxopts::value<std::string>())
      ("container", "Container directory", cxxopts::value<std::string>())
      ("h,help", "Show this help");
  opts.parse_positional({"container"});
  opts.positional_help("<container>");

  const auto r = parse_args(opts, args);
  if (wants_help(r, opts)) return 0;
  if (!r.count("container") || !r.count("key"))
    return usage_error(opts, "a container and --key <x25519-key.pem> are required");
  const auto path = r["container"].as<std::string>();
  const auto keyfile = r["key"].as<std::string>();

  auto container = mcdf::DirectoryContainer::open(path);
  if (!container) {
    fmt::print(stderr, "error: {}\n", container.error().message);
    return 1;
  }

  bool ok = false;
  const std::string pem = read_file(keyfile, ok);
  if (!ok) {
    fmt::print(stderr, "error: cannot read key {}\n", keyfile);
    return 1;
  }
  auto key = mcdf::EncPrivateKey::from_pem(pem);
  if (!key) {
    fmt::print(stderr, "error: {}\n", key.error().message);
    return 1;
  }

  auto result = mcdf::decrypt_container(**container, *key);
  if (!result) {
    fmt::print(stderr, "error: {}\n", result.error().message);
    return 1;
  }
  fmt::print("decrypted\n");
  return 0;
}

int cmd_sign(const std::vector<std::string>& args) {
  cxxopts::Options opts("mcdf sign", "Sign the manifest with a private key");
  opts.add_options()
      ("key", "Signing key PEM (ed25519 or ecdsa-p256)",
       cxxopts::value<std::string>())
      ("name", "Signature file stem under signatures/",
       cxxopts::value<std::string>()->default_value("author"))
      ("container", "Container directory", cxxopts::value<std::string>())
      ("h,help", "Show this help");
  opts.parse_positional({"container"});
  opts.positional_help("<container>");

  const auto r = parse_args(opts, args);
  if (wants_help(r, opts)) return 0;
  if (!r.count("container") || !r.count("key"))
    return usage_error(opts, "a container and --key <pem> are required");
  const auto path = r["container"].as<std::string>();
  const auto keyfile = r["key"].as<std::string>();
  const auto name = r["name"].as<std::string>();

  // Signing writes a signature file, so a directory container is required.
  auto container = mcdf::DirectoryContainer::open(path);
  if (!container) {
    fmt::print(stderr, "error: {}\n", container.error().message);
    return 1;
  }

  // Only sign a container whose manifest matches its content.
  if (!(*container)->contains("manifest.json")) {
    fmt::print(stderr, "error: manifest.json not found; run 'mcdf manifest' first\n");
    return 1;
  }
  auto raw = (*container)->read("manifest.json");
  if (!raw) {
    fmt::print(stderr, "error: {}\n", raw.error().message);
    return 1;
  }
  auto manifest = mcdf::parse_manifest_json(*raw);
  if (!manifest) {
    fmt::print(stderr, "error: {}\n", manifest.error().message);
    return 1;
  }
  auto integrity = mcdf::verify_manifest(**container, *manifest);
  if (!integrity) {
    fmt::print(stderr, "error: {}\n", integrity.error().message);
    return 1;
  }
  if (!integrity->ok) {
    fmt::print(stderr, "error: manifest does not match content; re-run 'mcdf manifest'\n");
    return 1;
  }

  bool ok = false;
  const std::string pem = read_file(keyfile, ok);
  if (!ok) {
    fmt::print(stderr, "error: cannot read key {}\n", keyfile);
    return 1;
  }
  auto key = mcdf::PrivateKey::from_pem(pem);
  if (!key) {
    fmt::print(stderr, "error: {}\n", key.error().message);
    return 1;
  }
  auto kid = key->did_key();
  if (!kid) {
    fmt::print(stderr, "error: {}\n", kid.error().message);
    return 1;
  }

  auto jws = mcdf::sign_container(**container, *key, *kid);
  if (!jws) {
    fmt::print(stderr, "error: {}\n", jws.error().message);
    return 1;
  }

  const std::string sig_path = "signatures/" + name + ".sig";
  auto written = (*container)->write(sig_path, *jws);
  if (!written) {
    fmt::print(stderr, "error: {}\n", written.error().message);
    return 1;
  }

  fmt::print("signed by {}\n  -> {}\n", *kid, sig_path);
  return 0;
}

int cmd_verify(const std::vector<std::string>& args) {
  cxxopts::Options opts("mcdf verify", "Verify the manifest and any signatures");
  opts.add_options()
      ("container", "Container directory or .mcdf archive",
       cxxopts::value<std::string>())
      ("h,help", "Show this help");
  opts.parse_positional({"container"});
  opts.positional_help("<container>");

  const auto r = parse_args(opts, args);
  if (wants_help(r, opts)) return 0;
  if (!r.count("container")) return usage_error(opts, "a container is required");
  const auto path = r["container"].as<std::string>();

  auto container = mcdf::open_container(path);
  if (!container) {
    fmt::print(stderr, "error: {}\n", container.error().message);
    return 1;
  }

  bool ok = true;

  if (!(*container)->contains("manifest.json")) {
    fmt::print("manifest: MISSING\n");
    ok = false;
  } else {
    auto raw = (*container)->read("manifest.json");
    if (!raw) {
      fmt::print(stderr, "error: {}\n", raw.error().message);
      return 1;
    }
    auto manifest = mcdf::parse_manifest_json(*raw);
    if (!manifest) {
      fmt::print(stderr, "error: {}\n", manifest.error().message);
      return 1;
    }
    auto integrity = mcdf::verify_manifest(**container, *manifest);
    if (!integrity) {
      fmt::print(stderr, "error: {}\n", integrity.error().message);
      return 1;
    }
    if (integrity->ok) {
      fmt::print("manifest: OK ({} files)\n", manifest->files.size());
    } else {
      fmt::print("manifest: FAILED\n");
      ok = false;
      for (const auto& p : integrity->mismatched) fmt::print("  mismatch: {}\n", p);
      for (const auto& p : integrity->missing)    fmt::print("  missing:  {}\n", p);
      for (const auto& p : integrity->extra)      fmt::print("  extra:    {}\n", p);
    }
  }

  auto checks = mcdf::verify_container(**container);
  if (!checks) {
    fmt::print(stderr, "error: {}\n", checks.error().message);
    return 1;
  }
  if (checks->empty()) {
    fmt::print("signatures: NONE\n");
    ok = false;
  } else {
    for (const auto& c : *checks) {
      if (c.valid) {
        fmt::print("signature {}: VALID ({}, {})\n", c.file, c.alg, c.kid);
      } else {
        fmt::print("signature {}: INVALID", c.file);
        if (!c.error.empty()) fmt::print(" ({})", c.error);
        fmt::print("\n");
        ok = false;
      }
    }
  }

  fmt::print("{}", ok ? "verify: OK\n" : "verify: FAILED\n");
  return ok ? 0 : 1;
}

int cmd_pack(const std::vector<std::string>& args) {
  cxxopts::Options opts("mcdf pack", "Pack a container directory into a .mcdf archive");
  opts.add_options()
      ("o,output", "Destination .mcdf file", cxxopts::value<std::string>())
      ("container", "Container directory", cxxopts::value<std::string>())
      ("h,help", "Show this help");
  opts.parse_positional({"container"});
  opts.positional_help("<container>");

  const auto r = parse_args(opts, args);
  if (wants_help(r, opts)) return 0;
  if (!r.count("container") || !r.count("output"))
    return usage_error(opts, "a container and -o <file.mcdf> are required");
  const auto path = r["container"].as<std::string>();
  const auto out = r["output"].as<std::string>();

  auto container = mcdf::open_container(path);
  if (!container) {
    fmt::print(stderr, "error: {}\n", container.error().message);
    return 1;
  }
  auto archive = mcdf::pack_container(**container);
  if (!archive) {
    fmt::print(stderr, "error: {}\n", archive.error().message);
    return 1;
  }

  std::ofstream f(out, std::ios::binary | std::ios::trunc);
  if (!f) {
    fmt::print(stderr, "error: cannot write {}\n", out);
    return 1;
  }
  f.write(archive->data(), static_cast<std::streamsize>(archive->size()));
  fmt::print("packed -> {} ({} bytes)\n", out, archive->size());
  return 0;
}

int cmd_unpack(const std::vector<std::string>& args) {
  cxxopts::Options opts("mcdf unpack", "Unpack a .mcdf archive into a directory");
  opts.add_options()
      ("o,output", "Destination directory", cxxopts::value<std::string>())
      ("archive", "Source .mcdf file", cxxopts::value<std::string>())
      ("h,help", "Show this help");
  opts.parse_positional({"archive"});
  opts.positional_help("<file.mcdf>");

  const auto r = parse_args(opts, args);
  if (wants_help(r, opts)) return 0;
  if (!r.count("archive") || !r.count("output"))
    return usage_error(opts, "an archive and -o <directory> are required");
  const auto file = r["archive"].as<std::string>();
  const auto out = r["output"].as<std::string>();

  bool ok = false;
  const std::string bytes = read_file(file, ok);
  if (!ok) {
    fmt::print(stderr, "error: cannot read {}\n", file);
    return 1;
  }
  auto result = mcdf::unpack_archive(bytes, out);
  if (!result) {
    fmt::print(stderr, "error: {}\n", result.error().message);
    return 1;
  }
  fmt::print("unpacked -> {}\n", out);
  return 0;
}

int cmd_audit(const std::vector<std::string>& args) {
  cxxopts::Options opts("mcdf audit", "Verify, append to, or checkpoint audit.log");
  opts.add_options()
      ("append", "Append an ACTION entry", cxxopts::value<std::string>())
      ("actor", "Actor recorded with --append",
       cxxopts::value<std::string>()->default_value("unknown"))
      ("checkpoint", "Write a signed checkpoint over the log head")
      ("key", "Ed25519 key PEM, required by --checkpoint",
       cxxopts::value<std::string>())
      ("container", "Container directory or .mcdf archive",
       cxxopts::value<std::string>())
      ("h,help", "Show this help");
  opts.parse_positional({"container"});
  opts.positional_help("<container>");

  const auto r = parse_args(opts, args);
  if (wants_help(r, opts)) return 0;
  if (!r.count("container")) return usage_error(opts, "a container is required");
  const auto path = r["container"].as<std::string>();
  const auto actor = r["actor"].as<std::string>();

  if (r.count("append")) {  // append mode
    const auto append_action = r["append"].as<std::string>();
    auto dir = mcdf::DirectoryContainer::open(path);
    if (!dir) {
      fmt::print(stderr, "error: {}\n", dir.error().message);
      return 1;
    }
    auto res = mcdf::audit_append(**dir, append_action, actor, now_rfc3339());
    if (!res) {
      fmt::print(stderr, "error: {}\n", res.error().message);
      return 1;
    }
    fmt::print("appended: {} by {}\n", append_action, actor);
    return 0;
  }

  if (r.count("checkpoint")) {  // checkpoint mode
    if (!r.count("key")) {
      fmt::print(stderr, "--checkpoint requires --key <ed25519.pem>\n");
      return 2;
    }
    auto dir = mcdf::DirectoryContainer::open(path);
    if (!dir) {
      fmt::print(stderr, "error: {}\n", dir.error().message);
      return 1;
    }
    bool ok = false;
    const std::string pem = read_file(r["key"].as<std::string>(), ok);
    if (!ok) {
      fmt::print(stderr, "error: cannot read key {}\n", r["key"].as<std::string>());
      return 1;
    }
    auto key = mcdf::PrivateKey::from_pem(pem);
    if (!key) {
      fmt::print(stderr, "error: {}\n", key.error().message);
      return 1;
    }
    auto kid = key->did_key();
    if (!kid) {
      fmt::print(stderr, "error: {}\n", kid.error().message);
      return 1;
    }
    auto res = mcdf::audit_checkpoint(**dir, *key, *kid);
    if (!res) {
      fmt::print(stderr, "error: {}\n", res.error().message);
      return 1;
    }
    fmt::print("checkpoint written (head signed by {})\n", *kid);
    return 0;
  }

  // verify mode (default)
  auto container = mcdf::open_container(path);
  if (!container) {
    fmt::print(stderr, "error: {}\n", container.error().message);
    return 1;
  }
  auto entries = mcdf::read_audit_log(**container);
  if (!entries) {
    fmt::print(stderr, "error: {}\n", entries.error().message);
    return 1;
  }
  for (const auto& e : *entries)
    fmt::print("  {}  {}  {}\n", e.timestamp, e.action, e.actor);
  auto v = mcdf::audit_verify(**container);
  if (!v) {
    fmt::print(stderr, "error: {}\n", v.error().message);
    return 1;
  }
  fmt::print("audit: {} ({} entries)", v->ok ? "OK" : "FAILED", v->entries);
  if (!v->ok) fmt::print(" - {}", v->error);
  fmt::print("\n");

  auto cp = mcdf::audit_verify_checkpoint(**container);
  if (cp && cp->present)
    fmt::print("checkpoint: {} ({})\n", cp->valid ? "VALID" : "INVALID", cp->kid);

  return v->ok ? 0 : 1;
}

int cmd_render(const std::vector<std::string>& args) {
  cxxopts::Options opts("mcdf render", "Render content to HTML or plain text");
  opts.add_options()
      ("o,output", "Write to a file instead of stdout",
       cxxopts::value<std::string>())
      ("format", "html|text", cxxopts::value<std::string>())
      ("container", "Container directory or .mcdf archive",
       cxxopts::value<std::string>())
      ("h,help", "Show this help");
  opts.parse_positional({"format", "container"});
  opts.positional_help("<html|text> <container>");

  const auto r = parse_args(opts, args);
  if (wants_help(r, opts)) return 0;
  if (!r.count("format") || !r.count("container"))
    return usage_error(opts, "a format (html|text) and a container are required");
  const auto format_name = r["format"].as<std::string>();
  const auto path = r["container"].as<std::string>();

  auto format = mcdf::parse_render_format(format_name);
  if (!format) {
    fmt::print(stderr, "error: {}\n", format.error().message);
    return 2;
  }

  auto container = mcdf::open_container(path);
  if (!container) {
    fmt::print(stderr, "error: {}\n", container.error().message);
    return 1;
  }

  auto rendered = mcdf::render(**container, *format);
  if (!rendered) {
    fmt::print(stderr, "error: {}\n", rendered.error().message);
    return 1;
  }

  if (!r.count("output")) {
    fmt::print("{}", *rendered);
    return 0;
  }
  const auto out = r["output"].as<std::string>();
  std::ofstream f(out, std::ios::binary | std::ios::trunc);
  if (!f) {
    fmt::print(stderr, "error: cannot write {}\n", out);
    return 1;
  }
  f.write(rendered->data(), static_cast<std::streamsize>(rendered->size()));
  fmt::print("rendered -> {}\n", out);
  return 0;
}

int dispatch(const std::vector<std::string>& args) {
  const std::string& verb = args[0];
  if (verb == "inspect")     return cmd_inspect(args);
  if (verb == "manifest")    return cmd_manifest(args);
  if (verb == "validate")    return cmd_validate(args);
  if (verb == "create")      return cmd_create(args);
  if (verb == "import-epub") return cmd_import_epub(args);
  if (verb == "import-html") return cmd_import_html(args);
  if (verb == "keygen")      return cmd_keygen(args);
  if (verb == "sign")        return cmd_sign(args);
  if (verb == "verify")      return cmd_verify(args);
  if (verb == "pack")        return cmd_pack(args);
  if (verb == "unpack")      return cmd_unpack(args);
  if (verb == "encrypt")     return cmd_encrypt(args);
  if (verb == "decrypt")     return cmd_decrypt(args);
  if (verb == "audit")       return cmd_audit(args);
  if (verb == "render")      return cmd_render(args);

  fmt::print(stderr, "unknown command: {}\n\n", verb);
  print_usage();
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  // Windows text-mode stdout rewrites '\n' as CRLF, which silently breaks the
  // byte-exactness the format depends on: the conformance kit diffs
  // `mcdf manifest` against a golden file, and `mcdf render` emits the spec's
  // canonical form. Binary mode makes every platform emit identical bytes.
  // Must run before any output.
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  const std::vector<std::string> args(argv + 1, argv + argc);

  if (args.empty() || args[0] == "-h" || args[0] == "--help") {
    print_usage();
    return 0;
  }
  if (args[0] == "-v" || args[0] == "--version") {
    fmt::print("mcdf {}\n", mcdf::version_string());
    return 0;
  }

  // Unknown options and missing values surface as cxxopts exceptions; they are
  // usage errors, which this CLI has always reported as exit code 2.
  try {
    return dispatch(args);
  } catch (const cxxopts::exceptions::exception& e) {
    fmt::print(stderr, "error: {}\n", e.what());
    return 2;
  }
}
