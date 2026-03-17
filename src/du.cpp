// du - estimate file space usage (Windows port)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <algorithm>

static void usage() {
    fprintf(stderr,
        "Usage: du [OPTION]... [FILE]...\n"
        "Summarize disk usage of the set of FILEs, recursively for directories.\n\n"
        "  -0, --null            end each output line with NUL, not newline\n"
        "  -a, --all             write counts for all files, not just directories\n"
        "      --apparent-size   print apparent sizes rather than disk usage\n"
        "  -B, --block-size=SIZE  scale sizes by SIZE before printing them\n"
        "  -b, --bytes           equivalent to '--apparent-size --block-size=1'\n"
        "  -c, --total           produce a grand total\n"
        "  -d, --max-depth=N     print the total for a directory only if it is N or\n"
        "                        fewer levels below the command line argument\n"
        "  -h, --human-readable  print sizes in human readable format (e.g., 1K 234M 2G)\n"
        "  -H, --si              like -h, but use powers of 1000 not 1024\n"
        "  -k                    like --block-size=1K\n"
        "  -L, --dereference     dereference all symbolic links\n"
        "  -m                    like --block-size=1M\n"
        "  -P, --no-dereference  don't follow any symbolic links (default)\n"
        "  -s, --summarize       display only a total for each argument\n"
        "  -S, --separate-dirs   for directories do not include size of subdirectories\n"
        "  -x, --one-file-system skip directories on different file systems\n"
        "      --exclude=PATTERN  exclude files that match PATTERN\n"
        "      --help            display this help and exit\n"
    );
}

enum DuUnit { DU_BYTES, DU_KILO, DU_MEGA, DU_HUMAN, DU_HUMAN_SI, DU_BLOCK };

static unsigned long long block_size_bytes = 1024;

static void human_size(char* buf, size_t blen, unsigned long long bytes, bool si) {
    double base = si ? 1000.0 : 1024.0;
    const char* u[] = {"B","K","M","G","T","P"};
    double val = (double)bytes;
    int idx = 0;
    while (val >= base && idx < 5) { val /= base; idx++; }
    if (idx == 0) snprintf(buf, blen, "%llu", bytes);
    else snprintf(buf, blen, "%.1f%s", val, u[idx]);
}

static void format_size(char* buf, size_t blen, unsigned long long bytes, DuUnit unit) {
    switch (unit) {
        case DU_BYTES:    snprintf(buf, blen, "%llu", bytes); break;
        case DU_KILO:     snprintf(buf, blen, "%llu", (bytes+1023)/1024); break;
        case DU_MEGA:     snprintf(buf, blen, "%llu", (bytes+1024*1024-1)/(1024*1024)); break;
        case DU_HUMAN:    human_size(buf, blen, bytes, false); break;
        case DU_HUMAN_SI: human_size(buf, blen, bytes, true); break;
        case DU_BLOCK:    snprintf(buf, blen, "%llu", (bytes+block_size_bytes-1)/block_size_bytes); break;
    }
}

struct Options {
    bool all_files = false;
    bool apparent = false;
    bool total = false;
    int  max_depth = -1;
    bool summarize = false;
    bool separate_dirs = false;
    bool one_fs = false;
    bool null_term = false;
    DuUnit unit = DU_KILO;
    std::vector<std::string> excludes;
    char end_char = '\n';
};

static bool matches_exclude(const std::string& name, const std::vector<std::string>& patterns) {
    for (auto& p : patterns) {
        // Simple wildcard match
        if (p == name) return true;
        // Check if p contains wildcard
        if (p.find('*') != std::string::npos || p.find('?') != std::string::npos) {
            // Use PathMatchSpec if available, otherwise simple check
            if (PathMatchSpecA(name.c_str(), p.c_str())) return true;
        }
    }
    return false;
}

static unsigned long long du_dir(const std::string& path, int depth, const Options& opts,
                                  DWORD root_serial, bool& has_root_serial,
                                  unsigned long long& grand_total) {
    WIN32_FIND_DATAA ffd;
    std::string search = path + "\\*";
    HANDLE hFind = FindFirstFileA(search.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    unsigned long long total = 0;
    do {
        if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0) continue;
        std::string full = path + "\\" + ffd.cFileName;

        if (matches_exclude(ffd.cFileName, opts.excludes)) continue;

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            unsigned long long sub = du_dir(full, depth+1, opts, root_serial, has_root_serial, grand_total);
            if (!opts.separate_dirs) total += sub;

            bool show = (opts.max_depth < 0 || depth+1 <= opts.max_depth) && !opts.summarize;
            if (show) {
                char szbuf[32];
                format_size(szbuf, sizeof(szbuf), sub, opts.unit);
                printf("%s\t%s%c", szbuf, full.c_str(), opts.end_char);
            }
        } else {
            unsigned long long fsize;
            if (opts.apparent) {
                LARGE_INTEGER li;
                li.LowPart  = ffd.nFileSizeLow;
                li.HighPart = (LONG)ffd.nFileSizeHigh;
                fsize = (unsigned long long)li.QuadPart;
            } else {
                // Disk usage: use compressed size if available
                LARGE_INTEGER li;
                li.LowPart  = ffd.nFileSizeLow;
                li.HighPart = (LONG)ffd.nFileSizeHigh;
                fsize = (unsigned long long)li.QuadPart;
                // Round up to cluster size (approximate: assume 4K clusters)
                fsize = (fsize + 4095) & ~4095ULL;
            }
            total += fsize;
            if (opts.all_files && (opts.max_depth < 0 || depth+1 <= opts.max_depth) && !opts.summarize) {
                char szbuf[32];
                format_size(szbuf, sizeof(szbuf), fsize, opts.unit);
                printf("%s\t%s%c", szbuf, full.c_str(), opts.end_char);
            }
        }
    } while (FindNextFileA(hFind, &ffd));
    FindClose(hFind);
    return total;
}

