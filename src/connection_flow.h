#pragma once
#include <stdint.h>
enum host_phase { HOST_IDLE, HOST_PROBING, HOST_FINDING, HOST_AUTHENTICATING,
    HOST_PAIRING, HOST_FETCHING_APPS, HOST_LAUNCHING, HOST_NEGOTIATING,
    HOST_WAITING_VIDEO, HOST_WAITING_AUDIO, HOST_STREAMING, HOST_CANCELLING, HOST_FAILED };
#define HOST_PROBE_MS 1200u
#define HOST_DISCOVERY_MS 6000u
#define HOST_AUTH_MS 2400u
#define HOST_NEGOTIATION_MS 20000u
#define HOST_FIRST_MEDIA_MS 15000u
static const char *host_phase_text(int phase)
{
    switch(phase) {
    case HOST_PROBING:return "Connecting to PC...";
    case HOST_FINDING:return "Finding PC on the local network...";
    case HOST_AUTHENTICATING:return "Authenticating...";
    case HOST_PAIRING:return "Pairing...";
    case HOST_FETCHING_APPS:return "Fetching game library...";
    case HOST_LAUNCHING:return "Starting app...";
    case HOST_NEGOTIATING:return "Negotiating video...";
    case HOST_WAITING_VIDEO:return "Waiting for first frame...";
    case HOST_WAITING_AUDIO:return "Waiting for audio...";
    case HOST_CANCELLING:return "Cancelling...";
    case HOST_FAILED:return "PC unavailable";
    default:return "Connecting...";
    }
}
/* Signed difference supports wrap of the 32-bit monotonic clock. */
static unsigned host_remaining(uint32_t now,uint32_t deadline,unsigned ceiling)
{
    int32_t left=(int32_t)(deadline-now);
    return left<=0?0:(unsigned)left<ceiling?(unsigned)left:ceiling;
}
