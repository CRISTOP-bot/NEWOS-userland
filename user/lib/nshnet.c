/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/lib/nshnet.c
 * Description: NEWOS network + wireless userland helpers (IP parse, DNS,
 *             HTTP fetch over the TCP transport).
 * Added: 2026-11-22
 */
#include "nshlib.h"

#define SYSC0(nr) ({ long r; __asm__ __volatile__( \
    "int $0x80" : "=a"(r) : "a"(nr) : "memory"); r; })
#define SYSC1(nr, a) ({ long r; __asm__ __volatile__( \
    "int $0x80" : "=a"(r) : "a"(nr), "D"((long)(a)) : "memory"); r; })
#define SYSC2(nr, a, b) ({ long r; __asm__ __volatile__( \
    "int $0x80" : "=a"(r) : "a"(nr), "D"((long)(a)), "S"((long)(b)) \
    : "memory"); r; })
#define SYSC3(nr, a, b, c) ({ long r; \
    __asm__ __volatile( \
    "int $0x80" : "=a"(r) : "a"(nr), "D"((long)(a)), "S"((long)(b)), \
    "d"((long)(c)) : "memory"); r; })
#define SYSC4(nr, a, b, c, d) ({ long r; \
    register long _x10 __asm__("r10") = (long)(d); \
    __asm__ __volatile__( \
    "int $0x80" : "=a"(r) : "a"(nr), "D"((long)(a)), "S"((long)(b)), \
    "d"((long)(c)), "r"(_x10) : "memory"); r; })
#define SYSC5(nr, a, b, c, d, e) ({ long r; \
    register long _x10 __asm__("r10") = (long)(d); \
    register long _x8 __asm__("r8") = (long)(e); \
    __asm__ __volatile( \
    "int $0x80" : "=a"(r) : "a"(nr), "D"((long)(a)), "S"((long)(b)), \
    "d"((long)(c)), "r"(_x10), "r"(_x8) : "memory"); r; })

/* --- network syscall wrappers ------------------------------------------- */

long sys_net_socket(int type)
{
    return SYSC1(SYS_NET_SOCKET, type);
}

long sys_net_connect(int s, u32 ip, u32 port, u32 timeout_ms)
{
    return SYSC4(SYS_NET_CONNECT, s, ip, port, timeout_ms);
}

long sys_net_bind(int s, u32 port)
{
    return SYSC2(SYS_NET_BIND, s, port);
}

long sys_net_send(int s, const void *buf, u32 len, u32 timeout_ms)
{
    return SYSC4(SYS_NET_SEND, s, (long)(u64)(u32)(unsigned long)buf, len,
                 timeout_ms);
}

long sys_net_recv(int s, void *buf, u32 len, u32 timeout_ms)
{
    return SYSC4(SYS_NET_RECV, s, (long)(u64)(u32)(unsigned long)buf, len,
                 timeout_ms);
}

long sys_net_close(int s)
{
    return SYSC1(SYS_NET_CLOSE, s);
}

long sys_net_dhcp(u32 timeout_ms)
{
    return SYSC1(SYS_NET_DHCP, timeout_ms);
}

long sys_net_info(struct nsh_netinfo *ni)
{
    return SYSC2(SYS_NET_INFO, (long)(u64)(unsigned long)ni, 0);
}

long sys_net_setip(u32 ip, u32 mask, u32 gw)
{
    return SYSC3(SYS_NET_SETIP, ip, mask, gw);
}

long sys_net_ping(u32 ip, u32 timeout_ms)
{
    return SYSC3(SYS_NET_PING, ip, timeout_ms, 0);
}

long sys_wifi_scan(struct nsh_wifi_ap *aps, int max)
{
    return SYSC2(SYS_WIFI_SCAN, (long)(u64)(u32)(unsigned long)aps, max);
}

long sys_wifi_connect(const char *ssid, const char *pass, u32 timeout_ms)
{
    return SYSC3(SYS_WIFI_CONNECT, (long)(u64)(unsigned long)ssid,
                 (long)(u64)(unsigned long)pass, timeout_ms);
}

