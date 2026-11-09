# MoonX360 stabilization candidate — unpublished draft

**RELEASE CANDIDATE — READY FOR XBOX TESTING** applies only after the accompanying build manifest and PC checks pass. **PUBLIC RELEASE — APPROVED** has not been reached.

MoonX360 adds three Fast Decode modes without changing the controller-first navigation:

- **Off** uses accurate H.264 reconstruction, including required filtering between slices. New installations start here.
- **Partial** skips deblocking only on non-reference pictures. It preserves reference filtering and may provide little benefit when the encoder uses nearly every picture as a reference.
- **Full (experimental)** enables the existing FFmpeg FAST path. Normal deblocking remains enabled, but this old decoder can omit filtering between slices. Artifacts and reference drift remain possible.

Mode changes take effect on the next streaming connection. They do not rebuild the decoder during gameplay. The settings screen, stream information and stopped-session diagnostics identify the active policy.

Legacy `fast_decode=0` remains Off and `fast_decode=1` remains Full. The unsafe legacy `AVDISCARD_ALL` behavior is deliberately removed: Full now retains normal deblocking. This can increase decoding cost. Untagged INI geometry no longer selects a preset that overrides Off/Partial. Existing explicitly selected factory presets retain their effective settings; Smooth/Performance presets can select experimental Full. Check the displayed policy when comparing runs.

Saved profiles use checksummed format 5 to store Partial. Versions 1–4 remain readable. Older MoonX360 binaries cannot read format 5; back up profile/config files before trying this candidate or rolling back.

Diagnostics add explicit active mode, reference/non-reference access-unit counts and offline summary fields for completed FPS and decode p50. Missing measurements remain unavailable. Existing bounded recovery, frame queues, GPU ownership, input, audio, artwork and Aurora launch behavior are retained.

No Xbox performance gain, clean-image result or stutter elimination is claimed. No live host compatibility tests were performed for this candidate. All physical GPU/audio/network/decoder results are **PENDING PHYSICAL XBOX 360 VALIDATION**.

MoonX360 builds on the original XenonLight project and Moonlight technology. Original attribution and GPL/third-party licenses must accompany distribution. This is a local release draft; no GitHub tag, upload or public release is authorized by its creation.
