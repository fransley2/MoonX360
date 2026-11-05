# Protocol plan and current implementation boundary

## Existing XBDM (TCP 730)

`host/xbdm.py` implements one ASCII command per TCP session, a three-digit
status, dot-terminated `202` text replies, and bounded `203` binary replies.
`getfile` starts with a little-endian 32-bit length. `screenshot` starts with
an ASCII metadata line containing `pitch`, `width`, `height`, `format`, and
`framebuffersize`; exactly that many binary bytes must arrive. Errors record
status, remote EOF or timeout, and expected/received bytes. XBDM stays a
separate existing console service.

## Remote input proof of concept

`host/input_poc.py` is a **local state model only**. It accepts versioned
session context supplied by a future transport, strictly increasing sequence
numbers, sender and receiver timestamps, heartbeat, full state, individual
press/release, and release-all. It models buttons including Guide, keyboard
keys, four stick axes and two triggers. A 500 ms disconnect watchdog releases
all remote state; physical takeover also releases it. Tap, hold, combo and
sequence are client-side schedules of those transitions and need a real
service plus hardware tests before they count as supported controls.

There is no network listener, authenticated pairing, XAM hook, virtual USB
device, or gamepad injection yet. The agent must never accept remote input
until the heartbeat-only plugin survives measured title transitions, local
recovery is verified, and its system-module export warning is resolved.

The future input service should bind only to a trusted LAN interface, pair
with a random per-console secret, authenticate every packet, rate-limit
updates, keep bounded queues, and make disconnect neutral. Video/audio is a
separate optional pipeline; its absence must not affect input release.
