#!/usr/bin/env python3

import os
import sys
import struct
import subprocess
import hashlib
import json
import gzip
import bz2
import lzma
import tarfile
import zipfile
import re
from pathlib import Path

INPUT_FILE = os.environ.get("BIN_FILE", "firmware.bin")
OUTPUT_DIR = os.environ.get("OUTPUT_DIR", "extracted")

MAX_SLICE_SIZE = 32 * 1024 * 1024
MAX_SLICES = 20
MAX_TOTAL_EMBEDDED_BYTES = 200 * 1024 * 1024


def run(cmd, cwd=None, timeout=120):
    try:
        r = subprocess.run(
            cmd, shell=True, capture_output=True, text=True,
            cwd=cwd, timeout=timeout
        )
        return r.stdout, r.stderr, r.returncode
    except Exception as e:
        return "", str(e), -1


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def le32(b, off=0):
    return struct.unpack_from("<I", b, off)[0]


def le64(b, off=0):
    return struct.unpack_from("<Q", b, off)[0]


def detect_magic(data):
    sigs = []
    if data[:4] == b"PK\x03\x04":
        sigs.append("zip")
    if data[:2] == b"\x1f\x8b":
        sigs.append("gzip")
    if data[:3] == b"BZh":
        sigs.append("bzip2")
    if data[:6] == b"\xfd7zXZ\x00":
        sigs.append("xz")
    if data[:4] in (b"\x04\x22\x4d\x18", b"\x02\x21\x4c\x18"):
        sigs.append("lz4")
    if data[:4] == b"\x89\x4c\x5a\x4f":
        sigs.append("lzo")
    if data[:8] == b"ANDROID!":
        sigs.append("android_boot")
    if data[:4] == b"\x27\x05\x19\x56" or data[:4] == b"\xd0\x0d\xfe\xed":
        sigs.append("uimage_or_dtb")
    if data[:4] in (b"hsqs", b"sqsh", b"shsq", b"qshs"):
        sigs.append("squashfs")
    if data[:4] == b"\x3a\xff\x26\xed":
        sigs.append("android_sparse")
    if data[:4] == b"\xe2\xe1\xf5\xe0":
        sigs.append("ext_fs")
    if len(data) > 0x43a and data[0x438:0x43a] == b"\x53\xef":
        sigs.append("ext2_3_4")
    if data[:4] == b"CrAU":
        sigs.append("chrome_payload")
    if data[:4] == b"DHTB":
        sigs.append("dhtb_header")
    if data[:4] == b"OZIP":
        sigs.append("oppo_ozip")
    if data[:4] == b"RKFW":
        sigs.append("rockchip_fw")
    if data[:4] == b"KRNL":
        sigs.append("krnl_header")
    if len(data) >= 4 and le32(data) == 0xC7F4E493:
        sigs.append("qualcomm_custom_header")
    return sigs


def try_extract_zip(path, outdir):
    try:
        Path(outdir).mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(path, "r") as z:
            z.extractall(outdir)
        return True
    except:
        return False


def try_extract_gzip(path, outdir):
    try:
        Path(outdir).mkdir(parents=True, exist_ok=True)
        with gzip.open(path, "rb") as f:
            data = f.read()
        out = Path(outdir) / (Path(path).stem + ".ungz")
        out.write_bytes(data)
        return True
    except:
        return False


def try_extract_bzip2(path, outdir):
    try:
        Path(outdir).mkdir(parents=True, exist_ok=True)
        with bz2.open(path, "rb") as f:
            data = f.read()
        out = Path(outdir) / (Path(path).stem + ".unbz2")
        out.write_bytes(data)
        return True
    except:
        return False


def try_extract_xz(path, outdir):
    try:
        Path(outdir).mkdir(parents=True, exist_ok=True)
        with lzma.open(path, "rb") as f:
            data = f.read()
        out = Path(outdir) / (Path(path).stem + ".unxz")
        out.write_bytes(data)
        return True
    except:
        return False


def try_extract_tar(path, outdir):
    try:
        Path(outdir).mkdir(parents=True, exist_ok=True)
        with tarfile.open(path) as t:
            t.extractall(outdir)
        return True
    except:
        return False


