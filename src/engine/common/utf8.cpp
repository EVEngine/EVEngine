#include "utf8.h"

namespace eve
{

namespace {

size_t utf8_step(unsigned char c)
{
	if ((c & 0x80) == 0)
		return 1;
	if ((c & 0xE0) == 0xC0)
		return 2;
	if ((c & 0xF0) == 0xE0)
		return 3;
	if ((c & 0xF8) == 0xF0)
		return 4;
	return 1;
}

} // namespace

size_t utf8_codepoint_count(const std::string &s)
{
	size_t n = 0;
	for (size_t i = 0; i < s.size();)
	{
		const size_t step = utf8_step(static_cast<unsigned char>(s[i]));
		if (i + step > s.size())
			break;
		i += step;
		++n;
	}
	return n;
}

size_t utf8_byte_offset_for_codepoints(const std::string &s, size_t codepoints)
{
	size_t n = 0;
	size_t i = 0;
	while (i < s.size() && n < codepoints)
	{
		const size_t step = utf8_step(static_cast<unsigned char>(s[i]));
		if (i + step > s.size())
			break;
		i += step;
		++n;
	}
	return i;
}

#ifdef EVENGINE_WINDOWS

std::string to_utf8(LPCWSTR wstr)
{
	size_t wide_len = wcslen(wstr)+1;

	// Get size in UTF-8.
	int utf8_size = WideCharToMultiByte(CP_UTF8, 0, wstr, static_cast<int>(wide_len), 0, 0, 0, 0);

	char *utf8_str = new char[utf8_size];

	// Convert to UTF-8.
	int ok = WideCharToMultiByte(CP_UTF8, 0, wstr, static_cast<int>(wide_len), utf8_str, utf8_size, 0, 0);

	std::string ret;
	if (ok)
		ret = utf8_str;

	delete[] utf8_str;
	return ret;
}

std::wstring to_widestr(const std::string &str)
{
	if (str.empty())
		return std::wstring();

	int wide_size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int) str.length(), nullptr, 0);

	if (wide_size == 0)
		return std::wstring();

	std::wstring widestr;
	widestr.resize(wide_size);

	int ok = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int) str.length(), &widestr[0], static_cast<int>(widestr.length()));

	if (!ok)
		return std::wstring();

	return widestr;
}

void replace_char(std::string &str, char find, char replace)
{
	int length = static_cast<int>(str.length());

	for (int i = 0; i<length; i++)
	{
		if (str[i] == find)
			str[i] = replace;
	}
}

#endif // EVENGINE_WINDOWS

} // eve
