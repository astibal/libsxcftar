#include "crypto.hpp"
#include <array>
#include <memory>
#include <openssl/evp.h>

namespace sx::cftar::crypto {
std::optional<std::string> sha256(std::string_view input) {
    const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
        EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context) return std::nullopt;
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned size = 0;
    const bool initialized = EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) == 1;
    if (!initialized) return std::nullopt;
    const bool updated = EVP_DigestUpdate(context.get(), input.data(), input.size()) == 1;
    if (!updated) return std::nullopt;
    const bool finalized = EVP_DigestFinal_ex(context.get(), digest.data(), &size) == 1;
    if (!finalized) return std::nullopt;
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(size * 2);
    for (unsigned i = 0; i < size; ++i) {
        result += hex[digest[i] >> 4];
        result += hex[digest[i] & 15];
    }
    return result;
}
} // namespace sx::cftar::crypto
