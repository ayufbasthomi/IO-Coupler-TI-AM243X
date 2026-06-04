/*!
 *  \file ESL_eoeDemo.c
 *
 *  \brief
 *  EtherCAT<sup>&reg;</sup> Ethernet over EtherCAT Example
 *
 *  \author
 *  Texas Instruments Incorporated
 *
 *  \copyright
 *  Copyright (C) 2025 Texas Instruments Incorporated
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <osal.h>
#include <ESL_eoeDemo.h>
#ifdef LWIP_SUPPORT
#include <ESL_eoeDemoData.h>
#include <ESL_eeprom.h>

#include "kernel/dpl/TaskP.h"
#include "kernel/dpl/SystemP.h"

#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "netif/ethernet.h"
#include "netif/etharp.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/sockets.h"
#include "lwip/errno.h"

static eoeContext eoeContext_s;
struct netif gnetif;

// #undef EOE_DEBUG_PRINTS
#define EOE_DEBUG_PRINTS 

/*!
*
* \brief
*
* Sends data from the network interface to the virtual PHY
*
* \details
*
* This function is called from the LWIP stack when it wants to send a packet.
*
* \param[in] netif
*
* Pointer to the network interface structure for the device.
*
* \param[in] p
*
* Pointer to the packet to send.
*
* \return
*
* ERR_OK if the packet was sent successfully.
*
* ERR_MEM if memory allocation failed.
*/
err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    #ifdef EOE_DEBUG_PRINTS
    DebugP_log("EOE: low_level_output() called\r\n");
    #endif //EOE_DEBUG_PRINTS

    OSALUNREF_PARM(netif); //Not used

    err_t error = ERR_OK;
    uint32_t ecerror;
    uint32_t tries = 20;

    struct pbuf *q = pbuf_alloc(PBUF_RAW, p->len, PBUF_RAM);
    if (q == NULL)
    {
        return ERR_MEM;
    }

    pbuf_copy_partial(p, q->payload, p->len, 0);

    do
    {
        int32_t result = SemaphoreP_pend(&eoeContext_s.txLock, SystemP_WAIT_FOREVER);
        DebugP_assert(result == SystemP_SUCCESS);

        ecerror = EC_API_SLV_EoE_sendFrame(NULL, p->len, q->payload);
        SemaphoreP_post(&eoeContext_s.txLock);
        if (ecerror != EC_API_eERR_NONE)
        {
            OSAL_SCHED_sleep(EOE_MESSAGE_OUTPUT_DELAY_MS);
        }
    } while (ecerror != EC_API_eERR_NONE && tries--);

    if (ecerror != EC_API_eERR_NONE)
    {
        DebugP_log("EOE: drop packet\r\n");
#ifdef MIB2_STATS
        gnetif.mib2_counters.ifoutdiscards++;
#endif
    }

    pbuf_free(q);

    return error;
}


/**
*
* \brief
*
* Dummy function used to initialize the EoE network interface
*
* \details
*
* This function is called by the LWIP stack during initialization.
* It initializes the EoE network interface and sets the default
* MAC address.
*
* \param[in] netif
*
* Pointer to the network interface structure for the device.
*
* \return
*
* ERR_OK if initialization was successful.
*/
err_t low_level_init_dummy(struct netif *netif)
{
    OSALUNREF_PARM(netif); //Not used

    return ERR_OK;
}

/**
*
* \brief
*
* Callback function used to signal the completion of TCP initialization.
*
* \details
*
* This function is called when TCP initialization is complete.
* It signals the completion of initialization by signalling a semaphore.
*
* \param[in] arg
*
* Pointer to the semaphore to signal.
*
* \return
*
* None.
*/
void tcp_init_done(void *arg)
{
    sys_sem_t *sem = arg;
    sys_sem_signal(sem);
}

