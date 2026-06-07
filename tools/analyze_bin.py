#!/usr/bin/env python3

import os
import sys
import struct
import shutil
import subprocess
import hashlib
import json
import zlib
import gzip
import bz2
import lzma
import tarfile
import zipfile
import tempfile
import re
from pathlib import Path

INPUT_FILE = os.environ.get("BIN_FILE", "firmware.bin")
OUTPUT_DIR = os.environ.get("OUTPUT_DIR", "extracted")


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


def read_bytes(f, offset, length):
    f.seek(offset)
    return f.read(length)


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
    if data[:4] == b"\x04\x22\x4d\x18" or data[:4] == b"\x02\x21\x4c\x18":
        sigs.append("lz4")
    if data[:4] == b"\x89\x4c\x5a\x4f":
        sigs.append("lzo")
    if data[:4] in (b"ANDROID!", b"\x41\x4e\x44\x52"):
        sigs.append("android_boot")
    if data[:8] == b"\x27\x05\x19\x56" or data[0:4] == b"\xd0\x0d\xfe\xed":
        sigs.append("uimage_or_dtb")
    if data[:4] == b"hsqs" or data[:4] == b"sqsh" or data[:4] == b"shsq" or data[:4] == b"qshs":
        sigs.append("squashfs")
    if data[:4] == b"\x3a\xff\x26\xed":
        sigs.append("android_sparse")
    if data[:4] == b"\xe2\xe1\xf5\xe0":
        sigs.append("ext_fs")
    if data[:8] == b"\x53\xef" or (len(data) > 0x438 and data[0x438:0x43a] == b"\x53\xef"):
        sigs.append("ext2_3_4")
    if data[:4] == b"CrAU":
        sigs.append("chrome_payload")
    if data[:4] == b"DHTB":
        sigs.append("dhtb_header")
    if data[:4] == b"\x00\x00\x00\x00" and len(data) >= 512:
        sigs.append("possible_sparse_or_raw")
    if data[:4] == b"OZIP":
        sigs.append("oppo_ozip")
    if data[:4] == b"RKFW":
        sigs.append("rockchip_fw")
    if data[:4] == b"KRNL":
        sigs.append("krnl_header")
    magic32 = le32(data) if len(data) >= 4 else 0
    if magic32 == 0xC7F4E493:
        sigs.append("qualcomm_custom_header")
    return sigs


def try_extract_zip(path, outdir):
    try:
        with zipfile.ZipFile(path, "r") as z:
            z.extractall(outdir)
        return True
    except:
        return False


def try_extract_gzip(path, outdir):
    try:
        with gzip.open(path, "rb") as f:
            data = f.read()
        out = Path(outdir) / (Path(path).stem + ".ungz")
        out.write_bytes(data)
        return True
    except:
        return False


def try_extract_bzip2(path, outdir):
    try:
        with bz2.open(path, "rb") as f:
            data = f.read()
        out = Path(outdir) / (Path(path).stem + ".unbz2")
        out.write_bytes(data)
        return True
    except:
        return False


def try_extract_xz(path, outdir):
    try:
        with lzma.open(path, "rb") as f:
            data = f.read()
        out = Path(outdir) / (Path(path).stem + ".unxz")
        out.write_bytes(data)
        return True
    except:
        return False


def try_extract_tar(path, outdir):
    try:
        with tarfile.open(path) as t:
            t.extractall(outdir)
        return True
    except:
        return False


def try_binwalk(path, outdir):
    stdout, stderr, rc = run(f"binwalk --extract --depth=8 --directory={outdir} {path}", timeout=300)
    return stdout, stderr


def try_7z(path, outdir):
    stdout, stderr, rc = run(f"7z x -o{outdir} -y {path}", timeout=300)
    return stdout, stderr, rc


def try_simg2img(path, outdir):
    out = Path(outdir) / (Path(path).stem + ".raw.img")
    stdout, stderr, rc = run(f"simg2img {path} {out}", timeout=120)
    return stdout, stderr, rc


def try_lz4(path, outdir):
    out = Path(outdir) / (Path(path).stem + ".unlz4")
    stdout, stderr, rc = run(f"lz4 -d {path} {out}", timeout=120)
    return stdout, stderr, rc


def try_ext_mount(path, outdir):
    mnt = Path(outdir) / "mnt_ext"
    mnt.mkdir(exist_ok=True)
    stdout, stderr, rc = run(f"debugfs -R 'ls -l /' {path}", timeout=60)
    if rc == 0:
        stdout2, stderr2, rc2 = run(
            f"debugfs -R 'rdump / {mnt}' {path}", timeout=300
        )
        return stdout + stdout2, stderr + stderr2
    return stdout, stderr


