#!/usr/bin/env bash
#
# Build Linux kernel for G233 target board
#
# Usage: ./build/build-linux.sh [linux-source-dir]
#
# If no argument is given, the script clones Linux v6.6 into build/linux/
#
set -euo pipefail

TOP_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${TOP_DIR}/build"
LINUX_DIR="${1:-${BUILD_DIR}/linux}"
LINUX_VER="v6.6"
CROSS_COMPILE="${CROSS_COMPILE:-riscv64-linux-gnu-}"
NPROC="$(nproc)"

# --------------- Dependency Checks ---------------
check_deps() {
    local missing=false
    local pkgs=()

    for cmd in bc bison flex make git; do
        if ! command -v "$cmd" &>/dev/null; then
            echo "[!] Missing command: $cmd"
            missing=true
            case "$cmd" in
                bc)    pkgs+=(bc) ;;
                bison) pkgs+=(bison) ;;
                flex)  pkgs+=(flex) ;;
                make)  pkgs+=(make) ;;
                git)   pkgs+=(git) ;;
            esac
        fi
    done

    # Check host gcc
    if ! command -v gcc &>/dev/null; then
        echo "[!] Missing: gcc (host compiler)"
        missing=true
        pkgs+=(gcc)
    fi

    # Check cross-compiler
    if ! command -v "${CROSS_COMPILE}gcc" &>/dev/null; then
        echo "[!] Missing cross-compiler: ${CROSS_COMPILE}gcc"
        echo "    Install it (e.g. apt install gcc-riscv64-linux-gnu) or set CROSS_PREFIX in Makefile.camp"
        missing=true
        pkgs+=(gcc-riscv64-linux-gnu)
    fi

    # Check header/lib packages (non-command dependencies)
    for pkg in libssl-dev libelf-dev; do
        if ! dpkg -s "$pkg" &>/dev/null 2>&1; then
            echo "[!] Missing package: $pkg"
            missing=true
            pkgs+=("$pkg")
        fi
    done

    if [ "$missing" = true ]; then
        if [ ${#pkgs[@]} -gt 0 ]; then
            echo ""
            echo "[*] Installing missing packages: ${pkgs[*]}"
            apt-get update -qq
            apt-get install -y -qq "${pkgs[@]}"
        fi
    fi

    echo "[*] All build dependencies satisfied."
}

# --------------- Main ---------------
check_deps

if [ ! -d "${LINUX_DIR}" ]; then
    echo "[*] Cloning Linux ${LINUX_VER} into ${LINUX_DIR}..."
    mkdir -p "$(dirname "${LINUX_DIR}")"
    git clone --depth=1 --branch "${LINUX_VER}" \
        https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git \
        "${LINUX_DIR}"
fi

cd "${LINUX_DIR}"

echo "[*] Cleaning previous build artifacts..."
make ARCH=riscv CROSS_COMPILE="${CROSS_COMPILE}" distclean >/dev/null 2>&1 || true

echo "[*] Generating default RISC-V defconfig..."
make ARCH=riscv CROSS_COMPILE="${CROSS_COMPILE}" defconfig

echo "[*] Applying G233 extra config..."
cat "${BUILD_DIR}/g233-kernel-extra.config" >> .config
make ARCH=riscv CROSS_COMPILE="${CROSS_COMPILE}" olddefconfig

echo "[*] Building kernel Image (using ${NPROC} parallel jobs)..."
make ARCH=riscv CROSS_COMPILE="${CROSS_COMPILE}" -j"${NPROC}" Image

echo ""
echo "[+] Kernel built successfully!"
echo "    Image: ${LINUX_DIR}/arch/riscv/boot/Image"
ls -lh "${LINUX_DIR}/arch/riscv/boot/Image"
