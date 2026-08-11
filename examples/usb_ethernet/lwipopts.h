#ifndef _LWIPOPTS_H_
#define _LWIPOPTS_H_

/* Bare-metal mode (no OS / no FreeRTOS) */
#define NO_SYS 1

/* Disable Sockets and Sequential APIs (They require an OS) */
#define LWIP_NETCONN 0
#define LWIP_SOCKET 0

/* Prevent lwIP from redefining struct timeval provided by newlib toolchain */
#define LWIP_TIMEVAL_PRIVATE 0

/* Core network features for USB Ethernet */
#define MEM_ALIGNMENT 4
#define MEM_SIZE (16 * 1024)

#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_IPV4 1
#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_DHCP 1

#endif // _LWIPOPTS_H_