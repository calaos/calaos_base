/* LD_PRELOAD shim for the coverage job: write the gcov counters before _exit().
 *
 * Many tests leave through _exit() (forked children, and mains that must skip
 * static destructors), which bypasses the atexit-time dump, so everything those
 * processes ran would count as never executed. Needs the test binaries linked
 * with -Wl,--export-dynamic-symbol=__gcov_dump so the symbol is visible here.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <unistd.h>

extern void __gcov_dump(void) __attribute__((weak));

void _exit(int status)
{
    static void (*real)(int);

    if (__gcov_dump)
        __gcov_dump();
    if (!real)
        real = (void (*)(int))dlsym(RTLD_NEXT, "_exit");
    real(status);
    __builtin_unreachable();
}
