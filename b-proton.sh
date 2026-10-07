#!/bin/bash
set -e

# ==========================================
# INISIALISASI & PATH
# ==========================================
export KERNEL_ROOT=$GITHUB_WORKSPACE
export TOOLCHAIN_DIR=$KERNEL_ROOT/toolchain_download
export PROTON_DIR=$TOOLCHAIN_DIR/proton-clang

export OUT_DIR=$KERNEL_ROOT/out-proton
export ART_DIR=$GITHUB_WORKSPACE/output_artifacts_proton

export ARCH=arm64
export SUBARCH=arm64
export CC=clang

export CROSS_COMPILE="$PROTON_DIR/bin/aarch64-linux-gnu-"
export CROSS_COMPILE_ARM32="$PROTON_DIR/bin/arm-linux-gnueabi-"

export BSP_BUILD_FAMILY=sharkl3
export BSP_BUILD_ANDROID_OS=y
export DEFCONFIG="a3core_eur_open_defconfig"

# ==========================================
# TAHAP 1: KSU Integration
# ==========================================
echo "[+] Mengintegrasikan KernelSU..."
cd "$KERNEL_ROOT"

if [ -d "$KERNEL_ROOT/KernelSU" ]; then
    echo "[+] KernelSU terdeteksi, menggunakan source dari repository kernel."
else
    KSU_REPO="https://github.com/tiann/KernelSU.git"
    KSU_REF="0e4dafc"

    git clone "$KSU_REPO" "$KERNEL_ROOT/KernelSU"

    cd "$KERNEL_ROOT/KernelSU"
    git fetch --all --tags --force
    git checkout --force "$KSU_REF"

    echo "[+] KernelSU pinned:"
    echo "    Version : v3.2.4"
    echo "    Manager : 32457"
    echo "    Commit  : $(git rev-parse HEAD)"

    cd "$KERNEL_ROOT"
fi

# ==========================================
# TAHAP 2: Extract Kernel Component
# ==========================================
echo "[+] Mengekstrak komponen sound/soc/sprd..."

mkdir -p "$KERNEL_ROOT/output"

if [ -d "$KERNEL_ROOT/sound/soc/sprd" ]; then
    cp -r "$KERNEL_ROOT/sound/soc/sprd" "$KERNEL_ROOT/output/"
    cd "$KERNEL_ROOT/output"
    zip -r sprd.zip sprd
    cd "$KERNEL_ROOT"
else
    echo "[!] Direktori sound/soc/sprd tidak ditemukan, melewati tahap ini."
fi

# ==========================================
# TAHAP 3: DOWNLOAD PROTON CLANG
# ==========================================
mkdir -p "$TOOLCHAIN_DIR"

if [ ! -x "$PROTON_DIR/bin/clang" ]; then
    echo "[+] Proton Clang belum tersedia."
    echo "[+] Downloading Proton Clang..."

    rm -rf "$PROTON_DIR"

    git clone \
        --depth=1 \
        https://github.com/kdrag0n/proton-clang.git \
        "$PROTON_DIR"
else
    echo "[=] Proton Clang ditemukan dari cache."
    echo "[=] Menggunakan toolchain yang tersedia."
fi

# ==========================================
# TAHAP 4: VERIFY TOOLCHAIN
# ==========================================
echo ""
echo "===== Proton Clang ====="

export PATH="$PROTON_DIR/bin:$PATH"

echo "clang:"
clang --version | head -3

echo ""
echo "ld.lld:"
ld.lld --version | head -1

echo ""
echo "===== Toolchain paths ====="
which clang
which ld.lld
which llvm-ar
which llvm-nm

# ==========================================
# TAHAP 5: CONFIGURATION
# ==========================================
echo ""
echo "[+] Melakukan konfigurasi kernel dengan $DEFCONFIG..."

make -C "$KERNEL_ROOT" \
    O="$OUT_DIR" \
    ARCH="$ARCH" \
    CC="$CC" \
    CROSS_COMPILE="$CROSS_COMPILE" \
    CROSS_COMPILE_ARM32="$CROSS_COMPILE_ARM32" \
    LD=ld.lld \
    AR=llvm-ar \
    NM=llvm-nm \
    "$DEFCONFIG"

