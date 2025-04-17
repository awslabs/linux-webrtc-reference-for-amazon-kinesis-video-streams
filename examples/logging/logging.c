#include "logging.h"
#include <time.h>
#include <stdint.h>

char * Logging_GetTime()
{
    static char timebuffer[ 32 ] = { 0 };
    struct tm * tmval = NULL;
    struct timespec curtime = { 0 };
    char * ret = timebuffer;
    uint32_t ms;
    size_t timeStringLength;

    /* Get current time */
    clock_gettime( CLOCK_REALTIME, &curtime );
    ms = curtime.tv_nsec / 1000000;

    if( ( tmval = gmtime( &curtime.tv_sec ) ) != NULL )
    {
        /* Build the first part of the time */
        timeStringLength = strftime( timebuffer, sizeof( timebuffer ), "%Y-%m-%d %H:%M:%S", tmval );

        /* Add the milliseconds part and build the time string */
        snprintf( timebuffer + timeStringLength, sizeof( timebuffer ) - timeStringLength, ".%03u", ms );
    }

    return ret;
}