def try_7z(path, outdir):
    Path(outdir).mkdir(parents=True, exist_ok=True)
    stdout, stderr, rc = run(f"7z x -o{outdir} -y {path}", timeout=300)
    return stdout, stderr, rc


def try_simg2img(path, outdir):
    Path(outdir).mkdir(parents=True, exist_ok=True)
    out = Path(outdir) / (Path(path).stem + ".raw.img")
    stdout, stderr, rc = run(f"simg2img {path} {out}", timeout=120)
    return stdout, stderr, rc


def try_lz4(path, outdir):
    Path(outdir).mkdir(parents=True, exist_ok=True)
    out = Path(outdir) / (Path(path).stem + ".unlz4")
    stdout, stderr, rc = run(f"lz4 -d {path} {out}", timeout=120)
    return stdout, stderr, rc


def try_ext_debugfs(path, outdir):
    Path(outdir).mkdir(parents=True, exist_ok=True)
    stdout, stderr, rc = run(f"debugfs -R 'ls -l /' {path}", timeout=60)
    if rc == 0:
        rdump_out = Path(outdir) / "rootfs"
        rdump_out.mkdir(exist_ok=True)
        stdout2, stderr2, rc2 = run(
            f"debugfs -R 'rdump / {rdump_out}' {path}", timeout=300
        )
        return stdout + stdout2, stderr + stderr2
    return stdout, stderr


def try_payload_extract(path, outdir):
    Path(outdir).mkdir(parents=True, exist_ok=True)
    stdout, stderr, rc = run(
        f"payload-dumper-go -output {outdir} {path}", timeout=600
    )
    return stdout, stderr, rc


def try_qualcomm_mbn(path, outdir):
    results = []
    file_size = Path(path).stat().st_size

    with open(path, "rb") as f:
        header = f.read(min(4096, file_size))

    magic = le32(header, 0) if len(header) >= 4 else 0
    results.append(f"magic=0x{magic:08X}")

    if magic != 0xC7F4E493:
        return "\n".join(results)

    results.append("Qualcomm custom header confirmed")

    hdr_fields = {
        "field_00": f"0x{le32(header,0x00):08X}",
        "field_04": f"0x{le32(header,0x04):08X}",
        "field_08": f"0x{le32(header,0x08):08X}",
        "field_0c": f"0x{le32(header,0x0c):08X}",
        "field_10": f"0x{le32(header,0x10):08X}",
        "field_14": f"0x{le32(header,0x14):08X}",
        "field_18": f"0x{le32(header,0x18):08X}",
        "field_1c": f"0x{le32(header,0x1c):08X}",
        "field_20": f"0x{le32(header,0x20):08X}",
        "field_24": f"0x{le32(header,0x24):08X}",
        "field_28": f"0x{le32(header,0x28):08X}",
        "field_2c": f"0x{le32(header,0x2c):08X}",
        "field_c8_crc_or_hash": f"0x{le32(header,0xc8):08X}",
        "field_cc": f"0x{le32(header,0xcc):08X}",
        "field_d0": f"0x{le32(header,0xd0):08X}",
        "field_d4": f"0x{le32(header,0xd4):08X}",
        "field_d8": f"0x{le32(header,0xd8):08X}",
        "field_dc": f"0x{le32(header,0xdc):08X}",
        "field_e0": f"0x{le32(header,0xe0):08X}",
        "field_e4": f"0x{le32(header,0xe4):08X}",
        "field_e8": f"0x{le32(header,0xe8):08X}",
    }
    results.append(json.dumps(hdr_fields, indent=2))

    results.append("\n--- raw header hex (first 256 bytes) ---")
    for row in range(0, min(256, len(header)), 16):
        hex_part = " ".join(f"{b:02x}" for b in header[row:row+16])
        results.append(f"  {row:04x}: {hex_part}")

    outdir_p = Path(outdir) / "qcom_chunks"
    outdir_p.mkdir(parents=True, exist_ok=True)

    field_08 = le32(header, 0x08)
    field_0c = le32(header, 0x0c)
    results.append(f"\nCandidate data offset: 0x{field_08:08X} ({field_08})")
    results.append(f"Candidate data size:   0x{field_0c:08X} ({field_0c})")

    for candidate_offset in [field_08, 0x100, 0x200, 0x400, 0x1000]:
        if candidate_offset == 0 or candidate_offset >= file_size:
            continue
        with open(path, "rb") as f:
            f.seek(candidate_offset)
            slice_data = f.read(min(MAX_SLICE_SIZE, file_size - candidate_offset))
        slice_path = outdir_p / f"slice_at_0x{candidate_offset:08x}.bin"
        slice_path.write_bytes(slice_data)
        results.append(f"Wrote slice at 0x{candidate_offset:08x} ({len(slice_data)} bytes) -> {slice_path.name}")

    return "\n".join(results)


