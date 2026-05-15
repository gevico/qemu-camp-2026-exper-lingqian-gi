#!/usr/bin/env bash
#
# One-click setup: build QEMU + Linux kernel + rootfs for G233
#
# Run this on a fresh clone of the repository.
# Usage: bash setup-g233.sh
#
set -euo pipefail

NPROC="$(nproc)"

echo "=========================================="
echo "  G233 Linux Target Full Setup"
echo "=========================================="
echo ""

# Step 1: Install system dependencies
echo "[1/4] Installing system build dependencies..."
apt-get update -qq
apt-get install -y -qq \
    bc bison flex cpio wget \
    libssl-dev libelf-dev \
    gcc-riscv64-linux-gnu libc6-dev-riscv64-cross \
    e2fsprogs

echo ""
echo "[2/4] Building QEMU with G233 machine..."
make -f Makefile.camp configure
make -f Makefile.camp build -j"${NPROC}"

echo ""
echo "[3/4] Building Linux kernel..."
bash build/build-linux.sh

echo ""
echo "[4/4] Building minimal rootfs..."
bash build/create-minimal-rootfs.sh

echo ""
echo "=========================================="
echo "  Setup complete!"
echo "=========================================="
echo ""
echo "Run the following to boot G233:"
echo ""
echo "    bash build/boot-g233.sh"
echo ""
