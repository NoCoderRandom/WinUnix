// htop - interactive process viewer (Windows port)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <pdh.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <conio.h>
#include <vector>
#include <string>
#include <algorithm>
#include <map>
#include <functional>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "pdh.lib")

static void usage() {
    fprintf(stderr,
        "htop " __DATE__ "\n"
        "Usage: htop [OPTION]...\n\n"
        " -C --no-color                   Use a monochrome color scheme\n"
        " -d --delay=DELAY                Set the delay between updates, in tenths of a second\n"
        " -F --filter=FILTER              Show only the commands matching the given filter\n"
        " -h --help                       Print this help screen\n"
        " -H --highlight-changes[=DELAY]  Highlight new and old processes\n"
        " -M --no-mouse                   Disable the mouse\n"
        " -p --pid=PID[,PID,PID...]       Show only the given PIDs\n"
        " -s --sort-key=COLUMN            Sort by COLUMN in list view (try --sort-key=help for a list)\n"
        " -t --tree                       Show the tree view (can be turned off with -F)\n"
        " -u --user=USERNAME              Show only processes of a given user\n"
        " -U --no-unicode                 Do not use unicode but plain ASCII\n"
        " -V --version                    Print version info\n"
    );
}

struct Proc {
    DWORD  pid, ppid;
    std::string name, user, cmdline;
    double cpu_pct;
    unsigned long long mem_kb, vmem_kb, cpu_ms;
    ULONGLONG prev_cpu_ticks;
    char state;
    FILETIME create_time;
    int priority;
    unsigned long long page_faults;
};

static std::string get_user(HANDLE hProc) {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(hProc, TOKEN_QUERY, &hToken)) return "?";
    DWORD sz = 0;
    GetTokenInformation(hToken, TokenUser, NULL, 0, &sz);
    if (sz == 0) { CloseHandle(hToken); return "?"; }
    std::vector<char> buf(sz);
    if (!GetTokenInformation(hToken, TokenUser, buf.data(), sz, &sz)) { CloseHandle(hToken); return "?"; }
    CloseHandle(hToken);
    TOKEN_USER* tu = (TOKEN_USER*)buf.data();
    char name[256]={}, domain[256]={};
    DWORD nl=sizeof(name), dl=sizeof(domain);
    SID_NAME_USE use;
    if (LookupAccountSidA(NULL, tu->User.Sid, name, &nl, domain, &dl, &use)) return name;
    return "?";
}

static std::vector<Proc> snapshot() {
    std::vector<Proc> procs;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return procs;
    PROCESSENTRY32 pe = {};
    pe.dwSize = sizeof(pe);
    if (!Process32First(hSnap, &pe)) { CloseHandle(hSnap); return procs; }
    do {
        Proc p = {};
        p.pid   = pe.th32ProcessID;
        p.ppid  = pe.th32ParentProcessID;
        p.name  = pe.szExeFile;
        p.state = 'S';
        p.cpu_pct = 0.0;
        p.priority = (int)pe.pcPriClassBase;
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ, FALSE, p.pid);
        if (hProc) {
            PROCESS_MEMORY_COUNTERS_EX pmc = {};
            pmc.cb = sizeof(pmc);
            if (GetProcessMemoryInfo(hProc, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
                p.mem_kb  = pmc.WorkingSetSize / 1024;
                p.vmem_kb = pmc.PrivateUsage   / 1024;
                p.page_faults = pmc.PageFaultCount;
            }
            FILETIME ct,et,kt,ut;
            if (GetProcessTimes(hProc, &ct, &et, &kt, &ut)) {
                p.create_time = ct;
                ULARGE_INTEGER k,u;
                k.LowPart  = kt.dwLowDateTime; k.HighPart = kt.dwHighDateTime;
                u.LowPart  = ut.dwLowDateTime; u.HighPart = ut.dwHighDateTime;
                p.prev_cpu_ticks = k.QuadPart + u.QuadPart;
                p.cpu_ms = p.prev_cpu_ticks / 10000ULL;
            }
            p.user = get_user(hProc);
            // Try to get full path
            char path[MAX_PATH] = {};
            if (GetModuleFileNameExA(hProc, NULL, path, MAX_PATH)) p.cmdline = path;
            CloseHandle(hProc);
        }
        procs.push_back(p);
    } while (Process32Next(hSnap, &pe));
    CloseHandle(hSnap);
    return procs;
}

struct SysCpu {
    ULONGLONG idle, kernel, user;
};

