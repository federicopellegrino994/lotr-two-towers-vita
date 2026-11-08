/* PS Vita: sysinfo(2) for the runtime's GlobalMemoryStatus. Reports the
 * user memory a homebrew title normally has; the guest's own sizing does
 * not depend on it. */
#ifndef XBOXRECOMP_VITA_SYSINFO_H
#define XBOXRECOMP_VITA_SYSINFO_H
struct sysinfo {
    long uptime;
    unsigned long loads[3];
    unsigned long totalram, freeram, sharedram, bufferram;
    unsigned long totalswap, freeswap;
    unsigned short procs;
    unsigned long totalhigh, freehigh;
    unsigned int mem_unit;
};
static inline int sysinfo(struct sysinfo *si)
{
    if (!si) return -1;
    __builtin_memset(si, 0, sizeof *si);
    si->totalram = 256ul << 20;
    si->freeram  = 64ul << 20;
    si->procs = 1;
    si->mem_unit = 1;
    return 0;
}
static inline int get_nprocs(void) { return 3; }
#endif
