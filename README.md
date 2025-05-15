# linux-webrtc-reference-for-amazon-kinesis-video-streams

> [!IMPORTANT]
> This repository is currently in development and not recommended for production use.

## Submodule Update
```
git submodule update --init --recursive
```

## Setup
### Requirements
The following packages are required to run this demo:
1. OpenSSL
    ```sh
    sudo apt install -y openssl libssl-dev
    ```
1. pkg-config
    ```sh
    sudo apt install -y pkg-config
    ```

### Required Configuration
Before choosing an authentication method, configure these common settings:
   * Copy `examples/demo_config/demo_config_template.h` and rename it to `examples/demo_config/demo_config.h` and set the following:
   * Set `AWS_REGION` to your AWS region.
   * Set `AWS_KVS_CHANNEL_NAME` to your KVS signaling channel name.

### Authentication Methods
Choose ONE of the following authentication options:

#### Option 1: Using Access Keys
   * Set `AWS_ACCESS_KEY_ID` to your access key.
   * Set `AWS_SECRET_ACCESS_KEY` to your secret access key.
   * Set `AWS_SESSION_TOKEN` to your session token (required only for temporary credentials).

#### Option 2: Using IoT Role-alias
   * Set `AWS_CREDENTIALS_ENDPOINT` to your AWS Endpoint.
   * Set `AWS_IOT_THING_NAME` to your Thing Name associated with that Certificate.
   * Set `AWS_IOT_THING_ROLE_ALIAS` to your Role Alias.
   * Set `AWS_IOT_THING_CERT_PATH` to your IOT Core Certificate Path.
   * Set `AWS_IOT_THING_PRIVATE_KEY_PATH` to your IOT Core Private Key Path.

## Compile Commands
```
cmake -S . -B build
make -C build
```

## Execute

Master Example
```
./build/WebRTCLinuxApplicationMaster
```

