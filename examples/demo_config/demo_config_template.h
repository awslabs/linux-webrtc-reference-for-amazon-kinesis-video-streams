/*
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

 #ifndef DEMO_CONFIG_H
#define DEMO_CONFIG_H

#define AWS_REGION "us-west-2"

#define AWS_KVS_CHANNEL_NAME ""

#define AWS_KVS_AGENT_NAME "AWS-SDK-KVS"

#define AWS_CA_CERT_PATH "cert/cert.pem"

#ifndef ENABLE_SCTP_DATA_CHANNEL
#define ENABLE_SCTP_DATA_CHANNEL 0U
#endif

#ifndef ENABLE_TWCC_SUPPORT
#define ENABLE_TWCC_SUPPORT 1U
#endif

/* Uncomment to use fetching credentials by IoT Role-alias for Authentication. */
// #define AWS_CREDENTIALS_ENDPOINT ""
// #define AWS_IOT_THING_NAME ""
// #define AWS_IOT_THING_ROLE_ALIAS ""
// #define AWS_IOT_THING_CERT_PATH ""
// #define AWS_IOT_THING_PRIVATE_KEY_PATH ""

/* Uncomment to use AWS Access Key Credentials for Authentication. */
// #define AWS_ACCESS_KEY_ID ""
// #define AWS_SECRET_ACCESS_KEY ""
// #define AWS_SESSION_TOKEN ""

#if defined( AWS_ACCESS_KEY_ID ) && defined( AWS_IOT_THING_ROLE_ALIAS )
#error "Configuration Error: AWS_ACCESS_KEY_ID and AWS_IOT_THING_ROLE_ALIAS are mutually exclusive authentication methods. Please define only one of them."
#endif /* #if defined( AWS_ACCESS_KEY_ID ) && defined( AWS_IOT_THING_ROLE_ALIAS ). */

#define AWS_MAX_VIEWER_NUM ( 2 )

/* Audio format setting - currently only opus codec is supported in Linux environments */
#define AUDIO_OPUS         1

/* Join Storage Session setting. */
#ifndef JOIN_STORAGE_SESSION
#define JOIN_STORAGE_SESSION 0
#endif

#endif /* DEMO_CONFIG_H */
