#!/system/bin/sh
set -eu

LOGTAG="GSI_FIX"
EMPTY="/data/local/tmp/gsi_fix_empty"
BACKUP="/data/local/tmp/gsi_fix_backup"

log() {
  echo "[$LOGTAG] $*"
  log -t "$LOGTAG" "$*" 2>/dev/null || true
}

mkdir -p "$EMPTY"
chmod 755 "$EMPTY"

log "start"

if [ -d /vendor/app/t6 ]; then
  mkdir -p "$BACKUP/vendor_app_t6" 2>/dev/null || true
  mountpoint -q /vendor/app/t6 && umount /vendor/app/t6 || true
  mount -o bind "$EMPTY" /vendor/app/t6
  log "bind-mounted empty dir on /vendor/app/t6"
fi

if [ -d /data/vendor/t6/app ]; then
  mkdir -p "$BACKUP/data_vendor_t6_app" 2>/dev/null || true
  mountpoint -q /data/vendor/t6/app && umount /data/vendor/t6/app || true
  mount -o bind "$EMPTY" /data/vendor/t6/app
  log "bind-mounted empty dir on /data/vendor/t6/app"
fi

rm -f /data/system/locksettings* 2>/dev/null || true
rm -rf /data/misc/keystore/* 2>/dev/null || true
rm -rf /data/misc/keystore2/* 2>/dev/null || true
rm -rf /data/vendor/t6/* 2>/dev/null || true

sync
log "done"
exit 0
