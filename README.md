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

## compile commands

```bash
cmake -S . -B build
make -C build
```

**Note**: `BUILD_USRSCTP_LIBRARY` flag can be used to disable data channel and the build of `usrsctp` library. It can be used like: `cmake -S . -B build -DBUILD_USRSCTP_LIBRARY=OFF`

## execute

```bash
./build/WebRTCLinuxApplicationMaster
```

## output sample

```
[2024/03/28 06:43:35:9749] N: lws_create_context: LWS: 4.3.3-v4.3.3, NET CLI H1 H2 WS ConMon IPv6-absent
[2024/03/28 06:43:35:9855] N: __lws_lc_tag:  ++ [wsi|0|pipe] (1)
[2024/03/28 06:43:35:9856] N: __lws_lc_tag:  ++ [vh|0|netlink] (1)
[2024/03/28 06:43:35:9878] N: __lws_lc_tag:  ++ [vh|1|default||-1] (2)
[2024/03/28 06:43:36:0003] E: Unable to load SSL Client certs file from  -- client ssl isn't going to work
```

## buildtestcontainer

Development and testing can be done inside a container.
This avoids issues with your local Linux installation.

```bash
podman build misc/buildtestcontainer -t buildtestcontainer:latest
podman run -it -v $PWD/..:/work --replace --name buildtestcontainer buildtestcontainer:latest
```

E.g. run a build

```bash
cd /work/FreeRTOS-WebRTC-Application
rm -rf build/ && cmake -S . -B build -DCMAKE_C_FLAGS='-DLIBRARY_LOG_LEVEL=LOG_VERBOSE' && make -C build -j $(nproc)
```

E.g. run a test

```bash
cd /work/FreeRTOS-WebRTC-Application/
./build/WebRTCLinuxApplicationMaster
```

## Yocto test qemu image

See [yocto setup guide](.yocto/README.md).

## Generate iot certs

Run `tests/iot-credentials/create-iot-credentials.sh` to generate valid certificates and `tests/iot-credentials/destroy-iot-credentials.sh` to destroy them.

## Code Style & Pre-commit Hooks

This project uses pre-commit hooks to maintain code quality and consistency.

### Prerequisites

#### Install uncrustify:

```bash
Ubuntu/Debian:
sudo apt-get install uncrustify

macOS:
brew install uncrustify
```

### Setup

#### Install pre-commit:

```bash
pip install pre-commit
```

#### Install the pre-commit hooks:

pre-commit install

#### (Optional) Run hooks against all files:

```bash
pre-commit run --all-files
```

#### To bypass hooks for a particular commit (not recommended):

```bash
git commit -m "message" --no-verify
```


#### pre-commit autoupdate

Configuration
Hook configurations are defined in .pre-commit-config.yaml. See this file for specific settings and behaviors.

For more information about pre-commit, visit [pre-commit.com](https://pre-commit.com/).

## CI Setup

See [ci setup guide](.github/workflows/README.md).
