As long as there is no public repo we add the yocto recipe to build the software here.

### init-yocto-build.sh
Will generate a folder ../FreeRTOS_WebRTC_App_yocto_build/ with a complete Yocto setup to build a qemu image, as done in CI.
Requirements see [here](https://docs.yoctoproject.org/5.0.6/brief-yoctoprojectqs/index.html#build-host-packages)

### start-yocto-build
Will start the build.
This require a proper demo_config.h in /examples/master

Like this section (modify acc. to your generated certs, settings):
```
#define AWS_REGION "eu-central-1"

#define AWS_KVS_CHANNEL_NAME "KVSWebRTCDevice_1739436405"
#define AWS_CREDENTIALS_ENDPOINT "c2cw693ei5usp5.credentials.iot.eu-central-1.amazonaws.com"
#define AWS_IOT_THING_NAME "KVSWebRTCDevice_1739436405"
#define AWS_IOT_THING_ROLE_ALIAS "KVSWebRTCRole_1739436405"
#define AWS_IOT_THING_CERT_PATH "/etc/certificate.pem"
#define AWS_IOT_THING_PRIVATE_KEY_PATH "/etc/private.key"
```
And tests/iot-credentials/destroy-iot-credentials.sh to generate valid certificates.

Once finished you can run qemu with:

### start-qemu.sh
Login as `root` with no password.

Start the ptest with
```bash
ptest-runner
```

Exit qemu with `ctrl-a q`
