#ifndef CHARCODE_CONVERT_H
#define CHARCODE_CONVERT_H
#include "charcode.h"

#ifdef __cplusplus
extern "C" {
#endif

void CharCode_ToAsciiString(charcode_t * charcodeString, char * cString, int len);
// The game's characters for ASCII text (letters, digits, space, basic punctuation, \n for
// a new line; "e'" isn't parsed: use \x01 for the e of Pokemon's accent). Ends with
// CHAR_EOS; at most max characters including it.
int CharCode_FromAscii(const char * text, charcode_t * out, int max);

#ifdef __cplusplus
}
#endif

#endif