long sys_wifi_status(struct nsh_wifi_status *st)
{
    return SYSC2(SYS_WIFI_STATUS, (long)(u64)(unsigned long)st, 0);
}

long sys_wifi_disconnect(void)
{
    return SYSC0(SYS_WIFI_DISCONNECT);
}

long sys_net_dns(const char *name, u32 *ips, int max)
{
    return SYSC3(SYS_NET_DNS, (long)(u64)(unsigned long)name,
                 (long)(u64)(u32)(unsigned long)ips, max);
}

/* --- IP helpers ------------------------------------------------------------ */

int nsh_parse_ip(const char *s, u32 *out)
{
    u32 parts[4];
    int idx = 0;
    u32 cur = 0;
    int digits = 0;

    if (!s || !*s)
        return -1;
    for (u32 i = 0; s[i]; i++) {
        if (s[i] >= '0' && s[i] <= '9') {
            cur = cur * 10 + (u32)(s[i] - '0');
            digits++;
            if (digits > 3)
                return -1;
        } else if (s[i] == '.') {
            if (digits == 0)
                return -1;
            parts[idx++] = cur;
            cur = 0;
            digits = 0;
        } else {
            return -1;
        }
    }
    if (idx != 3 || digits == 0)
        return -1;
    parts[3] = cur;
    *out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return 0;
}

void nsh_ip_str(u32 ip, char *out)
{
    u8 b[4];
    u32 o = 0;
    u32 v;

    b[0] = (u8)(ip >> 24);
    b[1] = (u8)(ip >> 16);
    b[2] = (u8)(ip >> 8);
    b[3] = (u8)(ip);

    /* Print each octet in decimal */
    for (int i = 0; i < 4; i++) {
        v = b[3 - i];
        if (v >= 100) {
            out[o++] = (char)('0' + v / 100);
            v %= 100;
            out[o++] = (char)('0' + v / 10);
            out[o++] = (char)('0' + v % 10);
        } else if (v >= 10) {
            out[o++] = (char)('0' + v / 10);
            out[o++] = (char)('0' + v % 10);
        } else {
            out[o++] = (char)('0' + v);
        }
        if (i < 3)
            out[o++] = '.';
    }
    out[o] = 0;
}

int nsh_resolve(const char *name, u32 *ip)
{
    static u32 ips[8];
    int n;

    if (!name || !*name)
        return -1;
    if (nsh_parse_ip(name, ip) == 0)
        return 0;
    n = (int)sys_net_dns(name, ips, 8);
    if (n <= 0 || ips[0] == 0)
        return -1;
    *ip = ips[0];
    return 0;
}

/* --- HTTP fetch ------------------------------------------------------------ */

#define HTTP_HDR_MAX 512
#define HTTP_BODY_MAX 2048
#define HTTP_MAX_REDIRECTS 5

struct http_url {
    char host[64];
    char path[128];
    u32 port;
};

/* Copy up to max-1 bytes of s into d (NUL-terminated) and report the
 * length. Returns 0. */
static int http_copy(char *d, const char *s, u32 max, u32 *len_out)
{
    u32 i = 0;

    if (max == 0)
        return -1;
    while (s[i] && i + 1 < max) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
    *len_out = i;
    return 0;
}

/* Parse "http://host[:port]/path". Returns 0 on success. */
static int http_parse(const char *url, struct http_url *u)
{
    const char *host_start;
    const char *host_end;
    const char *path_start;
    const char *p;
    u32 hl, pl;

    if (!url || url[0] != 'h')
        return -1;
    host_start = url + 7;
    host_end = host_start;
    while (*host_end && *host_end != '/' && *host_end != ':')
        host_end++;
    path_start = host_end;
    while (*path_start && *path_start != '/')
        path_start++;

    hl = (u32)(host_end - host_start);
    if (hl == 0 || hl >= sizeof(u->host))
        return -1;
    http_copy(u->host, host_start, sizeof(u->host), &pl);
    u->host[hl] = 0;

    pl = (u32)(path_start - host_end);
    if (pl == 0) {
        u->path[0] = '/';
        u->path[1] = 0;
    } else {
        if (pl >= sizeof(u->path))
            return -1;
        http_copy(u->path, host_end, sizeof(u->path), &pl);
    }

    u->port = 80;
    p = host_end;
    if (*p == ':') {
        p++;
        u->port = 0;
        {
            u32 v = 0;
            u32 d = 0;

            while (*p >= '0' && *p <= '9') {
                v = v * 10 + (u32)(*p - '0');
                d++;
                p++;
            }
            if (d > 0)
                u->port = v;
        }
    }
    return 0;
}