/**
*
* \brief
*
* Initializes the network interface.
*
* \details
*
* This function initializes the network interface. It creates a semaphore
* and initializes the TCP/IP stack.abs
*
* \param[in] netif
*
* Pointer to the network interface structure for the device.
*
* \return
*
* ERR_OK if initialization was successful.
*/
err_t netif_if_init(struct netif *netif)
{
    err_t error = ERR_OK;
    sys_sem_t init_sem;
    ip4_addr_t emptyip = { 0 };

    error = sys_sem_new(&init_sem, 0);

    tcpip_init(tcp_init_done, &init_sem);

    sys_sem_wait(&init_sem);
    sys_sem_free(&init_sem);

    LOCK_TCPIP_CORE();
    netif_add(netif, &emptyip, &emptyip, &emptyip, NULL, low_level_init_dummy, ethernet_input);

    netif->name[0] = 't';
    netif->name[1] = 'i';
    netif->hwaddr_len = 6;

    if (!EC_SLV_APP_EEP_getMacAddress(EEPROM_MAGIC_KEY_BOARD_ID, netif->hwaddr))
    {
        #ifdef EOE_DEBUG_PRINTS
        DebugP_log("EOE: Failed to get MAC address from EEPROM\r\n");
        #endif //EOE_DEBUG_PRINTS
        memset(netif->hwaddr, 0, sizeof(netif->hwaddr));
    }

    #if (LWIP_ARP == 1)
    netif->output = etharp_output;
    #else
    netif->output = NULL;
    #endif

    netif->linkoutput = low_level_output;
    netif->mtu = EOE_MAX_BUFFER_SIZE_BYTES;
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_ETHERNET;

    netif_set_default(netif);
    netif_set_up(netif);
    netif_set_link_up(netif);
    netif_set_link_callback(netif, NULL);
    UNLOCK_TCPIP_CORE();

    error = netif_is_up(netif);

    return error;
}

/**
*
* \brief
*
* Converts an error code to an error string.
*
* \details
*
* This function converts an error code to an error string.
*
* \param[in] err_p
*
* Error code
*
* \return
*
* Error string
*/
static char *EC_SLV_APP_EoE_SS_WebSrvStrError(int err_p)
{
    switch (err_p)
    {
    case 0:  return ("No error");
    case EACCES:  return ("Permission denied");
    case EPERM:  return ("Permission denied");
    case EADDRINUSE:  return ("Local Address already in use");
    case EADDRNOTAVAIL:  return ("Address not available");
    case EAFNOSUPPORT:  return ("Incorrect address family address");
    case EAGAIN:  return ("Operation would block, Try Again");
    case EALREADY:  return ("Previous operation still in progress");
    case EBADF:  return ("Not a valid file descriptor");
    case ECONNREFUSED:  return ("Connection refused");
    case EFAULT:  return ("Socket structure address is not valid");
    case EINPROGRESS:  return ("Previous operation is now in progress");
    case EINTR:  return ("Operation interrupted by signal");
    case EISCONN:  return ("Socket already in use");
    case ENETUNREACH:  return ("Network is unreachable");
    case ENOTSOCK:  return ("Given file descriptor is not a socket");
    case EPROTOTYPE:  return ("Socket does not support the comms protocol");
    case ETIMEDOUT:  return ("Timeout while attempting connection");
    case ELOOP:  return ("Too many symbolic links were encountered");
    case ENAMETOOLONG:  return ("File name too long");
    case ENOENT:  return ("Socket pathname does not exist");
    case ENOMEM:  return ("Insufficient kernel memory");
    case ENOTDIR:  return ("Path prefix is not a directory");
    case EROFS:  return ("Socket inode in a read-only filesystem");
    case EOPNOTSUPP:  return ("Operation not supported");
    case EPROTO:  return ("Socket does not support the comms protocol");
    case ECONNABORTED:  return ("Connection aborted");
    case ECONNRESET:  return ("Connection reset by peer");
    case ENOTCONN:  return ("Transport endpoint not connected");
    }

    return ("Unknown error");
}

/*!
*
*  \brief
*  Helper function to check if errno is fatal.
*
*  \details
*
*  Helper function to check if errno is fatal.
*  Non-Fatal are :
*  EAGAIN
*  EWOULDBLOCK
*  ECONNABORTED
*  EINTR
*  ECONNRESET
*
*  \param[in]     errNo_p
*
*  errno set by the lwip stack.
*
*  \return
*
*  result of the operation as bool
*/
static bool EC_SLV_APP_EoE_SS_WebSrvIsFatalErr(int errNo_p)
{
    bool isFatal = true;
    if ((errNo_p == EAGAIN) ||
        (errNo_p == EWOULDBLOCK) ||
        (errNo_p == ECONNABORTED) ||
        (errNo_p == EINTR) ||
        (errNo_p == ECONNRESET))
    {
        isFatal = false;
    }
    return isFatal;
}

