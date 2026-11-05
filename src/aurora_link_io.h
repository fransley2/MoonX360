#pragma once
/* Exact wire payload operations compiled into the writer and shortcut reader. */
#include <stdio.h>
#include <string.h>
#include "aurora_launch.h"

static size_t moonx_link_write_payload(FILE *file,const struct moonx_launch_data *link)
{return file && link?fwrite(link,1,sizeof(*link),file):0;}
static size_t moonx_link_read_payload(FILE *file,struct moonx_launch_data *link)
{if(!file || !link)return 0;memset(link,0,sizeof(*link));return fread(link,1,sizeof(*link),file);}