/* Build a request into req (size req_max) and return its length. */
static u32 http_build_req(char *req, u32 req_max, const struct http_url *u,
                          int head_only)
{
    u32 rl = 0;

    if (!head_only) {
        const char *p = "GET ";

        while (*p) {
            if (rl + 1 >= req_max)
                break;
            req[rl++] = *p++;
        }
        {
            u32 i;

            for (i = 0; u->path[i] && rl + 1 < req_max; i++)
                req[rl++] = u->path[i];
        }
        {
            const char *h = " HTTP/1.0\r\nHost: ";

            while (*h) {
                if (rl + 1 >= req_max)
                    break;
                req[rl++] = *h++;
            }
            {
                char pbuf[8];
                u32 v = u->port;
                u32 ti = 0;

                if (v == 0)
                    v = 80;
                pbuf[ti++] = (char)('0' + (v / 1000) % 10);
                pbuf[ti++] = (char)('0' + (v / 100) % 10);
                pbuf[ti++] = (char)('0' + (v / 10) % 10);
                pbuf[ti++] = (char)('0' + v % 10);
                pbuf[ti] = 0;
                for (u32 i = 0; i < ti; i++) {
                    if (rl + 1 >= req_max)
                        break;
                    req[rl++] = pbuf[i];
                }
            }
        }
        {
            const char *e = "\r\nUser-Agent: NEWOS\r\nConnection: close\r\n\r\n";

            while (*e) {
                if (rl + 1 >= req_max)
                    break;
                req[rl++] = *e++;
            }
        }
    } else {
        const char *p = "HEAD ";

        while (*p) {
            if (rl + 1 >= req_max)
                break;
            req[rl++] = *p++;
        }
        {
            u32 i;

            for (i = 0; u->path[i] && rl + 1 < req_max; i++)
                req[rl++] = u->path[i];
        }
        {
            const char *e = " HTTP/1.0\r\nHost: ";

            while (*e) {
                if (rl + 1 >= req_max)
                    break;
                req[rl++] = *e++;
            }
            {
                char pbuf[8];
                u32 v = u->port;
                u32 ti = 0;

                if (v == 0)
                    v = 80;
                pbuf[ti++] = (char)('0' + (v / 1000) % 10);
                pbuf[ti++] = (char)('0' + (v / 100) % 10);
                pbuf[ti++] = (char)('0' + (v / 10) % 10);
                pbuf[ti++] = (char)('0' + v % 10);
                pbuf[ti] = 0;
                for (u32 i = 0; i < ti; i++) {
                    if (rl + 1 >= req_max)
                        break;
                    req[rl++] = pbuf[i];
                }
            }
        }
        {
            const char *e = "\r\nUser-Agent: NEWOS\r\nConnection: close\r\n\r\n";

            while (*e) {
                if (rl + 1 >= req_max)
                    break;
                req[rl++] = *e++;
            }
        }
    }
    req[rl] = 0;
    return (int)rl;
}

/* Exchange one HTTP request/response. Returns 0 on success; fills
 * *status_out, *location_out (NUL-terminated, may be 0) and the body
 * length. The body is copied to out_fd when out_fd >= 0 and !head_only. */
