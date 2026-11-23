#pragma once

#include "cfg_tar.hpp"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace sx::cfg {

// Application-owned policy, copied into archives and sessions. No global default.
struct ArchiveSchema {
    std::string format;
    unsigned int version = 1;
    std::vector<std::string> required_files;
    std::vector<std::string> optional_files;
    // Recursive prefixes, with a trailing slash (for example "certs/").
    std::vector<std::string> allowed_directories;
    // Optional extension rule for root files only; entries include the dot.
    std::vector<std::string> allowed_root_extensions;
    // Optional roles used by main_config/startup_config and pack input shortcuts.
    // A nonempty role must name a required file. Empty disables that role.
    std::string main_config;
    std::string startup_config;
    // Throw CfgTarError on failure. Basic namespace/security checks run first.
    std::function<void(const CfgTar&)> validate_content;
    // Additional offline editing checks, applied by session open/put/remove/save.
    std::function<void(const CfgTar&)> validate_session;
    // Optional JSON descriptions emitted when a directory contains resources.
    std::map<std::string, std::string> directory_descriptions;

    void validate() const;
    bool allows(std::string_view path) const;
    bool is_resource_path(std::string_view path) const;
};

// Explicit compatibility profile; never selected implicitly.
ArchiveSchema smithproxy_schema();

} // namespace sx::cfg
