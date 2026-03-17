// pstree - display a tree of processes (Windows port)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <algorithm>
#include <functional>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")

static void usage() {
    fprintf(stderr,
        "Usage: pstree [-acGhlnpsSuUZ] [ -H PID ] [ -T ] [ PID | USER ]\n"
        "   or: pstree -V\n\n"
        " -a     show command line arguments\n"
        " -A     use ASCII line drawing characters\n"
        " -c     don't compact identical subtrees\n"
        " -h     highlight current process and its ancestors\n"
        " -H PID highlight this process and its ancestors\n"
        " -l     don't truncate long lines\n"
        " -n     sort output by process ID\n"
        " -p     show PIDs; implies -c\n"
        " -s     show parent process of specified process\n"
        " -T     hide threads\n"
        " -u     show uid transitions\n"
        " -U     use UTF-8 (Unicode) line drawing characters\n"
        " -V     display version information\n"
    );
}

struct Proc {
    DWORD pid, ppid;
    std::string name, user;
};

static std::string get_user(DWORD pid) {
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return "";
    HANDLE hToken = NULL;
    if (!OpenProcessToken(hProc, TOKEN_QUERY, &hToken)) { CloseHandle(hProc); return ""; }
    CloseHandle(hProc);
    DWORD sz = 0;
    GetTokenInformation(hToken, TokenUser, NULL, 0, &sz);
    if (!sz) { CloseHandle(hToken); return ""; }
    std::vector<char> buf(sz);
    if (!GetTokenInformation(hToken, TokenUser, buf.data(), sz, &sz)) { CloseHandle(hToken); return ""; }
    CloseHandle(hToken);
    TOKEN_USER* tu = (TOKEN_USER*)buf.data();
    char name[256]={}, domain[256]={};
    DWORD nl=sizeof(name), dl=sizeof(domain);
    SID_NAME_USE use;
    if (LookupAccountSidA(NULL, tu->User.Sid, name, &nl, domain, &dl, &use)) return name;
    return "";
}

