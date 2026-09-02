#pragma once
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace aioairctrl {
class DigestMismatchException : public std::runtime_error {
public:
    DigestMismatchException() : std::runtime_error("Payload SHA-256 digest mismatch") {}
};

// Wire-compatible with betaboon/aioairctrl's EncryptionContext.
// Stateful; callers must serialize encrypt/set_client_key.
class EncryptionContext {
public:
    void set_client_key(std::string_view client_key);
    std::string encrypt(std::string_view payload);
    std::string decrypt(std::string_view payload_encrypted) const;
private:
    std::optional<std::uint32_t> client_key_;
};
} // namespace aioairctrl
