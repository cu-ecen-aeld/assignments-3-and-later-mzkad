#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi
if [ ! -d ${OUTDIR} ]
then
    mkdir -p ${OUTDIR}
    if [ ! $? -eq 0 ]
    then
        echo "Unable to create directory ${OUTDIR}. Exiting..."
        exit 1
    fi
fi

OUTDIR=$(realpath ${OUTDIR})

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
    cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image "$OUTDIR"
fi


echo "Adding the Image in outdir"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
ROOTFS_DIR="$OUTDIR/rootfs"
if [ -d "$ROOTFS_DIR" ]
then
	echo "Deleting rootfs directory at ${ROOTFS_DIR} and starting over"
    sudo rm  -rf ${ROOTFS_DIR}
fi

# TODO: Create necessary base directories
mkdir -p "$ROOTFS_DIR"
cd "$ROOTFS_DIR"
mkdir -p bin conf dev etc home lib lib64  proc sbin  sys tmp  usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

cd "$OUTDIR"

if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    ls -lt

    # TODO:  Configure busybox
    make distclean
    make defconfig
else
    cd busybox
fi

# TODO: Make and install busybox
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX="$ROOTFS_DIR" ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

echo "Library dependencies"
cd "$ROOTFS_DIR"
echo "${CROSS_COMPILE}readelf -a $ROOTFS_DIR/bin/busybox | grep program interpreter"
${CROSS_COMPILE}readelf -a "$ROOTFS_DIR/bin/busybox" | grep "program interpreter"
echo "${CROSS_COMPILE}readelf -a $ROOTFS_DIR/bin/busybox | grep Shared library"
${CROSS_COMPILE}readelf -a "$ROOTFS_DIR/bin/busybox" | grep "Shared library"
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)

ls -lt "$SYSROOT"

# TODO: Add library dependencies to rootfs
cd "$SYSROOT"
#the format of the program interpreter output: [Requesting program interpreter: /lib/ld-linux-aarch64.so.1]
${CROSS_COMPILE}readelf -a "$ROOTFS_DIR/bin/busybox" | grep "program interpreter" | awk '{print $NF}' | tr -d ']' | while read -r line; do
    interpreter="$SYSROOT/${line}"
    if [ -f "$interpreter" ]; then
        cp "$interpreter" "$ROOTFS_DIR/lib"
        echo "  Copied $interpreter"
    else
        echo "  WARNING: $interpreter not found"
    fi
done

#the format of the Shared library output: 0x0000001 (NEEDED) Shared library: [libm.so.6]
${CROSS_COMPILE}readelf -a "$ROOTFS_DIR/bin/busybox" | grep "Shared library" | awk -F'[][]' '{print $2}' | while read -r line; do
    shared_lib="$SYSROOT/lib64/${line}"
    if [ -f "$shared_lib" ]; then
        cp "$shared_lib" "$ROOTFS_DIR/lib64"
        echo "  Copied $shared_lib"
    else
        echo "  WARNING: $shared_lib not found"
    fi
done

# TODO: Make device nodes
cd "$ROOTFS_DIR"
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 600 dev/console c 5 1

# TODO: Clean and build the writer utility
cd "$FINDER_APP_DIR"
make clean
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cd "$FINDER_APP_DIR"

cp *.sh "$ROOTFS_DIR/home"
cp ../conf/*.txt "$ROOTFS_DIR/conf"
cp -rf conf "$ROOTFS_DIR/home"
cp writer "$ROOTFS_DIR/home"

# TODO: Chown the root directory
cd "$ROOTFS_DIR"
sudo chown -R root:root *

# TODO: Create initramfs.cpio.gz
cd "$ROOTFS_DIR"
find . | cpio -H newc -ov --owner root:root | gzip > ${OUTDIR}/initramfs.cpio.gz