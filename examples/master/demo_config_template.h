#ifndef DEMO_CONFIG_H
#define DEMO_CONFIG_H

#define AWS_REGION "us-west-2"

#define AWS_KVS_CHANNEL_NAME ""

#define AWS_KVS_AGENT_NAME "AWS-SDK-KVS"

#define AWS_CA_CERT_PATH "cert/cert.pem"

#ifndef ENABLE_SCTP_DATA_CHANNEL
#define ENABLE_SCTP_DATA_CHANNEL 0U
#endif

/* Uncomment to use fetching credentials by IoT Role-alias for Authentication */
// #define AWS_CREDENTIALS_ENDPOINT ""
// #define AWS_IOT_THING_NAME ""
// #define AWS_IOT_THING_ROLE_ALIAS ""
// #define AWS_IOT_THING_CERT_PATH ""
// #define AWS_IOT_THING_PRIVATE_KEY_PATH ""

/* Uncomment to use AWS Access Key Credentials for Authentication */
// #define AWS_ACCESS_KEY_ID ""
// #define AWS_SECRET_ACCESS_KEY ""
// #define AWS_SESSION_TOKEN ""

#if defined( AWS_ACCESS_KEY_ID ) && defined( AWS_IOT_THING_ROLE_ALIAS )
#error "Configuration Error: AWS_ACCESS_KEY_ID and AWS_IOT_THING_ROLE_ALIAS are mutually exclusive authentication methods. Please define only one of them."
#endif /* #if defined( AWS_ACCESS_KEY_ID ) && defined( AWS_IOT_THING_ROLE_ALIAS ) */

#define AWS_MAX_VIEWER_NUM ( 2 )

/* Audio format setting. */
#define AUDIO_G711_MULAW    0
#define AUDIO_G711_ALAW     0
#define AUDIO_OPUS          1
#if ( AUDIO_G711_MULAW + AUDIO_G711_ALAW + AUDIO_OPUS ) != 1
#error only one of audio format should be set.
#endif

/* Video format setting. */
#define USE_H265 1

#endif /* DEMO_CONFIG_H */
