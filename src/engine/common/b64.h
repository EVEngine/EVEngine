#pragma once

#include "common/config.h"

#include <stddef.h>


namespace eve
{

/**
 * Base64-encode data.
 *
 * @param src The data to encode.
 * @param srclen The size in bytes of the data.
 * @param linelen The maximum length of each line in the encoded string.
 *        0 indicates no maximum length.
 * @param dstlen The length of the encoded string is stored here.
 * @return A string containing the base64-encoded data (allocated with new[]).
 */
char *b64_encode(const char *src, size_t srclen, size_t linelen, size_t &dstlen);

/**
 * Decode base64 encoded data.
 *
 * @param src The string containing the base64 data.
 * @param srclen The length of the string.
 * @param dstlen The size of the binary data is stored here.
 * @return A chunk of memory containing the binary data (allocated with new[]).
 */
char *b64_decode(const char *src, size_t srclen, size_t &dstlen);

} // eve

