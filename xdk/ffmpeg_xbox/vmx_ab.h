/* Offline experiment only. No candidate is installed in streaming tables. */
#ifndef MOONX_VMX_AB_H
#define MOONX_VMX_AB_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*vmx_ab_qpel_fn)(uint8_t *, uint8_t *, int);
typedef void (*vmx_ab_chroma_fn)(uint8_t *, uint8_t *, int, int, int, int);
struct vmx_ab_tables {
    vmx_ab_qpel_fn qpel[2][2][16]; /* size 16/8, put/avg, x+4*y */
    vmx_ab_chroma_fn chroma[2][2]; /* width 8/4, put/avg */
};
struct vmx_ab_callbacks {
    void *context;
    uint64_t (*clock)(void *context);
    uint64_t frequency;
    int (*emit)(void *context, const char *line);
    int (*cancel)(void *context);
    /* Optional 128x48 Y tile copied from a decoded real capture, never a
     * decoder-owned pointer. NULL is reported as real-reference unavailable. */
    const uint8_t *reference;
    int integrity_only;
    int strict_destination_alignment; /* native: honor FFmpeg dst alignment contract */
};
void ff_xbox_vmx_ab_tables(struct vmx_ab_tables *reference,struct vmx_ab_tables *moon);
void ff_multiplex_qpel_tables(struct vmx_ab_tables *tables);
void ff_multiplex_chroma_tables(struct vmx_ab_tables *tables);
/* 0 passed, 1 mismatch (candidate timing suppressed), 2 cancelled, -1 I/O/config error. */
int ff_xbox_vmx_ab_run(const struct vmx_ab_callbacks *callbacks);
#ifdef __cplusplus
}
#endif
#endif
