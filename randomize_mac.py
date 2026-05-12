import os
import sys
import random
import struct
import hashlib
import datetime

def random_mac():
    mac = [random.randint(0x00, 0xff) for _ in range(6)]
    mac[0] = (mac[0] & 0xfc) | 0x02
    return bytes(mac)

def crc16_ccitt(data):
    crc = 0xffff
    for b in data:
        crc ^= (b << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xffff
    return crc

def find_record_checksum(data, mac_off):
    for start in range(max(0, mac_off - 0x200), mac_off, 16):
        if start + 4 > len(data):
            continue
        length_field = struct.unpack('<H', data[start+2:start+4])[0]
        if length_field == 0 or start + length_field > len(data):
            continue
        if start + length_field - 2 >= 0:
            cksum_off = start + length_field - 2
            if cksum_off + 2 <= len(data):
                return start, length_field, cksum_off
    return None, None, None

def main():
    img_path = "nvram.img"
    log_path = "mac_change.log"
    wifi_offset = int(sys.argv[1] if len(sys.argv) > 1 else "0x20008", 16)
    bt_offset = int(sys.argv[2] if len(sys.argv) > 2 else "0x2080A", 16)

    if not os.path.exists(img_path):
        print(f"Error: {img_path} not found")
        sys.exit(1)

    with open(img_path, 'r+b') as f:
        data = bytearray(f.read())

    changes = []
    for name, off in [('WiFi', wifi_offset), ('Bluetooth', bt_offset)]:
        if off + 6 > len(data):
            print(f"Offset 0x{off:06x} out of range")
            continue
        old = bytes(data[off:off+6])
        new = random_mac()
        data[off:off+6] = new
        changes.append((name, off, old, new))

        rec_start, rec_len, cksum_off = find_record_checksum(data, off)
        if rec_start is not None and cksum_off is not None:
            old_csum = data[cksum_off:cksum_off+2]
            data[cksum_off:cksum_off+2] = b'\x00\x00'
            new_csum = crc16_ccitt(data[rec_start:rec_start+rec_len])
            data[cksum_off:cksum_off+2] = struct.pack('<H', new_csum)
            changes[-1] = (*changes[-1][:3], old_csum, new_csum)

    with open(log_path, 'w') as log:
        log.write(f"{datetime.datetime.now().isoformat()}\n")
        for item in changes:
            if len(item) == 4:
                name, off, old, new = item
                log.write(f"{name} | 0x{off:06x} | {old.hex(':')} -> {new.hex(':')}\n")
            else:
                name, off, old, new, old_cs, new_cs = item
                log.write(f"{name} | 0x{off:06x} | {old.hex(':')} -> {new.hex(':')} | checksum {old_cs.hex()} -> {new_cs:04x}\n")

    with open(img_path, 'wb') as f:
        f.write(data)

    print("Done. nvram.img and log saved.")

if __name__ == "__main__":
    main()
