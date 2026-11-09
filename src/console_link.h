#pragma once
#include <stdint.h>
enum console_link {CONSOLE_LINK_UNKNOWN,CONSOLE_LINK_WIRED,CONSOLE_LINK_WIRELESS};
/* Masks come from the installed platform SDK; inactive is always Unknown. */
static int console_link_classify(uint32_t flags,uint32_t active,uint32_t wireless)
{
    return !(flags&active)?CONSOLE_LINK_UNKNOWN:(flags&wireless)?CONSOLE_LINK_WIRELESS:CONSOLE_LINK_WIRED;
}
static const char *console_link_name(int link)
{
    return link==CONSOLE_LINK_WIRED?"Wired":link==CONSOLE_LINK_WIRELESS?"Wireless":"Unknown";
}
