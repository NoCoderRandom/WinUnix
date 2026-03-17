// uname - print system information (Windows port)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void usage() {
    fprintf(stderr,
        "Usage: uname [OPTION]...\n"
        "Print certain system information.  With no OPTION, same as -s.\n\n"
        "  -a, --all                print all information\n"
        "  -s, --kernel-name        print the kernel name\n"
        "  -n, --nodename           print the network node hostname\n"
        "  -r, --kernel-release     print the kernel release\n"
        "  -v, --kernel-version     print the kernel version\n"
        "  -m, --machine            print the machine hardware name\n"
        "  -p, --processor          print the processor type\n"
        "  -i, --hardware-platform  print the hardware platform\n"
        "  -o, --operating-system   print the operating system\n"
    );
}

int main(int argc, char* argv[]) {
    bool opt_a = false, opt_s = false, opt_n = false, opt_r = false;
    bool opt_v = false, opt_m = false, opt_p = false, opt_i = false, opt_o = false;
    bool any = false;

    if (argc == 1) { opt_s = true; any = true; }

    for (int i = 1; i < argc; i++) {
        char* arg = argv[i];
        if (arg[0] == '-' && arg[1] == '-') {
            if (strcmp(arg, "--all") == 0)               opt_a = true;
            else if (strcmp(arg, "--kernel-name") == 0)  opt_s = true;
            else if (strcmp(arg, "--nodename") == 0)     opt_n = true;
            else if (strcmp(arg, "--kernel-release") == 0) opt_r = true;
            else if (strcmp(arg, "--kernel-version") == 0) opt_v = true;
            else if (strcmp(arg, "--machine") == 0)      opt_m = true;
            else if (strcmp(arg, "--processor") == 0)    opt_p = true;
            else if (strcmp(arg, "--hardware-platform") == 0) opt_i = true;
            else if (strcmp(arg, "--operating-system") == 0)  opt_o = true;
            else { fprintf(stderr, "uname: invalid option '%s'\n", arg); usage(); return 1; }
            any = true;
        } else if (arg[0] == '-') {
            for (int j = 1; arg[j]; j++) {
                switch (arg[j]) {
                    case 'a': opt_a = true; break;
                    case 's': opt_s = true; break;
                    case 'n': opt_n = true; break;
                    case 'r': opt_r = true; break;
                    case 'v': opt_v = true; break;
                    case 'm': opt_m = true; break;
                    case 'p': opt_p = true; break;
                    case 'i': opt_i = true; break;
                    case 'o': opt_o = true; break;
                    default:
                        fprintf(stderr, "uname: invalid option -- '%c'\n", arg[j]);
                        usage(); return 1;
                }
                any = true;
            }
        }
    }

    if (opt_a) { opt_s = opt_n = opt_r = opt_v = opt_m = opt_p = opt_i = opt_o = true; }

    // Gather info
    OSVERSIONINFOEXW osvi = {};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    // Use RtlGetVersion via function pointer to avoid deprecation warning
    typedef LONG (WINAPI* RtlGetVersionFn)(OSVERSIONINFOEXW*);
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    RtlGetVersionFn pfnRtlGetVersion = (RtlGetVersionFn)GetProcAddress(hNtdll, "RtlGetVersion");
    if (pfnRtlGetVersion) pfnRtlGetVersion(&osvi);

    char hostname[256] = {};
    DWORD sz = sizeof(hostname);
    GetComputerNameA(hostname, &sz);

    SYSTEM_INFO si = {};
    GetNativeSystemInfo(&si);

    char machine[32] = "x86_64";
    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64)
        strcpy(machine, "aarch64");
    else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
        strcpy(machine, "i686");

    char release[64];
    snprintf(release, sizeof(release), "%lu.%lu.%lu",
        osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber);

    char version[128];
    // Build a version string like Linux's #1 SMP
    snprintf(version, sizeof(version), "#1 SMP (Windows Build %lu)", osvi.dwBuildNumber);

    char processor[32];
    strcpy(processor, machine);

    bool first = true;
    auto pr = [&](const char* s) {
        if (!first) printf(" ");
        printf("%s", s);
        first = false;
    };

    if (opt_s) pr("Windows");
    if (opt_n) pr(hostname);
    if (opt_r) pr(release);
    if (opt_v) pr(version);
    if (opt_m) pr(machine);
    if (opt_p) pr(processor);
    if (opt_i) pr(machine);
    if (opt_o) pr("Windows_NT");

    printf("\n");
    return 0;
}
