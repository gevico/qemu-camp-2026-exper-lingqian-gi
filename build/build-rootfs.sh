#!/usr/bin/env bash
#
# Build a minimal rootfs for G233 using Buildroot
#
# Usage: ./build/build-rootfs.sh
#
set -euo pipefail

TOP_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${TOP_DIR}/build"
BUILDROOT_DIR="${BUILD_DIR}/buildroot"
BUILDROOT_OUT="${BUILDROOT_DIR}/output"
ROOTFS_IMAGE="${BUILD_DIR}/rootfs.ext4"

BR_DEFCONFIG="qemu_riscv64_virt_defconfig"
BR_VERSION="2024.11"
NPROC="$(nproc)"

# --------------- Dependency Checks ---------------
check_deps() {
    local missing=false
    local pkgs=()

    for cmd in make git wget curl sed cpio; do
        if ! command -v "$cmd" &>/dev/null; then
            echo "[!] Missing command: $cmd"
            missing=true
            case "$cmd" in
                make) pkgs+=(make) ;;
                git)  pkgs+=(git) ;;
                wget) pkgs+=(wget) ;;
                curl) pkgs+=(curl) ;;
                sed)  pkgs+=(sed) ;;
                cpio) pkgs+=(cpio) ;;
            esac
        fi
    done

    if [ "$missing" = true ]; then
        echo ""
        echo "[*] Installing missing packages: ${pkgs[*]}"
        apt-get update -qq
        apt-get install -y -qq "${pkgs[@]}"
    fi

    echo "[*] All build dependencies satisfied."
}

# --------------- Main ---------------
check_deps

if [ ! -d "${BUILDROOT_DIR}" ]; then
    echo "[*] Cloning Buildroot ${BR_VERSION} into ${BUILDROOT_DIR}..."
    mkdir -p "$(dirname "${BUILDROOT_DIR}")"
    git clone --depth=1 --branch "${BR_VERSION}" \
        https://github.com/buildroot/buildroot.git \
        "${BUILDROOT_DIR}"
fi

export FORCE_UNSAFE_CONFIGURE=1

cd "${BUILDROOT_DIR}"

echo "[*] Configuring Buildroot for RISC-V 64 (${BR_DEFCONFIG})..."
make "${BR_DEFCONFIG}"

# Adjust config: ext4 rootfs + basic packages
cat >> .config << 'BRCFG'
BR2_TARGET_ROOTFS_EXT2=y
BR2_TARGET_ROOTFS_EXT2_SIZE="256M"
BR2_PACKAGE_BUSYBOX=y
BRCFG

make olddefconfig

echo "[*] Building rootfs (using ${NPROC} parallel jobs, this may take a while)..."
make -j"${NPROC}"

echo ""
echo "[+] Rootfs built!"
if [ -f "${BUILDROOT_OUT}/images/rootfs.ext2" ]; then
    cp "${BUILDROOT_OUT}/images/rootfs.ext2" "${ROOTFS_IMAGE}"
    # Fix up ext4 if e2fsprogs is available
    if command -v e2fsck &>/dev/null; then
        e2fsck -y "${ROOTFS_IMAGE}" 2>/dev/null || true
    fi
    echo "    Rootfs: ${ROOTFS_IMAGE}"
    ls -lh "${ROOTFS_IMAGE}"
else
    echo "    [!] rootfs.ext2 not found at ${BUILDROOT_OUT}/images/"
    ls -la "${BUILDROOT_OUT}/images/" 2>/dev/null || true
fi
