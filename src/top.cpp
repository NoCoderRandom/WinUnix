// top - display Linux processes (Windows port)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <pdh.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <io.h>
#include <conio.h>
#include <vector>
#include <string>
#include <algorithm>
#include <map>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "pdh.lib")

static void usage() {
    fprintf(stderr,
        "Usage:\n"
        "  top -hv | -bcEeHiOSs1 -d secs -n max -u|U user -p pid -o fld -w [cols]\n\n"
        "Options:\n"
        "  -b          Batch-mode operation (non-interactive)\n"
        "  -c          Command-line/name toggle\n"
        "  -d <secs>   Delay time interval (default: 3.0)\n"
        "  -H          Threads-mode operation\n"
        "  -i          Idle-process toggle\n"
        "  -n <num>    Number of iterations limit\n"
        "  -o <fld>    Override sort field\n"
        "  -p <pidlist> Monitor only these PIDs\n"
        "  -s          Secure-mode operation\n"
        "  -S          Cumulative-time toggle\n"
        "  -u <user>   User filter\n"
        "  -U <user>   User filter (real id)\n"
        "  -w [cols]   Output width\n"
        "  -1          Single CPU toggle\n"
        "  -h|-v       help/version\n"
    );
}

struct ProcInfo {
    DWORD pid, ppid;
    std::string name, user;
    double cpu_pct;
    unsigned long long mem_kb, vmem_kb;
    unsigned long long cpu_time_ms;
    char state;
    FILETIME create_time;
    ULONGLONG prev_cpu; // for delta calculation
};

static std::string get_user(HANDLE hProc) {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(hProc, TOKEN_QUERY, &hToken)) return "?";
    DWORD sz = 0;
    GetTokenInformation(hToken, TokenUser, NULL, 0, &sz);
    if (!sz) { CloseHandle(hToken); return "?"; }
    std::vector<char> buf(sz);
    if (!GetTokenInformation(hToken, TokenUser, buf.data(), sz, &sz)) { CloseHandle(hToken); return "?"; }
    CloseHandle(hToken);
    TOKEN_USER* tu = (TOKEN_USER*)buf.data();
    char name[256]={}, domain[256]={};
    DWORD nl=sizeof(name),dl=sizeof(domain);
    SID_NAME_USE use;
    if (LookupAccountSidA(NULL, tu->User.Sid, name, &nl, domain, &dl, &use)) return name;
    return "?";
}

struct CpuUsage {
    ULONGLONG idle, kernel, user;
};

static CpuUsage get_cpu_times() {
    FILETIME idle_ft, kernel_ft, user_ft;
    GetSystemTimes(&idle_ft, &kernel_ft, &user_ft);
    CpuUsage c;
    c.idle   = ((ULONGLONG)idle_ft.dwHighDateTime   << 32) | idle_ft.dwLowDateTime;
    c.kernel = ((ULONGLONG)kernel_ft.dwHighDateTime << 32) | kernel_ft.dwLowDateTime;
    c.user   = ((ULONGLONG)user_ft.dwHighDateTime   << 32) | user_ft.dwLowDateTime;
    return c;
}

static std::vector<ProcInfo> snapshot_procs() {
    std::vector<ProcInfo> procs;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return procs;
    PROCESSENTRY32 pe = {};
    pe.dwSize = sizeof(pe);
    if (!Process32First(hSnap, &pe)) { CloseHandle(hSnap); return procs; }
    do {
        ProcInfo p;
        p.pid   = pe.th32ProcessID;
        p.ppid  = pe.th32ParentProcessID;
        p.name  = pe.szExeFile;
        p.state = 'S';
        p.cpu_pct = 0.0;
        p.mem_kb = p.vmem_kb = p.cpu_time_ms = 0;
        p.prev_cpu = 0;
        memset(&p.create_time, 0, sizeof(p.create_time));
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ, FALSE, p.pid);
        if (hProc) {
            PROCESS_MEMORY_COUNTERS_EX pmc = {};
            pmc.cb = sizeof(pmc);
            if (GetProcessMemoryInfo(hProc, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
                p.mem_kb  = pmc.WorkingSetSize / 1024;
                p.vmem_kb = pmc.PrivateUsage   / 1024;
            }
            FILETIME ct,et,kt,ut;
            if (GetProcessTimes(hProc, &ct, &et, &kt, &ut)) {
                p.create_time = ct;
                ULARGE_INTEGER k,u2;
                k.LowPart  = kt.dwLowDateTime;  k.HighPart  = kt.dwHighDateTime;
                u2.LowPart = ut.dwLowDateTime;  u2.HighPart = ut.dwHighDateTime;
                p.cpu_time_ms = (k.QuadPart + u2.QuadPart) / 10000ULL;
                p.prev_cpu = k.QuadPart + u2.QuadPart;
            }
            p.user = get_user(hProc);
            CloseHandle(hProc);
        }
        procs.push_back(p);
    } while (Process32Next(hSnap, &pe));
    CloseHandle(hSnap);
    return procs;
}

