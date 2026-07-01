#!/system/bin/sh
/system/bin/stp_dump --live "allow init block_device:blk_file { read open getattr }" 2>/dev/null
/system/bin/stp_dump --live "allow init seccfg_device:blk_file { read open getattr }" 2>/dev/null
HASH=$(dd if=/dev/block/platform/bootdevice/by-name/seccfg bs=4096 2>/dev/null | sha256sum | cut -d' ' -f1)
setprop seccfg_hash "$HASH"
