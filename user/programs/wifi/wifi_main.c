/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/wifi/wifi_main.c
 * Description: NEWOS wifi — scan/connect/disconnect/status.
 * Added: 2026-11-22
 */
#include "../../lib/nshlib.h"

static void bstr(const u8 b[6], char *out)
{
    const char hex[] = "0123456789abcdef";

    for (int i = 0; i < 6; i++) {
        out[i * 3] = hex[(b[i] >> 4) & 0xF];
        out[i * 3 + 1] = hex[b[i] & 0xF];
        out[i * 3 + 2] = ':';
    }
    out[18] = 0;
}

static int do_scan(void)
{
    struct nsh_wifi_ap aps[16];
    int n = (int)sys_wifi_scan(aps, 16);

    if (n < 0)
        return fail("wifi", "scan failed");
    for (int i = 0; i < n; i++) {
        char b[24];

        bstr(aps[i].bssid, b);
        nputs(aps[i].ssid);
        nputc('\t');
        nputs(b);
        nputc('\t');
        putf("sec=%d ch=%d rssi=%d %s\n", aps[i].security, aps[i].channel,
             aps[i].rssi, aps[i].connected ? "connected" : "");
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct nsh_wifi_status st;

    if (argc < 2) {
        if (sys_wifi_status(&st) != 0)
            return fail("wifi", "cannot read status");
        nputs("wifi: status unavailable\n");
        return 0;
    }
    if (nstrcmp(argv[1], "scan") == 0)
        return do_scan();
    if (nstrcmp(argv[1], "status") == 0) {
        if (sys_wifi_status(&st) != 0)
            return fail("wifi", "cannot read status");
        nputs("wifi: connected=");
        putf("%d", st.connected);
        nputc('\n');
        return 0;
    }
    if (nstrcmp(argv[1], "disconnect") == 0) {
        if (sys_wifi_disconnect() != 0)
            return fail("wifi", "disconnect failed");
        return 0;
    }
    if (nstrcmp(argv[1], "connect") == 0) {
        const char *ssid = argv[2];
        const char *pass = argc > 3 ? argv[3] : 0;
        u32 timeout = 5000;

        if (argc > 4)
            timeout = (u32)natoi(argv[4]);
        if (sys_wifi_connect(ssid, pass,
                             (long)(u64)(u32)(unsigned long)timeout) != 0)
            return fail("wifi", "connect failed");
        return 0;
    }
    return fail("wifi", "unknown command");
}
