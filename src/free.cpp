// free - display amount of free and used memory (Windows port)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void usage() {
    fprintf(stderr,
        "Usage:\n"
        " free [options]\n\n"
        "Options:\n"
        " -b, --bytes         show output in bytes\n"
        " -k, --kilo          show output in kilobytes (default)\n"
        " -m, --mega          show output in megabytes\n"
        " -g, --giga          show output in gigabytes\n"
        "     --tera          show output in terabytes\n"
        "     --peta          show output in petabytes\n"
        " -h, --human         show human-readable output\n"
        "     --si            use powers of 1000 not 1024\n"
        " -l, --lohi          show detailed low and high memory statistics\n"
        " -t, --total         show total for RAM + swap\n"
        " -v, --committed     show committed memory and commit limit\n"
        " -s N, --seconds N   repeat printing every N seconds\n"
        " -c N, --count N     repeat printing N times, then exit\n"
        " -w, --wide          wide output\n"
        "     --help          display this help\n"
    );
}

enum Unit { BYTES, KILO, MEGA, GIGA, TERA, PETA, HUMAN };

static void human_size(char* buf, size_t buflen, unsigned long long bytes, bool si) {
    double base = si ? 1000.0 : 1024.0;
    const char* units_si[]  = {"B","kB","MB","GB","TB","PB"};
    const char* units_bin[] = {"B","Ki","Mi","Gi","Ti","Pi"};
    const char** units = si ? units_si : units_bin;
    double val = (double)bytes;
    int idx = 0;
    while (val >= base && idx < 5) { val /= base; idx++; }
    if (idx == 0) snprintf(buf, buflen, "%lluB", bytes);
    else snprintf(buf, buflen, "%.1f%s", val, units[idx]);
}

static unsigned long long convert(unsigned long long bytes, Unit unit, bool si) {
    double base = si ? 1000.0 : 1024.0;
    switch (unit) {
        case BYTES: return bytes;
        case KILO:  return bytes / (unsigned long long)base;
        case MEGA:  return bytes / (unsigned long long)(base*base);
        case GIGA:  return bytes / (unsigned long long)(base*base*base);
        case TERA:  return bytes / (unsigned long long)(base*base*base*base);
        case PETA:  return bytes / (unsigned long long)(base*base*base*base*base);
        default:    return bytes;
    }
}

static void print_info(Unit unit, bool si, bool show_total, bool wide, bool committed) {
    MEMORYSTATUSEX ms = {};
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);

    PERFORMANCE_INFORMATION pi = {};
    pi.cb = sizeof(pi);
    // GetPerformanceInfo from psapi
    typedef BOOL(WINAPI* GetPerfInfoFn)(PPERFORMANCE_INFORMATION, DWORD);
    HMODULE hPsapi = LoadLibraryA("psapi.dll");
    bool hasPerfInfo = false;
    unsigned long long committed_total = 0, commit_limit = 0;
    if (hPsapi) {
        auto fn = (GetPerfInfoFn)GetProcAddress(hPsapi, "GetPerformanceInfo");
        if (fn && fn(&pi, sizeof(pi))) {
            hasPerfInfo = true;
            committed_total = (unsigned long long)pi.CommitTotal * pi.PageSize;
            commit_limit    = (unsigned long long)pi.CommitLimit * pi.PageSize;
        }
        FreeLibrary(hPsapi);
    }

    unsigned long long total_phys = ms.ullTotalPhys;
    unsigned long long free_phys  = ms.ullAvailPhys;
    unsigned long long used_phys  = total_phys - free_phys;
    // "buff/cache" - approximate via WorkingSet
    unsigned long long cached = 0;
    // On Windows: available = free+cached pages. We'll use AvailVirtual - AvailPhys roughly.
    // Better: use MemoryLoad to compute cache
    // For simplicity, we'll treat buffers=0, cache≈(total-used-free) if any rounding
    unsigned long long buf_cache = 0; // Windows doesn't expose this easily
    unsigned long long available = ms.ullAvailPhys; // Linux "available" ≈ AvailPhys

    // Swap = page file
    unsigned long long swap_total = ms.ullTotalPageFile > total_phys ? ms.ullTotalPageFile - total_phys : 0;
    unsigned long long swap_free  = ms.ullAvailPageFile > free_phys  ? ms.ullAvailPageFile  - free_phys  : 0;
    unsigned long long swap_used  = swap_total - swap_free;

    auto fmt = [&](char* buf, size_t len, unsigned long long bytes) {
        if (unit == HUMAN) human_size(buf, len, bytes, si);
        else { snprintf(buf, len, "%llu", convert(bytes, unit, si)); }
    };

    const char* unit_str = "";
    switch (unit) {
        case BYTES: unit_str = "B";  break;
        case KILO:  unit_str = "Ki"; break;
        case MEGA:  unit_str = "Mi"; break;
        case GIGA:  unit_str = "Gi"; break;
        case TERA:  unit_str = "Ti"; break;
        case PETA:  unit_str = "Pi"; break;
        case HUMAN: unit_str = ""; break;
    }

    char t[32],u[32],f[32],sh[32],bc[32],av[32];
    fmt(t,  sizeof(t),  total_phys);
    fmt(u,  sizeof(u),  used_phys);
    fmt(f,  sizeof(f),  free_phys);
    fmt(sh, sizeof(sh), 0ULL);
    fmt(bc, sizeof(bc), buf_cache);
    fmt(av, sizeof(av), available);

    if (wide)
        printf("%-14s %12s %12s %12s %12s %12s %12s\n",
            "", "total", "used", "free", "shared", "buffers", "cache");
    else
        printf("%-14s %12s %12s %12s %12s %12s %12s\n",
            "", "total", "used", "free", "shared", "buff/cache", "available");

    if (wide)
        printf("%-14s %12s %12s %12s %12s %12s %12s\n",
            "Mem:", t, u, f, sh, sh, bc);
    else
        printf("%-14s %12s %12s %12s %12s %12s %12s\n",
            "Mem:", t, u, f, sh, bc, av);

    char st[32],su[32],sf[32];
    fmt(st, sizeof(st), swap_total);
    fmt(su, sizeof(su), swap_used);
    fmt(sf, sizeof(sf), swap_free);
    printf("%-14s %12s %12s %12s\n", "Swap:", st, su, sf);

    if (show_total) {
        char tt[32],tu2[32],tf[32];
        fmt(tt,  sizeof(tt),  total_phys + swap_total);
        fmt(tu2, sizeof(tu2), used_phys  + swap_used);
        fmt(tf,  sizeof(tf),  free_phys  + swap_free);
        printf("%-14s %12s %12s %12s\n", "Total:", tt, tu2, tf);
    }

    if (committed && hasPerfInfo) {
        char ct[32], cu[32];
        fmt(ct, sizeof(ct), commit_limit);
        fmt(cu, sizeof(cu), committed_total);
        printf("%-14s %12s %12s\n", "Committed:", ct, cu);
    }
}

