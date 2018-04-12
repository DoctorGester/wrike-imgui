/*	Copyright 2012 Christoph GÃĪrtner
	Distributed under the Boost Software License, Version 1.0
*/

#ifndef DECODE_HTML_ENTITIES_UTF8_
#define DECODE_HTML_ENTITIES_UTF8_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t decode_html_entities_utf8(char *dest, const char *src, const char* src_end);
/*	Takes input from <src> and decodes into <dest>, which should be a buffer
	large enough to hold <strlen(src)> characters.

	If <src> is <NULL>, input will be taken from <dest>, decoding
	the entities in-place.

	The function returns the length of the decoded string.
*/

#ifdef __cplusplus
}
#endif

#endif