def scan_embedded_offsets(path, outdir):
    signatures = {
        b"PK\x03\x04": "zip",
        b"\x1f\x8b": "gzip",
        b"BZh": "bzip2",
        b"\xfd7zXZ\x00": "xz",
        b"ANDROID!": "android_boot",
        b"\x27\x05\x19\x56": "uimage",
        b"\xd0\x0d\xfe\xed": "fdt",
        b"hsqs": "squashfs",
        b"\x3a\xff\x26\xed": "android_sparse",
        b"CrAU": "payload",
        b"DHTB": "dhtb",
        b"RKFW": "rkfw",
    }
    file_size = Path(path).stat().st_size
    chunk_size = 4 * 1024 * 1024
    overlap = 16
    buffer = b""
    abs_offset = 0
    found = []

    with open(path, "rb") as f:
        while True:
            chunk = f.read(chunk_size)
            if not chunk:
                break
            data = buffer[-overlap:] + chunk if buffer else chunk
            data_offset = abs_offset - len(buffer[-overlap:]) if buffer else abs_offset
            for sig, name in signatures.items():
                start = 0
                while True:
                    idx = data.find(sig, start)
                    if idx == -1:
                        break
                    real_offset = data_offset + idx
                    found.append((real_offset, name))
                    start = idx + 1
            buffer = chunk
            abs_offset += len(chunk)

    found = sorted(set(found))

    outdir_p = Path(outdir) / "embedded"
    outdir_p.mkdir(parents=True, exist_ok=True)
    report = []
    total_written = 0
    slices_written = 0

    for offset, name in found:
        line = f"0x{offset:016X} ({offset}) : {name}"
        report.append(line)

        if slices_written >= MAX_SLICES:
            report.append(f"  [SKIP] slice limit {MAX_SLICES} reached")
            continue
        if total_written >= MAX_TOTAL_EMBEDDED_BYTES:
            report.append(f"  [SKIP] total size limit reached")
            continue

        slice_size = min(MAX_SLICE_SIZE, file_size - offset)
        extract_path = outdir_p / f"0x{offset:016X}_{name}.bin"

        with open(path, "rb") as f:
            f.seek(offset)
            slice_data = f.read(slice_size)
        extract_path.write_bytes(slice_data)
        total_written += slice_size
        slices_written += 1

        inner_out = outdir_p / f"0x{offset:016X}_{name}"
        inner_out.mkdir(exist_ok=True)

        if name == "zip":
            try_extract_zip(str(extract_path), str(inner_out))
        elif name == "gzip":
            try_extract_gzip(str(extract_path), str(inner_out))
        elif name == "bzip2":
            try_extract_bzip2(str(extract_path), str(inner_out))
        elif name == "xz":
            try_extract_xz(str(extract_path), str(inner_out))
        elif name == "android_sparse":
            try_simg2img(str(extract_path), str(inner_out))
        elif name == "payload":
            try_payload_extract(str(extract_path), str(inner_out))

    return report


def collect_file_tree(outdir):
    tree = []
    for p in sorted(Path(outdir).rglob("*")):
        if p.is_file():
            try:
                size = p.stat().st_size
                tree.append({"path": str(p.relative_to(outdir)), "size": size})
            except:
                pass
    return tree


