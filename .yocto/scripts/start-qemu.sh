SCRIPT_DIR=$(dirname "$(readlink -f "$0")")
echo SCRIPT_DIR: "${SCRIPT_DIR}"

# need to change bitbake layer includes when changing this
# this does not work with a "-" in path name - why?
# DEBUG_BUILD_DIR=../../../FreeRTOS_WebRTC-Application_yocto_build

DEBUG_BUILD_DIR=${SCRIPT_DIR}/../../../FreeRTOS_WebRTC_App_yocto_build
cd ${DEBUG_BUILD_DIR}

source poky/oe-init-build-env build

runqemu nographic