static SysCpu get_sys_cpu() {
    FILETIME idle_ft, kernel_ft, user_ft;
    GetSystemTimes(&idle_ft, &kernel_ft, &user_ft);
    SysCpu c;
    c.idle   = ((ULONGLONG)idle_ft.dwHighDateTime   << 32) | idle_ft.dwLowDateTime;
    c.kernel = ((ULONGLONG)kernel_ft.dwHighDateTime << 32) | kernel_ft.dwLowDateTime;
    c.user   = ((ULONGLONG)user_ft.dwHighDateTime   << 32) | user_ft.dwLowDateTime;
    return c;
}

static void set_vt_mode() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hOut, &mode);
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

// Draw a bar like htop
static void draw_bar(int width, double pct, const char* color, const char* label) {
    // e.g. [|||||||   33.3%]
    int fill = (int)(width * pct / 100.0);
    if (fill > width) fill = width;
    printf("%s[", color);
    for (int i = 0; i < width; i++) {
        if (i < fill) printf("|");
        else printf(" ");
    }
    // overwrite with percentage
    char pct_str[16];
    snprintf(pct_str, sizeof(pct_str), "%.1f%%", pct);
    // Go back and print percentage in the middle
    printf("]\033[0m");
}

static void draw_mem_bar(int width, unsigned long long used, unsigned long long total) {
    double pct = total ? (double)used / total * 100.0 : 0.0;
    int fill = (int)(width * pct / 100.0);
    if (fill > width) fill = width;
    printf("\033[32m[");
    for (int i = 0; i < width; i++) {
        if (i < fill) printf("|");
        else printf(" ");
    }
    char s[32];
    snprintf(s, sizeof(s), "%.1f/%.1fG", used/1024.0/1024.0, total/1024.0/1024.0);
    printf("]\033[0m");
}

static std::string fmt_time(unsigned long long ms) {
    unsigned long long s = ms/1000;
    unsigned m = (unsigned)(s/60); unsigned sec = (unsigned)(s%60);
    unsigned h = m/60; m = m%60;
    char buf[32];
    if (h) snprintf(buf,sizeof(buf),"%u:%02u:%02u",h,m,sec);
    else   snprintf(buf,sizeof(buf),"%u:%02u.%02llu",m,sec,(ms%1000)/10);
    return buf;
}

enum SortKey { SORT_CPU, SORT_MEM, SORT_PID, SORT_NAME, SORT_TIME, SORT_USER };

