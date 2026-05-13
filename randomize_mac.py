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

def mtk_crc64(data):
    crc = 0
    poly = 0x42F0E1EBA9EA3693
    for b in data:
        crc ^= (b << 56)
        for _ in range(8):
            if crc & (1 << 63):
                crc = (crc << 1) ^ poly
            else:
                crc <<= 1
            crc &= (1 << 64) - 1
    return crc.to_bytes(8, 'little')

def update_crc64(data, start, length):
    checksum_offset = 0x308
    if checksum_offset + 8 > len(data):
        return False
    data[start+checksum_offset:start+checksum_offset+8] = mtk_crc64(data[start:start+length])
    return True

def update_version(data, offset, lid_size=512):
    ver_offset = offset - 0x2A
    if ver_offset + 4 > len(data):
        return False
    ver = struct.unpack('<I', data[ver_offset:ver_offset+4])[0]
    new_ver = ver + 1
    data[ver_offset:ver_offset+4] = struct.pack('<I', new_ver)
    return True

def main():
    img_path = "nvram.img"
    log_path = "nvram_analysis.log"
    wifi_info = {'lid_name': 'AP_CFG_RDEB_FILE_WIFI_LID', 'file_ver_name': 'AP_CFG_RDEB_FILE_WIFI_LID_VERNO', 'detected_offset': None}
    bt_info = {'lid_name': 'AP_CFG_RDEB_FILE_BT_ADDR_LID', 'file_ver_name': 'AP_CFG_RDEB_FILE_BT_ADDR_LID_VERNO', 'detected_offset': None}
    wifi_offset = 0x20008
    bt_offset = 0x2080A

    if not os.path.exists(img_path):
        print(f"Error: {img_path} not found")
        sys.exit(1)

    with open(img_path, 'r+b') as f:
        data = bytearray(f.read())

    old_wifi = bytes(data[wifi_offset:wifi_offset+6])
    new_wifi = random_mac()
    data[wifi_offset:wifi_offset+6] = new_wifi

    old_bt = bytes(data[bt_offset:bt_offset+6])
    new_bt = random_mac()
    data[bt_offset:bt_offset+6] = new_bt

    update_version(data, wifi_offset - 0x2A, 512)
    update_version(data, bt_offset, 512)

    update_crc64(data, 0x20000, 512)
    update_crc64(data, 0x20800, 512)

    with open(img_path, 'wb') as f:
        f.write(data)

    with open(log_path, 'w') as log:
        log.write(f"{datetime.datetime.now().isoformat()}\n")
        log.write(f"WiFi MAC: {old_wifi.hex(':')} -> {new_wifi.hex(':')}\n")
        log.write(f"Bluetooth MAC: {old_bt.hex(':')} -> {new_bt.hex(':')}\n")
        log.write("Version numbers and CRC64 checksums have been updated.\n")

    print("Done. Modified nvram.img and log saved.")

if __name__ == "__main__":
    main()