int main(int argc, char* argv[]) {
    Unit unit = KILO;
    bool si = false, show_total = false, wide = false, committed = false;
    double interval = 0.0;
    int count = 0;

    for (int i = 1; i < argc; i++) {
        char* a = argv[i];
        if (strcmp(a, "--help") == 0)        { usage(); return 0; }
        else if (strcmp(a, "--bytes") == 0) unit = BYTES;
        else if (strcmp(a, "--kilo") == 0)  unit = KILO;
        else if (strcmp(a, "--mega") == 0)  unit = MEGA;
        else if (strcmp(a, "--giga") == 0)  unit = GIGA;
        else if (strcmp(a, "--tera") == 0)  unit = TERA;
        else if (strcmp(a, "--peta") == 0)  unit = PETA;
        else if (strcmp(a, "--human") == 0) unit = HUMAN;
        else if (strcmp(a, "--si") == 0)    si = true;
        else if (strcmp(a, "--total") == 0) show_total = true;
        else if (strcmp(a, "--wide") == 0)  wide = true;
        else if (strcmp(a, "--committed") == 0) committed = true;
        else if (strcmp(a, "--lohi") == 0) {} // ignored on Windows
        else if (strcmp(a, "--seconds") == 0 && i+1 < argc)
            interval = atof(argv[++i]);
        else if (strcmp(a, "--count") == 0 && i+1 < argc)
            count = atoi(argv[++i]);
        else if (a[0] == '-' && a[1] != '-') {
            // Handle combined short flags: -ht, -gw, etc.
            bool err = false;
            for (int j = 1; a[j] && !err; j++) {
                switch (a[j]) {
                    case 'b': unit = BYTES; break;
                    case 'k': unit = KILO; break;
                    case 'm': unit = MEGA; break;
                    case 'g': unit = GIGA; break;
                    case 'h': unit = HUMAN; break;
                    case 't': show_total = true; break;
                    case 'w': wide = true; break;
                    case 'v': committed = true; break;
                    case 'l': break; // ignored on Windows
                    case 's':
                        if (a[j+1]) { interval = atof(a+j+1); j=(int)strlen(a)-1; }
                        else if (i+1 < argc) interval = atof(argv[++i]);
                        break;
                    case 'c':
                        if (a[j+1]) { count = atoi(a+j+1); j=(int)strlen(a)-1; }
                        else if (i+1 < argc) count = atoi(argv[++i]);
                        break;
                    default:
                        fprintf(stderr, "free: invalid option -- '%c'\n", a[j]);
                        fprintf(stderr, "Try 'free --help' for more information.\n");
                        err = true;
                }
            }
            if (err) return 1;
        }
        else {
            fprintf(stderr, "free: invalid option '%s'\n", a);
            fprintf(stderr, "Try 'free --help' for more information.\n");
            return 1;
        }
    }

    if (count == 0 && interval > 0) count = -1; // infinite
    if (count == 0) count = 1;

    for (int n = 0; count < 0 || n < count; n++) {
        if (n > 0) Sleep((DWORD)(interval * 1000));
        print_info(unit, si, show_total, wide, committed);
    }
    return 0;
}
