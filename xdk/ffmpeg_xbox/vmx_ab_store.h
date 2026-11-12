/* Exact-width stores for the FFmpeg 1.2 block ownership contract.
 * The Multiplex kernels' original masked 16-byte RMW stores are intentionally
 * not retained for 4/8-byte blocks: restoring adjacent values is still a write. */
static __forceinline void vmx_ab_store(__vector4 value,uint8_t *dst,int width)
{
    if(width==16) {
        if(!((uintptr_t)dst&15))__stvx(value,dst,0);
        else {__stvlx(value,dst,0);__stvrx(value,dst,16);}
    } else if(!((uintptr_t)dst&3)) {
        value=__vperm(value,value,__lvsr(dst,0));
        __stvewx(value,dst,0);
        if(width==8)__stvewx(value,dst,4);
    } else {
        __declspec(align(16)) uint8_t temporary[16];
        __stvx(value,temporary,0);memcpy(dst,temporary,width);
    }
}
