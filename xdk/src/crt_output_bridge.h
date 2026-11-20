#ifndef MOONX_CRT_OUTPUT_BRIDGE_H
#define MOONX_CRT_OUTPUT_BRIDGE_H

#include "../../src/crt_output.h"

#ifdef __cplusplus
extern "C" {
#endif

void crt_bridge_init(int backbuffer_w, int backbuffer_h);
void crt_bridge_query(struct crt_output_status *out);
bool crt_bridge_enter_240p(int width, int height);
bool crt_bridge_exit_240p(void);
void crt_bridge_on_system_ui(bool active);
bool crt_bridge_is_240p_active(void);
int crt_bridge_last_reason(void);
const volatile struct crt_s240_beacon *crt_bridge_beacon_ptr(void);

#ifdef __cplusplus
}
#endif

#endif /* MOONX_CRT_OUTPUT_BRIDGE_H */
