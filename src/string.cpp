/* $Id$ */

/** @file string.cpp Handling of C-type strings (char*). */

#include "stdafx.h"
#include "openttd.h"
#include "debug.h"
#include "core/alloc_func.hpp"
#include "core/math_func.hpp"
#include "string_func.h"

#include "table/control_codes.h"

#include <stdarg.h>
#include <ctype.h> // required for tolower()

#include "safeguards.h"

void ttd_strlcat(char *dst, const char *src, size_t size)
{
	assert(size > 0);
	for (; size > 0 && *dst != '\0'; --size, ++dst) {}
	assert(size > 0);
	while (--size > 0 && *src != '\0') *dst++ = *src++;
	*dst = '\0';
}


void ttd_strlcpy(char *dst, const char *src, size_t size)
{
	assert(size > 0);
	while (--size > 0 && *src != '\0') *dst++ = *src++;
	*dst = '\0';
}


char* strecat(char* dst, const char* src, const char* last)
{
	assert(dst <= last);
	for (; *dst != '\0'; ++dst)
		if (dst == last) return dst;
	for (; *src != '\0' && dst != last; ++dst, ++src) *dst = *src;
	*dst = '\0';
	return strecpy(dst, src, last);
}


char* strecpy(char* dst, const char* src, const char* last)
{
	assert(dst <= last);
	for (; *src != '\0' && dst != last; ++dst, ++src) *dst = *src;
	*dst = '\0';
	if (dst == last && *src != '\0') {
#ifdef STRGEN
		error("String too long for destination buffer");
#else /* STRGEN */
		DEBUG(misc, 0, "String too long for destination buffer");
		*dst = '\0';
#endif /* STRGEN */
	}
	return dst;
}


char *CDECL str_fmt(const char *str, ...)
{
	char buf[4096];
	va_list va;

	va_start(va, str);
	int len = vseprintf(buf, lastof(buf), str, va);
	va_end(va);
	char *p = MallocT<char>(len + 1);
	memcpy(p, buf, len + 1);
	return p;
}


void str_validate(char *str)
{
	char *dst = str;
	WChar c;
	size_t len;

	for (len = Utf8Decode(&c, str); c != '\0'; len = Utf8Decode(&c, str)) {
		if (IsPrintable(c) && (c < SCC_SPRITE_START || c > SCC_SPRITE_END ||
			IsValidChar(c - SCC_SPRITE_START, CS_ALPHANUMERAL))) {
			/* Copy the character back. Even if dst is current the same as str
			 * (i.e. no characters have been changed) this is quicker than
			 * moving the pointers ahead by len */
			do {
				*dst++ = *str++;
			} while (--len != 0);
		} else {
			/* Replace the undesirable character with a question mark */
			str += len;
			*dst++ = '?';
		}
	}

	*dst = '\0';
}


void str_strip_colours(char *str)
{
	char *dst = str;
	WChar c;
	size_t len;

	for (len = Utf8Decode(&c, str); c != '\0'; len = Utf8Decode(&c, str)) {
		if (c < SCC_BLUE || c > SCC_BLACK) {
			/* Copy the character back. Even if dst is current the same as str
			 * (i.e. no characters have been changed) this is quicker than
			 * moving the pointers ahead by len */
			do {
				*dst++ = *str++;
			} while (--len != 0);
		} else {
			/* Just skip (strip) the colour codes */
			str += len;
		}
	}
	*dst = '\0';
}

/** Convert a given ASCII string to lowercase.
 * NOTE: only support ASCII characters, no UTF8 fancy. As currently
 * the function is only used to lowercase data-filenames if they are
 * not found, this is sufficient. If more, or general functionality is
 * needed, look to r7271 where it was removed because it was broken when
 * using certain locales: eg in Turkish the uppercase 'I' was converted to
 * '?', so just revert to the old functionality
 * @param str string to convert */
void strtolower(char *str)
{
	for (; *str != '\0'; str++) *str = tolower(*str);
}

/**
 * Only allow certain keys. You can define the filter to be used. This makes
 *  sure no invalid keys can get into an editbox, like BELL.
 * @param key character to be checked
 * @param afilter the filter to use
 * @return true or false depending if the character is printable/valid or not
 */
bool IsValidChar(WChar key, CharSetFilter afilter)
{
	switch (afilter) {
		case CS_ALPHANUMERAL: return IsPrintable(key);
		case CS_NUMERAL:      return (key >= '0' && key <= '9');
		case CS_ALPHA:        return IsPrintable(key) && !(key >= '0' && key <= '9');
	}

	return false;
}

#ifdef WIN32
/* Since version 3.14, MinGW Runtime has snprintf() and vsnprintf() conform to C99 but it's not the case for older versions */
#if (__MINGW32_MAJOR_VERSION < 3) || ((__MINGW32_MAJOR_VERSION == 3) && (__MINGW32_MINOR_VERSION < 14))
int CDECL snprintf(char *str, size_t size, const char *format, ...)
{
	va_list ap;
	int ret;

	va_start(ap, format);
	ret = vsnprintf(str, size, format, ap);
	va_end(ap);
	return ret;
}
#endif /* MinGW Runtime < 3.14 */

#ifdef _MSC_VER
/* *nprintf broken, not POSIX compliant, MSDN description
 * - If len < count, then len characters are stored in buffer, a null-terminator is appended, and len is returned.
 * - If len = count, then len characters are stored in buffer, no null-terminator is appended, and len is returned.
 * - If len > count, then count characters are stored in buffer, no null-terminator is appended, and a negative value is returned
 */
