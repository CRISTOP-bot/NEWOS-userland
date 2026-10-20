#include "../../lib/nshlib.h"

int main(int argc, char **argv)
{
    u64 s = 0, centi = 0, m, h, d;

    (void)argc;
    (void)argv;
    if (nsh_proc_uptime(&s, &centi) != 0) {
        static struct nsh_sysinfo si;
        if (sys_sysinfo(&si) != 0)
            return fail("uptime", "cannot read system info");
        s = si.uptime_sec;
    }
    (void)centi;

    d = s / 86400;
    s %= 86400;
    h = s / 3600;
    s %= 3600;
    m = s / 60;
    s %= 60;
    nputs("up ");
    if (d) {
        putu(d);
        nputs(d == 1 ? " day, " : " days, ");
    }
    if (h || d) {
        putu(h);
        nputs(h == 1 ? " hour, " : " hours, ");
    }
    putu(m);
    nputs(m == 1 ? " min" : " mins");
    nputc('\n');
    return 0;
}
