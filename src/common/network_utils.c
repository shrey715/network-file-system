/*
 * network_utils.c - Network utility functions
 * 
 * Provides utility functions for network operations.
 */

#include "common.h"
#include <ifaddrs.h>
#include <net/if.h>

/**
 * get_local_network_ip
 * @brief Get the local machine's primary network IP address.
 *
 * Iterates through network interfaces to find an active, non-loopback interface
 * with an IPv4 address. Returns the first suitable address found.
 *
 * @param ip_out Buffer to store the IP address (must be at least MAX_IP bytes).
 * @param size Size of the output buffer.
 * @return 0 on success, -1 if no suitable interface found.
 */
int get_local_network_ip(char* ip_out, size_t size) {
    struct ifaddrs *ifaddr, *ifa;
    int result = -1;
    
    if (getifaddrs(&ifaddr) == -1) {
        return -1;
    }
    
    // Walk through linked list of interfaces
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) 
            continue;
        
        // Only interested in IPv4
        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;
        
        // Skip loopback interfaces
        if (ifa->ifa_flags & IFF_LOOPBACK)
            continue;
        
        // Skip down interfaces
        if (!(ifa->ifa_flags & IFF_UP))
            continue;
        
        struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
        
        // Skip loopback addresses (127.x.x.x)
        if ((ntohl(addr->sin_addr.s_addr) & 0xFF000000) == 0x7F000000)
            continue;
        
        // Found a valid non-loopback address
        if (inet_ntop(AF_INET, &addr->sin_addr, ip_out, size) != NULL) {
            result = 0;
            break;
        }
    }
    
    freeifaddrs(ifaddr);
    return result;
}
