#!/usr/bin/env bash
#
# Quick minimal rootfs for G233 using system cross-compiler + prebuilt Busybox
#
# This is a lightweight alternative to the full Buildroot build.
# Usage: ./build/create-minimal-rootfs.sh
#
set -euo pipefail

TOP_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${TOP_DIR}/build"
ROOTFS_DIR="${BUILD_DIR}/rootfs"
ROOTFS_IMAGE="${BUILD_DIR}/rootfs.ext4"
CROSS_COMPILE="${CROSS_COMPILE:-riscv64-linux-gnu-}"
NPROC="$(nproc)"

# --------------- Helper ---------------
die() { echo "[!] $*"; exit 1; }

check_deps() {
    local missing=false
    for cmd in "${CROSS_COMPILE}gcc" wget tar dd mkfs.ext4; do
        if ! command -v "$cmd" &>/dev/null; then
            echo "[!] Missing: $cmd"
            missing=true
        fi
    done
    # For the ones that may not be a typical command
    if ! command -v mkfs.ext4 &>/dev/null; then
        echo "[!] Missing: mkfs.ext4 (install e2fsprogs)"
        missing=true
    fi
    if [ "$missing" = true ]; then
        apt-get install -y -qq e2fsprogs wget 2>/dev/null || true
    fi
    echo "[*] All dependencies satisfied."
}

build_busybox() {
    local BUSYBOX_VER="1.36.1"
    local BUSYBOX_DIR="${BUILD_DIR}/busybox-${BUSYBOX_VER}"
    local BUSYBOX_SRC="https://busybox.net/downloads/busybox-${BUSYBOX_VER}.tar.bz2"
    local SYSROOT="/usr/riscv64-linux-gnu"

    if [ ! -f "${BUSYBOX_DIR}/busybox" ]; then
        if [ ! -d "${BUSYBOX_DIR}" ]; then
            echo "[*] Downloading Busybox ${BUSYBOX_VER}..."
            wget -q "${BUSYBOX_SRC}" -O /tmp/busybox.tar.bz2
            tar xf /tmp/busybox.tar.bz2 -C "${BUILD_DIR}"
            rm /tmp/busybox.tar.bz2
        fi
        cd "${BUSYBOX_DIR}"

        echo "[*] Configuring Busybox for RISC-V..."
        make ARCH=riscv CROSS_COMPILE="${CROSS_COMPILE}" defconfig

        # Build static binary to avoid shared lib dependency
        sed -i 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
        # Set sysroot for the cross-compiler
        sed -i '/^EXTRA_CFLAGS/d' .config
        sed -i '/^CONFIG_EXTRA_CFLAGS/d' .config
        echo 'CONFIG_EXTRA_CFLAGS="--sysroot='"${SYSROOT}"'"' >> .config
        echo 'CONFIG_EXTRA_LDFLAGS="--sysroot='"${SYSROOT}"'"' >> .config

        echo "[*] Building Busybox..."
        make ARCH=riscv CROSS_COMPILE="${CROSS_COMPILE}" -j"${NPROC}"
    fi
}

