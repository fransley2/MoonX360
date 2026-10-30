# Console and PC setup

These instructions describe the current dashboard implementation with the menu language set to **English**. Spanish and Brazilian Portuguese translations are available under **Settings → Appearance and display → Language**. Game and host names come from the server. The XeLL backend has different platform capabilities; see [BUILD.md](BUILD.md).

## Prepare the PC server

Install a GameStream-compatible host server from its official source. [Sunshine](https://github.com/LizardByte/Sunshine) is the primary example used by this project's inherited setup flow. Configure capture, an H.264-capable encoder, application entries and input permissions according to the server's documentation. MoonX360 requests H.264; do not require HEVC or AV1 for this client. Keep an active desktop/display or a host-supported virtual display available for capture.

Sunshine operation is reported by upstream Xenonlight, including pairing and 720p30 streaming. This publication does not independently verify a Sunshine version or encoder combination on hardware. Strings referring to Vibepollo or Sunshine in the pairing screen are not compatibility certification. Apollo and other servers are unverified for this snapshot.

Check that the desired game or desktop is listed in the server's applications. For launch failures, inspect the server application's executable/command and working directory and its logs. Configure the server's virtual gamepad support if its platform requires a driver. Confirm capture and input using another supported client when isolating a host problem.

## Network and console installation

Connect PC and Xbox 360 to the same reachable LAN. Prefer Ethernet for initial diagnosis. Configure the console's networking normally; the dashboard backend uses XNet/Winsock and the system connection. Avoid guest/client isolation between the two devices. Permit the streaming server through the host firewall using its documented ports and discovery rules. Do not disable the firewall globally. Internet port forwarding is not needed for same-LAN use and is not covered here.

Copy your locally built `default.xex` to a writable console folder and launch it using Aurora, FSD or XeXMenu. See [BUILD.md](BUILD.md). Keep the adjacent `moonlight` folder during updates. The application embeds its fonts, brand and shaders; runtime settings, cached artwork and identity files are created separately.

Use the D-pad or menu stick navigation, **A** to select and **B** to return; follow the on-screen button hints for the current screen. Main tabs are **Home**, **Library**, **Stream** and **Settings**.

## Find and select the PC

On **Home**, **Y** opens the host selector. You can also open **Settings → PCs and connection → Saved and discovered PCs**. **Find PCs automatically (mDNS)** controls discovery of `_nvstream._tcp.local.` services. The client keeps a saved-host cache; a cached entry does not prove the PC is currently reachable. **Y** in the host selector searches again.

Select the PC with **A**, or choose **+ Add host manually** and enter its IPv4 address using the displayed editor controls. Use the PC's current address on the LAN that reaches the console. Manual entry can bypass failed discovery, but does not bypass firewall, routing or server problems. **X** on a saved PC opens its settings.

**Host actions** includes **Refresh host**, **Pair with host**, **Stop app**, **Unpair**, **Wake PC**, **Host MAC**, **Host settings** and **Connection diagnosis**. **Wake PC** requires a valid saved MAC and firmware/NIC/power configuration on the PC; sending a packet does not guarantee the host wakes. A manually entered MAC and automatic wake options are supported. Wake behavior on a particular LAN remains a hardware check.

## Pair by PIN

Select the correct reachable PC, open **Host actions → Pair with host**, and enter the PIN displayed by MoonX360 into the host server's PIN/pairing interface. Keep the pairing flow open while completing it on the PC. Refresh the host afterward if needed and check for the **Paired** state.

The dashboard saves its generated client identity in `game:\moonlight\client.pem` and `key.pem`. Keep these files private. Moving the executable without its data folder can create a new identity and require pairing again. Use **Unpair** for an intentional pairing reset. If the application explicitly reports an unreadable stored certificate, back up the private data securely before removing `client.pem` and `key.pem` together and pairing again; this changes the client identity.

## Launch and stop applications

Open **Library**, choose a game/application and press **A** to launch or resume it. Applications come from the host list; MoonX360 does not install PC games or supply a cloud catalog. The library has **Recent**, **Favorites**, **All Apps** and **Desktop** filters, and application details/context actions. **Home** offers recent items and **Continue** when relevant.

Disconnecting a stream can leave the application running on the PC. **Host actions → Stop app** explicitly requests stopping the current host application. Check the confirmation/action text before using it.

## Video, audio and controls

Under **Stream**, **Stream Profile** selects a factory preset. **Stream adjustments** contains **Video resolution**, **Frames per second (FPS)**, **Video bitrate (Mbps)**, **Automatic quality adjustment**, **V-Sync** and **Image quality**. Resolution choices are 854×480, 1280×720 and 1920×1080; FPS choices are 24, 30, 40, 50 and 60. Bitrate ranges from 1 to 70 Mbps in 1 Mbps steps through 30, then 5 Mbps steps.

These values request a host stream. They do not guarantee decoder throughput or presentation rate. Start with a lower resolution/FPS/bitrate if playback struggles and compare measured decoded/shown rates on the console. Neither a low preset nor a high preset is a universal recommendation. Avoid changing several variables at once when diagnosing performance.

Factory profile values in the current source are:

| Stream Profile | Resolution | Requested FPS | Requested Mbps |
| --- | --- | ---: | ---: |
| Performance | 854×480 | 60 | 25 |
| Standard | 1280×720 | 30 | 12 |
| Smooth | 1280×720 | 60 | 18 |
| 1080p Smooth | 1920×1080 | 60 | 20 |
| Maximum Quality | 1280×720 | 60 | 35 |
| Custom | Manually selected | Manually selected | Manually selected |

Fresh `config_defaults()` selects **Custom**, 1280×720, 60 FPS and 30 Mbps, with Fast Decode and V-Sync on, stereo, four decode workers and Balanced CPU layout. Older saved configurations retain their values. Some in-app help incorrectly describes Standard/30 FPS as the fresh default; the executable settings logic takes precedence. Preset names describe configurations, not measured stability.

**Performance**, **Smooth** and **1080p Smooth** force effective Fast Decode on and the lighter **Fast** GPU image policy. Other presets retain the existing decoder/image selections. Manual stream/decoder/image edits can select **Custom** while retaining the effective options. **Automatic quality adjustment** can reconnect with lighter temporary settings under pressure (up to three attempts); it does not overwrite a named profile and does not promise constant FPS.

**Stream → Audio** offers **Xbox audio output** (**Stereo** or **5.1**) and **Play audio on PC**. Start with Stereo when diagnosing missing channels or failed audio initialization. Selecting surround also requires matching host negotiation, content and console/output support; physical channel mapping has not been verified for this publication.

**Settings → Controllers and mouse** includes **Calibrate sticks**, **Disconnect shortcut** and **Remote mouse and keyboard**. The default stream-disconnect shortcut is to hold **LB + RB + left-stick click + right-stick click** for about two seconds. The alternate setting uses **Back + Start + LB + RB**. This is a disconnect action, not necessarily termination of the game on the PC.

During dashboard streaming, **Back + right-stick click** toggles controller mouse mode. The left stick moves the pointer, the right stick scrolls, and **A/B** are left/right mouse buttons. **Remote mouse and keyboard** exposes **Mouse sensitivity**, **Mouse acceleration** and **Scroll interval**. Connected keyboard events can be forwarded during streaming; the local UI suppresses remote input when it owns editing. Device/server behavior requires physical testing; physical mouse support is not asserted.

**Back + left-stick click** opens the quick menu for local stream actions, volume/mute, mouse mode and statistics. **Settings → Appearance and display → Streaming interface** configures **Compact**/**Sidebar** quick-menu presentation and the mouse indicator. Return controls to neutral after local UI or Guide transitions so held input is not sent unexpectedly. The Guide remains the console's system interface.

## Fast Decode, image processing and statistics

Open **Settings → Advanced → Decoding** for **Fast decode**, **H.264 slices**, **CPU layout** and **Decode workers**. Fast Decode on skips all H.264 deblocking in the CPU decoder. This reduces CPU work and can make block edges more visible. Off requests normal filtering, but overload recovery can temporarily reduce filtering on non-reference frames; **Reduced (recovery)** can therefore appear with Fast Decode off. Requested slices are Auto/8/12/16; the host controls the actual encoded slice layout. Workers are 3 or 4, and layouts are **Balanced** or **Legacy**; neither setting is a verified best choice for every stream.

**Image quality** controls GPU image processing separately from H.264 filtering. **Auto Quality** can try **Fast** after submission-time measurements; **Quality** retains the smoothing policy. These timings are CPU-side command-submission observations, not GPU utilization measurements. The dashboard renderer uses the GPU; shared CPU renderer code is not a selectable dashboard H.264 hardware-decoding path.

**Settings → History and performance → On-screen statistics** has four levels:

| Level | Information |
| --- | --- |
| Off | No performance overlay |
| Basic | Resolution, shown/target FPS, actual/configured bitrate, status, H.264 filter/Fast Decode and GPU image mode |
| Detailed | Basic plus decode budget/time, RTT, queue age, skipped display/decode errors, audio queue and host processing |
| Developer | Detailed plus received/decoded rates, rolling decode p95/p99, workers/slices/recovery, pipeline/copy/submit/fence/Present timings and display/backlog details |

Text updates approximately once per second. Statistics have a cost; Off avoids overlay collection/drawing overhead. Displayed payload Mbps is not total network traffic. Frame gaps/skipped displays do not prove packet loss, and loss/FEC metrics are unavailable.

## Saving and storage

Ordinary settings are saved when leaving settings; check any error message. Changes normally apply on the next connection. **App resolution** under **App display** requires restarting the app and is separate from the stream resolution and detected physical TV output.

To save a named stream profile, open **Stream → Saved profiles → Save current configuration**. The client creates a **My profile N** entry. Select it to open **Profile actions**, then **Apply**, **Rename** or **Delete**. **Apply** saves it for the next connection. **Rename** uses the controller editor: D-pad changes position/character, **X** deletes, **Y** inserts a space, **A** saves and **B** cancels. Delete requires confirmation and affects only the named profile. Up to eight named profiles are supported, with names limited to 63 UTF-8 bytes.

Named profiles include stream, decoder, renderer and audio options. Host, language, control calibration and diagnostics stay global. Application details can bind a factory/named profile to a game; **Use current settings** removes that binding. Reassign a named per-game binding if its profile is renamed or deleted. Temporary per-game settings are restored after streaming.

The dashboard stores data in `game:\moonlight`, next to the launched XEX:

| Files | Purpose |
| --- | --- |
| `moonlight.ini` | Main preferences |
| `profiles-a.tsv`, `profiles-b.tsv` | Alternating validated generations of named profiles |
| `hosts.tsv` | Saved hosts, addresses and host metadata |
| `sessions.tsv`, `favorites.tsv`, `game-bindings.tsv` | History, favorites and per-game profiles |
| `client.pem`, `key.pem` | Private client pairing identity |
| Artwork cache and optional diagnostic/report files | Runtime resources and troubleshooting |

Some state writes use verified replacement with `.new`/`.bak`; this does not establish power-loss atomicity on FATX. Never publish the data folder. If saving fails, check free space and write permissions; an in-memory selection may remain usable only until the app closes. Do not edit alternating profile files manually.

## Troubleshooting

| Symptom | Checks |
| --- | --- |
| PC not discovered | Confirm server and console are on a reachable LAN, mDNS enabled, no client isolation and firewall discovery rules allowed. Search again or use manual IPv4 entry. |
| PC listed but connection fails | A cached PC may be offline. Use **Refresh host** and **Connection diagnosis**. Confirm current address, server listening/capture readiness and firewall streaming rules. |
| PIN/pairing fails | Choose the correct host, complete its PIN prompt while the client waits, and check server logs. Preserve the saved identity; regenerate only when intentionally resetting or resolving the explicit unreadable-certificate error. |
| Black screen | Verify H.264 capture works on the host and a display/capture target exists. Try lower requested settings and a simple desktop stream. Check decoder errors and server logs rather than assuming GPU decoding is available. |
| Stutter or freezes | Compare received/decoded/shown FPS, decode budget and queue age. Lower bitrate/resolution/FPS separately, compare Fast Decode, and test Ethernet. Record build/settings/content; no preset guarantees success. |
| Crash or hard hang | Enable **Diagnostic capture** before reproduction and examine **View logs** / **Recovery information** after restart. Checkpoints show progress, not a crash backtrace; a hard crash can lose the RAM log. |
| No audio or missing channels | Try Stereo, check PC stream audio capture and **Xbox audio output**, and check console/TV volume and quick-menu mute. **Play audio on PC** controls concurrent host playback, not Xbox volume. |
| Controller or keyboard fails | Test the controller locally, reconnect it, check mouse mode/local UI ownership and calibration, then verify the PC server's virtual input driver and the game's gamepad settings. Compare another game/client to isolate host or game behavior. |
| Settings disappear | Keep the same executable/data-folder location, check writable storage and save errors, and preserve both profile generation files during updates. |

For diagnostics, **Settings → Advanced → Technical diagnostics → Logs and report** contains **Diagnostic capture**, **View logs** and **Save session report**. Reports may contain private host information; redact before sharing. A saved report is not proof of sustained performance. See [LIMITATIONS.md](LIMITATIONS.md) for measurement boundaries.
