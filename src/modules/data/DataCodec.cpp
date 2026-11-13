/** @file DataCodec.cpp
 * @brief Canonical data byte codecs shared by native and WASM; no Poco or script-provider implementation.
 */
#include "data/DataCodec.h"
#include <cstdint>
#include <exception>
#include "common/Exception.h"
#include "common/b64.h"

namespace {

static const char hexchars[] = "0123456789abcdef";

char *bytesToHex(const uint8_t *src, size_t srclen, size_t &dstlen) {
    dstlen = srclen * 2;

    if (dstlen == 0) return nullptr;

    char *dst = nullptr;
    try {
        dst = new char[dstlen + 1];
    } catch (std::exception &) {
        throw eve::Exception("Out of memory.");
    }

    for (size_t i = 0; i < srclen; i++) {
        uint8_t b      = src[i];
        dst[i * 2 + 0] = hexchars[b >> 4];
        dst[i * 2 + 1] = hexchars[b & 0xF];
    }

    dst[dstlen] = '\0';
    return dst;
}

uint8_t nibble(char c) {
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');

    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 0x0a);

    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 0x0a);

    return 0;
}

uint8_t *hexToBytes(const char *src, size_t srclen, size_t &dstlen) {
    if (srclen >= 2 && src[0] == '0' && (src[1] == 'x' || src[1] == 'X')) {
        src += 2;
        srclen -= 2;
    }

    dstlen = (srclen + 1) / 2;

    if (dstlen == 0) return nullptr;

    uint8_t *dst = nullptr;
    try {
        dst = new uint8_t[dstlen];
    } catch (std::exception &) {
        throw eve::Exception("Out of memory.");
    }

    for (size_t i = 0; i < dstlen; i++) {
        dst[i] = nibble(src[i * 2]) << 4;

        if (i * 2 + 1 < srclen) dst[i] |= nibble(src[i * 2 + 1]);
    }

    return dst;
}

}  // anonymous namespace

namespace eve {
namespace data {

CompressedData *compress(std::string format, const char *rawbytes, size_t rawsize, int level) {
    Compressor *compressor = Compressor::getCompressor(format);

    if (compressor == nullptr) throw eve::Exception("Invalid compression format.");

    size_t compressedsize = 0;
    char  *cbytes         = compressor->compress(format, rawbytes, rawsize, level, compressedsize);

    CompressedData *data = nullptr;

    try {
        data = new CompressedData(format, cbytes, compressedsize, rawsize, true);
    } catch (eve::Exception &) {
        delete[] cbytes;
        throw;
    }

    return data;
}

char *decompress(CompressedData *data, size_t &decompressedsize) {
    size_t rawsize = data->getDecompressedSize();

    char *rawbytes = decompress(data->getFormat(), (const char *)data->getData(), data->getSize(), rawsize);

    decompressedsize = rawsize;
    return rawbytes;
}

char *decompress(std::string format, const char *cbytes, size_t compressedsize, size_t &rawsize) {
    Compressor *compressor = Compressor::getCompressor(format);

    if (compressor == nullptr) throw eve::Exception("Invalid compression format.");

    return compressor->decompress(format, cbytes, compressedsize, rawsize);
}

char *encode(std::string format, const char *src, size_t srclen, size_t &dstlen, size_t linelen) {
    if (format == "hex")
        return bytesToHex((const uint8_t *)src, srclen, dstlen);
    else if (format == "base64")
        return b64_encode(src, srclen, linelen, dstlen);
    throw eve::Exception("Invalid encoding format.");
}

char *decode(std::string format, const char *src, size_t srclen, size_t &dstlen) {
    if (format == "hex")
        return (char *)hexToBytes(src, srclen, dstlen);
    else if (format == "base64")
        return b64_decode(src, srclen, dstlen);
    throw eve::Exception("Invalid encoding format.");
}

std::string hash(std::string function, Data *input) {
    return hash(function, (const char *)input->getData(), input->getSize());
}

std::string hash(std::string function, const char *input, uint64_t size) {
    HashFunction::Value output;
    hash(function, input, size, output);
    return std::string(output.data, output.size);
}

void hash(std::string function, Data *input, HashFunction::Value &output) {
    hash(function, (const char *)input->getData(), input->getSize(), output);
}

void hash(std::string function, const char *input, uint64_t size, HashFunction::Value &output) {
    HashFunction *hashfunction = HashFunction::getHashFunction(function);
    if (hashfunction == nullptr) throw eve::Exception("Invalid hash function.");

    hashfunction->hash(function, input, size, output);
}


}  // namespace data
}  // namespace eve
