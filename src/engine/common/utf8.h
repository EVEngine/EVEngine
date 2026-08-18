
#include "common/config.h"

#include <cstddef>
#include <string>

#ifdef EVENGINE_WINDOWS
#define NOMINMAX
#include <windows.h>
#endif

namespace eve
{

/**
 * @brief Count UTF-8 code points in a string.
 * Invalid / truncated sequences stop the scan.
 */
size_t utf8_codepoint_count(const std::string &s);

/**
 * @brief Byte offset of the N-th UTF-8 code point (0-based count of code points).
 * Returns s.size() if there are fewer than N code points.
 */
size_t utf8_byte_offset_for_codepoints(const std::string &s, size_t codepoints);

#ifdef EVENGINE_WINDOWS

/**
 * @brief Convert the wide string to a UTF-8 encoded string.
 * @param wstr The wide-char string.
 * @return A UTF-8 string.
 **/
std::string to_utf8(LPCWSTR wstr);

/**
 * @brief Convert a UTF-8 encoded string to a wide string.
 * @param str The UTF-8 string.
 * @return A wide string.
**/
std::wstring to_widestr(const std::string &str);

/**
 * @brief Replace all occurences of 'find' with 'replace' in a string.
 * @param str The string to modify.
 * @param find The character to match.
 * @param replace The character to replace matches.
 **/
void replace_char(std::string &str, char find, char replace);

#endif // EVENGINE_WINDOWS

} // eve
