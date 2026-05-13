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

def get_lid_start(data, mac_off, lid_size=512):
    start = mac_off - (mac_off % lid_size)
    if start < 0 or start + lid_size > len(data):
        return None
    return start

def update_lid_checksum(data, lid_start, lid_size, log, update_version=True):
    if lid_start + lid_size > len(data):
        return False
    if update_version:
        old_ver = struct.unpack('<H', data[lid_start:lid_start+2])[0]
        new_ver = (old_ver + 1) & 0xFFFF
        data[lid_start:lid_start+2] = struct.pack('<H', new_ver)
        log.write(f"     Version: 0x{old_ver:04x} -> 0x{new_ver:04x}\n")
    cksum_off = lid_start + lid_size - 2
    old_csum = data[cksum_off:cksum_off+2]
    data[cksum_off:cksum_off+2] = b'\x00\x00'
    new_csum = crc16_ccitt(data[lid_start:lid_start+lid_size])
    data[cksum_off:cksum_off+2] = struct.pack('<H', new_csum)
    log.write(f"     Checksum: 0x{old_csum.hex()} -> 0x{new_csum:04x}\n")
    return True

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--wifi-offset', default=None, help='WiFi MAC offset in hex')
    parser.add_argument('--bt-offset', default=None, help='Bluetooth MAC offset in hex')
    parser.add_argument('--lid-size', default='512', help='LID size in bytes (default 512)')
    parser.add_argument('--no-version-inc', action='store_true', help='Do not increment version number')
    args = parser.parse_args()
    lid_size = int(args.lid_size)

    img_path = "nvram.img"
    if not os.path.exists(img_path):
        print(f"ERROR: {img_path} not found")
        sys.exit(1)

    with open(img_path, 'rb') as f:
        data = bytearray(f.read())

    log_path = "nvram_analysis.log"
    with open(log_path, 'w') as log:
        log.write(f"{datetime.datetime.now().isoformat()}\n")
        log.write(f"File size: {len(data)} bytes\n")
        log.write(f"LID size: {lid_size} bytes\n\n")

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

            lid_start = get_lid_start(data, off, lid_size)
            if lid_start is None:
                log.write(f"     ERROR: Cannot find LID start for offset 0x{off:06x}\n")
                continue
            log.write(f"     LID start: 0x{lid_start:06x}\n")
            if not update_lid_checksum(data, lid_start, lid_size, log, update_version=not args.no_version_inc):
                log.write(f"     WARNING: Failed to update LID checksum\n")

        with open(img_path, 'wb') as f:
            f.write(data)
        log.write("\nDone.\n")

    print("Done. Modified nvram.img and log saved.")

if __name__ == "__main__":
    main()
