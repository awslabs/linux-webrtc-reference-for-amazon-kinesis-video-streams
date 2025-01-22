#include <time.h>
#include <string.h>
#include "networking_utils.h"

#define NETWORKING_ISO8601_TIME_STRING_LENGTH ( 20 ) /* length of ISO8601 format (e.g. 2024-12-31T03:27:52Z). */

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
