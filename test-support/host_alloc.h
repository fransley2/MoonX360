#pragma once
#include <stdlib.h>
static void *_aligned_malloc(size_t n,size_t alignment){void *p=0;return posix_memalign(&p,alignment,n)==0?p:0;}
static void _aligned_free(void *p){free(p);}
