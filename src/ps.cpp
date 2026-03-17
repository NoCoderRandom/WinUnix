// ps - report a snapshot of the current processes (Windows port)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <algorithm>
#include <map>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")

static void usage() {
    fprintf(stderr,
        "Usage:\n"
        " ps [options]\n\n"
        "Basic options:\n"
        " -A, -e               all processes\n"
        " -a                   all with tty, except session leaders\n"
        "  a                   all with tty, including other users\n"
        " -d                   all except session leaders\n"
        "  r                   only running processes\n"
        "  T                   all processes on this terminal\n"
        "  x                   processes without controlling ttys\n\n"
        "Selection by list:\n"
        " -C <command>         command name\n"
        " -G, --Group <GID>    real group id or name\n"
        " -p, p, --pid <PID>   process id\n"
        "     --ppid <PID>     parent process id\n"
        " -q, q, --quick-pid <PID>\n"
        "                      process id (quick mode)\n"
        " -s, --sid <SID>      session id\n"
        " -t, t, --tty <TTY>   terminal\n"
        " -u, U, --user <UID>  effective user id or name\n"
        " -U, --User <UID>     real user id or name\n\n"
        "Output formats:\n"
        " -F                   extra full\n"
        " -f                   full-format, including command lines\n"
        "  f, --forest         ascii art process tree\n"
        " -H                   show process hierarchy\n"
        " -j                   jobs format\n"
        "  j                   BSD job control format\n"
        " -l                   long format\n"
        "  l                   BSD long format\n"
        " -o, o, --format <format>\n"
        "                      user-defined format\n"
        "  s                   signal format\n"
        "  u                   user-oriented format\n"
        "  v                   virtual memory format\n\n"
        "  --sort <key>        sort by key\n"
        "  --headers           repeat header lines\n"
        "  --no-headers        do not print header\n"
        "  --cols, --columns, --width <num>\n"
        "                      set screen width\n"
        "  --rows, --lines <num>\n"
        "                      set screen height\n"
    );
}

struct ProcInfo {
    DWORD  pid;
    DWORD  ppid;
    DWORD  tid_count;
    std::string name;
    std::string cmdline;
    std::string user;
    unsigned long long cpu_time_ms; // total cpu time ms
    unsigned long long mem_kb;      // working set KB
    unsigned long long vmem_kb;     // virtual mem KB
    DWORD  priority;
    char   state;
    FILETIME create_time;
    double cpu_pct;  // lifetime %CPU like Linux ps
};

static std::string get_process_user(HANDLE hProc) {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(hProc, TOKEN_QUERY, &hToken)) return "?";
    DWORD sz = 0;
    GetTokenInformation(hToken, TokenUser, NULL, 0, &sz);
    std::vector<char> buf(sz);
    if (!GetTokenInformation(hToken, TokenUser, buf.data(), sz, &sz)) {
        CloseHandle(hToken); return "?";
    }
    CloseHandle(hToken);
    TOKEN_USER* tu = (TOKEN_USER*)buf.data();
    char name[256]={}, domain[256]={};
    DWORD nlen=sizeof(name), dlen=sizeof(domain);
    SID_NAME_USE use;
    if (LookupAccountSidA(NULL, tu->User.Sid, name, &nlen, domain, &dlen, &use))
        return name;
    return "?";
}

static std::string get_cmdline(DWORD pid) {
    // Try to read via NtQueryProcessInformation or just use snapshot name
    // We'll use a simpler approach via WMI is complex, just return exe path for now
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProc) return "";
    char path[MAX_PATH] = {};
    GetModuleFileNameExA(hProc, NULL, path, MAX_PATH);
    CloseHandle(hProc);
    return path;
}

