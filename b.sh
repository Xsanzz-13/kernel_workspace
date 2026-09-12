#!/bin/bash
set -e

# ==========================================
# INISIALISASI & PATH 
# ==========================================
export KERNEL_ROOT=$GITHUB_WORKSPACE
export TOOLCHAIN_DIR=$KERNEL_ROOT/toolchain_download
export ARCH=arm64
export SUBARCH=arm64
export CC=clang
export CLANG_TRIPLE=aarch64-linux-gnu-
export BSP_BUILD_FAMILY=sharkl3
export BSP_BUILD_ANDROID_OS=y
export DEFCONFIG="a3core_eur_open_defconfig"

# ==========================================
# TAHAP 1: KSU Integration
# ==========================================
echo "[+] Mengintegrasikan KernelSU..."
cd $KERNEL_ROOT
curl -LSs "https://raw.githubusercontent.com/backslashxx/KernelSU/master/kernel/setup.sh" | bash
find KernelSU -name ".git" -exec rm -rf {} + || true

# ==========================================
# TAHAP 2: Extract Kernel Component
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
# TAHAP 3: Setup Toolchains dari Skrip
# ==========================================
mkdir -p $TOOLCHAIN_DIR/gcc-14.3 $TOOLCHAIN_DIR/clang-r383902b

if [ ! -f "$TOOLCHAIN_DIR/clang-r383902b/bin/clang" ]; then
  echo "[+] Downloading Clang ke toolchain directory..."
  wget -q https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/+archive/0e9e7035bf8ad42437c6156e5950eab13655b26c/clang-r383902b.tar.gz -O /tmp/clang.tar.gz
  tar -xf /tmp/clang.tar.gz -C $TOOLCHAIN_DIR/clang-r383902b && rm /tmp/clang.tar.gz
else
  echo "[=] Clang sudah terunduh, melewatinya."
fi

if [ ! -f "$TOOLCHAIN_DIR/gcc-14.3/bin/aarch64-none-linux-gnu-gcc" ]; then
  echo "[+] Downloading GCC 14.3 ke toolchain directory..."
  wget -q https://developer.arm.com/-/media/Files/downloads/gnu/14.3.rel1/binrel/arm-gnu-toolchain-14.3.rel1-x86_64-aarch64-none-linux-gnu.tar.xz -O /tmp/gcc.tar.xz
  tar -xf /tmp/gcc.tar.xz -C $TOOLCHAIN_DIR/gcc-14.3 --strip-components=1 && rm /tmp/gcc.tar.xz
else
  echo "[=] GCC 14.3 sudah terunduh, melewatinya."
fi

# Mengatur PATH secara presisi ke bin toolchain baru
export PATH="$TOOLCHAIN_DIR/clang-r383902b/bin:$TOOLCHAIN_DIR/gcc-14.3/bin:$PATH"
export CROSS_COMPILE="$TOOLCHAIN_DIR/gcc-14.3/bin/aarch64-none-linux-gnu-"
export CROSS_COMPILE_ARM32="$TOOLCHAIN_DIR/gcc-14.3/bin/arm-none-linux-gnueabihf-"

# ==========================================
# TAHAP 4: Konfigurasi & Build Kernel
# ==========================================
echo "[+] Melakukan konfigurasi kernel dengan $DEFCONFIG..."
make -C $KERNEL_ROOT O=$KERNEL_ROOT/out ARCH=$ARCH CC=$CC LD=ld.lld $DEFCONFIG

echo "[+] Memulai kompilasi Kernel Image..."
export KBUILD_BUILD_HOST="GitHub-Actions"
export KBUILD_BUILD_TIMESTAMP="$(date '+%a %b %d %T WIB %Y')"

# Tambahan Flags Optimasi CPU (Cortex-A55) & Linker
export KCFLAGS="-march=armv8.2-a+crypto -mtune=cortex-a55"
export KBUILD_LDFLAGS="--gc-sections --icf=all"

# Ignore yaml
sed -i '/yamltree.o/d' scripts/dtc/Makefile
sed -i '/dt_to_yaml/d' scripts/dtc/dtc.c

# Eksekusi Build Image
make -C $KERNEL_ROOT O=$KERNEL_ROOT/out -j$(nproc) ARCH=$ARCH \
    CC=$CC \
    CROSS_COMPILE=$CROSS_COMPILE \
    CROSS_COMPILE_ARM32=$CROSS_COMPILE_ARM32 \
    CLANG_TRIPLE=$CLANG_TRIPLE \
    LD=ld.lld \
    AR=llvm-ar \
    NM=llvm-nm \
    KCFLAGS="$KCFLAGS" \
    KBUILD_LDFLAGS="$KBUILD_LDFLAGS" \
    Image
    
# ==========================================
# ARTIFACTS: Pengumpulan Hasil Jadi
# ==========================================
echo "[+] Mengumpulkan hasil eksport..."
mkdir -p "$GITHUB_WORKSPACE/output_artifacts"

OUT="$KERNEL_ROOT/out"
ART="$GITHUB_WORKSPACE/output_artifacts"

# Kernel utama
if [ -f "$OUT/arch/arm64/boot/Image" ]; then
    cp "$OUT/arch/arm64/boot/Image" "$ART/"
    echo "[+] Image"
fi

# Kernel config
if [ -f "$OUT/.config" ]; then
    cp "$OUT/.config" "$ART/kernel.config"
    echo "[+] .config"
fi

# System.map
if [ -f "$OUT/System.map" ]; then
    cp "$OUT/System.map" "$ART/"
    echo "[+] System.map"
fi

# Module symbol table
if [ -f "$OUT/Module.symvers" ]; then
    cp "$OUT/Module.symvers" "$ART/"
    echo "[+] Module.symvers"
fi

# Ambil semua module .ko yang terbentuk
find "$OUT" -type f -name "*.ko" -exec cp --parents {} "$ART/modules/" \; 2>/dev/null || true

echo ""
echo "[+] Daftar artifact:"
du -h "$ART"/* "$ART"/modules/* 2>/dev/null || true
