#include <time.h>
#include "networking_utils.h"

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