create_rootfs() {
    echo "[*] Creating rootfs directory..."
    rm -rf "${ROOTFS_DIR}"
    mkdir -p "${ROOTFS_DIR}"/{bin,sbin,dev,etc,home,lib,lib64,mnt,opt,proc,root,run,sys,tmp,usr/{bin,sbin,lib},var/log}

    # Copy busybox
    local BUSYBOX_VER="1.36.1"
    local BUSYBOX_DIR="${BUILD_DIR}/busybox-${BUSYBOX_VER}"
    cp "${BUSYBOX_DIR}/busybox" "${ROOTFS_DIR}/bin/"

    # Install busybox applets
    cd "${ROOTFS_DIR}"
    for applet in sh ls cp mv rm cat echo ps mount umount mkdir rmdir chmod chown \
                  clear dmesg df du free grep kill killall ln login lsblk md5sum \
                  mkfifo mknod more mountpoint mv nc netstat nice nohup nproc \
                  nsenter od passwd paste patch pgrep pidof ping ping6 pipe_progress \
                  pkill pmap poweroff printf ps pstree pwd readlink realpath reboot \
                  reset resize rev rm rmdir sed seq setsid sha1sum sha256sum sha3sum \
                  sha512sum sleep sort split stat su sync sysctl tail tar tee test \
                  time timeout top touch tr true truncate tty umount uname uniq \
                  unlink uptime usleep uudecode uuencode vconfig vi watch wc wget \
                  which who whoami xargs xxd yes zcat; do
        ln -sf /bin/busybox "bin/${applet}" 2>/dev/null || true
    done
    ln -sf /bin/busybox "sbin/init" 2>/dev/null || true
    ln -sf /bin/busybox "sbin/poweroff" 2>/dev/null || true
    ln -sf /bin/busybox "sbin/reboot" 2>/dev/null || true
    cd "${TOP_DIR}"

    # Device nodes (minimal)
    echo "[*] Creating device nodes..."
    cd "${ROOTFS_DIR}"
    mknod dev/console c 5 1 2>/dev/null || true
    mknod dev/null c 1 3 2>/dev/null || true
    mknod dev/zero c 1 5 2>/dev/null || true
    mknod dev/tty c 5 0 2>/dev/null || true
    mknod dev/ttyS0 c 4 64 2>/dev/null || true
    mknod dev/random c 1 8 2>/dev/null || true
    mknod dev/urandom c 1 9 2>/dev/null || true
    cd "${TOP_DIR}"

    # /etc/inittab
    mkdir -p "${ROOTFS_DIR}/etc"
    cat > "${ROOTFS_DIR}/etc/inittab" << 'EOF'
::sysinit:/etc/init.d/rcS
ttyS0::respawn:-/bin/sh
::shutdown:/bin/sh -c "umount -a; reboot -f"
::ctrlaltdel:/sbin/reboot
EOF

    # /etc/init.d/rcS
    mkdir -p "${ROOTFS_DIR}/etc/init.d"
    cat > "${ROOTFS_DIR}/etc/init.d/rcS" << 'EOF'
#!/bin/sh
mount -t proc none /proc
mount -t sysfs none /sys
mount -t tmpfs none /run
mkdir -p /dev/pts /dev/shm
mount -t devpts devpts /dev/pts
#if mdev is available:
#echo /sbin/mdev > /proc/sys/kernel/hotplug 2>/dev/null
#mdev -s 2>/dev/null
EOF
    chmod +x "${ROOTFS_DIR}/etc/init.d/rcS"

    # /etc/fstab
    cat > "${ROOTFS_DIR}/etc/fstab" << 'EOF'
# <device>    <mount>    <type>    <options>          <dump> <pass>
proc          /proc      proc      defaults            0      0
sysfs         /sys       sysfs     defaults            0      0
tmpfs         /run       tmpfs     defaults            0      0
devpts        /dev/pts   devpts    defaults            0      0
EOF

    # /etc/hostname
    echo "g233" > "${ROOTFS_DIR}/etc/hostname"

    # /etc/passwd (minimal)
    cat > "${ROOTFS_DIR}/etc/passwd" << 'EOF'
root:x:0:0:root:/root:/bin/sh
EOF

    # /etc/group
    cat > "${ROOTFS_DIR}/etc/group" << 'EOF'
root:x:0:
EOF
}

create_image() {
    local SIZE_MB=64

    echo "[*] Creating ext4 rootfs image (${SIZE_MB}MB) with directory contents..."
    rm -f "${ROOTFS_IMAGE}"
    mkfs.ext4 -q -F -d "${ROOTFS_DIR}" "${ROOTFS_IMAGE}" "${SIZE_MB}M" 2>/dev/null
    e2fsck -p -f "${ROOTFS_IMAGE}" 2>/dev/null || true

    echo ""
    echo "[+] Minimal rootfs created!"
    ls -lh "${ROOTFS_IMAGE}"
}

# --------------- Main ---------------
check_deps
build_busybox
create_rootfs
create_image

echo "[*] Done! Boot with: ./build/boot-g233.sh"
