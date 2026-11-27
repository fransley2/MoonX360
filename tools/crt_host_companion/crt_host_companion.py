#!/usr/bin/env python3
"""
MoonX360 CRT Host Companion (Experimental Prototype)

Clean-room Python 3 stdlib helper for Windows hosts running Sunshine / Apollo
and RetroArch. Uses safe --dry-run defaults for every state-changing action:
- Enumerates / validates Win32 display modes (CDS_TEST before applying).
- Saves a JSON display state snapshot prior to switching and supports explicit
  `restore` + `atexit` automatic restoration for supervised runs.
- Generates RetroArch per-core/override `.cfg` snippets.
- Generates Sunshine/Apollo `prep-cmd` (`do` / `undo`) JSON entries with an
  explicit 4-layer resolution mapping (Game, Capture, Stream, Output).
- Optionally invokes an already-installed external `switchres` CLI tool (never
  bundles third-party SwitchRes code).
"""

from __future__ import annotations

import argparse
import atexit
import ctypes
from ctypes import wintypes
import json
import os
import shutil
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from typing import Any, Callable, Dict, List, Optional


# Win32 Display constants (clean-room definitions from public Win32 API docs)
ENUM_CURRENT_SETTINGS = -1
DM_BITSPERPEL = 0x00040000
DM_PELSWIDTH = 0x00080000
DM_PELSHEIGHT = 0x00100000
DM_DISPLAYFLAGS = 0x00200000
DM_DISPLAYFREQUENCY = 0x00400000

CDS_UPDATEREGISTRY = 0x00000001
CDS_TEST = 0x00000002
CDS_FULLSCREEN = 0x00000004
CDS_RESET = 0x40000000

DISP_CHANGE_SUCCESSFUL = 0
DISP_CHANGE_RESTART = 1
DISP_CHANGE_FAILED = -1
DISP_CHANGE_BADMODE = -2
DISP_CHANGE_NOTUPDATED = -3
DISP_CHANGE_BADFLAGS = -4
DISP_CHANGE_BADPARAM = -5

DISPLAY_DEVICE_PRIMARY_DEVICE = 0x00000004
DISPLAY_DEVICE_ATTACHED_TO_DESKTOP = 0x00000001

DEFAULT_SNAPSHOT_PATH = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "crt_host_snapshot.json",
)


class DEVMODEW(ctypes.Structure):
    _fields_ = [
        ("dmDeviceName", wintypes.WCHAR * 32),
        ("dmSpecVersion", wintypes.WORD),
        ("dmDriverVersion", wintypes.WORD),
        ("dmSize", wintypes.WORD),
        ("dmDriverExtra", wintypes.WORD),
        ("dmFields", wintypes.DWORD),
        ("dmPositionX", wintypes.LONG),
        ("dmPositionY", wintypes.LONG),
        ("dmDisplayOrientation", wintypes.DWORD),
        ("dmDisplayFixedOutput", wintypes.DWORD),
        ("dmColor", ctypes.c_short),
        ("dmDuplex", ctypes.c_short),
        ("dmYResolution", ctypes.c_short),
        ("dmTTOption", ctypes.c_short),
        ("dmCollate", ctypes.c_short),
        ("dmFormName", wintypes.WCHAR * 32),
        ("dmLogPixels", wintypes.WORD),
        ("dmBitsPerPel", wintypes.DWORD),
        ("dmPelsWidth", wintypes.DWORD),
        ("dmPelsHeight", wintypes.DWORD),
        ("dmDisplayFlags", wintypes.DWORD),
        ("dmDisplayFrequency", wintypes.DWORD),
        ("dmICMMethod", wintypes.DWORD),
        ("dmICMIntent", wintypes.DWORD),
        ("dmMediaType", wintypes.DWORD),
        ("dmDitherType", wintypes.DWORD),
        ("dmReserved1", wintypes.DWORD),
        ("dmReserved2", wintypes.DWORD),
        ("dmPanningWidth", wintypes.DWORD),
        ("dmPanningHeight", wintypes.DWORD),
    ]


