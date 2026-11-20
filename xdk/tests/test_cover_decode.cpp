#include "../../src/cover_decode.h"
#include "../../src/ui/image.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../third_party/stb/stb_image_write.h"
struct encoded {unsigned char *bytes;size_t len;};
static void output(void *p,void *data,int len){encoded *e=(encoded*)p;e->bytes=(unsigned char*)realloc(e->bytes,e->len+len);assert(e->bytes);memcpy(e->bytes+e->len,data,len);e->len+=len;}
int main(){
 unsigned char pixels[4*3*4];memset(pixels,127,sizeof(pixels));encoded e={};
 assert(stbi_write_png_to_func(output,&e,3,4,4,pixels,12));assert(cover_decode_allowed(e.bytes,e.len));
 int w,h;uint32_t *p=image_decode_argb(e.bytes,e.len,&w,&h);assert(p && w==3 && h==4 && p[0]==0x7f7f7f7f);free(p);
 // PNG IHDR dimension bombs are rejected before any full image allocation.
 e.bytes[16]=0;e.bytes[17]=1;e.bytes[18]=0;e.bytes[19]=0;assert(!cover_decode_allowed(e.bytes,e.len));free(e.bytes);
 e={};assert(stbi_write_jpg_to_func(output,&e,3,4,4,pixels,90));assert(cover_decode_allowed(e.bytes,e.len));
 p=image_decode_argb(e.bytes,e.len,&w,&h);assert(p && w==3 && h==4 && (p[0]>>24)==255);free(p);free(e.bytes);
 assert(!cover_decode_allowed(NULL,0));assert(!cover_decode_allowed((const unsigned char*)"broken",6));
 unsigned char data[1]={0};assert(!cover_decode_allowed(data,COVER_ENCODED_MAX+1));
 puts("PASS: real PNG info/decode, malformed bytes, source dimension bomb and encoded-size cap");
}
