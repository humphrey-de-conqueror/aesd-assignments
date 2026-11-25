#!/bin/bash
# Build ARM64 Linux kernel, BusyBox rootfs and finder application.
# Based on the AESD assignment manual-linux.sh outline.

set -euo pipefail

OUTDIR=/tmp/aeld
KERNEL_REPO=https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_REPO=https://git.busybox.net/busybox
BUSYBOX_VERSION=1_33_1

FINDER_APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$FINDER_APP_DIR")"

ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -ge 1 ]; then
    OUTDIR="$(realpath -m "$1")"
    echo "Using passed directory ${OUTDIR} for output"
else
    echo "Using default directory ${OUTDIR} for output"
fi

mkdir -p "$OUTDIR"

# --------------------------------------------------
# 1. Build Linux kernel
# --------------------------------------------------

cd "$OUTDIR"

if [ ! -d "$OUTDIR/linux-stable" ]; then
    echo "Cloning Linux stable ${KERNEL_VERSION}"
    git clone "$KERNEL_REPO" \
        --depth 1 \
        --single-branch \
        --branch "$KERNEL_VERSION" \
        linux-stable
fi

cd "$OUTDIR/linux-stable"

if [ ! -f "arch/${ARCH}/boot/Image" ]; then
    echo "Configuring Linux kernel"
    make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" defconfig

    echo "Building Linux kernel"
    make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" -j"$(nproc)"
fi

echo "Copying kernel Image"
cp "arch/${ARCH}/boot/Image" "$OUTDIR/Image"

# --------------------------------------------------
# 2. Create root filesystem directories
# --------------------------------------------------

echo "Creating root filesystem"

sudo rm -rf "$OUTDIR/rootfs"

ROOTFS="$OUTDIR/rootfs"

mkdir -p \
    "$ROOTFS/bin" \
    "$ROOTFS/dev" \
    "$ROOTFS/etc" \
    "$ROOTFS/home" \
    "$ROOTFS/lib" \
    "$ROOTFS/proc" \
    "$ROOTFS/sbin" \
    "$ROOTFS/sys" \
    "$ROOTFS/tmp" \
    "$ROOTFS/usr/bin" \
    "$ROOTFS/usr/lib" \
    "$ROOTFS/usr/sbin"

# --------------------------------------------------
# 3. Build and install BusyBox
# --------------------------------------------------

cd "$OUTDIR"

if [ ! -d "$OUTDIR/busybox" ]; then
    echo "Cloning BusyBox ${BUSYBOX_VERSION}"
    git clone "$BUSYBOX_REPO" busybox
fi

cd "$OUTDIR/busybox"

git checkout "$BUSYBOX_VERSION"

echo "Configuring BusyBox"
make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" defconfig

# Disable tc because BusyBox 1.33.1 has a compatibility
# issue with newer host Linux headers.
sed -i 's/^CONFIG_TC=y/# CONFIG_TC is not set/' .config

echo "Building BusyBox"
make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" -j"$(nproc)"

echo "Installing BusyBox into rootfs"
make ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" \
    CONFIG_PREFIX="$ROOTFS" install

# --------------------------------------------------
# 4. Install ARM64 shared libraries
# --------------------------------------------------

echo "Installing ARM64 shared libraries"

SYSROOT="$(${CROSS_COMPILE}gcc -print-sysroot)"

sudo cp -L "$SYSROOT/lib/ld-linux-aarch64.so.1" "$ROOTFS/lib/"
sudo cp -L "$SYSROOT/lib64/libc.so.6" "$ROOTFS/lib/"
sudo cp -L "$SYSROOT/lib64/libm.so.6" "$ROOTFS/lib/"
sudo cp -L "$SYSROOT/lib64/libresolv.so.2" "$ROOTFS/lib/"

# The ARM64 dynamic loader searches /lib64 and /usr/lib64.
# Provide /lib64 as a link to /lib.
sudo ln -s lib "$ROOTFS/lib64"

# --------------------------------------------------
# 5. Create device nodes
# --------------------------------------------------

echo "Creating device nodes"

sudo mknod -m 600 "$ROOTFS/dev/console" c 5 1
sudo mknod -m 666 "$ROOTFS/dev/null" c 1 3

# --------------------------------------------------
# 6. Build writer application
# --------------------------------------------------

echo "Building ARM64 writer application"

cd "$FINDER_APP_DIR"

make clean
make CROSS_COMPILE="$CROSS_COMPILE"

# --------------------------------------------------
# 7. Copy finder application and configuration
# --------------------------------------------------

echo "Installing finder application"

sudo cp writer finder.sh finder-test.sh writer.sh "$ROOTFS/home/"
sudo cp autorun-qemu.sh "$ROOTFS/home/"

sudo mkdir -p "$ROOTFS/home/conf"

sudo cp "$PROJECT_DIR/conf/username.txt" "$ROOTFS/home/conf/"
sudo cp "$PROJECT_DIR/conf/assignment.txt" "$ROOTFS/home/conf/"

sudo chmod +x "$ROOTFS/home/"*.sh

# --------------------------------------------------
# 8. Set root filesystem ownership
# --------------------------------------------------

echo "Setting root filesystem ownership"

sudo chown -R root:root "$ROOTFS"

# --------------------------------------------------
# 9. Generate initramfs
# --------------------------------------------------

echo "Generating initramfs"

cd "$ROOTFS"

sudo sh -c \
    'find . | cpio -H newc -o --owner=root:root | gzip -9 > "'"$OUTDIR"'/initramfs.cpio.gz"'

echo "Build completed successfully"
echo "Kernel:    $OUTDIR/Image"
echo "Initramfs: $OUTDIR/initramfs.cpio.gz"
