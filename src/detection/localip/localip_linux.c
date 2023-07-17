#include "localip.h"
#include "common/io/io.h"

#include <string.h>
#include <ctype.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>

#if defined(__FreeBSD__) || defined(__APPLE__)
#include <net/if_dl.h>
#else
#include <netpacket/packet.h>
#endif

static bool getDefaultRoute(char iface[16 /*IF_NAMESIZE*/ + 1])
{
    FILE* FF_AUTO_CLOSE_FILE netRoute = fopen("/proc/net/route", "r");
    // skip first line
    flockfile(netRoute);
    while (fgetc_unlocked(netRoute) != '\n');
    funlockfile(netRoute);
    unsigned long long destination; //, gateway, flags, refCount, use, metric, mask, mtu,

    while (fscanf(netRoute, "%16s%llx%*[^\n]", iface, &destination) == 2)
    {
        if (destination == 0)
            return true;
    }
    return false;
}

static void addNewIp(FFlist* list, const char* name, const char* addr, int type)
{
    FFLocalIpResult* ip = NULL;

    FF_LIST_FOR_EACH(FFLocalIpResult, temp, *list)
    {
        if (!ffStrbufEqualS(&temp->name, name)) continue;
        ip = temp;
        break;
    }
    if (!ip)
    {
        ip = (FFLocalIpResult*) ffListAdd(list);
        ffStrbufInitS(&ip->name, name);
        ffStrbufInit(&ip->ipv4);
        ffStrbufInit(&ip->ipv6);
        ffStrbufInit(&ip->mac);
        ip->defaultRoute = false;
    }

    switch (type)
    {
        case AF_INET:
            ffStrbufSetS(&ip->ipv4, addr);
            break;
        case AF_INET6:
            ffStrbufSetS(&ip->ipv6, addr);
            break;
        case -1:
            ffStrbufSetS(&ip->mac, addr);
            break;
    }
}

const char* ffDetectLocalIps(const FFLocalIpOptions* options, FFlist* results)
{
    struct ifaddrs* ifAddrStruct = NULL;
    if(getifaddrs(&ifAddrStruct) < 0)
        return "getifaddrs(&ifAddrStruct) failed";

    for (struct ifaddrs* ifa = ifAddrStruct; ifa; ifa = ifa->ifa_next)
    {
        if (!ifa->ifa_addr || !(ifa->ifa_flags & IFF_RUNNING))
            continue;

        if ((ifa->ifa_flags & IFF_LOOPBACK) && !(options->showType & FF_LOCALIP_TYPE_LOOP_BIT))
            continue;

        if (options->namePrefix.length && strncmp(ifa->ifa_name, options->namePrefix.chars, options->namePrefix.length) != 0)
            continue;

        if (ifa->ifa_addr->sa_family == AF_INET)
        {
            if (!(options->showType & FF_LOCALIP_TYPE_IPV4_BIT))
                continue;

            struct sockaddr_in* ipv4 = (struct sockaddr_in*) ifa->ifa_addr;
            char addressBuffer[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &ipv4->sin_addr, addressBuffer, INET_ADDRSTRLEN);
            addNewIp(results, ifa->ifa_name, addressBuffer, AF_INET);
        }
        else if (ifa->ifa_addr->sa_family == AF_INET6)
        {
            if (!(options->showType & FF_LOCALIP_TYPE_IPV6_BIT))
                continue;

            struct sockaddr_in6* ipv6 = (struct sockaddr_in6 *)ifa->ifa_addr;
            char addressBuffer[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &ipv6->sin6_addr, addressBuffer, INET6_ADDRSTRLEN);
            addNewIp(results, ifa->ifa_name, addressBuffer, AF_INET6);
        }
        #if defined(__FreeBSD__) || defined(__APPLE__)
        else if (ifa->ifa_addr->sa_family == AF_LINK)
        {
            if (!(options->showType & FF_LOCALIP_TYPE_MAC_BIT))
                continue;

            char addressBuffer[32];
            uint8_t* ptr = (uint8_t*) LLADDR((struct sockaddr_dl *)ifa->ifa_addr);
            snprintf(addressBuffer, sizeof(addressBuffer), "%02x:%02x:%02x:%02x:%02x:%02x",
                        ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5]);
            addNewIp(results, ifa->ifa_name, addressBuffer, -1);
        }
        #else
        else if (ifa->ifa_addr->sa_family == AF_PACKET)
        {
            if (!(options->showType & FF_LOCALIP_TYPE_MAC_BIT))
                continue;

            char addressBuffer[32];
            uint8_t* ptr = ((struct sockaddr_ll *)ifa->ifa_addr)->sll_addr;
            snprintf(addressBuffer, sizeof(addressBuffer), "%02x:%02x:%02x:%02x:%02x:%02x",
                        ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5]);
            addNewIp(results, ifa->ifa_name, addressBuffer, -1);
        }
        #endif
    }

    if (ifAddrStruct) freeifaddrs(ifAddrStruct);

    char iface[16 /*IF_NAMESIZE*/ + 1];
    if (getDefaultRoute(iface))
    {
        FF_LIST_FOR_EACH(FFLocalIpResult, ip, *results)
            ip->defaultRoute = ffStrbufEqualS(&ip->name, iface);
    }
    return NULL;
}