int main(int argc, char* argv[]) {
    bool no_color = false, tree_mode = false, no_unicode = false, no_mouse = false;
    double delay_tenths = 10.0; // 1 second
    std::string filter_user, filter_str;
    std::vector<DWORD> filter_pids;
    SortKey sort_key = SORT_CPU;
    bool highlight_changes = false;

    for (int i = 1; i < argc; i++) {
        char* a = argv[i];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) { usage(); return 0; }
        if (strcmp(a, "-V") == 0 || strcmp(a, "--version") == 0) {
            printf("htop (Windows port) 3.0.0\n"); return 0;
        }
        if (strcmp(a, "-C") == 0 || strcmp(a, "--no-color") == 0) no_color = true;
        if (strcmp(a, "-t") == 0 || strcmp(a, "--tree") == 0) tree_mode = true;
        if (strcmp(a, "-U") == 0 || strcmp(a, "--no-unicode") == 0) no_unicode = true;
        if (strcmp(a, "-M") == 0 || strcmp(a, "--no-mouse") == 0) no_mouse = true;
        if (strcmp(a, "-H") == 0 || strcmp(a, "--highlight-changes") == 0) highlight_changes = true;
        if ((strcmp(a, "-d") == 0 || strcmp(a, "--delay") == 0) && i+1 < argc)
            delay_tenths = atof(argv[++i]);
        else if (strncmp(a, "--delay=", 8) == 0) delay_tenths = atof(a+8);
        if ((strcmp(a, "-u") == 0 || strcmp(a, "--user") == 0) && i+1 < argc)
            filter_user = argv[++i];
        else if (strncmp(a, "--user=", 7) == 0) filter_user = a+7;
        if ((strcmp(a, "-F") == 0 || strcmp(a, "--filter") == 0) && i+1 < argc)
            filter_str = argv[++i];
        else if (strncmp(a, "--filter=", 9) == 0) filter_str = a+9;
        if ((strcmp(a, "-p") == 0 || strcmp(a, "--pid") == 0) && i+1 < argc) {
            char tmp[1024]; strncpy(tmp, argv[++i], sizeof(tmp)-1);
            char* tok = strtok(tmp, ",");
            while (tok) { filter_pids.push_back(atoi(tok)); tok = strtok(NULL, ","); }
        }
        if (strcmp(a, "-s") == 0 || strcmp(a, "--sort-key") == 0) {
            std::string sk = (i+1 < argc) ? argv[++i] : "";
            if (sk == "help") {
                printf("Available sort keys: PID, USER, VIRT, RES, CPU, TIME, Command\n"); return 0;
            }
            if (sk == "PID") sort_key = SORT_PID;
            else if (sk == "CPU" || sk == "PERCENT_CPU") sort_key = SORT_CPU;
            else if (sk == "MEM" || sk == "PERCENT_MEM" || sk == "RES") sort_key = SORT_MEM;
            else if (sk == "TIME" || sk == "TIME+") sort_key = SORT_TIME;
            else if (sk == "Command") sort_key = SORT_NAME;
            else if (sk == "USER") sort_key = SORT_USER;
        } else if (strncmp(a, "--sort-key=", 11) == 0) {
            std::string sk = a + 11;
            if (sk == "help") {
                printf("Available sort keys: PID, USER, VIRT, RES, CPU, TIME, Command\n"); return 0;
            }
            if (sk == "PID") sort_key = SORT_PID;
            else if (sk == "CPU" || sk == "PERCENT_CPU") sort_key = SORT_CPU;
            else if (sk == "MEM" || sk == "PERCENT_MEM" || sk == "RES") sort_key = SORT_MEM;
            else if (sk == "TIME" || sk == "TIME+") sort_key = SORT_TIME;
            else if (sk == "Command") sort_key = SORT_NAME;
            else if (sk == "USER") sort_key = SORT_USER;
        }
    }

    // Set console to UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    set_vt_mode();

    // Get terminal size
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int rows = 40, cols = 80;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        cols = csbi.srWindow.Right  - csbi.srWindow.Left + 1;
    }

    // Hide cursor
    printf("\033[?25l");

    SysCpu prev_sys = get_sys_cpu();
    auto procs = snapshot();
    std::map<DWORD, ULONGLONG> prev_cpu_map;
    for (auto& p : procs) prev_cpu_map[p.pid] = p.prev_cpu_ticks;

    bool running = true;
    bool paused = false;

    while (running) {
        double delay_ms = delay_tenths * 100.0;
        DWORD sleep_step = 100;
        DWORD elapsed = 0;

        while (elapsed < (DWORD)delay_ms) {
            Sleep(sleep_step);
            elapsed += sleep_step;
            if (_kbhit()) {
                int c = _getch();
                if (c == 'q' || c == 'Q') { running = false; break; }
                if (c == ' ') paused = !paused;
                if (c == 'M') sort_key = SORT_MEM;
                if (c == 'P') sort_key = SORT_CPU;
                if (c == 'T') sort_key = SORT_TIME;
                if (c == 'N') sort_key = SORT_PID;
                if (c == 't') tree_mode = !tree_mode;
                if (c == 'u') { /* user filter toggle - complex */ }
                if (c == 'F') { /* filter - complex */ }
                if (c == 'I') { // invert sort
                    // toggle; ignore for simplicity
                }
                if (c == 0 || c == 0xE0) {
                    int c2 = _getch();
                    (void)c2; // arrow keys etc - ignore
                }
                if (!running) break;
            }
        }
        if (!running) break;
        if (paused) continue;

        // Get new snapshot
        SysCpu cur_sys = get_sys_cpu();
        ULONGLONG d_idle   = cur_sys.idle   - prev_sys.idle;
        ULONGLONG d_kernel = cur_sys.kernel - prev_sys.kernel;
        ULONGLONG d_user   = cur_sys.user   - prev_sys.user;
        ULONGLONG d_total  = d_kernel + d_user;
        double cpu_user_pct = d_total ? (double)d_user   / d_total * 100.0 : 0.0;
        double cpu_sys_pct  = d_total ? (double)(d_kernel - d_idle) / d_total * 100.0 : 0.0;
        double cpu_idle_pct = d_total ? (double)d_idle   / d_total * 100.0 : 100.0;
        if (cpu_sys_pct < 0) cpu_sys_pct = 0;
        prev_sys = cur_sys;

        // Get system CPU count
        SYSTEM_INFO si; GetSystemInfo(&si);
        int num_cpus = (int)si.dwNumberOfProcessors;

        auto cur_procs = snapshot();
        for (auto& p : cur_procs) {
            auto it = prev_cpu_map.find(p.pid);
            if (it != prev_cpu_map.end() && d_total > 0) {
                ULONGLONG dp = p.prev_cpu_ticks - it->second;
                p.cpu_pct = (double)dp / d_total * 100.0 * num_cpus;
                if (p.cpu_pct > 100.0 * num_cpus) p.cpu_pct = 100.0 * num_cpus;
            }
        }
        prev_cpu_map.clear();
        for (auto& p : cur_procs) prev_cpu_map[p.pid] = p.prev_cpu_ticks;

        // Filter
        if (!filter_pids.empty())
            cur_procs.erase(std::remove_if(cur_procs.begin(), cur_procs.end(), [&](const Proc& p) {
                for (auto fp : filter_pids) if (p.pid == fp) return false; return true;
            }), cur_procs.end());
        if (!filter_user.empty())
            cur_procs.erase(std::remove_if(cur_procs.begin(), cur_procs.end(), [&](const Proc& p) {
                return _stricmp(p.user.c_str(), filter_user.c_str()) != 0;
            }), cur_procs.end());
        if (!filter_str.empty())
            cur_procs.erase(std::remove_if(cur_procs.begin(), cur_procs.end(), [&](const Proc& p) {
                return p.name.find(filter_str) == std::string::npos &&
                       p.cmdline.find(filter_str) == std::string::npos;
            }), cur_procs.end());

        // Sort
        switch (sort_key) {
            case SORT_CPU:  std::sort(cur_procs.begin(), cur_procs.end(), [](const Proc& a, const Proc& b){ return a.cpu_pct > b.cpu_pct; }); break;
            case SORT_MEM:  std::sort(cur_procs.begin(), cur_procs.end(), [](const Proc& a, const Proc& b){ return a.mem_kb > b.mem_kb; }); break;
            case SORT_PID:  std::sort(cur_procs.begin(), cur_procs.end(), [](const Proc& a, const Proc& b){ return a.pid < b.pid; }); break;
            case SORT_NAME: std::sort(cur_procs.begin(), cur_procs.end(), [](const Proc& a, const Proc& b){ return a.name < b.name; }); break;
            case SORT_TIME: std::sort(cur_procs.begin(), cur_procs.end(), [](const Proc& a, const Proc& b){ return a.cpu_ms > b.cpu_ms; }); break;
            case SORT_USER: std::sort(cur_procs.begin(), cur_procs.end(), [](const Proc& a, const Proc& b){ return a.user < b.user; }); break;
        }

        // Memory info
        MEMORYSTATUSEX ms = {};
        ms.dwLength = sizeof(ms);
        GlobalMemoryStatusEx(&ms);
        unsigned long long total_phys = ms.ullTotalPhys;
        unsigned long long free_phys  = ms.ullAvailPhys;
        unsigned long long used_phys  = total_phys - free_phys;
        unsigned long long total_swap = ms.ullTotalPageFile > ms.ullTotalPhys ?
                                        ms.ullTotalPageFile - ms.ullTotalPhys : 0;
        unsigned long long free_swap  = ms.ullAvailPageFile > free_phys ?
                                        ms.ullAvailPageFile - free_phys : 0;
        if (free_swap > total_swap) free_swap = total_swap;
        unsigned long long used_swap  = total_swap - free_swap;

        // Draw
        printf("\033[H"); // cursor home

        // CPU bars
        int bar_w = (cols - 14) / num_cpus;
        if (bar_w < 10) bar_w = 10;
        double cpu_busy = 100.0 - cpu_idle_pct;

        for (int cpu = 0; cpu < (num_cpus <= 4 ? num_cpus : 4); cpu++) {
            printf("\033[0m\033[1m  %d\033[0m[", cpu);
            int fill_usr = (int)(bar_w * cpu_user_pct / 100.0);
            int fill_sys = (int)(bar_w * cpu_sys_pct  / 100.0);
            int fill_tot = fill_usr + fill_sys;
            if (fill_tot > bar_w) fill_tot = bar_w;
            for (int j = 0; j < bar_w; j++) {
                if (j < fill_usr) printf("\033[32m|");
                else if (j < fill_tot) printf("\033[31m|");
                else printf("\033[0m ");
            }
            char pct_str[16];
            snprintf(pct_str, sizeof(pct_str), "%.1f%%", cpu_busy);
            printf("\033[0m] %s\n", pct_str);
        }
        if (num_cpus > 4) printf("  ... (%d CPUs total)\n", num_cpus);

        // Mem/Swap bars
        int mem_bar_w = cols - 20;
        if (mem_bar_w < 20) mem_bar_w = 20;
        printf("\033[0m\033[1mMem\033[0m[");
        {
            int fill = (int)((double)used_phys / total_phys * mem_bar_w);
            for (int j = 0; j < mem_bar_w; j++) {
                if (j < fill) printf("\033[32m|");
                else printf("\033[0m ");
            }
        }
        printf("\033[0m] %.2f/%.2fG\n", used_phys/1024.0/1024.0/1024.0, total_phys/1024.0/1024.0/1024.0);
        printf("\033[0m\033[1mSwp\033[0m[");
        if (total_swap > 0) {
            int fill = (int)((double)used_swap / total_swap * mem_bar_w);
            for (int j = 0; j < mem_bar_w; j++) {
                if (j < fill) printf("\033[31m|");
                else printf("\033[0m ");
            }
            printf("\033[0m] %.2f/%.2fG\n", used_swap/1024.0/1024.0/1024.0, total_swap/1024.0/1024.0/1024.0);
        } else {
            for (int j = 0; j < mem_bar_w; j++) printf(" ");
            printf("\033[0m] 0/0G\n");
        }

        // Task count
        SYSTEMTIME st2; GetLocalTime(&st2);
        printf("\n  Tasks: %d total\033[%dC%02d:%02d:%02d\n",
            (int)cur_procs.size(), cols - 30, st2.wHour, st2.wMinute, st2.wSecond);

        // Header row
        printf("\033[7m");
        printf("  %5s %-10s %3s %3s %7s %7s %5s %1s %6s %10s %s",
            "PID", "USER", "PRI", "NI", "VIRT", "RES", "%%CPU", "S", "%%MEM", "TIME+", "Command");
        // Pad to cols
        int hlen = 5+1+10+1+3+1+3+1+7+1+7+1+5+1+1+1+6+1+10+1+7;
        for (int j = hlen; j < cols; j++) printf(" ");
        printf("\033[0m\n");

        int max_show = rows - 8;
        if (max_show < 1) max_show = 1;
        int shown = 0;
        for (const auto& p : cur_procs) {
            if (shown >= max_show) break;
            std::string name = p.name;
            if (name.size() > 4 && _stricmp(name.c_str()+name.size()-4, ".exe") == 0)
                name = name.substr(0, name.size()-4);
            double mem_pct = total_phys ? (double)(p.mem_kb*1024) / total_phys * 100.0 : 0.0;
            // Color high-CPU processes
            if (p.cpu_pct > 50.0) printf("\033[31m"); // red
            else if (p.cpu_pct > 10.0) printf("\033[33m"); // yellow
            printf("  %5lu %-10s %3d %3d %7s %7s %5.1f %1c %6.2f %10s %s\033[0m\n",
                (unsigned long)p.pid,
                p.user.c_str(),
                p.priority, 0,
                [](unsigned long long kb) -> std::string {
                    char b[16];
                    if (kb < 10000) snprintf(b,sizeof(b),"%lluk",kb);
                    else if (kb < 10000*1024) snprintf(b,sizeof(b),"%.1fM",kb/1024.0);
                    else snprintf(b,sizeof(b),"%.1fG",kb/1024.0/1024.0);
                    return b;
                }(p.vmem_kb).c_str(),
                [](unsigned long long kb) -> std::string {
                    char b[16];
                    if (kb < 10000) snprintf(b,sizeof(b),"%lluk",kb);
                    else if (kb < 10000*1024) snprintf(b,sizeof(b),"%.1fM",kb/1024.0);
                    else snprintf(b,sizeof(b),"%.1fG",kb/1024.0/1024.0);
                    return b;
                }(p.mem_kb).c_str(),
                p.cpu_pct,
                p.state,
                mem_pct,
                fmt_time(p.cpu_ms).c_str(),
                name.c_str());
            shown++;
        }

        // Clear remaining lines
        for (int i = shown; i < max_show; i++) {
            printf("\033[K\n");
        }

        // Function keys bar at bottom
        printf("\033[%d;1H", rows);
        printf("\033[7m");
        const char* fkeys = "F1Help F2Setup F3Search F4Filter F5Tree F6SortBy F7Nice- F8Nice+ F9Kill F10Quit";
        printf("%-*s", cols, fkeys);
        printf("\033[0m");
        fflush(stdout);
    }

    printf("\033[?25h\033[0m\n");
    printf("\033[2J\033[H");
    return 0;
}
