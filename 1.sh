#!/bin/bash
set -euo pipefail

MANIFEST_URL='https://github.com/minimal-manifest-twrp/platform_manifest_twrp_omni'
MANIFEST_BRANCH='twrp-9.0'
DEVICE_TREE_URL='https://github.com/coara-chocomaru/TAB-A05-BD-Neo-TWRP'
DEVICE_TREE_BRANCH='yoko'
DEVICE_PATH='device/sts/a05bd'
DEVICE_NAME='a05bd'
MAKEFILE_NAME='omni_a05bd'
BUILD_TARGET='recovery'
FIXED_SALT='95180092a2a9db10da47b2c8d0b5a239af3d9e3d9c49e87fccb81701cf2ee3c3'


WORKSPACE_DIR="$PWD/workspace"
OUTPUT_DIR="$PWD/signed_images"
KEYS_DIR="$PWD/keys"
LOGFILE="$PWD/build.log"

echo "=== Starting build & signing process ===" | tee -a "$LOGFILE"
echo "Workspace: $WORKSPACE_DIR" | tee -a "$LOGFILE"
echo "Output:    $OUTPUT_DIR" | tee -a "$LOGFILE"

rm -rf "$WORKSPACE_DIR" "$OUTPUT_DIR" "$KEYS_DIR" signed *.img fec_keys.zip 2>/dev/null || true
mkdir -p "$WORKSPACE_DIR" "$OUTPUT_DIR" "$KEYS_DIR"

export DEBIAN_FRONTEND=noninteractive
export TZ=Asia/Tokyo

echo "Installing system packages..." | tee -a "$LOGFILE"
sudo apt update
sudo apt install -y tzdata
sudo apt upgrade -y
sudo apt install -y \
    gperf gcc-multilib gcc-10-multilib g++-multilib g++-10-multilib \
    libc6-dev lib32ncurses5-dev x11proto-core-dev libx11-dev tree \
    lib32z-dev libgl1-mesa-dev libxml2-utils xsltproc bc ccache \
    lib32readline-dev lib32z1-dev liblz4-tool libncurses5-dev \
    libsdl1.2-dev libwxgtk3.0-gtk3-dev libxml2 lzop pngcrush \
    schedtool squashfs-tools imagemagick libbz2-dev lzma ncftp \
    qemu-user-static libstdc++-10-dev libncurses5 python3 libtinfo5 \
    openssl python2.7 python2.7-dev git wget xxd python3-cryptography zip curl \
    python3-pycryptodome openjdk-8-jdk-headless repo p7zip-full

sudo update-alternatives --install /usr/bin/python python /usr/bin/python2.7 1
python --version | tee -a "$LOGFILE"

if ! command -v repo &>/dev/null; then
    echo "Installing repo tool..." | tee -a "$LOGFILE"
    mkdir -p ~/bin
    curl https://storage.googleapis.com/git-repo-downloads/repo > ~/bin/repo
    chmod a+x ~/bin/repo
    sudo ln -sf ~/bin/repo /usr/bin/repo
fi

git config --global user.name "azwhikaru"
git config --global user.email "azwhikaru+37921907@github.com"

if [[ "$MANIFEST_URL" == git@github.com:* ]] && [[ -n "${SSH_PRIVATE_KEY_PATH:-}" ]]; then
    eval "$(ssh-agent -s)"
    ssh-add "$SSH_PRIVATE_KEY_PATH"
fi

cd "$WORKSPACE_DIR"
echo "Initializing repo..." | tee -a "$LOGFILE"
repo init --depth=1 -u "$MANIFEST_URL" -b "$MANIFEST_BRANCH"
echo "Syncing repo (this may take a while)..." | tee -a "$LOGFILE"
repo sync -j$(nproc --all) --force-sync

echo "Cloning device tree into $DEVICE_PATH..." | tee -a "$LOGFILE"
git clone --depth 1 --branch "$DEVICE_TREE_BRANCH" "$DEVICE_TREE_URL" "$DEVICE_PATH"


echo "Setting up 12 GB swap..." | tee -a "$LOGFILE"
sudo fallocate -l 12G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile || true 

cd "$WORKSPACE_DIR"
source build/envsetup.sh
export ALLOW_MISSING_DEPENDENCIES=true
lunch "$MAKEFILE_NAME"-eng
make clean
make fec -j$(nproc --all)
sudo cp "$WORKSPACE_DIR/out/host/linux-x86/bin/fec" /usr/local/bin/fec
sudo chmod +x /usr/local/bin/fec

cd "$PWD"
git clone --depth 1 --branch android-9.0.0_r34 https://android.googlesource.com/platform/external/avb

git clone https://github.com/nccgroup/test_avb_key nccgroup-avb
git clone https://github.com/alltechdev/haha-you-used-testkeys haha-tool