def try_android_sparse(path, outdir):
    out = Path(outdir) / (Path(path).stem + ".raw.img")
    stdout, stderr, rc = run(f"simg2img {path} {out}", timeout=120)
    if rc == 0:
        try_ext_mount(str(out), outdir)
    return stdout, stderr


def try_payload_extract(path, outdir):
    stdout, stderr, rc = run(
        f"python3 -c \"import payload_dumper; payload_dumper.main()\" --out {outdir} {path}",
        timeout=600
    )
    if rc != 0:
        stdout, stderr, rc = run(
            f"payload-dumper-go -output {outdir} {path}", timeout=600
        )
    return stdout, stderr


def try_oppo_ozip(path, outdir):
    stdout, stderr, rc = run(f"ozipdecrypt {path}", timeout=120)
    return stdout, stderr


def try_qualcomm_mbn(path, outdir):
    results = []
    with open(path, "rb") as f:
        header = f.read(512)
    magic = le32(header, 0)
    results.append(f"magic=0x{magic:08X}")
    if magic == 0xC7F4E493:
        results.append("Qualcomm custom header confirmed")
        entry_size = 0x70
        num_entries_guess = 16
        entries = []
        for i in range(num_entries_guess):
            off = 0x08 + i * entry_size
            if off + entry_size > len(header):
                break
            chunk = header[off:off+entry_size]
            if all(b == 0 for b in chunk):
                break
            size_a = le32(chunk, 0)
            size_b = le32(chunk, 4)
            load_addr = le32(chunk, 8)
            if size_a == 0:
                break
            entries.append({"index": i, "size_a": size_a, "size_b": size_b, "load_addr": f"0x{load_addr:08X}"})
        results.append(json.dumps(entries, indent=2))
        outdir_p = Path(outdir) / "qcom_chunks"
        outdir_p.mkdir(exist_ok=True)
        file_size = Path(path).stat().st_size
        with open(path, "rb") as f:
            for idx, e in enumerate(entries):
                offset_guess = 0x200 + idx * e["size_a"]
                if offset_guess + e["size_a"] > file_size:
                    break
                f.seek(offset_guess)
                data = f.read(e["size_a"])
                chunk_path = outdir_p / f"chunk_{idx:02d}_0x{e['load_addr'][2:]}.bin"
                chunk_path.write_bytes(data)
    return "\n".join(results)


def scan_embedded_offsets(path, outdir):
    found = []
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

    outdir_p = Path(outdir) / "embedded"
    outdir_p.mkdir(exist_ok=True)
    report = []
    for offset, name in sorted(set(found)):
        report.append(f"0x{offset:016X} ({offset}) : {name}")
        extract_path = outdir_p / f"0x{offset:016X}_{name}.bin"
        with open(path, "rb") as f:
            f.seek(offset)
            slice_data = f.read(min(64 * 1024 * 1024, file_size - offset))
        extract_path.write_bytes(slice_data)
        inner_out = outdir_p / f"0x{offset:016X}_{name}"
        inner_out.mkdir(exist_ok=True)
        analyze_slice(str(extract_path), str(inner_out), name)
    return report


def analyze_slice(path, outdir, hint):
    Path(outdir).mkdir(exist_ok=True)
    if hint == "zip":
        try_extract_zip(path, outdir)
    elif hint == "gzip":
        try_extract_gzip(path, outdir)
    elif hint == "bzip2":
        try_extract_bzip2(path, outdir)
    elif hint == "xz":
        try_extract_xz(path, outdir)
    elif hint == "android_sparse":
        try_android_sparse(path, outdir)
    elif hint == "payload":
        try_payload_extract(path, outdir)
    try_7z(path, outdir)


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
    stdout, stderr, rc = run(f"strings -n {min_len} {path}", timeout=60)
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
    return interesting[:200]


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

    binwalk_out, binwalk_err = try_binwalk(input_path, output_dir)
    report["tool_outputs"]["binwalk_stdout"] = binwalk_out[:4096]
    report["tool_outputs"]["binwalk_stderr"] = binwalk_err[:1024]

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

    payload_out, payload_err = try_payload_extract(input_path, output_dir + "/payload_out")
    report["tool_outputs"]["payload_dumper"] = payload_out[:2048]

    embedded = scan_embedded_offsets(input_path, output_dir)
    report["embedded_offsets"] = embedded[:100]

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
