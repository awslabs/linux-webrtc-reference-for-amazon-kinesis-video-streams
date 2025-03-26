# FreeRTOS-WebRTC-Application

## Submodule Update
```
git submodule update --init --recursive
```
## Required Configuration
Before choosing an authentication method, configure these common settings:
   * Copy `examples/master/demo_config_template.h` and rename it to `examples/master/demo_config.h` and set the following:
   * Set `AWS_REGION` to your AWS region.
   * Set `AWS_KVS_CHANNEL_NAME` to your KVS signaling channel name.

## Authentication Methods
Choose ONE of the following authentication options:

### Option 1: Using Access Keys
   * Set `AWS_ACCESS_KEY_ID` to your access key.
   * Set `AWS_SECRET_ACCESS_KEY` to your secret access key.
   * Set `AWS_SESSION_TOKEN` to your session token (required only for temporary credentials).

### Option 2: Using IoT Role-alias
   * Set `AWS_CREDENTIALS_ENDPOINT` to your AWS Endpoint.
   * Set `AWS_IOT_THING_NAME` to your Thing Name associated with that Certificate.
   * Set `AWS_IOT_THING_ROLE_ALIAS` to your Role Alias.
   * Set `AWS_IOT_THING_CERT_PATH` to your IOT Core Certificate Path.
   * Set `AWS_IOT_THING_PRIVATE_KEY_PATH` to your IOT Core Private Key Path.

## TWCC support

Transport Wide Congestion Control (TWCC) is a mechanism in WebRTC designed to enhance the performance and reliability of real-time communication over the internet. TWCC addresses the challenges of network congestion by providing detailed feedback on the transport of packets across the network, enabling adaptive bitrate control and optimization of media streams in real-time. This feedback mechanism is crucial for maintaining high-quality audio and video communication, as it allows senders to adjust their transmission strategies based on comprehensive information about packet losses, delays, and jitter experienced across the entire transport path.

The importance of TWCC in WebRTC lies in its ability to ensure efficient use of available network bandwidth while minimizing the negative impacts of network congestion. By monitoring the delivery of packets across the network, TWCC helps identify bottlenecks and adjust the media transmission rates accordingly. This dynamic approach to congestion control is essential for preventing degradation in call quality, such as pixelation, stuttering, or drops in audio and video streams, especially in environments with fluctuating network conditions.

To learn more about TWCC, check [TWCC spec](https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01)

### Enabling TWCC support

TWCC is enabled by default in this application (via `ENABLE_TWCC_SUPPORT`) value set as `1` in `demo_config_template.h`. In order to disable it, set this value to `0`.

```c
#define ENABLE_TWCC_SUPPORT 1U
```

If not using the samples directly, following thing need to be done to set up Twcc:

1. Set the callback that will have the business logic to modify the bitrate based on packet loss information. The callback can be set using `PeerConnection_SetSenderBandwidthEstimationCallback()` inside `PeerConnection_Init()`:
```c
ret = PeerConnection_SetSenderBandwidthEstimationCallback(  pSession,
                                                            PeerConnection_SampleSenderBandwidthEstimationHandler,
                                                            &pSession->twccMetaData );
```
## Compile Commands
```
cmake -S . -B build
make -C build
```

**Note**: `BUILD_USRSCTP_LIBRARY` flag can be used to disable data channel and the build of `usrsctp` library. It can be used like: `cmake -S . -B build -DBUILD_USRSCTP_LIBRARY=OFF`

## Execute

```
./build/WebRTCLinuxApplicationMaster
```

## Output Sample
```
[2024/03/28 06:43:35:9749] N: lws_create_context: LWS: 4.3.3-v4.3.3, NET CLI H1 H2 WS ConMon IPv6-absent
[2024/03/28 06:43:35:9855] N: __lws_lc_tag:  ++ [wsi|0|pipe] (1)
[2024/03/28 06:43:35:9856] N: __lws_lc_tag:  ++ [vh|0|netlink] (1)
[2024/03/28 06:43:35:9878] N: __lws_lc_tag:  ++ [vh|1|default||-1] (2)
[2024/03/28 06:43:36:0003] E: Unable to load SSL Client certs file from  -- client ssl isn't going to work
```
