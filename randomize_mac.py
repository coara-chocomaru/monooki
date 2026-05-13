import os
import sys
import random
import struct
import argparse
import datetime

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

def is_valid_mac(data, off):
    if off + 6 > len(data):
        return False
    mac = data[off:off+6]
    if mac == b'\x00'*6 or mac == b'\xff'*6:
        return False
    if mac[0] & 0x01:
        return False
    return True

def find_lid_boundary(data, start, search_step=512):
    """LIDの終了位置を推測 (デフォルト512バイト、次の非ゼロ領域まで)"""
    end = start + search_step
    while end + 2 < len(data):
        if data[end:end+2] in (b'\x01\x00', b'\x00\x00') and data[end+2:end+4] != b'\x00\x00':
            break
        end += 1
        if end - start > 4096:
            end = start + 512
            break
    return end

def update_lid_checksum(data, start, end, log, update_version=True):
    """指定されたLID領域のバージョンとチェックサムを更新"""
    if end - start < 4:
        return False

    if update_version:
        old_ver = struct.unpack('<H', data[start:start+2])[0]
        new_ver = (old_ver + 1) & 0xFFFF
        data[start:start+2] = struct.pack('<H', new_ver)
        log.write(f"       Version: 0x{old_ver:04x} -> 0x{new_ver:04x}\n")

    cksum_off = end - 2
    if cksum_off + 2 > len(data):
        return False
    old_csum = data[cksum_off:cksum_off+2]
    data[cksum_off:cksum_off+2] = b'\x00\x00'
    new_csum = crc16_ccitt(data[start:end])
    data[cksum_off:cksum_off+2] = struct.pack('<H', new_csum)
    log.write(f"       Checksum: {old_csum.hex()} -> {new_csum:04x}\n")
    return True

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--wifi-offset', type=lambda x: int(x,16), help='WiFi MAC address offset (hex)')
    parser.add_argument('--bt-offset', type=lambda x: int(x,16), help='Bluetooth MAC address offset (hex)')
    parser.add_argument('--lid-size', type=int, default=512, help='Fixed LID size (if auto-detect fails)')
    parser.add_argument('--no-version-inc', action='store_true', help='Do not increment version number')
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
        log.write(f"File size: {len(data)} bytes\n")
        log.write(f"LID size hint: {args.lid_size}\n\n")

        if args.wifi_offset is not None:
            wifi_off = args.wifi_offset
            log.write(f"WiFi offset manual: 0x{wifi_off:06x}\n")
        else:
            pattern = bytes([0x01, 0x00, 0x08, 0x00, 0x00, 0x03])
            idx = data.find(pattern)
            if idx == -1:
                log.write("ERROR: Cannot find WiFi MAC pattern\n")
                sys.exit(1)
            wifi_off = idx + len(pattern)
            log.write(f"WiFi offset detected: 0x{wifi_off:06x}\n")

        if not is_valid_mac(data, wifi_off):
            log.write(f"ERROR: No valid MAC at 0x{wifi_off:06x}\n")
            sys.exit(1)

        if args.bt_offset is not None:
            bt_off = args.bt_offset
            log.write(f"BT offset manual: 0x{bt_off:06x}\n")
        else:
            cand = wifi_off + 0x802
            if is_valid_mac(data, cand):
                bt_off = cand
            else:
                bt_str = b'BT_Addr'
                idx = data.find(bt_str)
                if idx != -1:
                    cand2 = idx + len(bt_str) + 1
                    if is_valid_mac(data, cand2):
                        bt_off = cand2
                    else:
                        bt_off = cand
                else:
                    bt_off = cand
            log.write(f"BT offset detected: 0x{bt_off:06x}\n")

        def get_lid_range(off):
            start = off - (off % args.lid_size)
            end = find_lid_boundary(data, start, args.lid_size)
            if end <= start:
                end = start + args.lid_size
            if data[start] == 0 and data[start+1] == 0:
                start2 = start + args.lid_size
                if start2 + args.lid_size <= len(data):
                    start = start2
                    end = start + args.lid_size
            return start, end

        log.write("\n--- Randomization & Checksum Update ---\n")
        for name, off in [('WiFi', wifi_off), ('Bluetooth', bt_off)]:
            old_mac = bytes(data[off:off+6])
            new_mac = random_mac()
            data[off:off+6] = new_mac
            log.write(f"{name} MAC: 0x{off:06x}  {old_mac.hex(':')} -> {new_mac.hex(':')}\n")

            start, end = get_lid_range(off)
            log.write(f"  LID range: 0x{start:06x} - 0x{end:06x} ({end-start} bytes)\n")
            if not update_lid_checksum(data, start, end, log, update_version=not args.no_version_inc):
                log.write(f"  WARNING: Failed to update LID checksum for {name}\n")

        if len(data) > 0x10 and data[0:2] != b'\x00\x00':
            log.write("\n--- Global header checksum (experimental) ---\n")
            global_csum_off = 0x04   # 仮定
            if global_csum_off + 2 <= len(data):
                old_global = data[global_csum_off:global_csum_off+2]
                data[global_csum_off:global_csum_off+2] = b'\x00\x00'
                new_global = crc16_ccitt(data[:0x200])  
                data[global_csum_off:global_csum_off+2] = struct.pack('<H', new_global)
                log.write(f"  Global checksum at 0x{global_csum_off:04x}: {old_global.hex()} -> {new_global:04x}\n")

        with open(img_path, 'wb') as f:
            f.write(data)
        log.write("\nDone.\n")

    print("Modified nvram.img written. See nvram_analysis.log for details.")

def random_mac():
    mac = [random.randint(0x00, 0xff) for _ in range(6)]
    mac[0] = (mac[0] & 0xfc) | 0x02 
    return bytes(mac)

if __name__ == "__main__":
    main()
