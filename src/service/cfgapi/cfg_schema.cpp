#include "cfg_schema.hpp"
#include "cfg_archive.hpp"
#include <utils/utf8.hpp>
#include <algorithm>
#include <libconfig.h++>

namespace sx::cfg {
namespace {

bool listed(const std::vector<std::string>& names, std::string_view path) {
    return std::find(names.begin(), names.end(), path) != names.end();
}

bool safe_path(std::string_view path) {
    try {
        return CfgTar().checked_path(path) == path;
    } catch (const CfgTarError&) {
        return false;
    }
}

bool root_extension(const ArchiveSchema& schema, std::string_view path) {
    if (path.find('/') != std::string_view::npos) return false;
    for (const auto& extension : schema.allowed_root_extensions) {
        const bool matches = path.size() >= extension.size() &&
            path.substr(path.size() - extension.size()) == extension;
        if (matches) return true;
    }
    return false;
}

bool below(std::string_view path, std::string_view directory) {
    return path.size() > directory.size() && path.substr(0, directory.size()) == directory;
}

} // namespace

void ArchiveSchema::validate() const {
    if (format.empty() || version == 0) throw CfgTarError("schema format and positive version are required");
    // Bound the identity before JSON serialization, as well as on archive read.
    if (format.size() > 256 || !sx::text::valid_utf8(format, sx::text::utf8_policy::plain_text)) {
        throw CfgTarError("invalid schema format identifier");
    }
    for (const auto* files : {&required_files, &optional_files}) {
        for (const auto& path : *files) {
            if (!safe_path(path) || path == CfgManifest::path) {
                throw CfgTarError("invalid schema file: " + path);
            }
        }
    }
    for (const auto& directory : allowed_directories) {
        const bool valid = !directory.empty() && directory.back() == '/' &&
            safe_path(directory.substr(0, directory.size() - 1)) &&
            directory != "manifest.json/";
        if (!valid) throw CfgTarError("invalid schema directory: " + directory);
    }
    for (const auto& extension : allowed_root_extensions) {
        const bool valid = extension.size() > 1 && extension.front() == '.' &&
            safe_path("file" + extension) && extension.find('/') == std::string::npos;
        if (!valid) throw CfgTarError("invalid schema root extension: " + extension);
    }
    for (const auto* role : {&main_config, &startup_config}) {
        if (!role->empty() && !listed(required_files, *role)) {
            throw CfgTarError("schema configuration role must name a required file: " + *role);
        }
    }
    if (!main_config.empty() && main_config == startup_config) {
        throw CfgTarError("schema main and startup roles must be distinct");
    }
    for (const auto& [directory, text] : directory_descriptions) {
        (void)text;
        const bool valid = !directory.empty() && directory.back() == '/' &&
            safe_path(directory.substr(0, directory.size() - 1)) &&
            is_resource_path(directory + "resource.json");
        if (!valid) throw CfgTarError("invalid schema description directory: " + directory);
    }
}

bool ArchiveSchema::allows(std::string_view path) const {
    if (!safe_path(path)) return false;
    if (path == CfgManifest::path || listed(required_files, path) ||
        listed(optional_files, path) || root_extension(*this, path)) return true;
    return std::any_of(allowed_directories.begin(), allowed_directories.end(),
        [&](const auto& directory) { return below(path, directory); });
}

bool ArchiveSchema::is_resource_path(std::string_view path) const {
    const bool control = path == CfgManifest::path || path == main_config ||
        path == startup_config || root_extension(*this, path);
    return !control && allows(path);
}

bool ArchiveSchema::is_managed_path(std::string_view path) const {
    const bool protected_control = path == CfgManifest::path || path == main_config ||
        path == startup_config;
    return !protected_control && allows(path);
}

ArchiveSchema smithproxy_schema() {
    ArchiveSchema schema;
    schema.format = "smithproxy-cftar";
    schema.required_files = {"smithproxy.cfg", "smithproxy.startup.cfg"};
    schema.allowed_directories = {"pki/", "templates/", "commands/"};
    schema.allowed_root_extensions = {".cfg"};
    schema.main_config = "smithproxy.cfg";
    schema.startup_config = "smithproxy.startup.cfg";
    schema.validate_content = [](const CfgTar& archive) {
        for (const auto& [path, entry] : archive.entries()) {
            const bool root_config = path.find('/') == std::string::npos &&
                path.size() >= 4 && path.substr(path.size() - 4) == ".cfg";
            if (!root_config) continue;
            if (entry.executable) {
                throw CfgTarError("CfgTar configuration cannot be executable: " + path);
            }
            if (entry.data.size() > CfgManifest::max_config_size) {
                throw CfgTarError("CfgTar configuration exceeds its size limit: " + path);
            }
            if (!sx::text::valid_utf8(entry.data, sx::text::utf8_policy::plain_text)) {
                throw CfgTarError("CfgTar configuration is not valid UTF-8 text: " + path);
            }
        }
    };
    schema.validate_session = [](const CfgTar& archive) {
        try {
            libconfig::Config parsed;
            parsed.readString(archive.at("smithproxy.cfg").data);
        } catch (const libconfig::ParseException& error) {
            throw CfgTarError("invalid smithproxy.cfg at line " +
                std::to_string(error.getLine()) + ": " + error.getError());
        }
    };
    schema.directory_descriptions = {
        {"pki/ca/default/", "Default Smithproxy certificate authority and service identities."},
        {"pki/trust/", "Product-managed trust anchors used for upstream certificate verification."},
        {"pki/mitm/ssh/hostkeys/", "SSH host keys presented by the interception service."},
        {"pki/mitm/tls/server/sni/", "TLS server identities selected by the intercepted SNI value."},
        {"pki/mitm/tls/server/ip/", "TLS server identities selected by the intercepted destination address."},
        {"pki/mitm/tls/client/sni/", "TLS client identities selected by the upstream SNI value."},
        {"pki/mitm/tls/client/ip/", "TLS client identities selected by the upstream destination address."},
        {"templates/replacement/", "Localized response templates presented to intercepted clients."},
        {"commands/macros/", "Administrative command macros stored as inert configuration resources."},
        {"commands/scripts/", "Administrative scripts stored as inert configuration resources."},
    };
    return schema;
}

} // namespace sx::cfg
