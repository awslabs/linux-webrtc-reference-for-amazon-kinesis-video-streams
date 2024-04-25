#include <arpa/inet.h>
#include "logging.h"
#include "ice_controller.h"
#include "ice_controller_private.h"

IceControllerResult_t IceControllerNet_ConvertIpString( const char *pIpAddr, size_t ipAddrLength, IceIPAddress_t *pDest )
{
    IceControllerResult_t ret = ICE_CONTROLLER_RESULT_OK;
    char ipAddress[ ICE_CONTROLLER_IP_ADDR_STRING_BUFFER_LENGTH + 1 ];

    if( ipAddrLength > ICE_CONTROLLER_IP_ADDR_STRING_BUFFER_LENGTH )
    {
        LogWarn( ( "invalid IP address detected, IP: %.*s",
                   ( int ) ipAddrLength, pIpAddr ) );
        ret = ICE_CONTROLLER_RESULT_IP_BUFFER_TOO_SMALL;
    }

    if( ret == ICE_CONTROLLER_RESULT_OK )
    {
        memcpy( ipAddress, pIpAddr, ipAddrLength );
        ipAddress[ ipAddrLength ] = '\0';

        if( inet_pton( AF_INET, ipAddress, pDest->ipAddress.address ) == 1 )
        {
            pDest->ipAddress.family = STUN_ADDRESS_IPv4;
        }
        else if( inet_pton( AF_INET6, ipAddress, pDest->ipAddress.address ) == 1 )
        {
            pDest->ipAddress.family = STUN_ADDRESS_IPv6;
        }
        else
        {
            ret = ICE_CONTROLLER_RESULT_INVALID_IP_ADDR;
        }
    }

    return ret;
}

IceControllerResult_t IceControllerNet_Htos( uint16_t port, uint16_t *pOutPort )
{
    *pOutPort = htons( port );

    return ICE_CONTROLLER_RESULT_OK;
}
