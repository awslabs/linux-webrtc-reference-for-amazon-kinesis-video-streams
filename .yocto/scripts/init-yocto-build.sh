#!/bin/bash
set -e

SCRIPT_DIR=$(dirname "$(readlink -f "$0")")
echo SCRIPT_DIR: "${SCRIPT_DIR}"

cd ${SCRIPT_DIR}

# need to change bitbake layer includes when changing this
# this does not work with a "-" in path name - why?
# DEBUG_BUILD_DIR=../../../FreeRTOS_WebRTC-Application_yocto_build

DEBUG_BUILD_DIR=../../../FreeRTOS_WebRTC_App_yocto_build

echo "Creating ${SCRIPT_DIR}/$DEBUG_BUILD_DIR"

mkdir -p $DEBUG_BUILD_DIR
cd $DEBUG_BUILD_DIR 

git clone git://git.yoctoproject.org/poky -b scarthgap-5.0.6 --depth 1

git clone https://github.com/aws4embeddedlinux/meta-aws.git -b scarthgap
cd meta-aws
git reset --hard 4948d231207662af4f1776c1543b1e06574a8e8a
cd -

git clone https://github.com/openembedded/meta-openembedded.git -b scarthgap
cd meta-openembedded
git reset --hard 4f11a12b2352bbdfafb6b7d956bf424af4992977
cd -

source poky/oe-init-build-env build

echo QEMU_USE_KVM = \"\" >> conf/local.conf

# set to the same as core-image-ptest
echo QB_MEM = \"-m 1024\" >> conf/local.conf

# use slirp networking instead of TAP interface (require root rights)
echo QEMU_USE_SLIRP = \"1\" >> conf/local.conf
echo TEST_RUNQEMUPARAMS += \"slirp\" >> conf/local.conf
echo TEST_SERVER_IP = \"127.0.0.1\" >> conf/local.conf
echo DISTRO_FEATURES += \"ptest\" >> conf/local.conf
echo DISTRO = \"poky\" >> conf/local.conf

#if there is a shared cache and downloads one level up from the repo
echo 'DL_DIR ?= "${TOPDIR}/../../downloads"' >> conf/local.conf
echo 'SSTATE_DIR ?= "${TOPDIR}/../../sstate-cache"' >> conf/local.conf

# this will specify what test should run when running testimage cmd - oeqa layer tests + ptests:
# Ping and SSH are not required, but do help in debugging. ptest will discover all ptest packages.
echo TEST_SUITES = \" ping ssh ptest\" >> conf/local.conf
echo IMAGE_INSTALL:append = \" ptest-runner ssh amazon-kvs-webrtc-sdk-c-ptest\" >> conf/local.conf

# this will allow - running testimage cmd: bitbake core-image-minimal -c testimage
echo IMAGE_CLASSES += \"testimage\" >> conf/local.conf

echo EXTRA_OECMAKE:append:pn-amazon-kvs-webrtc-sdk = \" -DIOT_CORE_ENABLE_CREDENTIALS=ON\" >> conf/local.conf

bitbake-layers add-layer ../meta-openembedded/meta-oe
bitbake-layers add-layer ../meta-openembedded/meta-python
bitbake-layers add-layer ../meta-openembedded/meta-multimedia
bitbake-layers add-layer ../meta-openembedded/meta-networking
bitbake-layers add-layer ../meta-aws
bitbake-layers add-layer ../../FreeRTOS-WebRTC-Application/.yocto

# debug
cat conf/local.conf

cd -