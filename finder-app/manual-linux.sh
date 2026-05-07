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
CROSS_COMPILE_VERSION=13.3.rel1

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}
if [ $? -ne 0 ]; then
    echo "Failed to create directory ${OUTDIR}"
    exit 1
fi

# download cross compiler
echo "Downloading cross compiler"
mkdir -p "$OUTDIR/toolchain"
cd "$OUTDIR/toolchain"
if [ ! -d "${OUTDIR}/toolchain/arm-gnu-toolchain-${CROSS_COMPILE_VERSION}-x86_64-aarch64-none-linux-gnu" ]; then
    wget https://developer.arm.com/-/media/Files/downloads/gnu/${CROSS_COMPILE_VERSION}/binrel/arm-gnu-toolchain-${CROSS_COMPILE_VERSION}-x86_64-aarch64-none-linux-gnu.tar.xz
    tar -xf arm-gnu-toolchain-${CROSS_COMPILE_VERSION}-x86_64-aarch64-none-linux-gnu.tar.xz
    export PATH=$PATH:${OUTDIR}/toolchain/arm-gnu-toolchain-${CROSS_COMPILE_VERSION}-x86_64-aarch64-none-linux-gnu/bin
fi

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    # clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # kernel build steps
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
fi

echo "Adding the Image in outdir"
if [ ! -e ${OUTDIR}/Image ]; then
    cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}
fi

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# necessary base directories
mkdir -p ${OUTDIR}/rootfs
cd ${OUTDIR}/rootfs
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
    git clone https://github.com/mirror/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # configure busybox
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
else
    cd busybox
fi

# make and install busybox
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -j4
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} CONFIG_PREFIX=${OUTDIR}/rootfs install

# Add library dependencies to rootfs
echo "Adding library dependencies"
copy_lib() {
    lib=$1
    source_path=$(find ${OUTDIR}/toolchain/arm-gnu-toolchain-${CROSS_COMPILE_VERSION}-x86_64-aarch64-none-linux-gnu/ -name ${lib})
    target_path="${OUTDIR}/rootfs/$2"
    echo "Copying ${source_path} to ${target_path}"
    cp ${source_path} ${target_path}
}

ld_lib_rootfs_path=$(${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter" | tr -d '[]' | cut -d ':' -f 2)
copy_lib $(basename ${ld_lib_rootfs_path}) $(dirname ${ld_lib_rootfs_path})

shared_libs=$(${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library" | grep -o "\[.*\]" | tr -d '[]')
for lib in ${shared_libs}
do
    copy_lib ${lib} "lib64"
done


# Make device nodes
echo "Making device nodes"
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/char0 c 5 0

# Clean and build the writer utility
echo "Building the writer utility"
cd ${FINDER_APP_DIR}
make clean
make CROSS_COMPILE=${CROSS_COMPILE}

# Copy the finder related scripts and executables to the /home directory on the target rootfs
echo "Copying finder app to the rootfs"
cp ${FINDER_APP_DIR}/finder.sh ${OUTDIR}/rootfs/home/
cp ${FINDER_APP_DIR}/finder-test.sh ${OUTDIR}/rootfs/home/
cp ${FINDER_APP_DIR}/autorun-qemu.sh ${OUTDIR}/rootfs/home/
cp ${FINDER_APP_DIR}/writer ${OUTDIR}/rootfs/home/
cp -r ${FINDER_APP_DIR}/../conf ${OUTDIR}/rootfs/home/

# Chown the root directory
echo "Chowning the root directory recursively"
sudo chown -R root:root ${OUTDIR}/rootfs

# Create initramfs.cpio.gz
echo "Creating initramfs"
cd ${OUTDIR}/rootfs
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
gzip -f ${OUTDIR}/initramfs.cpio
