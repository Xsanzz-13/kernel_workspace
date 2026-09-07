#!/bin/bash
set -e

# ==========================================
# INISIALISASI & PATH DOCKER
# ==========================================
export KERNEL_ROOT=/work/x 
export TOOLCHAIN_DIR=/work/toolchain
export ARCH=arm64
export CLANG_TRIPLE=aarch64-linux-gnu-

# Langsung arahkan ke direktori toolchain lokal, tidak perlu download
export PATH=$TOOLCHAIN_DIR/clang-r383902/bin:$PATH
export CROSS_COMPILE=$TOOLCHAIN_DIR/aarch64-linux-android-4.9/bin/aarch64-linux-android-

export DEFCONFIG="a3core_eur_open_defconfig"

# Variabel khusus Samsung SharkL3
export BSP_BUILD_FAMILY=sharkl3
export BSP_BUILD_ANDROID_OS=y
export DTC_OVERLAY_TEST_EXT=$KERNEL_ROOT/tools/mkdtimg/ufdt_apply_overlay

# ==========================================
# TAHAP 1: Install Dependencies
# ==========================================
echo "[+] Menginstal dependencies sistem..."
apt-get update && apt-get install -y \
    build-essential bc bison flex libssl-dev libelf-dev ccache \
    python3 python3-pip python3-minimal python2 git zip unzip curl wget cpio lld \
    gcc-aarch64-linux-gnu libncurses-dev libyaml-dev tar && apt-get clean

# ==========================================
# TAHAP 2: KSU Integration
# ==========================================
echo "[+] Mengintegrasikan KernelSU..."
cd $KERNEL_ROOT
curl -LSs "https://raw.githubusercontent.com/backslashxx/KernelSU/master/kernel/setup.sh" | bash
find KernelSU -name ".git" -exec rm -rf {} + || true

# ==========================================
# TAHAP 3: Extract Kernel Component
# ==========================================
echo "[+] Mengekstrak komponen sound/soc/sprd..."
mkdir -p $KERNEL_ROOT/output
if [ -d "$KERNEL_ROOT/sound/soc/sprd" ]; then
  cp -r $KERNEL_ROOT/sound/soc/sprd $KERNEL_ROOT/output/
  cd $KERNEL_ROOT/output
  zip -r sprd.zip sprd
  cd $KERNEL_ROOT
else
  echo "[!] Direktori sound/soc/sprd tidak ditemukan, melewati tahap ini."
fi

# ==========================================
# TAHAP 4: Konfigurasi & Build Kernel
# ==========================================

echo "[+] Melakukan konfigurasi kernel dengan $DEFCONFIG..."
# Untuk Step Generate Config
make O=out CC=$TOOLCHAIN_DIR/clang-r383902/bin/clang LD=$TOOLCHAIN_DIR/clang-r383902/bin/ld.lld $DEFCONFIG

echo "[+] Memulai kompilasi Kernel Image..."
export KBUILD_BUILD_HOST="$(. /etc/os-release && echo ${NAME}-${VERSION_ID})"
export KBUILD_BUILD_TIMESTAMP="$(date '+%a %b %d %T WIB %Y')"

export KCFLAGS="-march=armv8.2-a+crypto+fp16+rcpc -mtune=cortex-a55"
export KBUILD_LDFLAGS="--gc-sections --icf=all"

# Ignore yaml
sed -i '/yamltree.o/d' scripts/dtc/Makefile
sed -i '/dt_to_yaml/d' scripts/dtc/dtc.c

# Untuk Step Build Utama
make -j$(nproc) O=out \
     CC=$TOOLCHAIN_DIR/clang-r383902/bin/clang \
     LD=$TOOLCHAIN_DIR/clang-r383902/bin/ld.lld \
     CLANG_TRIPLE=$CLANG_TRIPLE \
     CROSS_COMPILE=$CROSS_COMPILE \
     BSP_BUILD_DT_OVERLAY=y \
     KCFLAGS="$KCFLAGS" \
     KBUILD_LDFLAGS="$KBUILD_LDFLAGS" \
     Image

# ==========================================
# TAHAP 5: Membuat dtbo.img
# ==========================================
echo "[+] Membuat dtbo.img..."
./out/tools/mkdtboimg/mkdtboimg create out/dtbo.img out/arch/arm64/boot/dts/sprd/*.dtbo

echo "[+] Build selesai! Image, dtbo.img