def strings_scan(path, min_len=6):
    stdout, stderr, rc = run(f"strings -n {min_len} {path}", timeout=180)
    lines = stdout.splitlines()
    interesting = []
    patterns = [
        r"android", r"version", r"build", r"partition",
        r"system", r"boot", r"recovery", r"kernel",
        r"qualcomm", r"qcom", r"snapdragon", r"msm",
        r"vendor", r"product", r"model", r"device",
        r"\d+\.\d+\.\d+",
    ]
    for line in lines:
        for pat in patterns:
            if re.search(pat, line, re.IGNORECASE):
                interesting.append(line)
                break
    return interesting[:500]


def main():
    input_path = INPUT_FILE
    output_dir = OUTPUT_DIR

    if not Path(input_path).exists():
        print(f"ERROR: {input_path} not found", file=sys.stderr)
        sys.exit(1)

    Path(output_dir).mkdir(parents=True, exist_ok=True)

    report = {
        "input": input_path,
        "sha256": sha256(input_path),
        "size": Path(input_path).stat().st_size,
        "detections": [],
        "strings_interesting": [],
        "embedded_offsets": [],
        "extracted_files": [],
        "tool_outputs": {},
    }

    with open(input_path, "rb") as f:
        header = f.read(512)

    report["header_hex"] = header.hex()
    report["detections"] = detect_magic(header)

    report["strings_interesting"] = strings_scan(input_path)

    qcom_result = try_qualcomm_mbn(input_path, output_dir)
    report["tool_outputs"]["qualcomm_mbn_analysis"] = qcom_result

    sz_out, sz_err, _ = try_7z(input_path, output_dir + "/7z_out")
    report["tool_outputs"]["7z_stdout"] = sz_out[:2048]
    report["tool_outputs"]["7z_stderr"] = sz_err[:512]

    try_extract_zip(input_path, output_dir + "/zip_out")
    try_extract_gzip(input_path, output_dir + "/gzip_out")
    try_extract_bzip2(input_path, output_dir + "/bzip2_out")
    try_extract_xz(input_path, output_dir + "/xz_out")
    try_extract_tar(input_path, output_dir + "/tar_out")

    simg_out, simg_err, _ = try_simg2img(input_path, output_dir + "/simg_out")
    report["tool_outputs"]["simg2img"] = simg_out[:1024]

    lz4_out, lz4_err, _ = try_lz4(input_path, output_dir + "/lz4_out")
    report["tool_outputs"]["lz4"] = lz4_out[:1024]

    payload_out, payload_err, _ = try_payload_extract(input_path, output_dir + "/payload_out")
    report["tool_outputs"]["payload_dumper"] = payload_out[:2048]

    ext_out, ext_err = try_ext_debugfs(input_path, output_dir + "/ext_out")
    report["tool_outputs"]["debugfs"] = ext_out[:2048]

    embedded = scan_embedded_offsets(input_path, output_dir)
    report["embedded_offsets"] = embedded

    report["extracted_files"] = collect_file_tree(output_dir)

    report_path = Path(output_dir) / "analysis_report.json"
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)

    summary_path = Path(output_dir) / "summary.txt"
    with open(summary_path, "w", encoding="utf-8") as f:
        f.write(f"File: {report['input']}\n")
        f.write(f"Size: {report['size']} bytes\n")
        f.write(f"SHA256: {report['sha256']}\n\n")
        f.write("=== Detections ===\n")
        for d in report["detections"]:
            f.write(f"  {d}\n")
        f.write("\n=== Embedded Offsets ===\n")
        for e in report["embedded_offsets"]:
            f.write(f"  {e}\n")
        f.write("\n=== Interesting Strings ===\n")
        for s in report["strings_interesting"]:
            f.write(f"  {s}\n")
        f.write("\n=== Extracted Files ===\n")
        for item in report["extracted_files"]:
            f.write(f"  {item['path']}  ({item['size']} bytes)\n")

    print(f"Done. Report: {report_path}")
    print(f"Summary: {summary_path}")


if __name__ == "__main__":
    main()