int CDECL vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
	int ret;
	ret = _vsnprintf(str, size, format, ap);
	if (ret < 0 || ret == size) str[size - 1] = '\0';
	return ret;
}
#endif /* _MSC_VER */

#endif /* WIN32 */

/**
 * Safer implementation of snprintf; same as snprintf except:
 * - last instead of size, i.e. replace sizeof with lastof.
 * - return gives the amount of characters added, not what it would add.
 * @param str    buffer to write to up to last
 * @param last   last character we may write to
 * @param format the formatting (see snprintf)
 * @return the number of added characters
 */
int CDECL seprintf(char *str, const char *last, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	int ret = vseprintf(str, last, format, ap);
	va_end(ap);
	return ret;
}

/**
 * Safer implementation of vsnprintf; same as vsnprintf except:
 * - last instead of size, i.e. replace sizeof with lastof.
 * - return gives the amount of characters added, not what it would add.
 * @param str    buffer to write to up to last
 * @param last   last character we may write to
 * @param format the formatting (see snprintf)
 * @param ap     the list of arguments for the format
 * @return the number of added characters
 */
int CDECL vseprintf(char *str, const char *last, const char *format, va_list ap)
{
	if (str >= last) return 0;
	size_t size = last - str;
	return min((int)size, vsnprintf(str, size, format, ap));
}



/** Convert the md5sum to a hexadecimal string representation
 * @param buf buffer to put the md5sum into
 * @param last last character of buffer (usually lastof(buf))
 * @param md5sum the md5sum itself
 * @return a pointer to the next character after the md5sum */
char *md5sumToString(char *buf, const char *last, const uint8 md5sum[16])
{
	char *p = buf;

	for (uint i = 0; i < 16; i++) {
		p += seprintf(p, last, "%02X", md5sum[i]);
	}

	return p;
}


/* UTF-8 handling routines */


/* Decode and consume the next UTF-8 encoded character
 * @param c Buffer to place decoded character.
 * @param s Character stream to retrieve character from.
 * @return Number of characters in the sequence.
 */
size_t Utf8Decode(WChar *c, const char *s)
{
	assert(c != NULL);

	if (!HasBit(s[0], 7)) {
		/* Single byte character: 0xxxxxxx */
		*c = s[0];
		return 1;
	} else if (GB(s[0], 5, 3) == 6) {
		if (IsUtf8Part(s[1])) {
			/* Double byte character: 110xxxxx 10xxxxxx */
			*c = GB(s[0], 0, 5) << 6 | GB(s[1], 0, 6);
			if (*c >= 0x80) return 2;
		}
	} else if (GB(s[0], 4, 4) == 14) {
		if (IsUtf8Part(s[1]) && IsUtf8Part(s[2])) {
			/* Triple byte character: 1110xxxx 10xxxxxx 10xxxxxx */
			*c = GB(s[0], 0, 4) << 12 | GB(s[1], 0, 6) << 6 | GB(s[2], 0, 6);
			if (*c >= 0x800) return 3;
		}
	} else if (GB(s[0], 3, 5) == 30) {
		if (IsUtf8Part(s[1]) && IsUtf8Part(s[2]) && IsUtf8Part(s[3])) {
			/* 4 byte character: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
			*c = GB(s[0], 0, 3) << 18 | GB(s[1], 0, 6) << 12 | GB(s[2], 0, 6) << 6 | GB(s[3], 0, 6);
			if (*c >= 0x10000 && *c <= 0x10FFFF) return 4;
		}
	}

	//DEBUG(misc, 1, "[utf8] invalid UTF-8 sequence");
	*c = '?';
	return 1;
}


/* Encode a unicode character and place it in the buffer
 * @param buf Buffer to place character.
 * @param c   Unicode character to encode.
 * @return Number of characters in the encoded sequence.
 */
size_t Utf8Encode(char *buf, WChar c)
{
	if (c < 0x80) {
		*buf = c;
		return 1;
	} else if (c < 0x800) {
		*buf++ = 0xC0 + GB(c,  6, 5);
		*buf   = 0x80 + GB(c,  0, 6);
		return 2;
	} else if (c < 0x10000) {
		*buf++ = 0xE0 + GB(c, 12, 4);
		*buf++ = 0x80 + GB(c,  6, 6);
		*buf   = 0x80 + GB(c,  0, 6);
		return 3;
	} else if (c < 0x110000) {
		*buf++ = 0xF0 + GB(c, 18, 3);
		*buf++ = 0x80 + GB(c, 12, 6);
		*buf++ = 0x80 + GB(c,  6, 6);
		*buf   = 0x80 + GB(c,  0, 6);
		return 4;
	}

	//DEBUG(misc, 1, "[utf8] can't UTF-8 encode value 0x%X", c);
	*buf = '?';
	return 1;
}

/**
 * Properly terminate an UTF8 string to some maximum length
 * @param s string to check if it needs additional trimming
 * @param maxlen the maximum length the buffer can have.
 * @return the new length in bytes of the string (eg. strlen(new_string))
 * @NOTE maxlen is the string length _INCLUDING_ the terminating '\0'
 */
size_t Utf8TrimString(char *s, size_t maxlen)
{
	size_t length = 0;

	for (const char *ptr = strchr(s, '\0'); *s != '\0';) {
		size_t len = Utf8EncodedCharLen(*s);
		/* Silently ignore invalid UTF8 sequences, our only concern trimming */
		if (len == 0) len = 1;

		/* Take care when a hard cutoff was made for the string and
		 * the last UTF8 sequence is invalid */
		if (length + len >= maxlen || (s + len > ptr)) break;
		s += len;
		length += len;
	}

	*s = '\0';
	return length;
}
