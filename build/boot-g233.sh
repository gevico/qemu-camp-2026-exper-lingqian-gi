#!/usr/bin/env bash
#
# Boot G233 with a standard Linux kernel and rootfs
#
# Usage: ./build/boot-g233.sh [--kernel Image] [--rootfs rootfs.ext4]
#                            [--append "extra cmdline args"]
#
set -euo pipefail

TOP_DIR="$(cd "$(dirname "$0")/.." && pwd)"
QEMU="${TOP_DIR}/build/qemu-system-riscv64"

# Defaults
KERNEL="${TOP_DIR}/build/linux/arch/riscv/boot/Image"
ROOTFS="${TOP_DIR}/build/rootfs.ext4"
APPEND="${APPEND:-console=ttyS0 root=/dev/vda rw}"
SMP="${SMP:-2}"
MEM="${MEM:-2G}"

# Parse arguments
while [ $# -gt 0 ]; do
    case "$1" in
        --kernel) KERNEL="$2"; shift 2 ;;
        --rootfs) ROOTFS="$2"; shift 2 ;;
        --append) APPEND="$2"; shift 2 ;;
        --smp)    SMP="$2";    shift 2 ;;
        --mem)    MEM="$2";    shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

if [ ! -f "${QEMU}" ]; then
    echo "[!] QEMU not found at ${QEMU}"
    echo "    Build QEMU first: make -f Makefile.camp"
    exit 1
fi

if [ ! -f "${KERNEL}" ]; then
    echo "[!] Kernel Image not found at ${KERNEL}"
    echo "    Build kernel first: ./build/build-linux.sh"
    exit 1
fi

if [ ! -f "${ROOTFS}" ]; then
    echo "[!] Rootfs not found at ${ROOTFS}"
    echo "    Build rootfs first: ./build/build-rootfs.sh"
    exit 1
fi

echo "[*] Booting G233..."
echo "    QEMU:   ${QEMU}"
echo "    Kernel: ${KERNEL}"
echo "    Rootfs: ${ROOTFS}"
echo "    SMP:    ${SMP}"
echo "    MEM:    ${MEM}"
echo "    Cmd:    ${APPEND}"

"${QEMU}" \
    -M g233 \
    -smp "${SMP}" \
    -m "${MEM}" \
    -bios default \
    -kernel "${KERNEL}" \
    -append "${APPEND}" \
    -drive file="${ROOTFS}",format=raw,if=virtio \
    -nographic

echo "[*] G233 shut down."