static std::vector<ProcInfo> get_processes() {
    std::vector<ProcInfo> procs;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return procs;

    PROCESSENTRY32 pe = {};
    pe.dwSize = sizeof(pe);
    if (!Process32First(hSnap, &pe)) { CloseHandle(hSnap); return procs; }

    do {
        ProcInfo p;
        p.pid = pe.th32ProcessID;
        p.ppid = pe.th32ParentProcessID;
        p.tid_count = pe.cntThreads;
        p.name = pe.szExeFile;
        p.priority = pe.pcPriClassBase;
        p.state = 'S';
        p.cpu_time_ms = 0;
        p.mem_kb = 0;
        p.vmem_kb = 0;
        p.cpu_pct = 0.0;
        p.user = "?";

        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, p.pid);
        if (hProc) {
            PROCESS_MEMORY_COUNTERS_EX pmc = {};
            pmc.cb = sizeof(pmc);
            if (GetProcessMemoryInfo(hProc, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
                p.mem_kb  = pmc.WorkingSetSize / 1024;
                p.vmem_kb = pmc.PrivateUsage    / 1024;
            }
            FILETIME ct,et,kt,ut;
            if (GetProcessTimes(hProc, &ct, &et, &kt, &ut)) {
                p.create_time = ct;
                ULARGE_INTEGER k,u2;
                k.LowPart  = kt.dwLowDateTime;  k.HighPart  = kt.dwHighDateTime;
                u2.LowPart = ut.dwLowDateTime;  u2.HighPart = ut.dwHighDateTime;
                p.cpu_time_ms = (k.QuadPart + u2.QuadPart) / 10000ULL;
                // Compute lifetime %CPU: cpu_ms / elapsed_ms * 100
                // (same formula Linux ps uses: pcpu = cpu_time / elapsed_time)
                FILETIME now_ft;
                GetSystemTimeAsFileTime(&now_ft);
                ULARGE_INTEGER now_ui, start_ui;
                now_ui.LowPart   = now_ft.dwLowDateTime;   now_ui.HighPart   = now_ft.dwHighDateTime;
                start_ui.LowPart = ct.dwLowDateTime;        start_ui.HighPart = ct.dwHighDateTime;
                ULONGLONG elapsed_100ns = now_ui.QuadPart - start_ui.QuadPart;
                if (elapsed_100ns > 0) {
                    p.cpu_pct = (double)(k.QuadPart + u2.QuadPart) / (double)elapsed_100ns * 100.0;
                    if (p.cpu_pct > 99.9) p.cpu_pct = 99.9;
                }
            }
            p.user = get_process_user(hProc);
            CloseHandle(hProc);
        }
        procs.push_back(p);
    } while (Process32Next(hSnap, &pe));

    CloseHandle(hSnap);
    return procs;
}

static std::string format_time(unsigned long long ms) {
    unsigned long long secs = ms / 1000;
    unsigned int h = (unsigned int)(secs / 3600);
    unsigned int m = (unsigned int)((secs % 3600) / 60);
    unsigned int s = (unsigned int)(secs % 60);
    char buf[32];
    if (h > 0) snprintf(buf, sizeof(buf), "%u:%02u:%02u", h, m, s);
    else snprintf(buf, sizeof(buf), "%u:%02u", m, s);
    return buf;
}

static std::string format_start(const FILETIME& ft) {
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);
    SYSTEMTIME now;
    GetLocalTime(&now);
    char buf[32];
    if (st.wYear != now.wYear || st.wMonth != now.wMonth || st.wDay != now.wDay)
        snprintf(buf, sizeof(buf), "%02u/%02u", st.wMonth, st.wDay);
    else
        snprintf(buf, sizeof(buf), "%02u:%02u", st.wHour, st.wMinute);
    return buf;
}

