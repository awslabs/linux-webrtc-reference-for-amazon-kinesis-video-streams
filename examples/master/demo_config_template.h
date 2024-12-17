#ifndef DEMO_CONFIG_H
#define DEMO_CONFIG_H

#define AWS_REGION "us-west-2"

#define AWS_KVS_CHANNEL_NAME ""

#define AWS_ACCESS_KEY_ID ""

#define AWS_SECRET_ACCESS_KEY ""

#define AWS_KVS_AGENT_NAME "AWS-SDK-KVS"

#define AWS_CA_CERT_PATH "cert/cert.pem"

#define AWS_MAX_VIEWER_NUM ( 2 )

/* Audio format setting. */
#define AUDIO_G711_MULAW    0
#define AUDIO_G711_ALAW     0
#define AUDIO_OPUS          1
#if ( AUDIO_G711_MULAW + AUDIO_G711_ALAW + AUDIO_OPUS ) != 1
    #error only one of audio format should be set.
#endif

/* Video format setting. */
#define USE_H265 0

#endif /* DEMO_CONFIG_H */
