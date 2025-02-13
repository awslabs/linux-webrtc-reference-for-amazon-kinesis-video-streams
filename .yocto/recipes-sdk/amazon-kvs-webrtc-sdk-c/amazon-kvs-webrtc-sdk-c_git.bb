SUMMARY = "Amazon Kinesis Video Streams WebRTC C"
DESCRIPTION = "Pure C WebRTC Client for Amazon Kinesis Video Streams - with minimal dependencies"
HOMEPAGE = "https://github.com/thomas-roos/FreeRTOS-WebRTC-Application"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=86d3f3a95c324c9479bd8986968f4327"

DEPENDS += "\
  libwebsockets \
  "

# default is stripped, we wanna do this by yocto
EXTRA_OECMAKE:append = " -DCMAKE_BUILD_TYPE=RelWithDebInfo"

# set log message debug level
EXTRA_OECMAKE:append = " -DLIBRARY_LOG_LEVEL=LOG_VERBOSE"

###
# Use this for development to specify a local folder as source dir (cloned repo)
inherit externalsrc
EXTERNALSRC = "${THISDIR}/../../.."

# this will force recipe to always rebuild
SSTATE_SKIP_CREATION = '1'
###
SRC_URI = "\
    file://run-ptest \
"

# BRANCH = "2025-01-09_main_runtime-tests"
# SRC_URI = "\
#     gitsm://github.com/thomas-roos/FreeRTOS-WebRTC-Application.git;protocol=https;branch=${BRANCH} \
#     file://run-ptest \
# "

# SRCREV = "316d0479e2fb894c3762a332afa56085695b9cde"

S = "${WORKDIR}/git"

PACKAGECONFIG:append:x86-64 = " ${@bb.utils.contains('PTEST_ENABLED', '1', 'sanitize', '', d)}"

PACKAGECONFIG[sanitize] = ",, gcc-sanitizers"

RDEPENDS:${PN} += "ca-certificates"

RDEPENDS:${PN}-ptest += "\
    amazon-kvs-webrtc-sdk \
    coreutils \
    util-linux \
"

inherit cmake ptest pkgconfig

do_install() {
  install -d ${D}${bindir}
  install -m 0755 ${B}/WebRTCLinuxApplicationMaster ${D}${bindir}

  install -d ${D}${bindir}/examples/app_media_source/samples/h264SampleFrames
  cp -r ${S}/examples/app_media_source/samples/h264SampleFrames/* ${D}${bindir}/examples/app_media_source/samples/h264SampleFrames/

  install -d ${D}${bindir}/examples/app_media_source/samples/opusSampleFrames
  cp -r ${S}/examples/app_media_source/samples/opusSampleFrames/* ${D}${bindir}/examples/app_media_source/samples/opusSampleFrames/

  install -d ${D}${sysconfdir}
  install -m 0664 ${S}/cert/cert.pem ${D}${sysconfdir}/
  install -m 0664 ${S}/certificate.pem ${D}${sysconfdir}/
  install -m 0664 ${S}/private.key ${D}${sysconfdir}/
}

do_install_ptest:append() {
  install -d ${D}${sysconfdir}
  install ${S}/tests/iot-credentials/THING_NAME ${D}${sysconfdir}/
  install ${S}/tests/iot-credentials/ROLE_ALIAS ${D}${sysconfdir}/
  install ${S}/tests/iot-credentials/CREDENTIALS_ENDPOINT ${D}${sysconfdir}/
  install ${S}/tests/iot-credentials/AWS_REGION ${D}${sysconfdir}/
}

OECMAKE_CXX_FLAGS += "${@bb.utils.contains('PACKAGECONFIG', 'sanitize', '-fsanitize=address,undefined -fno-omit-frame-pointer', '', d)}"
