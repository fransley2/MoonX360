#pragma once

/* The adapter owns the whole MCC decoder lifetime, including presentation.
 * An eventual system implementation must supply its own capability negotiation,
 * stop/drain and cleanup; it must never inherit FFmpeg's RFI advertisement.
 * Only backends with a real implementation belong in the registry. */
struct video_decoder_backend {
    int decoder;
    void (*init_callbacks)(DECODER_RENDERER_CALLBACKS *callbacks);
};