class DISPLAY_DEVICEW(ctypes.Structure):
    _fields_ = [
        ("cb", wintypes.DWORD),
        ("DeviceName", wintypes.WCHAR * 32),
        ("DeviceString", wintypes.WCHAR * 128),
        ("StateFlags", wintypes.DWORD),
        ("DeviceID", wintypes.WCHAR * 128),
        ("DeviceKey", wintypes.WCHAR * 128),
    ]


@dataclass
class DisplayMode:
    device: str
    width: int
    height: int
    hz: int
    bpp: int = 32
    flags: int = 0

    def key(self) -> tuple[int, int, int, int]:
        return (self.width, self.height, self.hz, self.bpp)


@dataclass
class CrtProfile:
    name: str
    game_width: int
    game_height: int
    capture_width: int
    capture_height: int
    stream_width: int
    stream_height: int
    xbox_output_width: int
    xbox_output_height: int
    hz: int = 60
    integer_scale: bool = True
    aspect_ratio_index: int = 22  # RetroArch core-provided or custom 4:3 override
    smooth: bool = False
    crt_switch_resolution: int = 0
    super_resolution: bool = False
    notes: str = ""

    @classmethod
    def from_dict(cls, name: str, data: Dict[str, Any]) -> "CrtProfile":
        game = data.get("game", {})
        capture = data.get("capture", {})
        stream = data.get("stream", {})
        output = data.get("xbox_output", {})
        ra = data.get("retroarch", {})
        return cls(
            name=name,
            game_width=int(game.get("width", capture.get("width", 640))),
            game_height=int(game.get("height", capture.get("height", 240))),
            capture_width=int(capture.get("width", 640)),
            capture_height=int(capture.get("height", 240)),
            stream_width=int(stream.get("width", 640)),
            stream_height=int(stream.get("height", 240)),
            xbox_output_width=int(output.get("width", 640)),
            xbox_output_height=int(output.get("height", 240)),
            hz=int(data.get("hz", 60)),
            integer_scale=bool(ra.get("integer_scale", True)),
            aspect_ratio_index=int(ra.get("aspect_ratio_index", 22)),
            smooth=bool(ra.get("smooth", False)),
            crt_switch_resolution=int(ra.get("crt_switch_resolution", 0)),
            super_resolution=bool(data.get("super_resolution", False)),
            notes=str(data.get("notes", "")),
        )

    def layer_summary(self) -> Dict[str, Any]:
        return {
            "profile": self.name,
            "layers": {
                "A_game_resolution": f"{self.game_width}x{self.game_height}@{self.hz}",
                "B_capture_resolution": f"{self.capture_width}x{self.capture_height}@{self.hz}",
                "C_stream_resolution": f"{self.stream_width}x{self.stream_height}@{self.hz}",
                "D_xbox_output_resolution": f"{self.xbox_output_width}x{self.xbox_output_height}",
            },
            "super_resolution": self.super_resolution,
            "notes": self.notes,
        }