# ==========================================
# TAHAP 6: BUILD INFORMATION
# ==========================================
export KBUILD_BUILD_USER="Xsanzz"
export KBUILD_BUILD_HOST="A3C-stable"
export KBUILD_BUILD_TIMESTAMP="$(date '+%a %b %d %T WIB %Y')"

# ==========================================
# TAHAP 7: IGNORE YAML
# ==========================================
sed -i '/yamltree.o/d' scripts/dtc/Makefile
sed -i '/dt_to_yaml/d' scripts/dtc/dtc.c

# ==========================================
# TAHAP 8: BUILD KERNEL IMAGE
# ==========================================
echo ""
echo "[+] Memulai kompilasi Kernel Image dengan Proton Clang..."

make -C "$KERNEL_ROOT" \
    O="$OUT_DIR" \
    -j"$(nproc)" \
    ARCH="$ARCH" \
    CC="$CC" \
    CROSS_COMPILE="$CROSS_COMPILE" \
    CROSS_COMPILE_ARM32="$CROSS_COMPILE_ARM32" \
    LD=ld.lld \
    AR=llvm-ar \
    NM=llvm-nm \
    Image

# ==========================================
# TAHAP 9: BUILD MODULES
# ==========================================
echo ""
echo "[+] Memulai kompilasi kernel modules (.ko)..."

make -C "$KERNEL_ROOT" \
    O="$OUT_DIR" \
    -j"$(nproc)" \
    ARCH="$ARCH" \
    CC="$CC" \
    CROSS_COMPILE="$CROSS_COMPILE" \
    CROSS_COMPILE_ARM32="$CROSS_COMPILE_ARM32" \
    LD=ld.lld \
    AR=llvm-ar \
    NM=llvm-nm \
    modules

echo ""
echo "[+] Jumlah .ko yang dihasilkan:"
find "$OUT_DIR" -type f -name "*.ko" | wc -l

# ==========================================
# TAHAP 10: ARTIFACTS
# ==========================================
echo ""
echo "[+] Mengumpulkan hasil eksport..."

mkdir -p "$ART_DIR"

# Kernel Image
if [ -f "$OUT_DIR/arch/arm64/boot/Image" ]; then
    cp "$OUT_DIR/arch/arm64/boot/Image" "$ART_DIR/"
    echo "[+] Image"
fi

# Kernel config
if [ -f "$OUT_DIR/.config" ]; then
    cp "$OUT_DIR/.config" "$ART_DIR/kernel.config"
    echo "[+] .config"
fi

# System.map
if [ -f "$OUT_DIR/System.map" ]; then
    cp "$OUT_DIR/System.map" "$ART_DIR/"
    echo "[+] System.map"
fi

# Module symbol table
if [ -f "$OUT_DIR/Module.symvers" ]; then
    cp "$OUT_DIR/Module.symvers" "$ART_DIR/"
    echo "[+] Module.symvers"
fi

# Modules
MODULE_DIR="$ART_DIR/modules"
mkdir -p "$MODULE_DIR"

find "$OUT_DIR" -type f -name "*.ko" | while read -r ko; do
    rel="${ko#$OUT_DIR/}"

    mkdir -p "$MODULE_DIR/$(dirname "$rel")"
    cp -v "$ko" "$MODULE_DIR/$rel"
done

# ==========================================
# FINAL REPORT
# ==========================================
echo ""
echo "===== Proton Clang Build selesai ====="

echo ""
echo "[+] Daftar module:"
find "$MODULE_DIR" -type f -name "*.ko" -exec du -h {} \;

echo ""
echo "[+] artifact:"
find "$ART_DIR" -type f -exec du -h {} \;

echo ""
echo "[+] Toolchain:"
echo "    Proton Clang : $PROTON_DIR"
echo "    Output       : $OUT_DIR"
echo "    Artifact     : $ART_DIR"
