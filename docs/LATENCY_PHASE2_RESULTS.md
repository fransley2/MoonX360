# Latency phase 2 results — 2026-11-02

## Scope and evidence

The physical reference is the single 45.036 s GTA: San Andreas capture from
2026-11-01 in [LATENCY_BASELINE_XBOX360_20261101.md](LATENCY_BASELINE_XBOX360_20261101.md).
Its known-good local XEX is `xdk/build/steam-library-ab-20261101/default.xex`,
SHA-256 `0e62c46c05b59bdb39cf9075c30edbc074a9f559168d7e502e2029ffcdaeeb44`.
That file and the existing production XEX were left untouched. The reference
console XEX's full hash could not be obtained in the original capture.

Raw phase 2 evidence is in
[`evidencias/latency-phase2-20261102-151441/`](../evidencias/latency-phase2-20261102-151441/).
`git-status-before.txt`, `working-tree-before.patch`, `baseline-hashes.json`,
and `phase2-manifest.json` record the starting state and hashes. The checkout
already had local changes when this run began. The separately built current-tree
control is **not** the physically validated XEX; its different SHA-256 prevents
treating it as a repeat of the physical baseline.

## Tests completed automatically

| Check | Result | Evidence |
| --- | --- | --- |
| Read-only XBDM probe of reference `192.168.15.9` | `DmGetXbeInfo` returned `0x82da0100`; no title or stream identified | `console-before.json` |
| Read-only XBDM probe of former address `192.168.15.8` | Reachable, but current title is `Apps\\Aurora\\Aurora.xex` | `console-alternate-address.json` |
| Current-tree control XDK build, aggressive profile | Complete XEX; SHA-256 `26390fa1fdad3aa4eb53596b572023213ea60ca8a6cd9ef7672df86ea6eccc26` | `control-build.log`, `control-current.xex` |
| Isolated 2 ms input polling build, aggressive profile | Complete XEX; SHA-256 `de2e11fa2c452137d6f42623fe641eceb9bc50be2ca0a9ed550128b255f42f4b` | `poll2-build.log`, `poll2-final-link.log`, `poll2-test.xex` |
| XEX inspection | Both `imagexex /dump` calls passed; candidate has XEX2 header, `xam.xex` and `xboxkrnl.exe` imports, title ID `4D583336`, and embedded SPA resource; build script extracted and byte-compared the SPA | `control-imagexex-dump.txt`, `poll2-final-imagexex-dump.txt` |
| Host input/render fixture | Current 4 ms control and isolated 2 ms fixture passed, including overrun yield, tick wrap, quit/release and render ownership | `control-latency-fixture.log`, `poll2-host.log`, `poll2-latency.cpp` |
| Source and build isolation | `xdk/src/input_xdk.cpp` restored byte for byte; only that app object differs between the two builds. Final links use the same `moonlight.lib` bytes. | `poll2-candidate.patch`, `phase2-manifest.json`, `build-differences.json` |

The 2 ms test changes only `SEND_INTERVAL_MS` from 4 to 2. The candidate patch
and both XEXs are preserved in the evidence directory. No performance change
was applied to the working source. XEX structure and host fixtures do not prove
that either new XEX launches or streams on the console.

## Tests requiring physical interaction or a working automatic launch path

**NOT TESTED:** candidate deployment, launch, stream, controller input,
post-run diagnostic collection, and physical A/B. The reference XBDM address
was unavailable. The alternate address was at Aurora, and the existing tools
only provide read-only title/diagnostic collection; they do not establish a
verified safe path to launch this test XEX. No XEX was uploaded, no existing
console file was overwritten, and no reboot or recovery action was attempted.
There was no candidate run after which fresh diagnostics could be collected.

## Baseline versus candidate metrics

The physical baseline is one game/scene/route only. Percentiles below are from
bounded recent-sample rings within that capture. The current-tree control and
2 ms candidate have **no physical measurements**, so no latency or FPS delta
can be computed.

