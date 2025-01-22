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
```
cmake -S . -B build
make -C build
```

## execute
```
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
