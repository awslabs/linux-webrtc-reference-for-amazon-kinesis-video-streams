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

#include <time.h>
#include <string.h>
#include "networking_utils.h"

/* length of ISO8601 format (e.g. 2024-12-31T03:27:52Z). */
#define NETWORKING_ISO8601_TIME_STRING_LENGTH ( 20 )

/*
   NETWORKING_NTP_OFFSET (2208988800ULL) represents the number of seconds between two important epochs:

   NTP Epoch: January 1, 1900, 00:00:00 UTC
   Unix Epoch: January 1, 1970, 00:00:00 UTC
   This offset is required because:

   NTP timestamps count seconds from January 1, 1900
   Unix timestamps (what we're converting from) count seconds from January 1, 1970
   The offset (2208988800) is exactly the number of seconds between these two dates
 */
#define NETWORKING_NTP_OFFSET    2208988800ULL

/*
   The scaling (NETWORKING_NTP_TIMESCALE = 2^32 = 4294967296) is used for handling fractional seconds in NTP's timestamp format. Here's why:

   NTP timestamp format consists of two 32-bit fields:

   1. First 32 bits: whole seconds since NTP epoch
   2. Second 32 bits: fractional second in fixed-point format

   The fractional part uses a fixed-point representation where:

   1. 2^32 (4294967296) represents 1 full second
   2. Any value from 0 to 2^32-1 represents a fraction of a second */
#define NETWORKING_NTP_TIMESCALE 4294967296ULL

uint64_t NetworkingUtils_GetCurrentTimeSec( void * pTick )
{
    return ( uint64_t ) time( NULL );
}

uint64_t NetworkingUtils_GetCurrentTimeUs( void * pTick )
{
    struct timespec nowTime;
    clock_gettime( CLOCK_REALTIME, &nowTime );
    return ( ( uint64_t ) nowTime.tv_sec * 1000 * 1000 ) + ( ( uint64_t ) nowTime.tv_nsec / 1000 );
}

uint64_t NetworkingUtils_GetTimeFromIso8601( const char * pDate,
                                             size_t dateLength )
{
    uint64_t ret = 0;
    char isoTimeBuffer[NETWORKING_ISO8601_TIME_STRING_LENGTH + 1];
    struct tm tm;
    time_t t;
    int year, month, day, hour, minute, second;

    if( ( dateLength == NETWORKING_ISO8601_TIME_STRING_LENGTH ) && ( pDate != NULL ) )
    {
        memcpy( isoTimeBuffer, pDate, dateLength );
        isoTimeBuffer[dateLength] = '\0';

        sscanf( isoTimeBuffer, "%d-%d-%dT%d:%d:%dZ", &year, &month, &day, &hour, &minute, &second );

        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_sec = second;

        t = timegm( &tm );

        if( t != -1 )
        {
            ret = ( uint64_t ) t;
        }
    }

    return ret;
}

/*
   Example walkthrough of ConvertMicrosecondsToNTP function
   Input: January 1, 2023 12:30:45.500000

   Using microseconds since Unix epoch (1970)
   timeUs = 1672527045500000ULL;  // Our input value in microseconds

   Step 1: Convert to seconds
   sec = timeUs / 1000000ULL;
   1672527045500000 / 1000000 = 1672527045 seconds

   Step 2: Get remainder (fractional part in microseconds)
   usec = timeUs % 1000000ULL;
   1672527045500000 % 1000000 = 500000 (0.5 seconds in microseconds)

   Step 3: Add NTP offset to seconds
   ntp_sec = sec + NETWORKING_NTP_OFFSET;
   1672527045 + 2208988800 = 3881515845

   Step 4: Convert fractional part to NTP scale
   NETWORKING_NTP_TIMESCALE is 2^32 = 4294967296
   500000 * 4294967296 / 1000000 = 2147483648

   Step 5: Combine into final NTP timestamp
   final_ntp = (ntp_sec << 32U | ntp_frac);
   3881515845 << 32 | 2147483648 = 16677181839663572288
 */

uint64_t NetworkingUtils_GetNTPTimeFromUnixTimeUs( uint64_t timeUs )
{
    uint64_t sec = timeUs / 1000000ULL;  // Convert microseconds to seconds
    uint64_t usec = timeUs % 1000000ULL; // Get microsecond remainder

    uint64_t ntp_sec = sec + NETWORKING_NTP_OFFSET;
    uint64_t ntp_frac = ( usec * NETWORKING_NTP_TIMESCALE ) / 1000000ULL;

    return( ntp_sec << 32U | ntp_frac );
}