int main(int argc, char* argv[]) {
    bool show_pids    = false;
    bool sort_by_pid  = false;
    bool use_ascii    = false;
    bool show_uid     = false;
    bool compact      = true;   // -c disables compaction
    DWORD highlight_pid = 0;
    DWORD root_pid    = 0;
    std::string root_user;

    for (int i = 1; i < argc; i++) {
        char* a = argv[i];
        if (strcmp(a, "--help") == 0) { usage(); return 0; }
        if (strcmp(a, "-V") == 0 || strcmp(a, "--version") == 0) {
            printf("pstree (Windows port) v1.0\n"); return 0;
        }
        if (a[0] == '-') {
            for (int j = 1; a[j]; j++) {
                switch(a[j]) {
                    case 'p': show_pids = true; compact = false; break;
                    case 'n': sort_by_pid = true; break;
                    case 'A': use_ascii = true; break;
                    case 'U': use_ascii = false; break;
                    case 'u': show_uid = true; break;
                    case 'h': highlight_pid = GetCurrentProcessId(); break;
                    case 'H':
                        if (a[j+1]) { highlight_pid = (DWORD)atoi(a+j+1); j=(int)strlen(a)-1; }
                        else if (i+1 < argc) highlight_pid = (DWORD)atoi(argv[++i]);
                        break;
                    case 'c': compact = false; break;
                    case 'a': case 'l': case 'T':
                    case 's': case 'S': case 'Z': case 'N':
                    case 'C': case 'G': case 'g': case 't':
                        break; // accepted, ignored
                }
            }
        } else {
            if (isdigit((unsigned char)a[0])) root_pid = (DWORD)atoi(a);
            else root_user = a;
        }
    }

    // Collect processes
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "pstree: cannot list processes\n"); return 1;
    }

    std::map<DWORD, Proc> procs;
    PROCESSENTRY32 pe = {};
    pe.dwSize = sizeof(pe);
    if (Process32First(hSnap, &pe)) {
        do {
            Proc p;
            p.pid  = pe.th32ProcessID;
            p.ppid = pe.th32ParentProcessID;
            p.name = pe.szExeFile;
            // Remove .exe
            if (p.name.size() > 4 &&
                _stricmp(p.name.c_str() + p.name.size() - 4, ".exe") == 0)
                p.name = p.name.substr(0, p.name.size() - 4);
            if (show_uid) p.user = get_user(p.pid);
            procs[p.pid] = p;
        } while (Process32Next(hSnap, &pe));
    }
    CloseHandle(hSnap);

    // Build children map
    std::map<DWORD, std::vector<DWORD>> children;
    std::set<DWORD> has_parent;
    for (auto& kv : procs) {
        DWORD pid  = kv.first;
        DWORD ppid = kv.second.ppid;
        if (ppid != pid && procs.count(ppid)) {
            children[ppid].push_back(pid);
            has_parent.insert(pid);
        }
    }

    // Sort children lists
    for (auto& kv : children) {
        auto& vec = kv.second;
        if (sort_by_pid)
            std::sort(vec.begin(), vec.end());
        else
            std::sort(vec.begin(), vec.end(), [&](DWORD a, DWORD b) {
                return procs[a].name < procs[b].name;
            });
    }

    // Line drawing characters
    const char* branch_mid = use_ascii ? "|-" : "\xe2\x94\x9c\xe2\x94\x80";
    const char* branch_end = use_ascii ? "`-" : "\xe2\x94\x94\xe2\x94\x80";
    const char* vert_bar   = use_ascii ? "|"  : "\xe2\x94\x82";

    // Enable VT output for color highlights
    if (highlight_pid) {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode))
            SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    // Recursive tree printer
    // own_prefix:   prefix printed on the same line as this node's label
    // child_prefix: prefix for this node's children's lines
    std::function<void(DWORD, const std::string&, const std::string&)> ptree =
        [&](DWORD pid, const std::string& own_prefix, const std::string& child_prefix) {
            auto it = procs.find(pid);
            if (it == procs.end()) return;
            const Proc& p = it->second;

            std::string label = p.name;
            if (show_uid && !p.user.empty())
                label += "(" + p.user + ")";
            if (show_pids)
                label += "(" + std::to_string(p.pid) + ")";

            bool do_hl = (highlight_pid != 0 && pid == highlight_pid);
            if (do_hl) printf("\033[1;31m");
            printf("%s%s\n", own_prefix.c_str(), label.c_str());
            if (do_hl) printf("\033[0m");

            auto cit = children.find(pid);
            if (cit == children.end()) return;
            const auto& kids = cit->second;

            // Compact duplicate leaf names: group consecutive same-name leafs
            // as "3*[name]" like Linux pstree does (when -c not specified)
            // We'll process kids with compaction
            std::vector<std::pair<std::string,std::vector<DWORD>>> groups; // (name, pids)
            for (DWORD kid : kids) {
                auto kit = procs.find(kid);
                if (kit == procs.end()) continue;
                const std::string& kname = kit->second.name;
                bool is_leaf = (children.find(kid) == children.end() || children.at(kid).empty());
                // Only compact if: leaf node, compact mode on, and no pid/uid display
                bool do_compact = is_leaf && compact && !show_pids && !show_uid;
                if (do_compact) {
                    if (!groups.empty() && groups.back().first == kname)
                        groups.back().second.push_back(kid);
                    else
                        groups.push_back({kname, {kid}});
                } else {
                    groups.push_back({kname, {kid}});
                }
            }

            size_t ng = groups.size();
            for (size_t gi = 0; gi < ng; gi++) {
                bool last = (gi == ng - 1);
                std::string kid_own   = child_prefix + (last ? branch_end : branch_mid);
                std::string kid_child = child_prefix + (last ? "  " : (std::string(vert_bar) + " "));
                auto& grp = groups[gi];
                if (grp.second.size() > 1) {
                    // Print as N*[name]
                    bool do_hl2 = false;
                    for (DWORD kpid : grp.second)
                        if (highlight_pid && kpid == highlight_pid) do_hl2 = true;
                    if (do_hl2) printf("\033[1;31m");
                    printf("%s%zu*[%s]\n", kid_own.c_str(), grp.second.size(), grp.first.c_str());
                    if (do_hl2) printf("\033[0m");
                } else {
                    ptree(grp.second[0], kid_own, kid_child);
                }
            }
        };

    // Determine root nodes
    std::vector<DWORD> roots;
    if (root_pid != 0) {
        roots.push_back(root_pid);
    } else if (!root_user.empty()) {
        for (auto& kv : procs)
            if (_stricmp(kv.second.user.c_str(), root_user.c_str()) == 0 &&
                !has_parent.count(kv.first))
                roots.push_back(kv.first);
    } else {
        for (auto& kv : procs)
            if (!has_parent.count(kv.first))
                roots.push_back(kv.first);
    }

    if (sort_by_pid)
        std::sort(roots.begin(), roots.end());
    else
        std::sort(roots.begin(), roots.end(), [&](DWORD a, DWORD b) {
            return procs[a].name < procs[b].name;
        });

    for (DWORD r : roots)
        ptree(r, "", "");

    return 0;
}
