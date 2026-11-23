#include "cfg_archive.hpp"

#include <nlohmann/json.hpp>
#include <detail/crypto.hpp>

#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <utility>

namespace sx::cfg {
namespace {

constexpr std::size_t max_manifest_size = 4096;

[[noreturn]] void fail(const std::string& message) {
    throw CfgTarError(message);
}

// Distinguish an absent archive from every other inspection error. Only
// ENOENT opens the one-time migration entrance; all other errors remain fatal.
bool path_exists(const std::string& path) {
    struct stat state {};
    if (::lstat(path.c_str(), &state) == 0) {
        return true;
    }
    if (errno == ENOENT) {
        return false;
    }
    fail("cannot inspect configuration source " + path + ": " + std::strerror(errno));
}

} // namespace

std::string CfgManifest::canonical(const ArchiveSchema& schema) {
    schema.validate();
    nlohmann::json manifest = {
        {"format", schema.format},
        {"version", schema.version},
    };
    return manifest.dump(2) + "\n";
}

void CfgManifest::validate(const ArchiveSchema& schema, const CfgTar& archive) {
    schema.validate();
    // Refuse archives without an identity record before inspecting payload
    // files. This also keeps an ordinary TAR from masquerading as a CfgTar.
    if (!archive.contains(path)) {
        fail("CfgTar manifest is missing");
    }

    // The manifest is intentionally tiny. Bound it independently of the
    // archive limits so parsing can never become an expensive startup step.
    const auto& manifest_data = archive.at(path).data;
    if (manifest_data.size() > max_manifest_size) {
        fail("CfgTar manifest exceeds its size limit");
    }

    nlohmann::json manifest;
    try {
        manifest = nlohmann::json::parse(manifest_data);
    }
    catch (const nlohmann::json::exception& error) {
        fail("invalid CfgTar manifest: " + std::string(error.what()));
    }

    // The envelope stays closed even when an application's format changes.
    const bool valid_shape = manifest.is_object() && manifest.size() == 2 &&
        manifest.contains("format") && manifest["format"].is_string() &&
        manifest.contains("version") && manifest["version"].is_number_unsigned();
    if (!valid_shape) fail("unsupported CfgTar manifest");
    const bool matching_identity = manifest["format"].get<std::string_view>() == schema.format &&
        manifest["version"].get<std::uint64_t>() == schema.version;
    if (!matching_identity) fail("unsupported CfgTar manifest");

    for (const auto& required : schema.required_files) {
        if (!archive.contains(required)) fail("CfgTar required file is missing: " + required);
    }
    for (const auto& [entry_path, entry] : archive.entries()) {
        (void)entry;
        if (!schema.allows(entry_path)) fail("unsupported CfgTar path: " + entry_path);
    }
    if (schema.validate_content) schema.validate_content(archive);
}

bool CfgManifest::is_resource_path(const ArchiveSchema& schema, std::string_view path) {
    schema.validate();
    return schema.is_resource_path(path);
}

CfgResource::CfgResource(Kind kind, std::string logical_path, std::string value)
    : kind_(kind), logical_path_(std::move(logical_path)), value_(std::move(value)) {}

CfgResource::Kind CfgResource::kind() const noexcept {
    return kind_;
}

bool CfgResource::is_native() const noexcept {
    return kind_ == Kind::native;
}

const std::string& CfgResource::logical_path() const noexcept {
    return logical_path_;
}

const std::string& CfgResource::content() const {
    if (!is_native()) {
        fail("legacy CfgTar resource has no native content: " + logical_path_);
    }
    return value_;
}

const std::string& CfgResource::legacy_path() const {
    if (is_native()) {
        fail("native CfgTar resource has no legacy path: " + logical_path_);
    }
    return value_;
}

CfgArchive::CfgArchive(ArchiveSchema schema, Kind kind, std::string path, std::optional<CfgTar> archive)
    : schema_(std::move(schema)), kind_(kind), path_(std::move(path)), archive_(std::move(archive)) {}

CfgArchive CfgArchive::open(ArchiveSchema schema, Paths paths) {
    schema.validate();
    // Explicit native input is useful for config checks, recovery tooling, and
    // isolated test instances. It receives the same strict validation as the
    // default archive and never falls back to a legacy file.
    if(paths.explicit_archive) {
        auto archive = CfgTar::read_file(*paths.explicit_archive);
        CfgManifest::validate(schema, archive);
        return CfgArchive(
            std::move(schema), Kind::cftar, std::move(*paths.explicit_archive), std::move(archive));
    }

    // Command-line intent is authoritative and keeps config-check workflows
    // independent of the system-wide CfgTar.
    if (paths.explicit_config) {
        return CfgArchive(std::move(schema), Kind::legacy, std::move(*paths.explicit_config), std::nullopt);
    }

    // Existence, not successful parsing, controls migration. A present but bad
    // archive is an operator-visible startup error rather than permission to
    // resurrect a potentially stale legacy configuration.
    if (path_exists(paths.archive)) {
        // Selection returns a ready-to-consume source. No caller can observe a
        // CfgTar Kind whose manifest or namespace has not passed validation.
        auto archive = CfgTar::read_file(paths.archive);
        CfgManifest::validate(schema, archive);
        return CfgArchive(std::move(schema), Kind::cftar, std::move(paths.archive), std::move(archive));
    }

    return CfgArchive(std::move(schema), Kind::legacy, std::move(paths.legacy), std::nullopt);
}

namespace {

std::string sibling_path(std::string_view legacy_path, std::string_view suffix) {
    if(legacy_path.empty()) fail("legacy configuration path is empty");
    constexpr std::string_view extension = ".cfg";
    std::string result(legacy_path);
    if(result.size() >= extension.size() &&
       result.compare(result.size() - extension.size(), extension.size(), extension) == 0) {
        result.resize(result.size() - extension.size());
    }
    result += suffix;
    return result;
}

}  // namespace

std::string CfgArchive::native_path_for(std::string_view legacy_path) {
    return sibling_path(legacy_path, ".cftar");
}

std::string CfgArchive::startup_path_for(std::string_view legacy_path) {
    return sibling_path(legacy_path, ".startup.cfg");
}

// Reconstruct the same validated source model as open(), but use archive bytes
// obtained through the privileged file channel instead of opening the path.
CfgArchive CfgArchive::read_archive(ArchiveSchema schema, std::string path, std::string_view bytes) {
    auto archive = CfgTar::read(bytes);
    CfgManifest::validate(schema, archive);
    return CfgArchive(std::move(schema), Kind::cftar, std::move(path), std::move(archive));
}

CfgArchiveInfo CfgArchive::inspect_archive(ArchiveSchema schema, std::string_view bytes) {
    auto archive = CfgTar::read(bytes);
    CfgManifest::validate(schema, archive);
    return CfgArchive(std::move(schema), Kind::cftar, {}, std::move(archive)).info();
}

CfgArchive::Kind CfgArchive::kind() const noexcept {
    return kind_;
}

const std::string& CfgArchive::path() const noexcept {
    return path_;
}

bool CfgArchive::is_cftar() const noexcept {
    return kind_ == Kind::cftar;
}

CfgArchiveInfo CfgArchive::info() const {
    CfgArchiveInfo result;
    result.native = is_cftar();
    result.path = path_;
    if (!result.native) return result;

    result.format_version = schema_.version;
    if(const auto hash = sx::cftar::crypto::sha256(archive().write())) {
        result.content_hash = "sha256:" + *hash;
    }
    result.entries.reserve(archive().entries().size());
    for (const auto& [path, entry] : archive().entries()) {
        result.payload_size += entry.data.size();
        result.entries.push_back({path, entry.data.size(), entry.executable});
    }
    return result;
}

const CfgTar& CfgArchive::archive() const {
    if (!archive_) {
        fail("legacy configuration has no CfgTar archive");
    }
    return *archive_;
}

std::string_view CfgArchive::main_config() const {
    if (schema_.main_config.empty()) fail("schema has no main configuration role");
    return archive().at(schema_.main_config).data;
}

std::string_view CfgArchive::startup_config() const {
    if (schema_.startup_config.empty()) fail("schema has no startup configuration role");
    return archive().at(schema_.startup_config).data;
}

CfgResource CfgArchive::resolve(std::string logical_path, std::string legacy_path) const {
    if (!CfgManifest::is_resource_path(schema_, logical_path)) {
        fail("invalid CfgTar resource path: " + logical_path);
    }

    // A native archive is a closed configuration source. Missing content is a
    // hard error and can never resurrect a stale legacy file behind the
    // administrator's back.
    if(is_cftar()) {
        if(!archive().contains(logical_path)) {
            fail("native CfgTar resource is missing: " + logical_path);
        }
        auto content = archive().at(logical_path).data;
        return {CfgResource::Kind::native, std::move(logical_path), std::move(content)};
    }
    if (legacy_path.empty()) {
        fail("explicit legacy path is empty for CfgTar resource: " + logical_path);
    }
    return {CfgResource::Kind::legacy, std::move(logical_path), std::move(legacy_path)};
}

std::vector<std::string> CfgArchive::resources(std::string_view logical_dir) const {
    if (logical_dir.empty() || logical_dir.back() != '/' ||
        !CfgManifest::is_resource_path(schema_, std::string(logical_dir) + "item")) {
        fail("invalid CfgTar resource directory: " + std::string(logical_dir));
    }

    std::vector<std::string> result;
    if (!is_cftar()) return result;
    for (const auto& [path, entry] : archive().entries()) {
        (void)entry;
        if (path.size() > logical_dir.size() &&
            path.compare(0, logical_dir.size(), logical_dir) == 0 &&
            schema_.is_resource_path(path)) {
            result.push_back(path);
        }
    }
    return result;
}

// Translate a new main configuration into the bytes expected by the selected
// persistent format. For CfgTar this also returns the exact accepted archive
// which must be made recoverable before the replacement is installed.
CfgSaveData CfgArchive::prepare_save(std::string main_config,
    const std::map<std::string, std::string>& additions) const {
    if (!is_cftar()) {
        return {std::move(main_config), std::nullopt};
    }

    if (schema_.main_config.empty()) fail("schema has no main configuration role");

    // Work on a copy so a failed validation or write cannot mutate the source
    // that still describes the configuration running in this process.
    auto updated = archive();
    updated.replace(schema_.main_config, std::move(main_config));

    for(const auto& [path, content] : additions) {
        if(!CfgManifest::is_resource_path(schema_, path)) {
            fail("invalid schema resource path: " + path);
        }
        if(!updated.contains(path)) {
            updated.add(path, content);
        }
    }

    CfgManifest::validate(schema_, updated);
    return {updated.write(), archive().write()};
}

CfgSaveData CfgArchive::prepare_resource(
    std::string path,
    std::optional<std::string> content,
    bool executable) const {
    if(!is_cftar()) fail("native resources require a CfgTar source");
    if(!CfgManifest::is_resource_path(schema_, path)) {
        fail("invalid CfgTar resource path: " + path);
    }

    // Work on a copy. The selected source must continue to describe durable
    // storage until the caller has backed up and published the replacement.
    auto updated = archive();
    if(content) {
        updated.upsert(std::move(path), std::move(*content), executable);
    }
    else if(!updated.remove(path)) {
        fail("cannot remove missing CfgTar resource: " + path);
    }

    CfgManifest::validate(schema_, updated);
    return {updated.write(), archive().write()};
}

// Advance the cached archive only after its prepared payload has reached disk.
// This keeps archive_payload() aligned with the last successful durable save.
void CfgArchive::accept_saved_main(std::string main_config) {
    if (!is_cftar()) {
        return;
    }
    if (schema_.main_config.empty()) fail("schema has no main configuration role");
    auto updated = archive();
    updated.replace(schema_.main_config, std::move(main_config));
    CfgManifest::validate(schema_, updated);
    archive_ = std::move(updated);
}

void CfgArchive::accept_saved_archive(std::string_view bytes) {
    if(!is_cftar()) fail("legacy configuration cannot adopt a CfgTar archive");
    auto updated = CfgTar::read(bytes);
    CfgManifest::validate(schema_, updated);
    archive_ = std::move(updated);
}

// Serialize the currently accepted native source byte-for-byte deterministically.
// archive() supplies the format check and rejects this operation for legacy.
std::string CfgArchive::archive_payload() const {
    return archive().write();
}

} // namespace sx::cfg
