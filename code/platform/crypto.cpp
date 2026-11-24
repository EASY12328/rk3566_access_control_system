#include "crypto.h"
#include <limits>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace platform {
std::string encodeBase64(const std::string &bytes)
{
    if (bytes.empty() || bytes.size() > std::numeric_limits<int>::max() / 2)
        return {};
    std::string encoded(4 * ((bytes.size() + 2) / 3) + 1, '\0');
    const int size = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(encoded.data()),
                                      reinterpret_cast<const unsigned char *>(bytes.data()),
                                      static_cast<int>(bytes.size()));
    encoded.resize(size);
    return encoded;
}

bool decodeBase64(const std::string &text, std::string &bytes)
{
    bytes.clear();
    if (text.empty())
        return true;
    if (text.size() % 4 != 0 || text.size() > std::numeric_limits<int>::max() / 2)
        return false;
    std::string result(text.size() / 4 * 3, '\0');
    int size = EVP_DecodeBlock(reinterpret_cast<unsigned char *>(result.data()),
                               reinterpret_cast<const unsigned char *>(text.data()),
                               static_cast<int>(text.size()));
    if (size < 0)
        return false;
    if (text.back() == '=')
        --size;
    if (text[text.size() - 2] == '=')
        --size;
    if (size < 0)
        return false;
    result.resize(size);
    if (encodeBase64(result) != text)
        return false;
    bytes = std::move(result);
    return true;
}

std::string randomBytes(int size)
{
    if (size <= 0)
        return {};
    std::string bytes(size, '\0');
    if (RAND_bytes(reinterpret_cast<unsigned char *>(bytes.data()), size) != 1)
        return {};
    return bytes;
}

std::string pinDigest(const std::string &pin, const std::string &salt, int iterations)
{
    std::string digest(32, '\0');
    if (PKCS5_PBKDF2_HMAC(pin.data(), static_cast<int>(pin.size()),
                          reinterpret_cast<const unsigned char *>(salt.data()),
                          static_cast<int>(salt.size()), iterations, EVP_sha256(),
                          static_cast<int>(digest.size()),
                          reinterpret_cast<unsigned char *>(digest.data())) != 1)
        return {};
    return digest;
}

bool equalDigest(const std::string &a, const std::string &b)
{
    return a.size() == 32 && b.size() == 32 && CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}
}