static int http_exchange(const char *url, int out_fd, int head_only,
                        int *status_out, const char **location_out,
                        u32 *body_len_out)
{
    struct http_url u;
    char req[512];
    char body[HTTP_BODY_MAX];
    u32 rl;
    u32 total = 0;
    u32 hdr_end = 0;
    int status = -1;
    int s;
    u32 ip = 0;

    *status_out = -1;
    *location_out = 0;
    *body_len_out = 0;
    if (http_parse(url, &u) != 0)
        return -1;
    if (nsh_resolve(u.host, &ip) != 0)
        return -1;

    rl = http_build_req(req, sizeof(req), &u, head_only);
    s = (int)sys_net_socket(0);
    if (s < 0)
        return -1;
    if (sys_net_connect(s, ip, u.port, 15000) != 0) {
        sys_net_close(s);
        return -1;
    }
    if (sys_net_send(s, req, rl, 15000) != (long)rl) {
        sys_net_close(s);
        return -1;
    }
    while (total < sizeof(body)) {
        long n = sys_net_recv(s, body + total, (u32)sizeof(body) - total, 15000);

        if (n <= 0)
            break;
        total += (u32)n;
        if (n == 0)
            break;
    }
    sys_net_close(s);

    /* Status line: find the first digit-run after the first space. */
    {
        u32 i = 0;

        while (i + 1 < total && (body[i] < '0' || body[i] > '9') &&
               body[i] != ' ')
            i++;
        status = 0;
        {
            u32 v = 0;
            u32 j = i + 1;

            while (j < total && body[j] >= '0' && body[j] <= '9') {
                v = v * 10 + (u32)(body[j] - '0');
                j++;
            }
            status = (int)v;
        }
    }
    *status_out = status;

    /* Header/body split. */
    while (hdr_end + 4 < total) {
        if (body[hdr_end] == '\r' && body[hdr_end + 1] == '\n' &&
            body[hdr_end + 2] == '\r' && body[hdr_end + 3] == '\n') {
            hdr_end += 4;
            break;
        }
        hdr_end++;
    }
    if (hdr_end + 4 >= total)
        return -1;
    *body_len_out = total - hdr_end;

    /* Locate the Location header (case-insensitive prefix "location:"). */
    {
        const char *loc = 0;
        u32 pos = 0;

        while (pos + 1 < hdr_end) {
            u32 e = pos;

            while (e < hdr_end && body[e] != '\r')
                e++;
            if (e + 1 < hdr_end && body[e] == '\r') {
                if (nstrncmp(&body[pos], "location:", 10) == 0) {
                    char lbuf[64];
                    u32 li = 0;
                    const char *q = &body[pos] + 10;

                    while (*q && *q != '\r' && li + 1 < sizeof(lbuf)) {
                        lbuf[li] = *q;
                        li++;
                        q++;
                    }
                    lbuf[li] = 0;
                    {
                        static char g_loc[64];

                        for (u32 k2 = 0; k2 < li; k2++)
                            g_loc[k2] = lbuf[k2];
                        g_loc[li] = 0;
                        loc = g_loc;
                    }
                }
            }
            pos = e + 2;
        }
        *location_out = loc;
    }

    if (!head_only && out_fd >= 0 && *body_len_out > 0) {
        sys_write(out_fd, body + hdr_end, *body_len_out);
    }
    return 0;
}

int nsh_http_fetch(const char *url, int out_fd, int head_only, int show_headers,
                   int verbose)
{
    char cur[192];
    const char *location = 0;
    int status = 0;
    int rc = -1;
    u32 hops = 0;

    (void)show_headers;
    (void)verbose;
    if (!url)
        return -1;
    if (http_copy(cur, url, sizeof(cur), &hops) != 0)
        return -1;
    while (hops < HTTP_MAX_REDIRECTS) {
        int ex_status = -1;
        u32 body_len = 0;

        if (http_exchange(cur, out_fd, head_only, &ex_status, &location,
                          &body_len) != 0)
            return -1;
        status = ex_status;
        rc = (status >= 200 && status < 300) ? 0 : -1;
        if (status < 300 || status >= 400)
            break;
        if (!location || !*location)
            break;
        if (http_copy(cur, location, sizeof(cur), &hops) != 0)
            break;
    }
    return rc;
}
