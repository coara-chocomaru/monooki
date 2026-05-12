import os
import sys
import random
import struct
import argparse
import datetime

def random_mac():
    mac = [random.randint(0x00, 0xff) for _ in range(6)]
    mac[0] = (mac[0] & 0xfc) | 0x02
    return bytes(mac)

def is_valid_mac(data, off):
    if off + 6 > len(data):
        return False
    mac = data[off:off+6]
    if mac == b'\x00'*6 or mac == b'\xff'*6:
        return False
    if mac[0] & 0x01:
        return False
    return True

def crc16_ccitt(data):
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

def find_wifi_offset(data):
    pattern = bytes([0x01, 0x00, 0x08, 0x00, 0x00, 0x03])
    idx = data.find(pattern)
    if idx == -1:
        return None
    mac_start = idx + len(pattern)
    if not is_valid_mac(data, mac_start):
        for delta in (-3, -2, -1, 1, 2, 3):
            cand = mac_start + delta
            if is_valid_mac(data, cand):
                return cand
        return None
    return mac_start

def find_bt_offset(data, wifi_off):
    cand = wifi_off + 0x802
    if is_valid_mac(data, cand):
        return cand
    bt_str = b'BT_Addr'
    idx = data.find(bt_str)
    if idx != -1:
        cand2 = idx + len(bt_str)
        if cand2 < len(data) and data[cand2] == 0:
            cand2 += 1
        if is_valid_mac(data, cand2):
            return cand2
    if is_valid_mac(data, wifi_off + 0x800):
        return wifi_off + 0x800
    return None

def find_record_bounds(data, mac_off):
    end = mac_off + 6
    while end + 2 <= len(data):
        if data[end:end+2] == b'\xff\xff':
            break
        end += 1
    if end + 2 > len(data):
        return None, None, None
    pattern = bytes([0x01, 0x00, 0x08, 0x00, 0x00, 0x03])
    start = max(0, mac_off - 512)
    rec_start = data.find(pattern, start, mac_off)
    if rec_start == -1:
        rec_start = max(0, mac_off - 18)
    cksum_off = end - 2
    if cksum_off < rec_start:
        return None, None, None
    rec_len = end - rec_start
    return rec_start, rec_len, cksum_off

def update_checksum(data, rec_start, rec_len, cksum_off, log):
    if cksum_off + 2 > len(data):
        return False
    old = data[cksum_off:cksum_off+2]
    data[cksum_off:cksum_off+2] = b'\x00\x00'
    new = crc16_ccitt(data[rec_start:rec_start+rec_len])
    data[cksum_off:cksum_off+2] = struct.pack('<H', new)
    log.write(f"     Checksum: 0x{old.hex()} -> 0x{new:04x}\n")
    return True

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--wifi-offset', default=None)
    parser.add_argument('--bt-offset', default=None)
    args = parser.parse_args()

    img_path = "nvram.img"
    if not os.path.exists(img_path):
        print(f"ERROR: {img_path} not found")
        sys.exit(1)

    with open(img_path, 'rb') as f:
        data = bytearray(f.read())

    log_path = "nvram_analysis.log"
    with open(log_path, 'w') as log:
        log.write(f"{datetime.datetime.now().isoformat()}\n")
        log.write(f"File size: {len(data)} bytes\n\n")

        if args.wifi_offset:
            wifi_off = int(args.wifi_offset, 16)
            log.write(f"WiFi offset manual: 0x{wifi_off:06x}\n")
        else:
            wifi_off = find_wifi_offset(data)
            if wifi_off is None:
                log.write("ERROR: Cannot detect WiFi offset\n")
                sys.exit(1)
            log.write(f"WiFi offset detected: 0x{wifi_off:06x}\n")

        if not is_valid_mac(data, wifi_off):
            log.write(f"ERROR: No valid MAC at 0x{wifi_off:06x}\n")
            sys.exit(1)

        if args.bt_offset:
            bt_off = int(args.bt_offset, 16)
            log.write(f"BT offset manual: 0x{bt_off:06x}\n")
        else:
            bt_off = find_bt_offset(data, wifi_off)
            if bt_off is None:
                log.write("WARNING: BT offset not found, using fallback +0x802\n")
                bt_off = wifi_off + 0x802
            log.write(f"BT offset detected: 0x{bt_off:06x}\n")

        log.write("\n--- Randomization ---\n")
        for name, off in [('WiFi', wifi_off), ('Bluetooth', bt_off)]:
            old = bytes(data[off:off+6])
            new = random_mac()
            data[off:off+6] = new
            log.write(f"{name}: 0x{off:06x}  old {old.hex(':')} -> new {new.hex(':')}\n")
            rec_start, rec_len, cksum_off = find_record_bounds(data, off)
            if rec_start is not None and cksum_off is not None:
                if update_checksum(data, rec_start, rec_len, cksum_off, log):
                    log.write(f"     Record checksum updated.\n")
                else:
                    log.write(f"     Warning: checksum update failed.\n")
            else:
                log.write(f"     Warning: could not locate record, checksum unchanged.\n")

        with open(img_path, 'wb') as f:
            f.write(data)
        log.write("\nDone.\n")

    print("Done. Modified nvram.img and log saved.")

if __name__ == "__main__":
    main()