/*!
*
*  \brief
*
*  Function to process the received http GET request and respond to it.
*
*  \details
*
*  Function to process the received http GET request and respond to it.
*  At the moment it only responds to the following get requests :
*  "/"
*  "/main.html"
*  "/favicon.ico
*  A 404 error will be returned for any other request.
*
*  \param[in] clientFd_p
*
*  Client file descriptor for sending response.
*
*  \param[in]        pBuf_p
*
*  Pointer to received data (with "GET " request removed).
*
*  \return
*
*  result of the operation as int
*/
static int EC_SLV_APP_EoE_SS_WebSrvProcessGetAndRespond(int clientFd_p, const char *const pBuf_p)
{
    int ret = -1;

    if ((strncmp(&pBuf_p[0], "/ HTTP/1.1\r\n", 12) == 0) ||
        (strncmp(&pBuf_p[0], "/main.html HTTP/1.1\r\n", 21) == 0))
    {
        // get request for main.html
        ret = send(clientFd_p, response_200_content_html, strlen(response_200_content_html), 0);

        if (ret > 0)
        {
            ret = send(clientFd_p, main_html, strlen(main_html), 0);
        }
    }
    else if ((strncmp(&pBuf_p[0], "/favicon.ico HTTP/1.1\r\n", 23) == 0))
    {
        // get request for favicon.ico
        ret = send(clientFd_p, response_200_content_image, strlen(response_200_content_image), 0);

        if (ret > 0)
        {
            ret = send(clientFd_p, favicon_ico, sizeof(favicon_ico), 0);
        }
    }
    else
    {
        // Unknown get request
        ret = send(clientFd_p, response_404, strlen(response_404), 0);
    }
    return ret;
}

/*!
*
*  \brief
*
*  Function to initialize the webserver.
*
*  \details
*
*  Function to initialize the webserver and set up the socket to listen
*  for incoming connections.
*
*  \return
*
*  0 on success, -1 on failure.
*/
int EC_SLV_APP_EoE_SS_initializeWebserver(void)
{
    int errorValue = -1;
    int sock = -1;
    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_port = PP_HTONS(80);
    addr.sin_addr.s_addr = PP_HTONL(INADDR_ANY);

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0)
    {
        DebugP_log("Failed to open socket : %d : %s \r\n", -errno,
                   EC_SLV_APP_EoE_SS_WebSrvStrError(errno));
    }
    else
    {
        errorValue = bind(sock, (struct sockaddr*)&addr, sizeof(addr));

        if (errorValue == 0)
        {
            errorValue = listen(sock, 3);

            if (errorValue == 0)
            {
                eoeContext_s.socketFd = sock;
            }
        }

        if (errorValue < 0)
        {
            DebugP_log("Failed to bind / listen to socket : %d : %s \r\n",
                       -errno, EC_SLV_APP_EoE_SS_WebSrvStrError(errno));
        }
    }

    #ifdef EOE_DEBUG_PRINTS
    DebugP_log("Net Task created\r\n");
    #endif //EOE_DEBUG_PRINTS

    return errorValue;
}
#endif  //LWIP_SUPPORT

/*! \brief
*  EoE Settings Indication callback. Called when EoE settings are received.
*
*  \param[in]  pContext            The pointer to the application instance.
*  \param[in]  pMac                Virtual Net MAC address
*  \param[in]  pIp                 Virtual Net IP address
*  \param[in]  pSubNet             Virtual Net Subnet
*  \param[in]  pDefaultGateway     Virtual Net Default Gateway
*  \param[in]  pDnsIp              Virtual Net DNS server address
*  \return     true if settings are handled, false otherwise
*
*  \ingroup EC_SLV_APP_EOE
*
* */
bool EC_SLV_APP_EoE_SS_settingIndHandler(
   void *pContext,
   uint16_t *pMac,
   uint16_t *pIp,
   uint16_t *pSubNet,
   uint16_t *pDefaultGateway,
   uint16_t *pDnsIp)
{
#ifdef LWIP_SUPPORT
    ip4_addr_t ip;
    ip4_addr_t subnet;
    ip4_addr_t gateway;
    ip4_addr_t dns;

    OSALUNREF_PARM(pContext); // Not used

    #ifdef EOE_DEBUG_PRINTS
    DebugP_log("EoE: Settings received.\r\n");
    #endif //EOE_DEBUG_PRINTS

    eoeContext_s.settingsReceived = 1;

    //Values received from EtherCAT MainDevice
    if (pMac != NULL && (pMac[0] | pMac[1] | pMac[2]) != 0)
    {
        gnetif.hwaddr[0] = pMac[0] >> 8;
        gnetif.hwaddr[1] = pMac[0] & 0xFF;
        gnetif.hwaddr[2] = pMac[1] >> 8;
        gnetif.hwaddr[3] = pMac[1] & 0xFF;
        gnetif.hwaddr[4] = pMac[2] >> 8;
        gnetif.hwaddr[5] = pMac[2] & 0xFF;
    }
    else
    {
        #ifdef EOE_DEBUG_PRINTS
        DebugP_log("EoE: Invalid MAC address received, keeping existing MAC.\r\n");
        #endif //EOE_DEBUG_PRINTS
    }

    IP4_ADDR(&ip,
        pIp[0] & 0xFF, pIp[0] >> 8,
        pIp[1] & 0xFF, pIp[1] >> 8);

    IP4_ADDR(&subnet,
        pSubNet[0] & 0xFF, pSubNet[0] >> 8,
        pSubNet[1] & 0xFF, pSubNet[1] >> 8);

    IP4_ADDR(&gateway,
        pDefaultGateway[0] & 0xFF, pDefaultGateway[0] >> 8,
        pDefaultGateway[1] & 0xFF, pDefaultGateway[1] >> 8);

    IP4_ADDR(&dns,
        pDnsIp[0] & 0xFF, pDnsIp[0] >> 8,
        pDnsIp[1] & 0xFF, pDnsIp[1] >> 8);

    LOCK_TCPIP_CORE();

    netif_set_addr(&gnetif, &ip, &subnet, &gateway);
    UNLOCK_TCPIP_CORE();

    #else
    OSALUNREF_PARM(pContext);
    OSALUNREF_PARM(pMac);
    OSALUNREF_PARM(pIp);
    OSALUNREF_PARM(pSubNet);
    OSALUNREF_PARM(pDefaultGateway);
    OSALUNREF_PARM(pDnsIp);
    #endif

    return true;
}