static std::string human_mem(unsigned long long kb) {
    char buf[32];
    if (kb < 10000)       snprintf(buf, sizeof(buf), "%lluk", kb);
    else if (kb < 10000*1024) snprintf(buf, sizeof(buf), "%.1fm", kb/1024.0);
    else snprintf(buf, sizeof(buf), "%.1fg", kb/1024.0/1024.0);
    return buf;
}

static std::string fmt_time(unsigned long long ms) {
    unsigned long long s = ms/1000;
    unsigned m = (unsigned)(s/60); unsigned sec = (unsigned)(s%60);
    unsigned h = m/60; m = m%60;
    char buf[32];
    if (h) snprintf(buf,sizeof(buf),"%u:%02u:%02u.%02llu",h,m,sec,(ms%1000)/10);
    else   snprintf(buf,sizeof(buf),"%u:%02u.%02llu",m,sec,(ms%1000)/10);
    return buf;
}

// g_batch is set before print_top is called
static bool g_batch = false;

static inline const char* ansi(const char* code) {
    return g_batch ? "" : code;
}

static void print_top(const std::vector<ProcInfo>& procs,
                      double cpu_user_pct, double cpu_sys_pct, double cpu_idle_pct,
                      int rows, bool show_full_cmd) {

    MEMORYSTATUSEX ms = {};
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);

    ULONGLONG total_phys = ms.ullTotalPhys / 1024;
    ULONGLONG free_phys  = ms.ullAvailPhys / 1024;
    ULONGLONG used_phys  = total_phys - free_phys;
    ULONGLONG total_swap = (ms.ullTotalPageFile > ms.ullTotalPhys) ?
                           (ms.ullTotalPageFile - ms.ullTotalPhys) / 1024 : 0;
    ULONGLONG free_swap  = (ms.ullAvailPageFile > ms.ullAvailPhys) ?
                           (ms.ullAvailPageFile - ms.ullAvailPhys) / 1024 : 0;
    ULONGLONG used_swap  = total_swap - free_swap;

    SYSTEMTIME st;
    GetLocalTime(&st);
    char timebuf[32];
    snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);

    int total_tasks = (int)procs.size();

    printf("%stop - %s up ?, %d tasks: %d running, %d sleeping, %d stopped, %d zombie\n",
        ansi("\033[0m"), timebuf, total_tasks, 0, total_tasks, 0, 0);
    printf("%%Cpu(s): %5.1f us, %5.1f sy,  0.0 ni, %5.1f id,  0.0 wa,  0.0 hi,  0.0 si,  0.0 st\n",
        cpu_user_pct, cpu_sys_pct, cpu_idle_pct);
    printf("MiB Mem :  %7.1f total, %7.1f free, %7.1f used,     0.0 buff/cache\n",
        total_phys/1024.0, free_phys/1024.0, used_phys/1024.0);
    printf("MiB Swap:  %7.1f total, %7.1f free, %7.1f used. %7.1f avail Mem\n\n",
        total_swap/1024.0, free_swap/1024.0, used_swap/1024.0, free_phys/1024.0);

    printf("%s%7s %-12s %2s %3s %6s %6s %6s %1s %5s %10s %s%s\n",
        ansi("\033[7m"),
        "PID", "USER", "PR", "NI", "VIRT", "RES", "SHR", "S", "%CPU", "TIME+", "COMMAND",
        ansi("\033[0m"));

    int shown = 0;
    int max_show = (rows > 8) ? rows - 8 : 9999;
    if (g_batch) max_show = 9999;

    for (const auto& p : procs) {
        if (shown >= max_show) break;
        std::string name = p.name;
        if (!show_full_cmd && name.size() > 4 && _stricmp(name.c_str()+name.size()-4, ".exe") == 0)
            name = name.substr(0, name.size()-4);
        printf("%7lu %-12s %2d %3d %6s %6s %6s %1c %5.1f %10s %s\n",
            (unsigned long)p.pid,
            p.user.c_str(),
            20, 0,
            human_mem(p.vmem_kb).c_str(),
            human_mem(p.mem_kb).c_str(),
            human_mem(p.mem_kb/2).c_str(),
            p.state,
            p.cpu_pct,
            fmt_time(p.cpu_time_ms).c_str(),
            name.c_str());
        shown++;
    }
}

