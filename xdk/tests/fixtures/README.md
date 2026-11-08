# Historical worker-generator fixture

`xb_thread_pinned.c` is the locally tracked, pinned worker source accepted by
`patch_worker_map.validate()`. Its unmapped normalized SHA-256 is
`a2ecc0e814e446765adaf95842b1b683e04411a72eda67c14b81af1a27a5a533`.
It tests the historical opt-in affinity/batched generator without using or changing
the newer local fast-decoder worker implementation. Unknown current sources still
fail closed in production. This fixture is never compiled into MoonX360.
