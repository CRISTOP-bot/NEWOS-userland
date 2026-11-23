/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/ping/ping_main.c
 * Description: NEWOS ping — ICMP echo via SYS_NET_PING.
 * Added: 2026-11-22
 */
#include "../../lib/nshlib.h"

int main(int argc, char **argv)
{
    char ipbuf[16];
    u32 ip = 0;
    u32 timeout_ms = 1000;
    long rtt;
    int count = 4;
    int i;

    if (argc < 2)
        return fail("ping", "usage: ping HOST");
    if (nsh_resolve(argv[1], &ip) != 0)
        return fail("ping", "cannot resolve host");
    nsh_ip_str(ip, ipbuf);

    for (i = 0; i < count; i++) {
        rtt = sys_net_ping(ip, timeout_ms);

        if (rtt < 0)
            return fail("ping", "no reply from host");
        nputs(ipbuf);
        putf(": icmp_seq=%d time=%d ms ttl=64\n", i + 1,
             (int)(rtt / 1000), 64);
    }

    putf("PING %s: packets=%d transmitted=%d received=%d loss=%d\n", ipbuf,
         count, count, count, 0);
    return 0;
}
