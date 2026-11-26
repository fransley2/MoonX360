"""Inspect actual H.264 Annex-B or MoonX360 MLC1 (capture.bin) bitstreams.

Parses SPS, PPS, and every video slice header (NAL types 1 and 5) to report
real encoded properties rather than trusting requested encoder settings:
- resolution, profile_idc, level_idc, max_num_ref_frames
- entropy_coding_mode_flag (0=CAVLC, 1=CABAC)
- deblocking_filter_control_present_flag and disable_deblocking_filter_idc (0/1/2)
- slice types (I/P/B), actual slices_per_au distribution, ref vs non-ref AUs
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct


class BitReader:
    def __init__(self, rbsp: bytes):
        self.data = rbsp
        self.bit_pos = 0
        self.total_bits = len(rbsp) * 8

    def read_bit(self) -> int:
        if self.bit_pos >= self.total_bits:
            return 0
        byte_idx = self.bit_pos >> 3
        bit_off = 7 - (self.bit_pos & 7)
        self.bit_pos += 1
        return (self.data[byte_idx] >> bit_off) & 1

    def read_bits(self, n: int) -> int:
        v = 0
        for _ in range(n):
            v = (v << 1) | self.read_bit()
        return v

    def read_ue(self) -> int:
        zeros = 0
        while self.bit_pos < self.total_bits and self.read_bit() == 0:
            zeros += 1
            if zeros > 31:
                return 0
        if zeros == 0:
            return 0
        suffix = self.read_bits(zeros)
        return ((1 << zeros) - 1) + suffix

    def read_se(self) -> int:
        code = self.read_ue()
        k = (code + 1) >> 1
        return k if (code & 1) else -k


def nal_to_rbsp(nal: bytes) -> bytes:
    out = bytearray()
    zeros = 0
    for b in nal:
        if zeros >= 2 and b == 3:
            zeros = 0
            continue
        out.append(b)
        if b == 0:
            zeros += 1
        else:
            zeros = 0
    return bytes(out)


def split_annexb_nals(au: bytes) -> list[bytes]:
    nals = []
    n = len(au)
    i = 0
    starts = []
    while i + 2 < n:
        if au[i] == 0 and au[i + 1] == 0:
            if au[i + 2] == 1:
                starts.append((i, 3))
                i += 3
                continue
            elif i + 3 < n and au[i + 2] == 0 and au[i + 3] == 1:
                starts.append((i, 4))
                i += 4
                continue
        i += 1
    for idx, (pos, sc_len) in enumerate(starts):
        end = starts[idx + 1][0] if idx + 1 < len(starts) else n
        payload = au[pos + sc_len:end]
        if payload:
            nals.append(payload)
    return nals


def load_access_units(path: Path) -> tuple[list[bytes], dict]:
    raw = path.read_bytes()
    if raw[:4] == b"MLC1":
        w, h, count = struct.unpack_from(">III", raw, 4)
        off = 16
        aus = []
        for _ in range(count):
            length = struct.unpack_from(">I", raw, off)[0]
            off += 4
            aus.append(raw[off:off + length])
            off += length
        return aus, {"container": "MLC1", "header_w": w, "header_h": h, "header_frames": count}
    # Raw Annex-B: split into AUs on AUD or first_mb_in_slice == 0 slice transitions.
    nals = split_annexb_nals(raw)
    aus = []
    cur = bytearray()
    seen_vcl = False
    for nal in nals:
        ntype = nal[0] & 0x1F
        is_new_au = False
        if ntype in (7, 8, 9) and seen_vcl:
            is_new_au = True
        elif ntype in (1, 5):
            br = BitReader(nal_to_rbsp(nal[1:8]))
            first_mb = br.read_ue()
            if first_mb == 0 and seen_vcl:
                is_new_au = True
            seen_vcl = True
        if is_new_au and cur:
            aus.append(bytes(cur))
            cur = bytearray()
            seen_vcl = ntype in (1, 5)
        cur.extend(b"\x00\x00\x00\x01")
        cur.extend(nal)
    if cur:
        aus.append(bytes(cur))
    return aus, {"container": "annexb"}


def parse_sps(nal_payload: bytes) -> dict:
    rbsp = nal_to_rbsp(nal_payload)
    br = BitReader(rbsp)
    profile_idc = br.read_bits(8)
    constraint_flags = br.read_bits(8)
    level_idc = br.read_bits(8)
    sps_id = br.read_ue()
    chroma_format_idc = 1
    separate_colour_plane_flag = 0
    if profile_idc in (100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134):
        chroma_format_idc = br.read_ue()
        if chroma_format_idc == 3:
            separate_colour_plane_flag = br.read_bit()
        bit_depth_luma = br.read_ue() + 8
        bit_depth_chroma = br.read_ue() + 8
        br.read_bit()  # qpprime_y_zero_transform_bypass_flag
        seq_scaling = br.read_bit()
        if seq_scaling:
            cnt = 8 if chroma_format_idc != 3 else 12
            for idx in range(cnt):
                if br.read_bit():
                    size = 16 if idx < 6 else 64
                    last_scale = next_scale = 8
                    for _ in range(size):
                        if next_scale != 0:
                            delta = br.read_se()
                            next_scale = (last_scale + delta + 256) % 256
                        last_scale = next_scale if next_scale != 0 else last_scale
    else:
        bit_depth_luma = 8
        bit_depth_chroma = 8
    log2_max_frame_num_minus4 = br.read_ue()
    pic_order_cnt_type = br.read_ue()
    log2_max_pic_order_cnt_lsb_minus4 = 0
    delta_pic_order_always_zero_flag = 0
    if pic_order_cnt_type == 0:
        log2_max_pic_order_cnt_lsb_minus4 = br.read_ue()
    elif pic_order_cnt_type == 1:
        delta_pic_order_always_zero_flag = br.read_bit()
        br.read_se()
        br.read_se()
        num_ref_in_cycle = br.read_ue()
        for _ in range(num_ref_in_cycle):
            br.read_se()
    max_num_ref_frames = br.read_ue()
    gaps_in_frame_num = br.read_bit()
    pic_w_mbs = br.read_ue() + 1
    pic_h_map_units = br.read_ue() + 1
    frame_mbs_only = br.read_bit()
    if not frame_mbs_only:
        br.read_bit()
    br.read_bit()  # direct_8x8_inference_flag
    crop = br.read_bit()
    cl = cr = ct = cb = 0
    if crop:
        cl = br.read_ue()
        cr = br.read_ue()
        ct = br.read_ue()
        cb = br.read_ue()
    sub_w = 2 if chroma_format_idc in (1, 2) else 1
    sub_h = 2 if chroma_format_idc == 1 else 1
    crop_unit_x = sub_w if separate_colour_plane_flag == 0 else 1
    crop_unit_y = (sub_h if separate_colour_plane_flag == 0 else 1) * (2 - frame_mbs_only)
    width = pic_w_mbs * 16 - (cl + cr) * crop_unit_x
    height = (2 - frame_mbs_only) * pic_h_map_units * 16 - (ct + cb) * crop_unit_y
    return {
        "sps_id": sps_id,
        "profile_idc": profile_idc,
        "constraint_flags": constraint_flags,
        "level_idc": level_idc,
        "chroma_format_idc": chroma_format_idc,
        "separate_colour_plane_flag": separate_colour_plane_flag,
        "bit_depth_luma": bit_depth_luma,
        "bit_depth_chroma": bit_depth_chroma,
        "log2_max_frame_num": log2_max_frame_num_minus4 + 4,
        "pic_order_cnt_type": pic_order_cnt_type,
        "log2_max_pic_order_cnt_lsb": log2_max_pic_order_cnt_lsb_minus4 + 4,
        "delta_pic_order_always_zero_flag": delta_pic_order_always_zero_flag,
        "max_num_ref_frames": max_num_ref_frames,
        "gaps_in_frame_num_value_allowed_flag": gaps_in_frame_num,
        "frame_mbs_only_flag": frame_mbs_only,
        "width": width,
        "height": height,
    }


def parse_pps(nal_payload: bytes) -> dict:
    rbsp = nal_to_rbsp(nal_payload)
    br = BitReader(rbsp)
    pps_id = br.read_ue()
    sps_id = br.read_ue()
    entropy_coding_mode_flag = br.read_bit()
    bottom_field_pic_order_in_frame_present_flag = br.read_bit()
    num_slice_groups_minus1 = br.read_ue()
    if num_slice_groups_minus1 > 0:
        return {"pps_id": pps_id, "sps_id": sps_id, "entropy_coding_mode_flag": entropy_coding_mode_flag, "unsupported_fmo": True}
    num_ref_idx_l0_default_active_minus1 = br.read_ue()
    num_ref_idx_l1_default_active_minus1 = br.read_ue()
    weighted_pred_flag = br.read_bit()
    weighted_bipred_idc = br.read_bits(2)
    br.read_se()  # pic_init_qp_minus26
    br.read_se()  # pic_init_qs_minus26
    br.read_se()  # chroma_qp_index_offset
    deblocking_filter_control_present_flag = br.read_bit()
    constrained_intra_pred_flag = br.read_bit()
    redundant_pic_cnt_present_flag = br.read_bit()
    return {
        "pps_id": pps_id,
        "sps_id": sps_id,
        "entropy_coding_mode_flag": entropy_coding_mode_flag,
        "entropy_mode": "CABAC" if entropy_coding_mode_flag else "CAVLC",
        "bottom_field_pic_order_in_frame_present_flag": bottom_field_pic_order_in_frame_present_flag,
        "num_ref_idx_l0_default_active": num_ref_idx_l0_default_active_minus1 + 1,
        "num_ref_idx_l1_default_active": num_ref_idx_l1_default_active_minus1 + 1,
        "weighted_pred_flag": weighted_pred_flag,
        "weighted_bipred_idc": weighted_bipred_idc,
        "deblocking_filter_control_present_flag": deblocking_filter_control_present_flag,
        "constrained_intra_pred_flag": constrained_intra_pred_flag,
        "redundant_pic_cnt_present_flag": redundant_pic_cnt_present_flag,
    }


def parse_slice_header(nal_header: int, nal_payload: bytes, sps_map: dict[int, dict], pps_map: dict[int, dict]) -> dict | None:
    nal_ref_idc = (nal_header >> 5) & 3
    nal_unit_type = nal_header & 0x1F
    # Read only the beginning of the slice header (24 bytes is ample).
    rbsp = nal_to_rbsp(nal_payload[:32])
    br = BitReader(rbsp)
    first_mb = br.read_ue()
    slice_type_raw = br.read_ue()
    slice_type = slice_type_raw % 5  # 0=P, 1=B, 2=I, 3=SP, 4=SI
    pps_id = br.read_ue()
    pps = pps_map.get(pps_id)
    if not pps:
        return None
    sps = sps_map.get(pps["sps_id"])
    if not sps:
        return None
    if sps.get("separate_colour_plane_flag"):
        br.read_bits(2)
    br.read_bits(sps["log2_max_frame_num"])  # frame_num
    field_pic_flag = 0
    if not sps["frame_mbs_only_flag"]:
        field_pic_flag = br.read_bit()
        if field_pic_flag:
            br.read_bit()
    if nal_unit_type == 5:
        br.read_ue()  # idr_pic_id
    if sps["pic_order_cnt_type"] == 0:
        br.read_bits(sps["log2_max_pic_order_cnt_lsb"])
        if pps["bottom_field_pic_order_in_frame_present_flag"] and not field_pic_flag:
            br.read_se()
    elif sps["pic_order_cnt_type"] == 1 and not sps["delta_pic_order_always_zero_flag"]:
        br.read_se()
        if pps["bottom_field_pic_order_in_frame_present_flag"] and not field_pic_flag:
            br.read_se()
    if pps["redundant_pic_cnt_present_flag"]:
        br.read_ue()
    if slice_type == 1:  # B slice
        br.read_bit()  # direct_spatial_mv_pred_flag
    num_ref_l0 = pps["num_ref_idx_l0_default_active"]
    num_ref_l1 = pps["num_ref_idx_l1_default_active"]
    if slice_type in (0, 3, 1):
        override = br.read_bit()
        if override:
            num_ref_l0 = br.read_ue() + 1
            if slice_type == 1:
                num_ref_l1 = br.read_ue() + 1
    # ref_pic_list_modification (not MVC)
    if slice_type not in (2, 4):
        if br.read_bit():
            while True:
                mod_idc = br.read_ue()
                if mod_idc in (0, 1):
                    br.read_ue()
                elif mod_idc == 2:
                    br.read_ue()
                elif mod_idc == 3:
                    break
                else:
                    break
    if slice_type == 1:
        if br.read_bit():
            while True:
                mod_idc = br.read_ue()
                if mod_idc in (0, 1, 2):
                    br.read_ue()
                else:
                    break
    # pred_weight_table
    if (pps["weighted_pred_flag"] and slice_type in (0, 3)) or (pps["weighted_bipred_idc"] == 1 and slice_type == 1):
        br.read_ue()
        chroma_array_type = 0 if sps["separate_colour_plane_flag"] else sps["chroma_format_idc"]
        if chroma_array_type != 0:
            br.read_ue()
        for _ in range(num_ref_l0):
            if br.read_bit():
                br.read_se(); br.read_se()
            if chroma_array_type != 0 and br.read_bit():
                br.read_se(); br.read_se(); br.read_se(); br.read_se()
        if slice_type == 1:
            for _ in range(num_ref_l1):
                if br.read_bit():
                    br.read_se(); br.read_se()
                if chroma_array_type != 0 and br.read_bit():
                    br.read_se(); br.read_se(); br.read_se(); br.read_se()
    # dec_ref_pic_marking
    if nal_ref_idc != 0:
        if nal_unit_type == 5:
            br.read_bit()  # no_output_of_prior_pics_flag
            br.read_bit()  # long_term_reference_flag
        else:
            if br.read_bit():  # adaptive_ref_pic_marking_mode_flag
                while True:
                    mmco = br.read_ue()
                    if mmco == 0:
                        break
                    if mmco in (1, 3):
                        br.read_ue()
                    if mmco in (2, 3, 6):
                        br.read_ue()
                    if mmco in (4, 5):
                        if mmco == 4:
                            br.read_ue()
    if pps["entropy_coding_mode_flag"] and slice_type not in (2, 4):
        br.read_ue()  # cabac_init_idc
    br.read_se()  # slice_qp_delta
    if slice_type in (3, 4):
        if slice_type == 3:
            br.read_bit()
        br.read_se()
    disable_deblocking_filter_idc = 0
    if pps["deblocking_filter_control_present_flag"]:
        disable_deblocking_filter_idc = br.read_ue()
    return {
        "nal_ref_idc": nal_ref_idc,
        "nal_unit_type": nal_unit_type,
        "first_mb": first_mb,
        "slice_type": slice_type,
        "pps_id": pps_id,
        "disable_deblocking_filter_idc": disable_deblocking_filter_idc,
    }


def analyze_file(path: Path, fps: float = 60.0) -> dict:
    aus, meta = load_access_units(path)
    sps_map: dict[int, dict] = {}
    pps_map: dict[int, dict] = {}
    sps_history: list[dict] = []
    pps_history: list[dict] = []
    slice_types = Counter()
    slices_per_au_hist = Counter()
    deblock_idc_hist = Counter()
    ref_slices = 0
    nonref_slices = 0
    idr_aus = 0
    nonref_aus = 0
    total_payload_bytes = 0
    parsed_slices = 0

    type_names = {0: "P", 1: "B", 2: "I", 3: "SP", 4: "SI"}

    for au in aus:
        total_payload_bytes += len(au)
        nals = split_annexb_nals(au)
        au_slices = 0
        au_has_idr = False
        au_ref_idc_max = 0
        for nal in nals:
            hdr = nal[0]
            ntype = hdr & 0x1F
            if ntype == 7:
                s = parse_sps(nal[1:])
                sps_map[s["sps_id"]] = s
                if s not in sps_history:
                    sps_history.append(s)
            elif ntype == 8:
                p = parse_pps(nal[1:])
                pps_map[p["pps_id"]] = p
                if p not in pps_history:
                    pps_history.append(p)
            elif ntype in (1, 5):
                au_slices += 1
                if ntype == 5:
                    au_has_idr = True
                nri = (hdr >> 5) & 3
                if nri > au_ref_idc_max:
                    au_ref_idc_max = nri
                if nri:
                    ref_slices += 1
                else:
                    nonref_slices += 1
                sh = parse_slice_header(hdr, nal[1:], sps_map, pps_map)
                if sh is not None:
                    parsed_slices += 1
                    slice_types[type_names.get(sh["slice_type"], str(sh["slice_type"]))] += 1
                    deblock_idc_hist[str(sh["disable_deblocking_filter_idc"])] += 1
        if au_slices:
            slices_per_au_hist[str(au_slices)] += 1
            if au_has_idr:
                idr_aus += 1
            if au_ref_idc_max == 0:
                nonref_aus += 1

    eff_mbps = (total_payload_bytes * 8.0 / 1_000_000.0) / (len(aus) / fps) if aus and fps > 0 else 0.0
    return {
        "file": str(path),
        "container": meta,
        "access_units": len(aus),
        "total_payload_bytes": total_payload_bytes,
        "effective_mbps_at_fps": round(eff_mbps, 3),
        "fps_assumed": fps,
        "sps": sps_history,
        "pps": pps_history,
        "parsed_slices": parsed_slices,
        "slice_types": dict(slice_types),
        "slices_per_au": dict(slices_per_au_hist),
        "disable_deblocking_filter_idc": dict(deblock_idc_hist),
        "reference_slices": ref_slices,
        "nonreference_slices": nonref_slices,
        "idr_aus": idr_aus,
        "nonreference_aus": nonref_aus,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="Path to capture.bin (MLC1) or raw .h264")
    parser.add_argument("--fps", type=float, default=60.0)
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()
    res = analyze_file(args.input, args.fps)
    text = json.dumps(res, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
