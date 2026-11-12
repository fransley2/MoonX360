#!/usr/bin/env python3
"""Read-only local SDK audit; writes metadata JSON, never SDK code or layouts.

Uses only Python's standard library. Parses Microsoft COFF archives/symbols and
the narrow CodeView records needed for D3DDevice_ResetLowPriorityQueues.
Symbol presence is deliberately separate from a verified callable decoder ABI.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys


H264 = (
    "H264InitParamStructureEx", "H264DecodeCreateEx", "H264DecodeStart",
    "H264DecodeGetOutputInfo", "H264DecodeGetOutputGPU",
    "H264DecodeReturnOutputGPU",
)
LOCKS = (
    "D3DLockDevice", "D3DUnlockDevice", "D3DSuspendDeviceLock",
    "D3DRestoreDeviceLock", "D3DThreadOwnsDeviceLock",
)
RESET = "D3DDevice_ResetLowPriorityQueues"


def u16(data, offset=0):
    return struct.unpack_from("<H", data, offset)[0]


def u32(data, offset=0):
    return struct.unpack_from("<I", data, offset)[0]


def cstring(data, offset=0):
    return data[offset:].split(b"\0", 1)[0].decode("ascii", errors="strict")


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def input_info(path, data):
    return {"path": str(path.resolve()), "size": len(data), "sha256": sha256(data)}


def archive_members(data):
    if data[:8] != b"!<arch>\n":
        raise ValueError("Not a COFF archive")
    offset, names = 8, b""
    while offset < len(data):
        header = data[offset:offset + 60]
        if len(header) != 60 or header[58:60] != b"`\n":
            raise ValueError("Malformed archive member header")
        size = int(header[48:58])
        start = offset + 60
        body = data[start:start + size]
        if len(body) != size:
            raise ValueError("Truncated archive member")
        name = header[:16].decode("ascii").strip()
        if name == "//":
            names = body
        elif name.startswith("/") and name[1:].isdigit():
            name = cstring(names, int(name[1:]))
        if name not in ("/", "//"):
            yield name.rstrip("/"), offset, body
        offset = start + size + (size & 1)


def coff_sections(data):
    if u16(data) != 0x1F2:
        raise ValueError("Expected Xbox PowerPC COFF object")
    start = 20 + u16(data, 16)
    result = []
    for index in range(u16(data, 2)):
        header = data[start + index * 40:start + (index + 1) * 40]
        size, pointer = u32(header, 16), u32(header, 20)
        result.append((cstring(header[:8]), data[pointer:pointer + size]))
    return result


def defined_symbols(data, sections):
    pointer, count = u32(data, 8), u32(data, 12)
    strings = pointer + count * 18
    index = 0
    while index < count:
        record = data[pointer + index * 18:pointer + (index + 1) * 18]
        name = (cstring(data, strings + u32(record, 4))
                if record[:4] == b"\0" * 4 else cstring(record[:8]))
        value, section, kind, storage, auxiliary = struct.unpack_from("<IhHBB", record, 8)
        if storage == 2 and section > 0:
            yield {"name": name, "symbol_index": index, "section_number": section,
                   "section": sections[section - 1][0], "value": value,
                   "coff_type": kind}
        index += 1 + auxiliary


def cv_types(data):
    if u32(data) != 4:
        raise ValueError("Expected CodeView C13 type stream")
    result, offset, index = {}, 4, 0x1000
    while offset < len(data):
        length, leaf = u16(data, offset), u16(data, offset + 2)
        body = data[offset + 4:offset + 2 + length]
        if length < 2 or len(body) != length - 2:
            raise ValueError("Malformed CodeView type record")
        result[index] = (leaf, body)
        # LF_PRECOMP imports an index range from the named object's .debug$P.
        index = u32(body) + u32(body, 4) if leaf == 0x1509 else index + 1
        offset += length + 2
    return result


def cv_symbols(data):
    if u32(data) != 4:
        raise ValueError("Expected CodeView C13 symbol stream")
    offset = 4
    while offset + 8 <= len(data):
        subsection, size = u32(data, offset), u32(data, offset + 4)
        body = data[offset + 8:offset + 8 + size]
        offset += 8 + ((size + 3) & ~3)
        if subsection != 0xF1:  # DEBUG_S_SYMBOLS
            continue
        cursor = 0
        while cursor + 4 <= len(body):
            length, kind = u16(body, cursor), u16(body, cursor + 2)
            if length < 2 or cursor + 2 + length > len(body):
                raise ValueError("Malformed CodeView symbol record")
            yield kind, body[cursor + 4:cursor + 2 + length]
            cursor += 2 + length


def type_name(index, records):
    primitive = {0x08: "HRESULT", 0x22: "unsigned long", 0x74: "int"}
    if index in primitive:
        return primitive[index]
    leaf, body = records[index]
    if leaf == 0x1002:  # LF_POINTER, only ordinary 32-bit pointers supported
        attributes = u32(body, 4)
        if attributes & 0x1F != 10 or (attributes >> 5) & 7:
            raise ValueError("Unsupported pointer encoding")
        return type_name(u32(body), records) + "*"
    if leaf == 0x1505:  # LF_STRUCTURE; read name only, never fields/layout
        if u16(body, 16) >= 0x8000:
            raise ValueError("Unsupported structure-name record")
        return cstring(body, 18)
    raise ValueError("Unsupported signature type: " + hex(index))


def decode_reset(lowpri, precomp):
    types = cv_types(next(data for name, data in lowpri if name == ".debug$T"))
    imports = [(index, body) for index, (leaf, body) in types.items() if leaf == 0x1509]
    if len(imports) != 1:
        raise ValueError("Expected one LF_PRECOMP dependency")
    _, dependency = imports[0]
    first, count, signature = u32(dependency), u32(dependency, 4), u32(dependency, 8)
    pool = cv_types(next(data for name, data in precomp if name == ".debug$P"))
    # LF_ENDPRECOMP authenticates the imported pool's signature.
    if not any(leaf == 0x14 and u32(body) == signature for leaf, body in pool.values()):
        raise ValueError("Precompiled type-pool signature mismatch")
    merged = {index: record for index, record in pool.items() if first <= index < first + count}
    merged.update({index: record for index, record in types.items() if record[0] != 0x1509})
    matches = []
    for name, data in lowpri:
        if name != ".debug$S":
            continue
        symbols = list(cv_symbols(data))
        procedures = [body for kind, body in symbols
                      if kind == 0x1110 and cstring(body, 35) == RESET]  # S_GPROC32
        for procedure in procedures:
            type_index = u32(procedure, 24)
            leaf, body = merged[type_index]
            if leaf != 0x1008 or body[4] != 0:  # LF_PROCEDURE, CV_CALL_NEAR_C
                raise ValueError("Unexpected procedure or calling convention")
            arguments = u32(body, 8)
            arg_leaf, arg_body = merged[arguments]
            if arg_leaf != 0x1201 or u32(arg_body) != u16(body, 6):
                raise ValueError("LF_ARGLIST parameter count mismatch")
            indices = [u32(arg_body, 4 + i * 4) for i in range(u32(arg_body))]
            parameters = [{"type_index": hex(index), "type": type_name(index, merged)}
                          for index in indices]
            named = [(u32(record), cstring(record, 6)) for kind, record in symbols
                     if kind == 0x1106]  # S_REGISTER
            if [index for index, _ in named] != indices:
                raise ValueError("Parameter register records do not match argument types")
            for parameter, (_, parameter_name) in zip(parameters, named):
                parameter["name"] = parameter_name
            matches.append({"status": "confirmed_embedded_debug_signature_not_public_header",
                            "procedure_type_index": hex(type_index),
                            "argument_list_type_index": hex(arguments),
                            "return_type_index": hex(u32(body)),
                            "return_type": type_name(u32(body), merged),
                            "calling_convention": "__cdecl", "parameters": parameters,
                            "precompiled_types": {"first_index": hex(first), "count": count,
                                                  "signature": hex(signature)}})
    if len(matches) != 1:
        raise ValueError("Expected exactly one named reset procedure")
    return matches[0]


def audit(sdk):
    evidence = {"schema_version": 1, "sdk_root": str(sdk.resolve()), "inputs": [],
                "members": [], "header_search": {"roots": ["include", "TechPreview"],
                "extensions": [".h", ".hpp", ".inl"], "matches": {}, "files_scanned": 0}}
    targets = set(H264 + LOCKS + (RESET, "XAVInitialize"))
    sections_by_member, names_by_member = {}, {}
    for library in ("xav.lib", "xavd.lib", "d3d9.lib"):
        path = sdk / "lib" / "xbox" / library
        data = path.read_bytes()
        evidence["inputs"].append(input_info(path, data))
        wanted = (("lowpri.obj", "precomp.obj") if library == "d3d9.lib" else
                  ("extracodecsshim.obj", "d3dlock.obj", "xboxplayer.obj"))
        for name, offset, body in archive_members(data):
            if not name.lower().endswith(tuple("\\" + suffix for suffix in wanted)):
                continue
            sections = coff_sections(body)
            found = [symbol for symbol in defined_symbols(body, sections)
                     if symbol["name"] in targets or symbol["name"].startswith("?XAVInitialize@@")]
            evidence["members"].append({"library": library, "name": name,
                "archive_header_offset": offset, "size": len(body), "sha256": sha256(body),
                "defined_symbols": found})
            if library == "d3d9.lib":
                basename = name.rsplit("\\", 1)[-1]
                sections_by_member[basename] = sections
                names_by_member[basename] = name
    evidence["low_priority_signature"] = decode_reset(
        sections_by_member["lowpri.obj"], sections_by_member["precomp.obj"])
    evidence["low_priority_signature"].update({"library": "d3d9.lib",
        "member": names_by_member["lowpri.obj"],
        "type_pool_member": names_by_member["precomp.obj"]})
    search = evidence["header_search"]
    pattern = re.compile(r"\b(?:" + "|".join(sorted(targets)) + r")\b")
    for root in search["roots"]:
        for path in sorted((sdk / root).rglob("*")):
            if not path.is_file() or path.suffix.lower() not in search["extensions"]:
                continue
            search["files_scanned"] += 1
            raw = path.read_bytes()
            for line, text in enumerate(raw.decode("latin1").splitlines(), 1):
                for match in pattern.finditer(text):
                    search["matches"].setdefault(match.group(), []).append(
                        {"path": str(path.relative_to(sdk)), "line": line})
    search["tokens_without_matches"] = sorted(targets - search["matches"].keys())
    for relative in ("include/xbox/xav.h", "include/xbox/xdk.h"):
        path = sdk / relative
        evidence["inputs"].append(input_info(path, path.read_bytes()))
    evidence["distinctions"] = {
        "h264": "Definitions found in the shim member; symbols alone do not establish callable decoder signatures. Private headers not located by this search.",
        "low_priority": "Debug signature recovered; safe reset/drain preconditions unconfirmed.",
        "motor_lock": "Internal D3D lock symbols exist; decoder integration, lock ordering and motor-lock contract unconfirmed.",
        "drain": "Safe decoder/GPU completion and output-return lifecycle unconfirmed.",
        "scope": "Local static SDK evidence only. No decoder execution, console operations, backend implementation or output-structure extraction.",
    }
    evidence["method"] = {
        "archive": "COFF archive headers and long-name table; SHA-256 of complete input files and exact member payloads, excluding archive headers/padding.",
        "symbols": "18-byte COFF symbol records; External storage class with positive section number means defined, not merely referenced.",
        "codeview": "C13 .debug$S DEBUG_S_SYMBOLS / S_GPROC32 / S_REGISTER; .debug$T LF_PRECOMP, LF_PROCEDURE, LF_ARGLIST; matching .debug$P LF_ENDPRECOMP signature. Reads type names, not structure fields.",
        "header_search": "Exact token occurrences in stated roots/extensions; matches may be comments, absence applies only to this search scope.",
    }
    return evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, default=Path(os.environ.get(
        "XEDK", r"C:\Program Files (x86)\Microsoft Xbox 360 SDK")))
    parser.add_argument("--output", type=Path, default=Path(
        "artifacts/system-gpu-audit-20261112/sdk-api-evidence.json"))
    args = parser.parse_args()
    evidence = audit(args.sdk)
    evidence["generator"] = input_info(Path(__file__), Path(__file__).read_bytes())
    evidence["reproduce_argv"] = [sys.executable, "-B", str(Path(__file__).resolve()),
                                  "--sdk", str(args.sdk.resolve()), "--output", str(args.output.resolve())]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print("Wrote metadata evidence:", args.output)


if __name__ == "__main__":
    main()
