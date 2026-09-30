#!/bin/sh
set -eu

GKI_ROOT=$(pwd)

# KernelSU pinned revision
# Version    : v3.2.4
# Manager    : 32457
KSU_REPO="https://github.com/tiann/KernelSU.git"
KSU_REF="0e4dafc"
KSU_VERSION="v3.2.4"
KSU_VERSION_CODE="32457"

display_usage() {
    echo "Usage: $0 [--cleanup | <commit-or-tag>]"
    echo "  --cleanup:              Cleans up previous modifications made by the script."
    echo "  <commit-or-tag>:        Sets up KernelSU to specified tag or commit."
    echo "  -h, --help:             Displays this usage information."
    echo "  (no args):              Uses KernelSU $KSU_VERSION (versionCode $KSU_VERSION_CODE)."
}

initialize_variables() {
    if test -d "$GKI_ROOT/common/drivers"; then
        DRIVER_DIR="$GKI_ROOT/common/drivers"
    elif test -d "$GKI_ROOT/drivers"; then
        DRIVER_DIR="$GKI_ROOT/drivers"
    else
        echo '[ERROR] "drivers/" directory not found.'
        exit 127
    fi

    DRIVER_MAKEFILE="$DRIVER_DIR/Makefile"
    DRIVER_KCONFIG="$DRIVER_DIR/Kconfig"
}

perform_cleanup() {
    echo "[+] Cleaning up..."

    [ -L "$DRIVER_DIR/kernelsu" ] &&
        rm "$DRIVER_DIR/kernelsu" &&
        echo "[-] Symlink removed."

    grep -q "kernelsu" "$DRIVER_MAKEFILE" &&
        sed -i '/kernelsu/d' "$DRIVER_MAKEFILE" &&
        echo "[-] Makefile reverted."

    grep -q "drivers/kernelsu/Kconfig" "$DRIVER_KCONFIG" &&
        sed -i '/drivers\/kernelsu\/Kconfig/d' "$DRIVER_KCONFIG" &&
        echo "[-] Kconfig reverted."

    if [ -d "$GKI_ROOT/KernelSU" ]; then
        rm -rf "$GKI_ROOT/KernelSU"
        echo "[-] KernelSU directory deleted."
    fi
}

setup_kernelsu() {
    echo "[+] Setting up KernelSU..."

    if [ ! -d "$GKI_ROOT/KernelSU/.git" ]; then
        rm -rf "$GKI_ROOT/KernelSU"
        git clone "$KSU_REPO" "$GKI_ROOT/KernelSU"
        echo "[+] Repository cloned."
    fi

    cd "$GKI_ROOT/KernelSU"

    git stash || true

    if [ -z "${1-}" ]; then
        REF="$KSU_REF"
        echo "[-] Using pinned KernelSU $KSU_VERSION"
        echo "[-] Manager versionCode: $KSU_VERSION_CODE"
        echo "[-] Commit: $KSU_REF"
    else
        REF="$1"
        echo "[-] Using specified KernelSU ref: $REF"
    fi

    git fetch --all --tags --force
    git checkout --force "$REF"

    echo "[-] Checked out: $(git rev-parse HEAD)"

    cd "$DRIVER_DIR"

    ln -sf \
        "$(realpath --relative-to="$DRIVER_DIR" "$GKI_ROOT/KernelSU/kernel")" \
        "kernelsu"

    echo "[+] Symlink created."

    if ! grep -q "obj-\$(CONFIG_KSU) += kernelsu/" "$DRIVER_MAKEFILE"; then
        printf "\nobj-\$(CONFIG_KSU) += kernelsu/\n" >> "$DRIVER_MAKEFILE"
        echo "[+] Modified Makefile."
    fi

    if ! grep -q 'source "drivers/kernelsu/Kconfig"' "$DRIVER_KCONFIG"; then
        sed -i '/endmenu/i\source "drivers/kernelsu/Kconfig"' "$DRIVER_KCONFIG"
        echo "[+] Modified Kconfig."
    fi

    echo "[+] KernelSU setup complete."
}

if [ "$#" -eq 0 ]; then
    initialize_variables
    setup_kernelsu

elif [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    display_usage

elif [ "$1" = "--cleanup" ]; then
    initialize_variables
    perform_cleanup

else
    initialize_variables
    setup_kernelsu "$@"
fi