int main(int argc, char* argv[]) {
    bool show_all = false;
    bool full_format = false;
    bool extra_full = false;
    bool long_format = false;
    bool user_format = false;
    bool no_header = false;
    std::string sort_key;
    std::vector<DWORD> filter_pids;
    std::vector<std::string> filter_users;
    std::vector<std::string> filter_cmds;
    std::string custom_format;

    // BSD-style no-dash args and POSIX-style
    for (int i = 1; i < argc; i++) {
        char* a = argv[i];
        if (strcmp(a, "--help") == 0) { usage(); return 0; }
        else if (strcmp(a, "-e") == 0 || strcmp(a, "-A") == 0) show_all = true;
        else if (strcmp(a, "-f") == 0) full_format = true;
        else if (strcmp(a, "-F") == 0) { full_format = true; extra_full = true; }
        else if (strcmp(a, "-l") == 0) long_format = true;
        else if (strcmp(a, "u") == 0 || strcmp(a, "-u") == 0) {
            // Check if next arg is a user name/UID
            if (i+1 < argc && argv[i+1][0] != '-') {
                filter_users.push_back(argv[++i]);
            } else {
                user_format = true;
                show_all = true;
            }
        }
        else if (strcmp(a, "a") == 0 || strcmp(a, "-a") == 0) show_all = true;
        else if (strcmp(a, "x") == 0) show_all = true;
        else if (strcmp(a, "aux") == 0 || strcmp(a, "axu") == 0 || strcmp(a, "ax") == 0) {
            show_all = true; user_format = true;
        }
        else if ((strcmp(a, "-p") == 0 || strcmp(a, "p") == 0 || strcmp(a, "--pid") == 0) && i+1 < argc)
            filter_pids.push_back((DWORD)atoi(argv[++i]));
        else if ((strcmp(a, "-C") == 0) && i+1 < argc)
            filter_cmds.push_back(argv[++i]);
        else if (strcmp(a, "--no-headers") == 0) no_header = true;
        else if (strcmp(a, "--headers") == 0) no_header = false;
        else if ((strcmp(a, "--sort") == 0) && i+1 < argc) sort_key = argv[++i];
        else if (strncmp(a, "--sort=", 7) == 0) sort_key = a+7;
        else if ((strcmp(a, "-o") == 0 || strcmp(a, "--format") == 0 || strcmp(a, "o") == 0) && i+1 < argc)
            custom_format = argv[++i];
        else if (strncmp(a, "-o", 2) == 0 && strlen(a) > 2) custom_format = a+2;
        // ignore unknown options gracefully
        else if (a[0] == '-') {
            // check for combined flags like -ef, -aux etc
            bool known = true;
            for (int j = 1; a[j] && known; j++) {
                switch(a[j]) {
                    case 'e': case 'A': show_all = true; break;
                    case 'f': full_format = true; break;
                    case 'F': full_format = extra_full = true; break;
                    case 'l': long_format = true; break;
                    case 'a': show_all = true; break;
                    case 'H': break; // hierarchy - ignore for now
                    case 'j': break;
                    case 'r': break;
                    case 'w': break;
                    default: known = false;
                }
            }
            if (!known) {
                // silently ignore unknown to be compatible
            }
        } else {
            // BSD-style combined no-dash
            bool ok = true;
            for (int j = 0; a[j]; j++) {
                switch(a[j]) {
                    case 'a': case 'x': show_all = true; break;
                    case 'u': user_format = true; show_all = true; break;
                    case 'f': full_format = true; break;
                    case 'l': long_format = true; break;
                    case 'e': show_all = true; break;
                    default: ok = false;
                }
            }
        }
    }

    auto procs = get_processes();

    // Filter
    std::vector<ProcInfo*> shown;
    DWORD my_pid = GetCurrentProcessId();
    for (auto& p : procs) {
        if (!show_all && filter_pids.empty() && filter_cmds.empty() && filter_users.empty()) {
            // default: show only current user's processes
            // We still show all for simplicity (Windows doesn't have TTY concept)
        }
        if (!filter_pids.empty()) {
            bool found = false;
            for (auto fp : filter_pids) if (p.pid == fp) found = true;
            if (!found) continue;
        }
        if (!filter_cmds.empty()) {
            bool found = false;
            for (auto& fc : filter_cmds) {
                std::string n = p.name;
                // remove .exe
                if (n.size() > 4 && _stricmp(n.c_str()+n.size()-4, ".exe") == 0)
                    n = n.substr(0, n.size()-4);
                if (_stricmp(n.c_str(), fc.c_str()) == 0) found = true;
            }
            if (!found) continue;
        }
        if (!filter_users.empty()) {
            bool found = false;
            for (auto& fu : filter_users)
                if (_stricmp(p.user.c_str(), fu.c_str()) == 0) found = true;
            if (!found) continue;
        }
        shown.push_back(&p);
    }

    // Sort
    if (!sort_key.empty()) {
        std::string key = sort_key;
        bool reverse = false;
        if (!key.empty() && (key[0] == '+' || key[0] == '-')) {
            reverse = (key[0] == '-');
            key = key.substr(1);
        }
        auto cmp = [&](ProcInfo* a, ProcInfo* b) {
            bool less;
            if (key == "pid" || key == "PID") less = a->pid < b->pid;
            else if (key == "ppid") less = a->ppid < b->ppid;
            else if (key == "comm" || key == "name") less = a->name < b->name;
            else if (key == "rss") less = a->mem_kb < b->mem_kb;
            else if (key == "vsz") less = a->vmem_kb < b->vmem_kb;
            else if (key == "time" || key == "cputime") less = a->cpu_time_ms < b->cpu_time_ms;
            else if (key == "uid" || key == "user") less = a->user < b->user;
            else less = a->pid < b->pid;
            return reverse ? !less : less;
        };
        std::sort(shown.begin(), shown.end(), cmp);
    }

    if (user_format) {
        if (!no_header)
            printf("%-12s %6s %5s %5s %8s %8s %4s %4s %-8s %8s %s\n",
                "USER", "PID", "%CPU", "%MEM", "VSZ", "RSS", "TTY", "STAT", "START", "TIME", "COMMAND");
        for (auto* p : shown) {
            // %MEM = RSS / total_phys * 100
            MEMORYSTATUSEX ms = {}; ms.dwLength = sizeof(ms);
            GlobalMemoryStatusEx(&ms);
            double mem_pct = ms.ullTotalPhys ? (double)(p->mem_kb * 1024) / ms.ullTotalPhys * 100.0 : 0.0;
            printf("%-12s %6lu %5.1f %5.1f %8llu %8llu %4s %4c %-8s %8s %s\n",
                p->user.c_str(), (unsigned long)p->pid,
                p->cpu_pct, mem_pct,
                p->vmem_kb, p->mem_kb,
                "?", p->state,
                format_start(p->create_time).c_str(),
                format_time(p->cpu_time_ms).c_str(),
                p->name.c_str());
        }
    } else if (full_format) {
        if (!no_header)
            printf("%-12s %6s %6s %6s %4s %8s %4s %-8s %8s %s\n",
                "UID", "PID", "PPID", "C", "PRI", "SZ", "TTY", "TIME", "START", "CMD");
        for (auto* p : shown) {
            printf("%-12s %6lu %6lu %6d %4lu %8llu %4s %-8s %8s %s\n",
                p->user.c_str(), (unsigned long)p->pid, (unsigned long)p->ppid, 0,
                (unsigned long)p->priority, p->vmem_kb,
                "?", format_time(p->cpu_time_ms).c_str(),
                format_start(p->create_time).c_str(),
                p->name.c_str());
        }
    } else if (long_format) {
        if (!no_header)
            printf("%1s %6s %-12s %6s %6s %4s %4s %8s %8s %1s %8s %s\n",
                "F", "PID", "USER", "PPID", "PRI", "NI", "SZ", "RSS", "WCHAN", "S", "TIME", "CMD");
        for (auto* p : shown) {
            printf("%1c %6lu %-12s %6lu %6lu %4d %4d %8llu %8llu %1c %8s %s\n",
                '4', (unsigned long)p->pid, p->user.c_str(), (unsigned long)p->ppid,
                (long)p->priority, 0, 0,
                p->vmem_kb, p->mem_kb,
                p->state,
                format_time(p->cpu_time_ms).c_str(),
                p->name.c_str());
        }
    } else {
        // Default: PID TTY TIME CMD
        if (!no_header)
            printf("%6s %-6s %8s %s\n", "PID", "TTY", "TIME", "CMD");
        for (auto* p : shown) {
            printf("%6lu %-6s %8s %s\n",
                (unsigned long)p->pid, "?",
                format_time(p->cpu_time_ms).c_str(),
                p->name.c_str());
        }
    }
    return 0;
}
