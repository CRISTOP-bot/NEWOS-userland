/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/ifconfig/ifconfig_main.c
 * Description: NEWOS ifconfig — interface state via SYS_NET_INFO,
 *             DHCP via SYS_NET_DHCP, static set via SYS_NET_SETIP.
 * Added: 2026-11-22
 */
#include "../../lib/nshlib.h"

int main(int argc, char **argv)
{
    struct nsh_netinfo ni;
    char ipbuf[16];

    if (argc > 1 && nstrcmp(argv[1], "dhcp") == 0) {
        u32 tmo = 5000;

        if (argc > 2)
            tmo = (u32)natoi(argv[2]);
        if (sys_net_dhcp((long)(u64)(u32)(unsigned long)tmo) != 0)
            return fail("ifconfig", "dhcp failed");
        return 0;
    }

    if (argc == 4) {
        u32 ip = 0, mask = 0, gw = 0;

        if (nsh_parse_ip(argv[1], &ip) != 0)
            return fail("ifconfig", "bad ip");
        if (nsh_parse_ip(argv[2], &mask) != 0)
            return fail("ifconfig", "bad netmask");
        if (nsh_parse_ip(argv[3], &gw) != 0)
            return fail("ifconfig", "bad gateway");
        if (sys_net_setip((long)(u64)(u32)(unsigned long)ip,
                          (long)(u64)(u32)(unsigned long)mask,
                          (long)(u64)(u32)(unsigned long)gw) != 0)
            return fail("ifconfig", "cannot set ip");
        return 0;
    }

    if (sys_net_info(&ni) != 0)
        return fail("ifconfig", "cannot read interface info");
    nputs("wlan0: ");
    nputs("inet ");
    nsh_ip_str(ni.ip, ipbuf);
    nputs(ipbuf);
    nputs("  netmask ");
    nsh_ip_str(ni.mask, ipbuf);
    nputs(ipbuf);
    nputs("  gateway ");
    nsh_ip_str(ni.gw, ipbuf);
    nputs(ipbuf);
    nputs("\n");
    return 0;
}