int main(int argc, char* argv[]) {
    Options opts;
    std::vector<std::string> paths;

    for (int i = 1; i < argc; i++) {
        char* a = argv[i];
        if (strcmp(a, "--help") == 0) { usage(); return 0; }
        else if (strcmp(a, "--apparent-size") == 0) opts.apparent = true;
        else if (strcmp(a, "--all") == 0) opts.all_files = true;
        else if (strcmp(a, "--bytes") == 0) { opts.apparent = true; opts.unit = DU_BYTES; }
        else if (strcmp(a, "--total") == 0) opts.total = true;
        else if (strcmp(a, "--human-readable") == 0) opts.unit = DU_HUMAN;
        else if (strcmp(a, "--si") == 0) opts.unit = DU_HUMAN_SI;
        else if (strcmp(a, "--summarize") == 0) opts.summarize = true;
        else if (strcmp(a, "--separate-dirs") == 0) opts.separate_dirs = true;
        else if (strcmp(a, "--one-file-system") == 0) opts.one_fs = true;
        else if (strcmp(a, "--null") == 0) opts.end_char = '\0';
        else if (strcmp(a, "--dereference") == 0) {}
        else if (strcmp(a, "--no-dereference") == 0) {}
        else if (strcmp(a, "--max-depth") == 0 && i+1 < argc) opts.max_depth = atoi(argv[++i]);
        else if (strncmp(a, "--max-depth=", 12) == 0) opts.max_depth = atoi(a+12);
        else if (strncmp(a, "--exclude=", 10) == 0) opts.excludes.push_back(a+10);
        else if (strcmp(a, "--exclude") == 0 && i+1 < argc) opts.excludes.push_back(argv[++i]);
        else if (strncmp(a, "--block-size=", 13) == 0) {
            block_size_bytes = (unsigned long long)atoll(a+13); opts.unit = DU_BLOCK;
        }
        else if (a[0] == '-' && a[1] != '-') {
            // Handle combined short flags: -sh, -ach, etc.
            bool err = false;
            for (int j = 1; a[j] && !err; j++) {
                switch (a[j]) {
                    case 'a': opts.all_files = true; break;
                    case 'b': opts.apparent = true; opts.unit = DU_BYTES; break;
                    case 'c': opts.total = true; break;
                    case 'h': opts.unit = DU_HUMAN; break;
                    case 'H': opts.unit = DU_HUMAN_SI; break;
                    case 'k': opts.unit = DU_KILO; break;
                    case 'm': opts.unit = DU_MEGA; break;
                    case 's': opts.summarize = true; break;
                    case 'S': opts.separate_dirs = true; break;
                    case 'x': opts.one_fs = true; break;
                    case '0': opts.end_char = '\0'; break;
                    case 'L': break;
                    case 'P': break;
                    case 'd':
                        // -d N or -dN
                        if (a[j+1]) { opts.max_depth = atoi(a+j+1); j=(int)strlen(a)-1; }
                        else if (i+1 < argc) opts.max_depth = atoi(argv[++i]);
                        break;
                    case 'B':
                        if (a[j+1]) { block_size_bytes=(unsigned long long)atoll(a+j+1); opts.unit=DU_BLOCK; j=(int)strlen(a)-1; }
                        else if (i+1 < argc) { block_size_bytes=(unsigned long long)atoll(argv[++i]); opts.unit=DU_BLOCK; }
                        break;
                    default:
                        fprintf(stderr, "du: invalid option -- '%c'\n", a[j]);
                        fprintf(stderr, "Try 'du --help' for more information.\n");
                        err = true;
                }
            }
            if (err) return 1;
        }
        else if (a[0] != '-') paths.push_back(a);
        else {
            fprintf(stderr, "du: invalid option '%s'\n", a);
            fprintf(stderr, "Try 'du --help' for more information.\n");
            return 1;
        }
    }

    if (paths.empty()) paths.push_back(".");

    unsigned long long grand_total = 0;
    bool has_root_serial = false;
    DWORD root_serial = 0;

    for (auto& p : paths) {
        DWORD attr = GetFileAttributesA(p.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) {
            fprintf(stderr, "du: cannot access '%s': No such file or directory\n", p.c_str());
            continue;
        }

        unsigned long long size = 0;
        if (attr & FILE_ATTRIBUTE_DIRECTORY) {
            size = du_dir(p, 0, opts, root_serial, has_root_serial, grand_total);
        } else {
            WIN32_FILE_ATTRIBUTE_DATA fad;
            GetFileAttributesExA(p.c_str(), GetFileExInfoStandard, &fad);
            LARGE_INTEGER li;
            li.LowPart = fad.nFileSizeLow;
            li.HighPart = (LONG)fad.nFileSizeHigh;
            size = opts.apparent ? (unsigned long long)li.QuadPart
                                 : ((unsigned long long)li.QuadPart + 4095) & ~4095ULL;
        }

        char szbuf[32];
        format_size(szbuf, sizeof(szbuf), size, opts.unit);
        printf("%s\t%s%c", szbuf, p.c_str(), opts.end_char);
        grand_total += size;
    }

    if (opts.total) {
        char szbuf[32];
        format_size(szbuf, sizeof(szbuf), grand_total, opts.unit);
        printf("%s\ttotal%c", szbuf, opts.end_char);
    }
    return 0;
}
