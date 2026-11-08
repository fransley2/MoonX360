"""Observe the supplied viewer in-process; never loads/reboots/unloads Xbox code.

Starts with a conservative profile, records media evidence outside the repo,
then requests the existing module's prepare-unload acknowledgement. The GUI
stays open; this tool does not hot-unload a DLL or switch console titles.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import threading
import time
import wave


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("viewer", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--wait", type=int, default=120)
    parser.add_argument("--phase-seconds", type=int, default=15)
    args = parser.parse_args()
    args.viewer = args.viewer.resolve()
    args.output = args.output.resolve()
    source = args.viewer.read_bytes()
    sha = hashlib.sha256(source).hexdigest()
    if sha != "f5530813079b892217a1c39469d1ac4a8ca562964fbcd1bf66b6954f6be669ab":
        raise ValueError("Observer requires the audited LAN-patched viewer")
    if not 5 <= args.phase_seconds <= 30 or not 10 <= args.wait <= 180:
        raise ValueError("observation duration exceeds bounds")
    args.output.mkdir(parents=True, exist_ok=False)
    import os
    os.chdir(args.output)
    import tkinter as tk
    namespace = {"__name__": "__main__", "__file__": str(args.viewer.resolve())}
    # Resolve the source path before entering the private working directory.
    start = time.monotonic()
    state = {"source_sha256": sha, "observation_origin": "live external HY37 viewer",
             "started_utc": datetime.now(timezone.utc).isoformat(),
             "game_session_verified": False, "audio_audibility_verified": False,
             "snapshots": [], "phase_images": {}, "prepare_requested": False}
    clock = {"connected_at": None, "phase": "360p", "prepare_at": None}
    log_lock = threading.Lock()
    original_mainloop = tk.Misc.mainloop

    def save(name):
        (args.output / name).write_text(json.dumps(state, indent=2, default=str) + "\n", encoding="utf-8")

    def finish(reason):
        namespace["set_audio_wav_enabled"](False)
        state["result"] = reason
        state["safe_to_unload_reported_by_module"] = bool(namespace["unload_state"]["safe"])
        state["wav_files"] = []
        for path in args.output.glob("*.wav"):
            with wave.open(str(path), "rb") as wav:
                state["wav_files"].append({"file": path.name, "channels": wav.getnchannels(),
                    "sample_rate": wav.getframerate(), "sample_width": wav.getsampwidth(),
                    "frames": wav.getnframes(), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        save("finished.json")

    def tick():
        try:
            now = time.monotonic()
            connected = namespace["client_socket"]["sock"] is not None
            if connected and clock["connected_at"] is None:
                clock["connected_at"] = now
            elapsed = 0 if clock["connected_at"] is None else now-clock["connected_at"]
            audio = namespace["audio_state"]
            state["snapshots"].append({"utc": datetime.now(timezone.utc).isoformat(),
                "connected": connected, "phase": clock["phase"], "connected_seconds": round(elapsed,3),
                "viewer_stats": dict(namespace["stats"]),
                "audio_packets": audio["packets"], "audio_bytes": audio["bytes"],
                "unload": dict(namespace["unload_state"])})
            image = namespace["last_frame_ref"]["image"]
            if image is not None and clock["phase"] not in state["phase_images"]:
                expected_height = 360 if clock["phase"] == "360p" else 480
                if image.height == expected_height:
                    path=args.output/(clock["phase"]+"-first.png")
                    image.copy().save(path)
                    state["phase_images"][clock["phase"]] = {"file": path.name,
                        "width": image.width, "height": image.height,
                        "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
            # Bound recorded PCM to about five seconds after connection.
            if elapsed >= 5 and audio["wav_recording"]:
                namespace["set_audio_wav_enabled"](False)
            if connected and elapsed >= args.phase_seconds and clock["phase"] == "360p":
                clock["phase"] = "480p"
                namespace["send_custom_config"](640,480,45,1,4,10,0,0)
            if connected and elapsed >= args.phase_seconds*2 and clock["prepare_at"] is None:
                namespace["prepare_unload"]()
                state["prepare_requested"] = True
                clock["prepare_at"] = now
            save("progress.json")
            if clock["prepare_at"] is not None:
                if namespace["unload_state"]["safe"]:
                    finish("capture finished; module reported SAFE_TO_UNLOAD");return
                if now-clock["prepare_at"] >= 20:
                    finish("prepare acknowledgement timed out; do not hot-unload");return
            if clock["connected_at"] is None and now-start >= args.wait:
                finish("no media connection observed before timeout");return
            if clock["connected_at"] is not None and not connected:
                finish("media connection closed; unload safety not inferred from EOF");return
        except Exception as error:
            state["observer_error"] = type(error).__name__+": "+str(error)
            save("observer-error.json")
            return
        namespace["root"].after(1000,tick)

    def monitored_mainloop(root, *values, **options):
        old_log=namespace["log"]
        def log(message):
            with log_lock:
                path=args.output/"viewer-events.jsonl"
                if not path.exists() or path.stat().st_size < 1024*1024:
                    with path.open("a",encoding="utf-8") as f:
                        f.write(json.dumps({"utc":datetime.now(timezone.utc).isoformat(),"message":str(message)})+"\n")
            old_log(message)
        namespace["log"] = log
        namespace["send_custom_config"](640,360,45,1,4,10,0,0)
        namespace["set_audio_wav_enabled"](True)
        state["observer_ready"] = True
        save("ready.json")
        root.after(1000,tick)
        return original_mainloop(root,*values,**options)

    tk.Misc.mainloop = monitored_mainloop
    try:
        exec(compile(source, namespace["__file__"], "exec"), namespace)
    finally:
        tk.Misc.mainloop = original_mainloop


if __name__ == "__main__":
    main()
