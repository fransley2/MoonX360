# Known limitations

- Xbox 360 decodes H.264 on the CPU. HEVC, AV1, and HDR are not supported.
- Smooth requests 1280×720 at 60 FPS and 12 Mbps; these are host requests, not a guarantee of received, decoded, or displayed frame rate.
- 1080p (Experimental) requests 1920×1080 at 24 FPS and 8 Mbps. Console performance can be poor and has not been guaranteed.
- This release build record marks hardware testing as false. Do not treat host fixtures, source inspection, or XEX packaging checks as physical console or LAN validation.
- Sunshine, Apollo, and Vibepollo compatibility paths are present in the source. This release has not been validated against a live host from each project.
- The project requires a separately obtained Xbox 360 SDK to build the dashboard XEX; SDK components are not included.
