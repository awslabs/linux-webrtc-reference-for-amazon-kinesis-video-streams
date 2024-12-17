# FreeRTOS-WebRTC-Application

## Submodule Update
```
git submodule update --init --recursive
```
## Setup
1. Copy `examples/master/demo_config_template.h` and rename it to `examples/master/demo_config.h` and set the following:
   * Set `AWS_KVS_CHANNEL_NAME` to your signaling channel name.
   * Set `AWS_ACCESS_KEY_ID` to your access key.
   * Set `AWS_SECRET_ACCESS_KEY` to your secret access key.

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
