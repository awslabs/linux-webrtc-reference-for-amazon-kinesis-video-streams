#include "../networking.h"
#include "logging.h"

NetworkingResult_t Networking_ConfigureLwsLogging( uint32_t logLevel )
{
    NetworkingResult_t ret = NETWORKING_RESULT_OK;
    int lws_levels = 0;
  
    /* Map application log levels to libwebsockets log levels */
    if( logLevel == LOG_NONE )
    {
        lws_levels = 0;
    }

    if( logLevel >= LOG_ERROR )
    {
        lws_levels |= LLL_ERR;
    }

    if( logLevel >= LOG_WARN )
    {
        lws_levels |= LLL_WARN;
    }

    if( logLevel >= LOG_INFO )
    {
        lws_levels |= LLL_NOTICE;
    }

    if( logLevel >= LOG_DEBUG )
    {
        lws_levels |= LLL_INFO;
    }

    if( logLevel >= LOG_VERBOSE )
    {
        lws_levels |= LLL_DEBUG;
    }

    /* Configure libwebsockets with the mapped log levels */
    lws_set_log_level( lws_levels, NULL );

    return ret;
}
