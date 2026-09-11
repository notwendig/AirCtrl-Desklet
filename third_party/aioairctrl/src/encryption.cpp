#include <aioairctrl/encryption.hpp>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <array>
#include <climits>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <vector>

namespace aioairctrl {
namespace {
int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    throw std::invalid_argument("Invalid hexadecimal payload");
}
std::string hex(const unsigned char* data, std::size_t size) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        result += digits[data[i] >> 4];
        result += digits[data[i] & 15];
    }
    return result;
}
std::vector<unsigned char> unhex(std::string_view text) {
    if (text.size() % 2) throw std::invalid_argument("Odd hex length");
    std::vector<unsigned char> out(text.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<unsigned char>((nibble(text[2*i]) << 4) | nibble(text[2*i+1]));
    return out;
}
std::string digest(std::string_view text, const EVP_MD* algorithm) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> out{};
    unsigned size = 0;
    if (!EVP_Digest(text.data(), text.size(), out.data(), &size, algorithm, nullptr))
        throw std::runtime_error("OpenSSL digest failed (MD5/SHA-256 must be available)");
    return hex(out.data(), size);
}
std::string crypt(std::string_view input, std::string_view key, bool encrypt) {
    if (input.size() > static_cast<std::size_t>(INT_MAX - EVP_MAX_BLOCK_LENGTH))
        throw std::length_error("Payload too large");
    // IMPORTANT: uppercase ASCII MD5 halves, NOT the raw MD5 bytes.
    const std::string material = digest("JiangPan" + std::string(key), EVP_md5());
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>
        ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx || !EVP_CipherInit_ex(ctx.get(), EVP_aes_128_cbc(), nullptr,
            reinterpret_cast<const unsigned char*>(material.data()),
            reinterpret_cast<const unsigned char*>(material.data() + 16), encrypt ? 1 : 0))
        throw std::runtime_error("AES initialization failed");
    std::string out(input.size() + EVP_MAX_BLOCK_LENGTH, '\0');
    int len = 0, final_len = 0;
    if (!EVP_CipherUpdate(ctx.get(), reinterpret_cast<unsigned char*>(out.data()), &len,
            reinterpret_cast<const unsigned char*>(input.data()), static_cast<int>(input.size())) ||
        !EVP_CipherFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(out.data()) + len, &final_len))
        throw std::runtime_error("AES operation failed (invalid ciphertext or PKCS#7 padding)");
    out.resize(static_cast<std::size_t>(len + final_len));
    return out;
}
} // namespace

void EncryptionContext::set_client_key(std::string_view key) {
    if (key.size() != 8) throw std::invalid_argument("Sync key must have 8 hexadecimal characters");
    std::uint32_t value = 0;
    for (char c : key) value = (value << 4) | static_cast<std::uint32_t>(nibble(c));
    client_key_ = value;
}

std::string EncryptionContext::encrypt(std::string_view payload) {
    if (!client_key_) throw std::logic_error("Client has not been synchronized");
    if (*client_key_ == std::numeric_limits<std::uint32_t>::max())
        throw std::overflow_error("Client key exhausted; synchronize again");
    ++*client_key_;
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setfill('0') << std::setw(8) << *client_key_;
    const std::string key = stream.str();
    const std::string ciphertext = crypt(payload, key, true);
    const std::string body = key + hex(reinterpret_cast<const unsigned char*>(ciphertext.data()), ciphertext.size());
    return body + digest(body, EVP_sha256());
}

std::string EncryptionContext::decrypt(std::string_view encrypted) const {
    if (encrypted.size() < 104 || (encrypted.size() - 72) % 32 != 0)
        throw std::invalid_argument("Invalid encrypted payload length");
    const std::string_view key = encrypted.substr(0, 8);
    for (char c : key) (void)nibble(c);
    const std::string_view body = encrypted.substr(0, encrypted.size() - 64);
    const std::string calculated = digest(body, EVP_sha256());
    if (CRYPTO_memcmp(calculated.data(), encrypted.data() + body.size(), 64) != 0)
        throw DigestMismatchException();
    const std::vector<unsigned char> bytes = unhex(encrypted.substr(8, encrypted.size() - 72));
    return crypt(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), key, false);
}
} // namespace aioairctrl
