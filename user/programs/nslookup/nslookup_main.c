/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/nslookup/nslookup_main.c
 * Description: NEWOS nslookup — resolve a host via SYS_NET_DNS.
 * Added: 2026-11-22
 */
#include "../../lib/nshlib.h"

int main(int argc, char **argv)
{
    struct nsh_netinfo ni;
    u32 ips[8];
    char ipbuf[16];
    int n;

    if (argc < 2)
        return fail("nslookup", "usage: nslookup HOST");

    if (sys_net_info(&ni) != 0)
        return fail("nslookup", "cannot read interface info");
    nputs("Server: ");
    nsh_ip_str(ni.dns, ipbuf);
    nputs(ipbuf);
    nputc('\n');

    n = (int)sys_net_dns(argv[1], ips, 8);
    if (n <= 0)
        return fail("nslookup", "no address found");
    for (int i = 0; i < n; i++) {
        nsh_ip_str(ips[i], ipbuf);
        nputs(ipbuf);
        nputc('\n');
    }
    return 0;
}