echo "Downloading Neo.zip..." | tee -a "$LOGFILE"
wget -O Neo.zip 'https://github.com/coara-chocomaru/monooki/releases/download/%E3%81%A7%E3%83%BC%E3%81%9F2/Neo.zip'
7z x Neo.zip -o. -aoa -y
if [ -d Neo ]; then
    mv Neo/*.img . 2>/dev/null || true
    rmdir Neo 2>/dev/null || true
fi
ls -la *.img | tee -a "$LOGFILE"

for img in dtbo.img boot.img recovery.img vbmeta.img; do
    if [[ -f "$img" ]]; then
        echo "Info for $img:" | tee -a "$LOGFILE"
        python avb/avbtool info_image --image "$img" 2>&1 | tee -a "$LOGFILE"
    fi
done

cp haha-tool/keys/vbmeta.pem "$KEYS_DIR/vbmeta.pem"
cp haha-tool/keys/vbmeta.pem "$KEYS_DIR/vbmeta.avbpubkey"


BOOT_PARTITION_SIZE=$(stat -c%s boot.img)
RECOVERY_PARTITION_SIZE=$(stat -c%s recovery.img)
SYSTEM_PARTITION_SIZE=$(stat -c%s system.img)
VENDOR_PARTITION_SIZE=$(stat -c%s vendor.img)
DTBO_PARTITION_SIZE=$(stat -c%s dtbo.img)

mkdir -p signed

cp boot.img signed/boot.img
python avb/avbtool erase_footer --image signed/boot.img 2>/dev/null || true
python avb/avbtool add_hash_footer \
    --image signed/boot.img \
    --partition_name boot \
    --partition_size "$BOOT_PARTITION_SIZE" \
    --hash_algorithm sha256 \
    --algorithm NONE \
    --salt "$FIXED_SALT"

cp recovery.img signed/recovery.img
python avb/avbtool erase_footer --image signed/recovery.img 2>/dev/null || true
python avb/avbtool add_hash_footer \
    --image signed/recovery.img \
    --partition_name recovery \
    --partition_size "$RECOVERY_PARTITION_SIZE" \
    --hash_algorithm sha256 \
    --key "$KEYS_DIR/recovery.pem" \
    --algorithm SHA256_RSA2048 \
    --salt "$FIXED_SALT"

cp system.img signed/system.img
export HOST_OUT="$WORKSPACE_DIR/out/host/linux-x86"
export LD_LIBRARY_PATH="$HOST_OUT/lib64:$HOST_OUT/lib:${LD_LIBRARY_PATH:-}"
export PATH="$HOST_OUT/bin:${PATH}"
python avb/avbtool erase_footer --image signed/system.img 2>/dev/null || true
python avb/avbtool add_hashtree_footer \
    --image signed/system.img \
    --partition_name system \
    --setup_as_rootfs_from_kernel \
    --partition_size "$SYSTEM_PARTITION_SIZE" \
    --key "$KEYS_DIR/system.pem" \
    --algorithm SHA256_RSA2048 \
    --hash_algorithm sha1 \
    --fec_num_roots 2 \
    --salt "$(openssl rand -hex 32)"

cp vendor.img signed/vendor.img
python avb/avbtool erase_footer --image signed/vendor.img 2>/dev/null || true
python avb/avbtool add_hashtree_footer \
    --image signed/vendor.img \
    --partition_name vendor \
    --partition_size "$VENDOR_PARTITION_SIZE" \
    --hash_algorithm sha1 \
    --fec_num_roots 2 \
    --algorithm NONE \
    --salt "$FIXED_SALT"

cp dtbo.img signed/dtbo.img

python avb/avbtool make_vbmeta_image \
    --output signed/vbmeta.img \
    --key "$KEYS_DIR/vbmeta.pem" \
    --algorithm SHA256_RSA2048 \
    --chain_partition recovery:1:"$KEYS_DIR/recovery.avbpubkey" \
    --chain_partition system:2:"$KEYS_DIR/system.avbpubkey" \
    --include_descriptors_from_image signed/vendor.img \
    --include_descriptors_from_image signed/dtbo.img \
    --include_descriptors_from_image signed/boot.img

echo "vbmeta info:" | tee -a "$LOGFILE"
python avb/avbtool info_image --image signed/vbmeta.img | tee -a "$LOGFILE"

echo "Verifying with nccgroup test_avb_key (may show warnings)..." | tee -a "$LOGFILE"
python3 nccgroup-avb/test_avb_key.py signed/vbmeta.img || true

curl https://bootstrap.pypa.io/pip/2.7/get-pip.py -o get-pip.py
python get-pip.py
python -m pip install --upgrade "pip<21"
python -m pip install "setuptools<45"
python -m pip uninstall -y pycryptodome || true
python -m pip install "pycrypto==2.6.1"

echo "Explicit verification with avbtool..." | tee -a "$LOGFILE"
python avb/avbtool verify_image \
    --image signed/vbmeta.img \
    --key "$KEYS_DIR/vbmeta.pem" \
    --expected_chain_partition recovery:1:"$KEYS_DIR/recovery.avbpubkey" \
    --expected_chain_partition system:2:"$KEYS_DIR/system.avbpubkey"

zip -r keys.zip "$KEYS_DIR"
cp -r signed/* "$OUTPUT_DIR/"
cp keys.zip "$OUTPUT_DIR/"

echo "==================================================" | tee -a "$LOGFILE"
echo "Build and signing completed successfully!" | tee -a "$LOGFILE"
echo "Signed images are in: $OUTPUT_DIR" | tee -a "$LOGFILE"
echo "Signing keys archive: $OUTPUT_DIR/keys.zip" | tee -a "$LOGFILE"
echo "Full log: $LOGFILE" | tee -a "$LOGFILE"
echo "==================================================" | tee -a "$LOGFILE"