/*! \brief
*  User defined EoE receive function. Called when an EoE frame is received.
*
*  \param[in]  pContext            function context
*  \param[in]  pData               EoE Frame Data
*  \param[in]  size                EoE Frame Size
*  \return true if frame is handle, false if it should be passed on.
*
*  \ingroup EC_SLV_APP_EOE
*
* */
bool EC_SLV_APP_EoE_SS_receiveHandler(void *pContext, uint16_t *pData, uint16_t size)
{
   #ifdef LWIP_SUPPORT
    OSALUNREF_PARM(pContext); // Not used

   #ifdef EOE_DEBUG_PRINTS
   DebugP_log("EoE: Message received: %d bytes\r\n", size);
   DebugP_log("EoE: Message received (raw): ");
    for (uint16_t i = 0; i < size; i++)
    {
       DebugP_log("%02X ", (pData[i] >> 8) & 0xFF);
       DebugP_log("%02X ", pData[i] & 0xFF);
   }
   DebugP_log("\r\n");
#endif // EOE_DEBUG_PRINTS

    LOCK_TCPIP_CORE();
    struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);

    DebugP_assert(p != NULL);
    pbuf_take(p, (const void *)pData, size);

#ifdef EOE_DEBUG_PRINTS
    {
        uint32_t i;
        DebugP_log("EoE: Incoming message (hex): ");
        for (i = 0; i < p->tot_len; i++)
   {
            DebugP_log("%02x ", *((uint8_t *)p->payload + i));
   }
        DebugP_log("\r\n");
    }
#endif // EOE_DEBUG_PRINTS

    if (gnetif.input(p, &gnetif) != ERR_OK)
    {
#ifdef EOE_DEBUG_PRINTS
        DebugP_log("Error in input\r\n");
#endif // EOE_DEBUG_PRINTS
        pbuf_free(p);
        p = NULL;
    }
    UNLOCK_TCPIP_CORE();
   #else
   OSALUNREF_PARM(pContext);
   OSALUNREF_PARM(pData);
#endif // LWIP_SUPPORT

   return true;
}

#ifdef LWIP_SUPPORT