Gstreamer Master Example (Refer to the [Gstreamer Master Demo](#gstreamer-master-demo) section for more details.)
```
./build/WebRTCLinuxApplicationGstMaster
```

### Output Sample
```
[2024/03/28 06:43:35:9749] N: lws_create_context: LWS: 4.3.3-v4.3.3, NET CLI H1 H2 WS ConMon IPv6-absent
[2024/03/28 06:43:35:9855] N: __lws_lc_tag:  ++ [wsi|0|pipe] (1)
[2024/03/28 06:43:35:9856] N: __lws_lc_tag:  ++ [vh|0|netlink] (1)
[2024/03/28 06:43:35:9878] N: __lws_lc_tag:  ++ [vh|1|default||-1] (2)
[2024/03/28 06:43:36:0003] E: Unable to load SSL Client certs file from  -- client ssl isn't going to work
```

## Feature Options
1. [Gstreamer Master Demo](#gstreamer-master-demo)
1. [Data Channel Support](#data-channel-support)
1. [TWCC Support](#twcc-support)
1. [Join Storage Session](#join-storage-session-support)
1. [Enabling Metrics Logging](#enabling-metrics-logging)

### Gstreamer Master Demo

The GStreamer master demo provides a reference implementation for using GStreamer with Amazon Kinesis Video Streams WebRTC. This demo shows how to capture and stream audio/video using GStreamer's powerful media processing capabilities.

GStreamer is an open-source multimedia framework that allows you to create a variety of media-handling components. The demo uses GStreamer pipelines to:
- Capture video from a webcam or video test source
- Capture audio from a microphone or audio test source  
- Encode the media streams using WebRTC-compatible codecs (H.264 for video, Opus for audio)

#### Requitements
Install the following packages to auto build GStreamer application: libgstreamer1.0-dev, libgstreamer-plugins-base1.0-dev, libgstreamer-plugins-bad1.0-dev

On Ubuntu/Debian:
```
sudo apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev
```

On Fedora:
```
sudo dnf install gstreamer1-devel gstreamer1-plugins-base-devel gstreamer1-plugins-bad-free-devel
```

### Data Channel Support

WebRTC Data Channel is a bidirectional peer-to-peer communication channel for arbitrary application data. It operates over SCTP (Stream Control Transmission Protocol) and provides both reliable and unreliable data delivery modes.

#### Enabling Data Channel Support

Data channel support is enabled by default in this application through the `BUILD_USRSCTP_LIBRARY` flag, which is set to `ON` in [CMakeLists.txt](./CMakeLists.txt). To disable data channel support, set this flag to `OFF` using the cmake command below.

```
cmake -S . -B build -DBUILD_USRSCTP_LIBRARY=OFF
```

### TWCC support

Transport Wide Congestion Control (TWCC) is a mechanism in WebRTC designed to enhance the performance and reliability of real-time communication over the internet. TWCC addresses the challenges of network congestion by providing detailed feedback on the transport of packets across the network, enabling adaptive bitrate control and optimization of media streams in real-time. This feedback mechanism is crucial for maintaining high-quality audio and video communication, as it allows senders to adjust their transmission strategies based on comprehensive information about packet losses, delays, and jitter experienced across the entire transport path.

The importance of TWCC in WebRTC lies in its ability to ensure efficient use of available network bandwidth while minimizing the negative impacts of network congestion. By monitoring the delivery of packets across the network, TWCC helps identify bottlenecks and adjust the media transmission rates accordingly. This dynamic approach to congestion control is essential for preventing degradation in call quality, such as pixelation, stuttering, or drops in audio and video streams, especially in environments with fluctuating network conditions.

To learn more about TWCC, check [TWCC spec](https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01)

#### Enabling TWCC support

TWCC is enabled by default in this application (via `ENABLE_TWCC_SUPPORT`) value set as `1` in `demo_config_template.h`. In order to disable it, set this value to `0`.

```c
#define ENABLE_TWCC_SUPPORT 1U
```

If not using the samples directly, following thing need to be done to set up Twcc:

1. Set the callback that will have the business logic to modify the bitrate based on packet loss information. The callback can be set using `PeerConnection_SetSenderBandwidthEstimationCallback()` inside `PeerConnection_Init()`:
```c
ret = PeerConnection_SetSenderBandwidthEstimationCallback( pSession,
                                                           SampleSenderBandwidthEstimationHandler,
                                                           &pSession->twccMetaData );
```

### Join Storage Session Support

> [!WARNING]  
> The "Join Storage Session" feature is currently in a beta state and may exhibit stability issues. Our team is actively working on improvements and fixes. Users may experience:
> - Intermittent connection drops
> - Missing media playback on cloud

Join Storage Session enables video producing devices to join or create WebRTC sessions for real-time media ingestion through Amazon Kinesis Video Streams. For Master configurations, this allows devices to ingest both audio and video media while maintaining synchronized playback capabilities.

In our implementation (Master participant only):
1. First connect to Kinesis Video Streams with WebRTC Signaling.
2. It calls the `JoinStorageSession` API to initiate a storage session WebRTC connection.
3. Once WebRTC connection is established, media is ingested to the configured Kinesis video stream.

#### Media Requirements
- **Video Track**: H.264 codec required.
- **Audio Track**: Opus codec required.
- Both audio and video tracks are mandatory for WebRTC ingestion.

#### Enabling Join Storage Session support

Join Storage Session is disabled by default in this application (via `JOIN_STORAGE_SESSION`) value set as `0` in `demo_config_template.h`. In order to enable it, set this value to `1`.
```c
#define JOIN_STORAGE_SESSION 0
```

#### Prerequisites for enabling Join Storage Session

Before using Join Storage Session, Set up Signaling Channel with Video Stream:
   - Create a Kinesis Video Streams signaling channel
   - Create a Kinesis Video Streams video stream
   - Connect the channel to the video stream
   - Ensure proper IAM permissions are configured

For detailed setup instructions, refer to: https://docs.aws.amazon.com/kinesisvideostreams-webrtc-dg/latest/devguide/webrtc-ingestion.html

### Enabling Metrics Logging
METRIC_PRINT_ENABLED flag enables detailed metrics logging for WebRTC setup. It logs the following time for each connection:
   - Duration to describe Signaling Channel
   - Duration to get Signaling Endpoints
   - Duration to get Ice Server List
   - Duration to connect Websocket Server
   - Duration to get Authentication Temporary Credentials
   - Duration to gather ICE Host Candidate
   - Duration to gather ICE Srflx Candidate
   - Duration to gather ICE Relay Candidate
   - Duration to join Storage Session
   - Duration to find Peer-To-Peer Connection
   - Duration to DTLS Handshaking Completion
   - Duration to sending First Frame
**Note**: `METRIC_PRINT_ENABLED` flag can be used to enable metrics logging. It can be used like: `cmake -S . -B build -DMETRIC_PRINT_ENABLED=ON`. The flag is disabled by default.

# Security

See [CONTRIBUTING](CONTRIBUTING.md#security-issue-notifications) for more information.

# License

This project is licensed under the Apache-2.0 License.
