#pragma once
#include <optional>
#include <string>
#include <string_view>
namespace sx::cftar::crypto {
std::optional<std::string> sha256(std::string_view input);
}
