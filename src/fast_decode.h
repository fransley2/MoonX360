/* Boolean runtime policy. Value 2 is accepted only for format-5 compatibility. */
#pragma once
#include <stdbool.h>
enum { FAST_DECODE_OFF=0, FAST_DECODE_ON=1, FAST_DECODE_COMPAT_VALUE=2 };
static int fast_decode_validate(int value) { return value==1 || value==2 ? 1 : 0; }
static int fast_decode_parse(const char *value) { return value && value[0]>='0' && value[0]<='2' && value[1]==0 ? fast_decode_validate(value[0]-'0') : 0; }
static const char *fast_decode_name(int value) { return fast_decode_validate(value) ? "On" : "Off"; }