class Win32DisplayBackend:
    """Thin ctypes wrapper around user32 display enumeration and mode switching."""

    def __init__(self) -> None:
        if sys.platform != "win32":
            raise RuntimeError("Win32DisplayBackend requires Windows.")
        self.user32 = ctypes.windll.user32

    def list_devices(self) -> List[Dict[str, Any]]:
        devices: List[Dict[str, Any]] = []
        idx = 0
        while True:
            dd = DISPLAY_DEVICEW()
            dd.cb = ctypes.sizeof(DISPLAY_DEVICEW)
            if not self.user32.EnumDisplayDevicesW(None, idx, ctypes.byref(dd), 0):
                break
            attached = bool(dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)
            primary = bool(dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
            if attached:
                devices.append(
                    {
                        "device": dd.DeviceName,
                        "description": dd.DeviceString,
                        "primary": primary,
                        "attached": attached,
                    }
                )
            idx += 1
        return devices

    def primary_device(self) -> str:
        devices = self.list_devices()
        for dev in devices:
            if dev["primary"]:
                return str(dev["device"])
        if devices:
            return str(devices[0]["device"])
        raise RuntimeError("No attached display device found.")

    def get_current_mode(self, device: Optional[str] = None) -> DisplayMode:
        target = device or self.primary_device()
        dm = DEVMODEW()
        dm.dmSize = ctypes.sizeof(DEVMODEW)
        if not self.user32.EnumDisplaySettingsW(target, ENUM_CURRENT_SETTINGS, ctypes.byref(dm)):
            raise RuntimeError(f"EnumDisplaySettingsW failed for {target}")
        return DisplayMode(
            device=target,
            width=int(dm.dmPelsWidth),
            height=int(dm.dmPelsHeight),
            hz=int(dm.dmDisplayFrequency),
            bpp=int(dm.dmBitsPerPel),
            flags=int(dm.dmDisplayFlags),
        )

    def list_modes(self, device: Optional[str] = None) -> List[DisplayMode]:
        target = device or self.primary_device()
        seen = set()
        modes: List[DisplayMode] = []
        idx = 0
        while True:
            dm = DEVMODEW()
            dm.dmSize = ctypes.sizeof(DEVMODEW)
            if not self.user32.EnumDisplaySettingsW(target, idx, ctypes.byref(dm)):
                break
            mode = DisplayMode(
                device=target,
                width=int(dm.dmPelsWidth),
                height=int(dm.dmPelsHeight),
                hz=int(dm.dmDisplayFrequency),
                bpp=int(dm.dmBitsPerPel),
                flags=int(dm.dmDisplayFlags),
            )
            if mode.key() not in seen:
                seen.add(mode.key())
                modes.append(mode)
            idx += 1
        modes.sort(key=lambda m: (m.width, m.height, m.hz, m.bpp))
        return modes

    def test_mode(self, mode: DisplayMode) -> int:
        dm = self._to_devmode(mode)
        return int(self.user32.ChangeDisplaySettingsExW(mode.device, ctypes.byref(dm), None, CDS_TEST, None))

    def apply_mode(self, mode: DisplayMode) -> int:
        test_rc = self.test_mode(mode)
        if test_rc != DISP_CHANGE_SUCCESSFUL:
            return test_rc
        dm = self._to_devmode(mode)
        # Do NOT pass CDS_UPDATEREGISTRY: session-only switch so a reboot restores OS defaults.
        return int(self.user32.ChangeDisplaySettingsExW(mode.device, ctypes.byref(dm), None, 0, None))

    @staticmethod
    def _to_devmode(mode: DisplayMode) -> DEVMODEW:
        dm = DEVMODEW()
        dm.dmSize = ctypes.sizeof(DEVMODEW)
        dm.dmPelsWidth = mode.width
        dm.dmPelsHeight = mode.height
        dm.dmDisplayFrequency = mode.hz
        dm.dmBitsPerPel = mode.bpp
        dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_BITSPERPEL
        return dm


def change_rc_name(rc: int) -> str:
    return {
        DISP_CHANGE_SUCCESSFUL: "DISP_CHANGE_SUCCESSFUL",
        DISP_CHANGE_RESTART: "DISP_CHANGE_RESTART",
        DISP_CHANGE_FAILED: "DISP_CHANGE_FAILED",
        DISP_CHANGE_BADMODE: "DISP_CHANGE_BADMODE",
        DISP_CHANGE_NOTUPDATED: "DISP_CHANGE_NOTUPDATED",
        DISP_CHANGE_BADFLAGS: "DISP_CHANGE_BADFLAGS",
        DISP_CHANGE_BADPARAM: "DISP_CHANGE_BADPARAM",
    }.get(rc, f"DISP_CHANGE_UNKNOWN({rc})")


def load_profiles(path: str) -> Dict[str, CrtProfile]:
    with open(path, "r", encoding="utf-8") as fh:
        raw = json.load(fh)
    profiles_raw = raw.get("profiles", raw)
    out: Dict[str, CrtProfile] = {}
    for name, data in profiles_raw.items():
        if isinstance(data, dict):
            out[name] = CrtProfile.from_dict(name, data)
    return out


def build_retroarch_override(profile: CrtProfile) -> str:
    """Build clean-room RetroArch override lines matching the CRT profile."""
    lines = [
        f"# Generated by MoonX360 CRT Host Companion for profile '{profile.name}'",
        f"# Layer A (Game): {profile.game_width}x{profile.game_height}@{profile.hz}",
        f"# Layer B (Capture): {profile.capture_width}x{profile.capture_height}@{profile.hz}",
        f"# Layer C (Stream): {profile.stream_width}x{profile.stream_height}@{profile.hz}",
        f"# Layer D (Xbox Output): {profile.xbox_output_width}x{profile.xbox_output_height}",
        f'video_fullscreen = "true"',
        f'video_fullscreen_x = "{profile.capture_width}"',
        f'video_fullscreen_y = "{profile.capture_height}"',
        f'video_refresh_rate = "{float(profile.hz):.6f}"',
        f'video_scale_integer = "{"true" if profile.integer_scale else "false"}"',
        f'video_smooth = "{"true" if profile.smooth else "false"}"',
        f'aspect_ratio_index = "{profile.aspect_ratio_index}"',
        f'crt_switch_resolution = "{profile.crt_switch_resolution}"',
    ]
    if profile.super_resolution:
        lines.append(f'crt_switch_resolution_super = "{profile.capture_width}"')
    return "\n".join(lines) + "\n"


def build_sunshine_prep_cmd(
    profile: CrtProfile,
    companion_script: str,
    profiles_path: str,
    snapshot_path: str,
    retroarch_cfg: Optional[str] = None,
    apply_live: bool = False,
) -> Dict[str, Any]:
    """Build a Sunshine/Apollo application `prep-cmd` snippet (safe dry-run by default)."""
    apply_flag = "--apply" if apply_live else "--dry-run"
    ra_arg = f' --retroarch-out "{retroarch_cfg}"' if retroarch_cfg else ""
    do_cmd = (
        f'python "{companion_script}" switch --profile {profile.name} '
        f'--profiles "{profiles_path}" --snapshot "{snapshot_path}" {apply_flag}{ra_arg}'
    )
    undo_cmd = (
        f'python "{companion_script}" restore --snapshot "{snapshot_path}" {apply_flag}'
    )
    return {
        "name": f"MoonX360 {profile.name}",
        "layers": profile.layer_summary()["layers"],
        "prep-cmd": [
            {
                "do": do_cmd,
                "undo": undo_cmd,
                "elevated": False,
            }
        ],
    }


def run_external_switchres(
    width: int,
    height: int,
    hz: int,
    switchres_bin: Optional[str] = None,
    apply_live: bool = False,
    runner: Optional[Callable[..., Any]] = None,
) -> Dict[str, Any]:
    """Optionally invoke an external user-installed SwitchRes CLI tool."""
    binary = switchres_bin or shutil.which("switchres")
    if not binary:
        return {
            "available": False,
            "executed": False,
            "reason": "External switchres executable not found in PATH.",
        }
    cmd = [binary, str(width), str(height), str(hz), "-c"]
    if not apply_live:
        return {
            "available": True,
            "executed": False,
            "dry_run": True,
            "command": cmd,
        }
    exec_fn = runner or subprocess.run
    proc = exec_fn(cmd, capture_output=True, text=True, check=False)
    return {
        "available": True,
        "executed": True,
        "dry_run": False,
        "command": cmd,
        "returncode": proc.returncode,
        "stdout": proc.stdout,
        "stderr": proc.stderr,
    }


def save_snapshot(snapshot_path: str, mode: DisplayMode) -> None:
    payload = {
        "saved_at_unix": int(time.time()),
        "mode": asdict(mode),
    }
    parent = os.path.dirname(os.path.abspath(snapshot_path))
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(snapshot_path, "w", encoding="utf-8") as fh:
        json.dump(payload, fh, indent=2)


def load_snapshot(snapshot_path: str) -> DisplayMode:
    with open(snapshot_path, "r", encoding="utf-8") as fh:
        payload = json.load(fh)
    m = payload["mode"]
    return DisplayMode(
        device=str(m["device"]),
        width=int(m["width"]),
        height=int(m["height"]),
        hz=int(m["hz"]),
        bpp=int(m.get("bpp", 32)),
        flags=int(m.get("flags", 0)),
    )


def switch_profile_display(
    backend: Win32DisplayBackend,
    profile: CrtProfile,
    snapshot_path: str,
    apply_live: bool = False,
    register_atexit: bool = True,
) -> Dict[str, Any]:
    """
    Validate and optionally switch host capture resolution for `profile`.
    Dry-run by default: only tests mode via CDS_TEST and writes nothing when apply_live=False.
    """
    current = backend.get_current_mode()
    target = DisplayMode(
        device=current.device,
        width=profile.capture_width,
        height=profile.capture_height,
        hz=profile.hz,
        bpp=current.bpp or 32,
        flags=0,
    )
    test_rc = backend.test_mode(target)
    result: Dict[str, Any] = {
        "profile": profile.name,
        "dry_run": not apply_live,
        "layers": profile.layer_summary()["layers"],
        "previous_mode": asdict(current),
        "target_mode": asdict(target),
        "cds_test_rc": test_rc,
        "cds_test_status": change_rc_name(test_rc),
        "snapshot_path": snapshot_path,
        "applied": False,
    }
    if not apply_live:
        return result
    if test_rc != DISP_CHANGE_SUCCESSFUL:
        result["error"] = f"CDS_TEST rejected mode: {change_rc_name(test_rc)}"
        return result

    save_snapshot(snapshot_path, current)
    apply_rc = backend.apply_mode(target)
    result["apply_rc"] = apply_rc
    result["apply_status"] = change_rc_name(apply_rc)
    result["applied"] = apply_rc == DISP_CHANGE_SUCCESSFUL
    if result["applied"] and register_atexit:
        atexit.register(lambda: restore_display(backend, snapshot_path, apply_live=True))
    return result


def restore_display(
    backend: Win32DisplayBackend,
    snapshot_path: str,
    apply_live: bool = False,
) -> Dict[str, Any]:
    if not os.path.exists(snapshot_path):
        return {
            "dry_run": not apply_live,
            "restored": False,
            "error": f"Snapshot not found: {snapshot_path}",
        }
    saved = load_snapshot(snapshot_path)
    test_rc = backend.test_mode(saved)
    result: Dict[str, Any] = {
        "dry_run": not apply_live,
        "snapshot_path": snapshot_path,
        "saved_mode": asdict(saved),
        "cds_test_rc": test_rc,
        "cds_test_status": change_rc_name(test_rc),
        "restored": False,
    }
    if not apply_live:
        return result
    apply_rc = backend.apply_mode(saved)
    result["apply_rc"] = apply_rc
    result["apply_status"] = change_rc_name(apply_rc)
    result["restored"] = apply_rc == DISP_CHANGE_SUCCESSFUL
    return result


def _default_profiles_path() -> str:
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), "profiles.example.json")


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="MoonX360 CRT Host Companion (Experimental, safe --dry-run default)"
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_inspect = sub.add_parser("inspect", help="Inspect current Win32 display and modes")
    p_inspect.add_argument("--device", default=None)
    p_inspect.add_argument("--filter-height", type=int, default=0)

    p_switch = sub.add_parser("switch", help="Validate/apply capture mode for a profile")
    p_switch.add_argument("--profile", required=True)
    p_switch.add_argument("--profiles", default=_default_profiles_path())
    p_switch.add_argument("--snapshot", default=DEFAULT_SNAPSHOT_PATH)
    p_switch.add_argument("--retroarch-out", default=None)
    p_switch.add_argument("--apply", action="store_true", help="Actually apply display/file changes")
    p_switch.add_argument("--dry-run", action="store_true", help="Force dry-run (default)")

    p_restore = sub.add_parser("restore", help="Restore saved display mode snapshot")
    p_restore.add_argument("--snapshot", default=DEFAULT_SNAPSHOT_PATH)
    p_restore.add_argument("--apply", action="store_true")
    p_restore.add_argument("--dry-run", action="store_true")

    p_ra = sub.add_parser("retroarch-cfg", help="Generate RetroArch override snippet")
    p_ra.add_argument("--profile", required=True)
    p_ra.add_argument("--profiles", default=_default_profiles_path())
    p_ra.add_argument("--out", default=None)
    p_ra.add_argument("--apply", action="store_true")

    p_sun = sub.add_parser("sunshine-prep", help="Generate Sunshine/Apollo prep-cmd JSON")
    p_sun.add_argument("--profile", required=True)
    p_sun.add_argument("--profiles", default=_default_profiles_path())
    p_sun.add_argument("--snapshot", default=DEFAULT_SNAPSHOT_PATH)
    p_sun.add_argument("--retroarch-out", default=None)
    p_sun.add_argument("--apply", action="store_true")

    p_sr = sub.add_parser("switchres", help="Optionally invoke user-installed SwitchRes CLI")
    p_sr.add_argument("--width", type=int, required=True)
    p_sr.add_argument("--height", type=int, required=True)
    p_sr.add_argument("--hz", type=int, default=60)
    p_sr.add_argument("--switchres-bin", default=None)
    p_sr.add_argument("--apply", action="store_true")

    args = parser.parse_args(argv)

    if args.cmd == "inspect":
        backend = Win32DisplayBackend()
        cur = backend.get_current_mode(args.device)
        modes = backend.list_modes(cur.device)
        if args.filter_height > 0:
            modes = [m for m in modes if m.height == args.filter_height]
        print(
            json.dumps(
                {
                    "devices": backend.list_devices(),
                    "current_mode": asdict(cur),
                    "matching_modes": [asdict(m) for m in modes],
                },
                indent=2,
            )
        )
        return 0

    if args.cmd == "switch":
        apply_live = bool(args.apply and not args.dry_run)
        profiles = load_profiles(args.profiles)
        if args.profile not in profiles:
            print(json.dumps({"error": f"Unknown profile: {args.profile}"}), file=sys.stderr)
            return 2
        profile = profiles[args.profile]
        backend = Win32DisplayBackend()
        out = switch_profile_display(backend, profile, args.snapshot, apply_live=apply_live)
        if args.retroarch_out:
            cfg_text = build_retroarch_override(profile)
            out["retroarch_out"] = args.retroarch_out
            out["retroarch_preview"] = cfg_text
            if apply_live:
                parent = os.path.dirname(os.path.abspath(args.retroarch_out))
                if parent:
                    os.makedirs(parent, exist_ok=True)
                with open(args.retroarch_out, "w", encoding="utf-8") as fh:
                    fh.write(cfg_text)
        print(json.dumps(out, indent=2))
        return 0 if out.get("cds_test_rc", 0) == DISP_CHANGE_SUCCESSFUL else 1

    if args.cmd == "restore":
        apply_live = bool(args.apply and not args.dry_run)
        backend = Win32DisplayBackend()
        out = restore_display(backend, args.snapshot, apply_live=apply_live)
        print(json.dumps(out, indent=2))
        return 0 if "error" not in out else 1

    if args.cmd == "retroarch-cfg":
        profiles = load_profiles(args.profiles)
        if args.profile not in profiles:
            print(json.dumps({"error": f"Unknown profile: {args.profile}"}), file=sys.stderr)
            return 2
        cfg_text = build_retroarch_override(profiles[args.profile])
        if args.out and args.apply:
            parent = os.path.dirname(os.path.abspath(args.out))
            if parent:
                os.makedirs(parent, exist_ok=True)
            with open(args.out, "w", encoding="utf-8") as fh:
                fh.write(cfg_text)
        sys.stdout.write(cfg_text)
        return 0

    if args.cmd == "sunshine-prep":
        profiles = load_profiles(args.profiles)
        if args.profile not in profiles:
            print(json.dumps({"error": f"Unknown profile: {args.profile}"}), file=sys.stderr)
            return 2
        snippet = build_sunshine_prep_cmd(
            profiles[args.profile],
            os.path.abspath(__file__),
            os.path.abspath(args.profiles),
            os.path.abspath(args.snapshot),
            retroarch_cfg=args.retroarch_out,
            apply_live=bool(args.apply),
        )
        print(json.dumps(snippet, indent=2))
        return 0

    if args.cmd == "switchres":
        out = run_external_switchres(
            args.width,
            args.height,
            args.hz,
            switchres_bin=args.switchres_bin,
            apply_live=bool(args.apply),
        )
        print(json.dumps(out, indent=2))
        return 0

    return 2


if __name__ == "__main__":
    raise SystemExit(main())
