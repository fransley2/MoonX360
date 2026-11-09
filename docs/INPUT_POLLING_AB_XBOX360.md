# Xbox 360 input polling A/B — 2026-11-02

## Decision

**A/B result: NOT TESTED.** There are no fresh physical captures for A or B.
The 2 ms build remains an isolated, unvalidated test XEX. It is **not retained
as a Low Latency profile setting** and Smooth/default remains at 4 ms. There is
no evidence yet to accept or reject 2 ms on performance or stability grounds.
No optimization, deployment, console reboot, or production XEX replacement was
performed in this continuation.

## Builds and verification

| Build | Poll interval in source | SHA-256 | Status |
| --- | ---: | --- | --- |
| A — current-tree control, `evidencias/latency-phase2-20261102-151441/control-current.xex` | 4 ms | `26390fa1fdad3aa4eb53596b572023213ea60ca8a6cd9ef7672df86ea6eccc26` | Local evidence and build-directory copy match Phase 2; physical launch NOT TESTED |
| B — isolated candidate, `evidencias/latency-phase2-20261102-151441/poll2-test.xex` | 2 ms | `de2e11fa2c452137d6f42623fe641eceb9bc50be2ca0a9ed550128b255f42f4b` | Local evidence and build-directory copy match Phase 2; physical launch NOT TESTED |

The separately preserved known-good XEX still hashes to
`0e62c46c05b59bdb39cf9075c30edbc074a9f559168d7e502e2029ffcdaeeb44`.
It differs from A and was **not** substituted for the current-tree control.
Full verification output is in
[`evidencias/input-polling-ab-20261102-152937/`](../evidencias/input-polling-ab-20261102-152937/)
(`xex-hashes-before.json` and `xex-hashes-final.json`).

## Physical attempt and stopping point

1. A read-only XBDM title probe at the reference Xbox address
   `192.168.15.9` returned `DmGetXbeInfo HRESULT 0x82da0100`. It did not
   identify a running title. See `console-15-9.json`.
2. The prior alternate address `192.168.15.8` answered and identified the
   current title ending in `Apps\MoonX360\MoonX360-rebuilt-20261101.xex`,
   **not A or B**. See `console-15-8.json`. Its file hash was not read from the
   console, so the title path alone does not establish binary identity.
3. A read-only diagnostic collection from `.8` succeeded, but its
   `latency-capture.txt` was already present before either test XEX was
   deployed or launched. It records 45.045 s and is **not a fresh A capture**.
   Its connection log refers to host `192.168.15.4`. The files and collection
   hashes are under `preexisting-diagnostics/`.
4. The previously recorded Apollo address `192.168.15.10` timed out on TCP
   port 47984; `192.168.15.4` answered. This checks reachability only and
   cannot establish whether `.4` is the same Apollo host. See
   `host-port-probe.json`.
5. The available workflow requires selecting a game in MoonX360 with a
   controller. There is no verified automated path here to launch A/B and
   select the same GTA: San Andreas scene. The test stopped before deployment
   or stream launch. No console recovery action was attempted.

Thus the same Apollo host, GTA scene, Smooth 1280×720, requested 60 FPS,
12 Mbps, 1024-byte packets, VSync ON, route, and ~45 s duration could not be
established for either build. The preexisting report is not used in the A/B
table. No diagnostics were produced by a new A or B run.

## Exact A/B measurements

Each build completed **0 of 2** proposed physical repetitions. `NOT TESTED`
means there is no measurement for that build, not a measured value of zero.

| Measurement | A — 4 ms | B — 2 ms | A/B conclusion |
| --- | --- | --- | --- |
| Input poll interval p50 / p95 / p99 | NOT TESTED | NOT TESTED | No observed cadence improvement |
| Controller local latency p50 / p95 / p99 | NOT TESTED | NOT TESTED | No comparison |
| InputSend queue age | NOT TESTED | NOT TESTED | No comparison |
| InputSend work | NOT TESTED | NOT TESTED | No comparison |
| Complete-frame FPS | NOT TESTED | NOT TESTED | No comparison |
| Decode FPS | NOT TESTED | NOT TESTED | No comparison |
| Present FPS | NOT TESTED | NOT TESTED | No comparison |
| H.264 decode p95 / p99 | NOT TESTED | NOT TESTED | No comparison |
| Decode queue age | NOT TESTED | NOT TESTED | No comparison |
| FEC recovery / failed deficit / frame loss | NOT TESTED | NOT TESTED | No comparison |
| CPU contention and stability | NOT TESTED | NOT TESTED | No comparison |

The existing `controller_local_latency` starts when a changed controller
state is detected, after any wait for the next poll. It cannot, by itself,
establish the expected 2 ms benefit. A `poll_wait_budget` estimate was **not
calculated** because there is no observed A/B poll cadence. A future estimate
may use half the measured poll interval as a simple average-wait model for
uniformly timed input, labeled as an estimate rather than input-to-photon
latency. It must not replace the actual poll distribution or subjective test.

## Subjective controller and video checks

| Check | A | B |
| --- | --- | --- |
| Analog responsiveness | NOT TESTED | NOT TESTED |
| Button responsiveness | NOT TESTED | NOT TESTED |
| Stuck buttons | NOT TESTED | NOT TESTED |
| Missing releases | NOT TESTED | NOT TESTED |
| Controller disconnect behavior | NOT TESTED | NOT TESTED |
| Additional stream stutter | NOT TESTED | NOT TESTED |

## Next controlled run

Use the exact preserved A and B XEXs from the hashes above, each from a
separate test filename while preserving the known-good production XEX. Confirm
the console title and the same Apollo host identity before every run. Use the
same GTA scene, Smooth settings and route, enable diagnostic capture, stream
for about 45 s, return normally, and collect reports into new A1/B1 and, if
practical, A2/B2 directories. Record subjective controller observations during
each run. If a launch fails, a console action is required, or XBDM becomes
unreachable, stop that specific run and mark it NOT TESTED. Consider retaining
2 ms for the Low Latency profile only if observed poll cadence improves
clearly, decode p99/FPS and stream stability do not regress, and controller
behavior remains correct. Keep Smooth/default at 4 ms during this decision.