/*! \brief
*  Virtual switch endpoint of EoE
*
*  \param[in]  arg      slave Handle pointer (argument)
*
*  \ingroup SLVAPI
*
* */
void EC_SLV_APP_EoE_SS_task(void *pArg)
{
    OSALUNREF_PARM(pArg); //Not used

    int errorValue;

    eoeContext_s.socketFd = -1;

    netif_if_init(&gnetif);

    errorValue = EC_SLV_APP_EoE_SS_initializeWebserver();
    if (errorValue != 0)
    {
        #ifdef EOE_DEBUG_PRINTS
        DebugP_log("EoE: Error intializing webserver.\r\n");
        #endif //EOE_DEBUG_PRINTS
    }

    if ((errorValue == 0) && eoeContext_s.socketFd >= 0)
    {
        #ifdef EOE_DEBUG_PRINTS
        DebugP_log("Webserver intialized.\r\n");
        #endif //EOE_DEBUG_PRINTS

    while(true)
    {
            eoeContext_s.clientFd = accept(eoeContext_s.socketFd, NULL, NULL); //blocking

            static char aBuf[1580] = { 0 };
            memset(aBuf, 0, sizeof(aBuf));

            if (eoeContext_s.clientFd < 0 )
            {
                if (EC_SLV_APP_EoE_SS_WebSrvIsFatalErr(errno))
                {
                    #ifdef EOE_DEBUG_PRINTS
                    DebugP_log("Failed to accept connection: %d : %s\r\n", -errno, EC_SLV_APP_EoE_SS_WebSrvStrError(errno));
                    #endif //EOE_DEBUG_PRINTS
                    break;
                }
                DebugP_log("EoE: Connection accepted.\r\n");
                continue;
            }

            errorValue = recv(eoeContext_s.clientFd, aBuf, sizeof(aBuf), 0);
            if (errorValue < 0)
            {
                if (EC_SLV_APP_EoE_SS_WebSrvIsFatalErr(errno))
                {
                    DebugP_log("Failed to receive bytes on connection : %d : %s \r\n", -errno, EC_SLV_APP_EoE_SS_WebSrvStrError(errno));
                    break;
                }
            }
            else if (errorValue == 0)
            {
                #ifdef EOE_DEBUG_PRINTS
                DebugP_log("Connection closed!\r\n");
                #endif //EOE_DEBUG_PRINTS
            }
            else
            {
                if (strncmp(&aBuf[0], "GET ", 4) == 0)
                {
                    // We received some bytes and it is a get request,
                    // call processing function.
                    #ifdef EOE_DEBUG_PRINTS
                    DebugP_log("EoE: Received GET request.\r\n");
                    #endif //EOE_DEBUG_PRINTS
                    errorValue = EC_SLV_APP_EoE_SS_WebSrvProcessGetAndRespond(eoeContext_s.clientFd, &aBuf[4]);
                    if (errorValue <= 0)
                    {
                        if (EC_SLV_APP_EoE_SS_WebSrvIsFatalErr(errno))
                        {
                            DebugP_log("Failed to process request sent : %d : %s \r\n", -errno,
                                       EC_SLV_APP_EoE_SS_WebSrvStrError(errno));
                            break;
                        }
                    }
                }
            }

            errorValue = shutdown(eoeContext_s.clientFd, SHUT_RDWR);
            if (errorValue != 0)
            {
                if ((EC_SLV_APP_EoE_SS_WebSrvIsFatalErr(errno)) &&
                    (errno != ENOTCONN))
                {
                    DebugP_log("Failed to shutdown connection : %d : %s \r\n", -errno,
                               EC_SLV_APP_EoE_SS_WebSrvStrError(errno));
                    break;
                }
            }

            errorValue = close(eoeContext_s.clientFd);
            if (errorValue != 0)
            {
                if (EC_SLV_APP_EoE_SS_WebSrvIsFatalErr(errno))
                {
                    DebugP_log("Failed to close connection : %d : %s \r\n", -errno,
                               EC_SLV_APP_EoE_SS_WebSrvStrError(errno));
                    break;
                }
            }

        }

        errorValue = close(eoeContext_s.socketFd);
        if (errorValue != 0)
        {
            DebugP_log("Failed to close socket : %d : %s \r\n", -errno,
                       EC_SLV_APP_EoE_SS_WebSrvStrError(errno));
        }
    }

    TaskP_exit();
}
uint8_t Webserver_start(void)
{
    SemaphoreP_constructMutex(&eoeContext_s.txLock);

    Webserver_pTaskHandle_s = OSAL_SCHED_startTask(EC_SLV_APP_EoE_SS_task
                                                  ,NULL
                                                  ,OSAL_TASK_Prio_10
                                                  ,(uint8_t*)webserverTaskStack_s
                                                  ,WEBSERVER_TASK_SIZE_BYTE
                                                  ,OSAL_OS_START_TASK_FLG_NONE
                                                  ,"WebServerTask");

    if ( NULL == Webserver_pTaskHandle_s)
    {
        OSAL_printf("Error return start WebServer Task\r\n");
        OSAL_error(__func__, __LINE__, OSAL_STACK_INIT_ERROR, true, 0);
    }

    return 0;
}
#endif //LWIP_SUPPORT
//*************************************************************************************************
