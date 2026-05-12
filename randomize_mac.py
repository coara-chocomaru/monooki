#!/usr/bin/env python3
import os
import sys
import random
import struct
import hashlib
import binascii
from typing import Tuple, Optional, List

def random_mac() -> bytes:
    mac = [random.randint(0x00, 0xff) for _ in range(6)]
    mac[0] = (mac[0] & 0xfc) | 0x02   # locally administered, unicast
    return bytes(mac)

def is_valid_mac(data: bytes, off: int) -> bool:
    if off + 6 > len(data):
        return False
    mac = data[off:off+6]
    if mac in (b'\x00'*6, b'\xff'*6):
        return False
    if mac[0] & 0x01:   # multicast bit must be 0
        return False
    # ローカル管理ビットは無視（ランダムなので1でも可）
    return True

def find_mac_by_string(data: bytes, search: str, offset_after: int = 0) -> Optional[int]:
    idx = data.find(search.encode())
    if idx == -1:
        return None
    cand = idx + len(search) + offset_after
    if is_valid_mac(data, cand):
        return cand
    return None

def find_mac_by_pattern(data: bytes) -> List[Tuple[int, str]]:
    candidates = []
    # 文字列ベースの検索
    wifi1 = find_mac_by_string(data, "WIFI", 0)
    if wifi1:
        candidates.append((wifi1, "WiFi (near 'WIFI')"))
    wifi2 = find_mac_by_string(data, "APRDEB/WIFI", 0)
    if wifi2:
        candidates.append((wifi2, "WiFi (near 'APRDEB/WIFI')"))
    bt1 = find_mac_by_string(data, "BT_Addr", 0)
    if bt1:
        candidates.append((bt1, "BT (near 'BT_Addr')"))
    bt2 = find_mac_by_string(data, "APRDEB/BT_Addr", 0)
    if bt2:
        candidates.append((bt2, "BT (near 'APRDEB/BT_Addr')"))
    
    # 全範囲スキャン（妥当なMACを探す）
    for off in range(len(data) - 6):
        if is_valid_mac(data, off):
            # 自分で追加する際は重複を避ける
            if not any(abs(off - c[0]) < 6 for c in candidates):
                candidates.append((off, f"pattern at 0x{off:06x}"))
    # 重複除去（近すぎるものはマージ）
    unique = []
    for off, desc in sorted(candidates, key=lambda x: x[0]):
        if not unique or off - unique[-1][0] > 6:
            unique.append((off, desc))
    return unique

def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc

def find_record_containing(data: bytes, mac_off: int) -> Optional[Tuple[int, int, int]]:
    """レコードの開始オフセット, 全体長, チェックサム位置（末尾から2バイトと仮定）を返す"""
    # MACの少し前から探索（最大512バイト前まで）
    start = max(0, mac_off - 512)
    # レコードの先頭によくあるマーカー: 'NVR' (0x4E5652) または 'MMM' など
    # まずは16バイトアライメントで試す
    for off in range(start, mac_off, 16):
        # 簡易ヘッダ: 2バイト長, 2バイトチェックサム? ここでは単純にMACの後ろに0xFFFFで終端されることが多い
        # MACから後方をスキャンして0xFFFFが連続する位置を終端と仮定
        end_candidate = mac_off + 6
        while end_candidate + 2 <= len(data):
            if data[end_candidate:end_candidate+2] == b'\xff\xff':
                # 終端らしい
                rec_len = end_candidate - off
                # チェックサムを末尾2バイトと仮定（よくあるパターン）
                if rec_len >= 2:
                    cksum_off = end_candidate - 2
                    if cksum_off >= off:
                        return off, rec_len, cksum_off
                break
            end_candidate += 1
    return None

def update_checksum(data: bytearray, rec_start: int, rec_len: int, cksum_off: int):
    if cksum_off + 2 > len(data):
        return
    # チェックサムフィールドをゼロにして全体のCRCを計算
    old_cksum = data[cksum_off:cksum_off+2]
    data[cksum_off:cksum_off+2] = b'\x00\x00'
    calc = crc16_ccitt(data[rec_start:rec_start+rec_len])
    data[cksum_off:cksum_off+2] = struct.pack('<H', calc)
    return old_cksum, calc

def randomize_mac_bytes(data: bytearray, offset: int, log, name: str) -> bool:
    old = bytes(data[offset:offset+6])
    new = random_mac()
    data[offset:offset+6] = new
    log.write(f"  {name}: 0x{offset:06x} | {old.hex(':')} -> {new.hex(':')}\n")
    # チェックサム処理を試みる
    rec_info = find_record_containing(data, offset)
    if rec_info:
        rec_start, rec_len, cksum_off = rec_info
        if cksum_off + 2 <= len(data):
            old_cksum, new_cksum = update_checksum(data, rec_start, rec_len, cksum_off)
            log.write(f"  Checksum updated: old=0x{old_cksum.hex()} new=0x{new_cksum:04x}\n")
            return True
    log.write(f"  Warning: No checksum found or update failed (device may reject)\n")
    return False

def main():
    img_path = "nvram.img"
    log_path = "nvram_analysis.log"

    if not os.path.exists(img_path):
        print(f"Error: {img_path} not found", file=sys.stderr)
        sys.exit(1)

    with open(img_path, 'rb') as f:
        data = bytearray(f.read())

    # 動的にMAC候補を全て検出
    candidates = find_mac_by_pattern(data)
    # 優先順位: 文字列近くのものを先に
    candidates.sort(key=lambda x: 0 if 'WiFi' in x[1] or 'BT' in x[1] else 1)

    if not candidates:
        print("No MAC address candidates found!", file=sys.stderr)
        sys.exit(1)

    with open(log_path, 'w') as log:
        log.write(f"NVRAM Analysis - {os.popen('date -Iseconds').read().strip()}\n")
        log.write(f"File size: {len(data)} bytes\n")
        log.write(f"Found {len(candidates)} MAC candidates:\n")
        for off, desc in candidates:
            mac = data[off:off+6].hex(':')
            log.write(f"  0x{off:06x} ({desc}) : {mac}\n")
        log.write("\nRandomizing selected MACs (first two distinct):\n")

    # WiFiとBTを選択（最初の2つ、または同じ場合は1つだけ）
    wifi_off = None
    bt_off = None
    for off, desc in candidates:
        if wifi_off is None:
            wifi_off = off
        elif bt_off is None and off != wifi_off:
            bt_off = off
            break

    if wifi_off is None:
        print("No WiFi candidate", file=sys.stderr)
        sys.exit(1)

    with open(log_path, 'a') as log:
        randomize_mac_bytes(data, wifi_off, log, "WiFi")
        if bt_off:
            randomize_mac_bytes(data, bt_off, log, "Bluetooth")
        else:
            log.write("Bluetooth MAC not found (using WiFi offset+0x802 as fallback)\n")
            bt_fallback = wifi_off + 0x802
            if bt_fallback + 6 <= len(data):
                randomize_mac_bytes(data, bt_fallback, log, "Bluetooth (fallback)")
            else:
                log.write("Fallback out of range, BT unchanged\n")

    with open(img_path, 'wb') as f:
        f.write(data)

    print("Done. Modified nvram.img and log saved.")

if __name__ == "__main__":
    main()