| Metric | Physical baseline, 4 ms source | Current-tree control | 2 ms candidate |
| --- | ---: | --- | --- |
| Input poll interval p50 / p95 / p99 | 4.984 / 5.112 / 5.850 ms | NOT TESTED | NOT TESTED |
| Controller sample to enqueue p95 | 0.030 ms | NOT TESTED | NOT TESTED |
| Input holder age at dequeue p95 | 0.014 ms | NOT TESTED | NOT TESTED |
| Input queue depth p95 / max | 1 / 1 packet | NOT TESTED | NOT TESTED |
| Input send work p95 / p99 | 0.089 / 0.185 ms | NOT TESTED | NOT TESTED |
| Complete RTP/FEC frames | 31.886 FPS | NOT TESTED | NOT TESTED |
| Decoder callbacks / decoded / Present calls | 31.886 / 31.886 / 31.886 FPS | NOT TESTED | NOT TESTED |
| H.264 decode p95 / p99 | 12.192 / 12.834 ms | NOT TESTED | NOT TESTED |
| Present interval p50 / p95 | 29.691 / 44.020 ms | NOT TESTED | NOT TESTED |
| Frame loss notifications / FEC recovered data packets | 0 / 4 | NOT TESTED | NOT TESTED |

## Individual experiment findings

### Input polling

The baseline's approximately 5 ms measured poll interval is the one local
stage worth an isolated trial. A 2 ms XEX was built and passed the existing
cadence fixture with the expected waits changed to 2/1/1/1/1 ms. **Physical
polling interval, CPU contention, stability, and input-to-photon impact: NOT
TESTED.** The source change was not retained pending a comparable A/B run.

### InputSend queue and priority

The baseline shows queue depth p95 and max of one, holder age p95 14 us,
zero queue-full events, and send work p95 89 us. These observations do not
justify queue-size or priority tuning yet. **Queue/priority candidate and
physical impact: NOT TESTED.** No queue or thread-priority code was changed.

### RTP delay

The `RTP_QUEUE_DELAY` definition in the current Moonlight `VideoStream.c` has
no active use, so changing that constant would not make a meaningful A/B
candidate. Observed gap wait was four samples, p95 33 us. **RTP-delay
candidate and physical impact: NOT TESTED.** No RTP behavior was changed.

### Packet size

The baseline requested 1024-byte packets and observed 27,725 UDP packets in
45.036 s. Host negotiation, payload distribution and loss under other packet
sizes were not measured. **Packet-size A/B: NOT TESTED.** No packet-size
candidate was built or deployed.

### FEC and loss

Four data packets were recovered by FEC, with zero failed data deficit and
zero frame-loss notifications in the reference capture. Exact network packet
loss is unavailable; expected parity counts cannot be read as actual packet
loss. **Impaired-network/FEC A/B: NOT TESTED.** FEC was not changed.

### Decoder and presentation

H.264 decode p95 was 12.192 ms, decode-unit age p95 10 us, and Present call
CPU duration p95 47 us. Complete RTP/FEC frame rate already matched decoder
callback, decoded, and Present rates at 31.886 FPS. The capture does not
separate host production from network delivery, and Present call timing is not
scanout timing. **Decoder/presenter change and physical A/B: NOT TESTED.** No
decoder or presenter change was made.

## Changes retained and rejected

- **Retained:** this report, the uniquely named raw evidence directory, the
  isolated current-tree control XEX, the isolated 2 ms test XEX, and the
  `.gitignore` exception that makes this report visible to Git.
- **Rejected for production in this run:** applying 2 ms polling to the working
  source without a physical A/B result. The source file was restored exactly.
- **Not attempted:** queue/priority, RTP, packet size, FEC, and decoder/presenter
  tuning, because their individual baseline evidence does not identify a
  measurable local bottleneck or the required automatic A/B path is unavailable.

## Recommended next physical test

Once the reference console and an approved safe test launch path are available,
first repeat the 45 s GTA: San Andreas baseline with the exact known-good XEX,
Smooth 720p60, 12 Mbps, 1024-byte packets, the same host, route, scene, capture
setting and duration. Then run the current-tree 4 ms control and 2 ms candidate
**separately** from test filenames, collecting fresh diagnostic files and XBDM
title state after each run into new directories. Verify both new XEXs launch
before attributing an A/B difference to polling, since their source is newer
than the known-good physical baseline. Compare poll p50/p95/p99, InputSend age
and work, complete-frame/decoded/Present FPS, decode age, present interval,
loss/FEC counters and stability. If a run hangs, requires Aurora or controller
navigation, or XBDM becomes unreachable, stop that test and mark it NOT TESTED.
