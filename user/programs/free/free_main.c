#include "../../lib/nshlib.h"

int main(int argc, char **argv)
{
    u64 total_kb = 0, free_kb = 0, used_kb;

    (void)argc;
    (void)argv;
    if (nsh_proc_meminfo(&total_kb, &free_kb) != 0) {
        /* /proc may be unavailable in a future reduced environment; the
         * SYS_SYSINFO path remains the compatibility fallback. */
        static struct nsh_sysinfo si;
        if (sys_sysinfo(&si) != 0)
            return fail("free", "cannot read system info");
        total_kb = si.total_frames * 4;
        free_kb = si.free_frames * 4;
    }
    used_kb = total_kb >= free_kb ? total_kb - free_kb : 0;
    nputs("             total       used       free\n");
    putf("Mem:     %10uK %10uK %10uK\n", total_kb, used_kb, free_kb);
    return 0;
}