int main(int argc, char* argv[]) {
    bool batch = false;
    bool show_full_cmd = false;
    double delay = 3.0;
    int max_iter = -1;
    bool show_idle = true;
    std::string sort_field = "cpu";
    std::vector<DWORD> filter_pids;
    std::string filter_user;
    int width = 0;

    for (int i = 1; i < argc; i++) {
        char* a = argv[i];
        if (strcmp(a, "-h") == 0 || strcmp(a, "-v") == 0) { usage(); return 0; }
        else if (strcmp(a, "-b") == 0) batch = true;
        else if (strcmp(a, "-c") == 0) show_full_cmd = true;
        else if (strcmp(a, "-i") == 0) show_idle = false;
        else if (strcmp(a, "-1") == 0) {}
        else if (strcmp(a, "-H") == 0) {}
        else if (strcmp(a, "-S") == 0) {}
        else if (strcmp(a, "-s") == 0) {}
        else if ((strcmp(a, "-d") == 0) && i+1 < argc) delay = atof(argv[++i]);
        else if (strncmp(a, "-d", 2) == 0 && strlen(a) > 2) delay = atof(a+2);
        else if ((strcmp(a, "-n") == 0) && i+1 < argc) max_iter = atoi(argv[++i]);
        else if (strncmp(a, "-n", 2) == 0 && strlen(a) > 2) max_iter = atoi(a+2);
        else if ((strcmp(a, "-o") == 0) && i+1 < argc) sort_field = argv[++i];
        else if ((strcmp(a, "-u") == 0 || strcmp(a, "-U") == 0) && i+1 < argc) filter_user = argv[++i];
        else if ((strcmp(a, "-p") == 0) && i+1 < argc) {
            char buf[4096]; strncpy(buf, argv[++i], sizeof(buf)-1);
            char* tok = strtok(buf, ",");
            while (tok) { filter_pids.push_back((DWORD)atoi(tok)); tok = strtok(NULL, ","); }
        }
        else if (strcmp(a, "-w") == 0) {
            if (i+1 < argc && isdigit((unsigned char)argv[i+1][0])) width = atoi(argv[++i]);
        }
        // ignore unknown flags silently for compatibility
    }

    g_batch = batch;

    // Set console to UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // In batch mode use binary stdout to avoid CRLF on Windows
    if (batch) _setmode(_fileno(stdout), _O_BINARY);

    // Get console size
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int con_rows = 40;
    if (!batch && GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        con_rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    if (width > 0) con_rows = 9999; // -w just controls cols, not rows

    if (!batch) {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        GetConsoleMode(hOut, &mode);
        SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        printf("\033[?25l"); // hide cursor
    }

    // Initial snapshot for CPU delta
    CpuUsage prev_cpu = get_cpu_times();
    std::map<DWORD, ULONGLONG> prev_procs_map;
    {
        auto procs = snapshot_procs();
        for (auto& p : procs) prev_procs_map[p.pid] = p.prev_cpu;
    }

    // In batch mode always sleep delay before first output (like Linux top -b)
    Sleep((DWORD)(delay * 1000));

    int iter = 0;
    while (max_iter < 0 || iter < max_iter) {
        CpuUsage cur_cpu = get_cpu_times();
        ULONGLONG d_idle   = cur_cpu.idle   - prev_cpu.idle;
        ULONGLONG d_kernel = cur_cpu.kernel - prev_cpu.kernel;
        ULONGLONG d_user   = cur_cpu.user   - prev_cpu.user;
        ULONGLONG d_total  = d_kernel + d_user;
        if (d_total == 0) d_total = 1;

        double cpu_idle_pct = (double)d_idle   / d_total * 100.0;
        double cpu_sys_pct  = (double)(d_kernel - d_idle) / d_total * 100.0;
        double cpu_user_pct = (double)d_user   / d_total * 100.0;
        if (cpu_sys_pct  < 0) cpu_sys_pct  = 0;
        if (cpu_idle_pct > 100) cpu_idle_pct = 100;
        prev_cpu = cur_cpu;

        auto cur_procs = snapshot_procs();
        for (auto& p : cur_procs) {
            auto it = prev_procs_map.find(p.pid);
            if (it != prev_procs_map.end()) {
                ULONGLONG dp = p.prev_cpu - it->second;
                p.cpu_pct = (double)dp / d_total * 100.0;
                if (p.cpu_pct < 0) p.cpu_pct = 0;
            }
        }
        prev_procs_map.clear();
        for (auto& p : cur_procs) prev_procs_map[p.pid] = p.prev_cpu;

        // Filter
        if (!filter_pids.empty())
            cur_procs.erase(std::remove_if(cur_procs.begin(), cur_procs.end(), [&](const ProcInfo& p) {
                for (auto fp : filter_pids) if (p.pid == fp) return false;
                return true;
            }), cur_procs.end());
        if (!filter_user.empty())
            cur_procs.erase(std::remove_if(cur_procs.begin(), cur_procs.end(), [&](const ProcInfo& p) {
                return _stricmp(p.user.c_str(), filter_user.c_str()) != 0;
            }), cur_procs.end());
        if (!show_idle)
            cur_procs.erase(std::remove_if(cur_procs.begin(), cur_procs.end(), [](const ProcInfo& p) {
                return p.cpu_pct < 0.05;
            }), cur_procs.end());

        // Sort
        if (sort_field == "cpu" || sort_field == "%CPU")
            std::sort(cur_procs.begin(), cur_procs.end(), [](const ProcInfo& a, const ProcInfo& b){ return a.cpu_pct > b.cpu_pct; });
        else if (sort_field == "mem" || sort_field == "rss" || sort_field == "RES" || sort_field == "VIRT")
            std::sort(cur_procs.begin(), cur_procs.end(), [](const ProcInfo& a, const ProcInfo& b){ return a.mem_kb > b.mem_kb; });
        else if (sort_field == "pid" || sort_field == "PID")
            std::sort(cur_procs.begin(), cur_procs.end(), [](const ProcInfo& a, const ProcInfo& b){ return a.pid < b.pid; });
        else if (sort_field == "time" || sort_field == "TIME+" || sort_field == "TIME")
            std::sort(cur_procs.begin(), cur_procs.end(), [](const ProcInfo& a, const ProcInfo& b){ return a.cpu_time_ms > b.cpu_time_ms; });
        else if (sort_field == "user" || sort_field == "USER")
            std::sort(cur_procs.begin(), cur_procs.end(), [](const ProcInfo& a, const ProcInfo& b){ return a.user < b.user; });
        else
            std::sort(cur_procs.begin(), cur_procs.end(), [](const ProcInfo& a, const ProcInfo& b){ return a.cpu_pct > b.cpu_pct; });

        if (!batch) printf("\033[2J\033[H"); // clear screen

        print_top(cur_procs, cpu_user_pct, cpu_sys_pct, cpu_idle_pct, con_rows, show_full_cmd);
        fflush(stdout);

        iter++;
        if (max_iter > 0 && iter >= max_iter) break;

        if (!batch) {
            // Sleep in small chunks to stay responsive to keypresses
            DWORD sleep_ms = (DWORD)(delay * 1000);
            DWORD step = 50;
            for (DWORD elapsed = 0; elapsed < sleep_ms; elapsed += step) {
                Sleep(step);
                if (_kbhit()) {
                    int c = _getch();
                    if (c == 'q' || c == 'Q') goto done;
                    if (c == 'c') show_full_cmd = !show_full_cmd;
                    if (c == 'M') sort_field = "mem";
                    if (c == 'P') sort_field = "cpu";
                    if (c == 'T') sort_field = "time";
                    if (c == 'N') sort_field = "pid";
                }
            }
        } else {
            Sleep((DWORD)(delay * 1000));
        }
    }
done:
    if (!batch) printf("\033[?25h\033[0m\n");
    return 0;
}
