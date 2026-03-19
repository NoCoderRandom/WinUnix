// df - report file system disk space usage (Windows port)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <string>

static void usage() {
    fprintf(stderr,
        "Usage: df [OPTION]... [FILE]...\n"
        "Show information about the file system on which each FILE resides,\n"
        "or all file systems by default.\n\n"
        "  -a, --all             include dummy file systems\n"
        "  -B, --block-size=SIZE  scale sizes by SIZE before printing them\n"
        "  -h, --human-readable  print sizes in powers of 1024 (e.g., 1023M)\n"
        "  -H, --si              print sizes in powers of 1000 (e.g., 1.1G)\n"
        "  -i, --inodes          list inode information instead of block usage\n"
        "  -k                    like --block-size=1K\n"
        "  -l, --local           limit listing to local file systems\n"
        "  -P, --portability     use the POSIX output format\n"
        "  --sync                invoke sync before getting usage info\n"
        "  -t, --type=TYPE       limit listing to file systems of type TYPE\n"
        "  -T, --print-type      print file system type\n"
        "  -x, --exclude-type=TYPE  limit listing to file systems not of type TYPE\n"
        "      --help            display this help and exit\n"
    );
}

enum Unit { BLOCK_512, BLOCK_1K, BLOCK_1M, BLOCK_1G, HUMAN_1024, HUMAN_SI };

static void human_size(char* buf, size_t blen, unsigned long long bytes, bool si) {
    double base = si ? 1000.0 : 1024.0;
    const char* u_si[]  = {"B","K","M","G","T","P"};
    const char* u_bin[] = {"B","K","M","G","T","P"};
    const char** u = si ? u_si : u_bin;
    double val = (double)bytes;
    int idx = 0;
    while (val >= base && idx < 5) { val /= base; idx++; }
    if (idx == 0) snprintf(buf, blen, "%lluB", bytes);
    else snprintf(buf, blen, "%.1f%s", val, u[idx]);
}

static unsigned long long to_blocks(unsigned long long bytes, Unit unit) {
    switch (unit) {
        case BLOCK_512:  return bytes / 512;
        case BLOCK_1K:   return bytes / 1024;
        case BLOCK_1M:   return bytes / (1024ULL*1024);
        case BLOCK_1G:   return bytes / (1024ULL*1024*1024);
        default:         return bytes / 1024;
    }
}

static std::string get_fs_type_str(const char* root) {
    char fs_name[256] = {};
    if (GetVolumeInformationA(root, NULL, 0, NULL, NULL, NULL, fs_name, sizeof(fs_name)))
        return fs_name;
    return "unknown";
}

static int get_drive_type_str(UINT t, char* buf, int blen) {
    switch (t) {
        case DRIVE_REMOVABLE: snprintf(buf, blen, "removable"); return 0;
        case DRIVE_FIXED:     snprintf(buf, blen, "fixed");     return 0;
        case DRIVE_REMOTE:    snprintf(buf, blen, "remote");    return 0;
        case DRIVE_CDROM:     snprintf(buf, blen, "cdrom");     return 0;
        case DRIVE_RAMDISK:   snprintf(buf, blen, "ramdisk");   return 0;
        default:              snprintf(buf, blen, "unknown");   return 0;
    }
}

int main(int argc, char* argv[]) {
    bool opt_all = false, opt_h = false, opt_H = false, opt_i = false;
    bool opt_k = false, opt_l = false, opt_T = false, opt_P = false;
    Unit unit = BLOCK_1K;
    std::vector<std::string> filter_types, exclude_types, paths;
    unsigned long long block_size = 1024;

    for (int i = 1; i < argc; i++) {
        char* a = argv[i];
        if (strcmp(a, "--help") == 0)         { usage(); return 0; }
        else if (strcmp(a, "--all") == 0) opt_all = true;
        else if (strcmp(a, "--human-readable") == 0) { opt_h = true; unit = HUMAN_1024; }
        else if (strcmp(a, "--si") == 0)  { opt_H = true; unit = HUMAN_SI; }
        else if (strcmp(a, "--inodes") == 0) opt_i = true;
        else if (strcmp(a, "--local") == 0) opt_l = true;
        else if (strcmp(a, "--print-type") == 0) opt_T = true;
        else if (strcmp(a, "--portability") == 0) opt_P = true;
        else if (strcmp(a, "--type") == 0 && i+1 < argc)
            filter_types.push_back(argv[++i]);
        else if (strncmp(a, "--type=", 7) == 0) filter_types.push_back(a+7);
        else if (strcmp(a, "--exclude-type") == 0 && i+1 < argc)
            exclude_types.push_back(argv[++i]);
        else if (strncmp(a, "--exclude-type=", 15) == 0) exclude_types.push_back(a+15);
        else if (strncmp(a, "--block-size=", 13) == 0) {
            block_size = (unsigned long long)atoll(a+13);
        }
        else if (a[0] == '-' && a[1] != '-') {
            // Handle combined short flags: -Th, -ahT, etc.
            bool err = false;
            for (int j = 1; a[j] && !err; j++) {
                switch (a[j]) {
                    case 'a': opt_all = true; break;
                    case 'h': opt_h = true; unit = HUMAN_1024; break;
                    case 'H': opt_H = true; unit = HUMAN_SI; break;
                    case 'i': opt_i = true; break;
                    case 'k': opt_k = true; unit = BLOCK_1K; break;
                    case 'l': opt_l = true; break;
                    case 'T': opt_T = true; break;
                    case 'P': opt_P = true; break;
                    case 't':
                        if (a[j+1]) { filter_types.push_back(a+j+1); j=(int)strlen(a)-1; }
                        else if (i+1 < argc) filter_types.push_back(argv[++i]);
                        break;
                    case 'x':
                        if (a[j+1]) { exclude_types.push_back(a+j+1); j=(int)strlen(a)-1; }
                        else if (i+1 < argc) exclude_types.push_back(argv[++i]);
                        break;
                    case 'B':
                        if (a[j+1]) { block_size=(unsigned long long)atoll(a+j+1); j=(int)strlen(a)-1; }
                        else if (i+1 < argc) block_size=(unsigned long long)atoll(argv[++i]);
                        break;
                    default:
                        fprintf(stderr, "df: invalid option -- '%c'\n", a[j]);
                        fprintf(stderr, "Try 'df --help' for more information.\n");
                        err = true;
                }
            }
            if (err) return 1;
        }
        else if (a[0] != '-') paths.push_back(a);
        else {
            fprintf(stderr, "df: invalid option '%s'\n", a);
            fprintf(stderr, "Try 'df --help' for more information.\n");
            return 1;
        }
    }

    // Print header
    char size_label[32] = "1K-blocks";
    if (unit == HUMAN_1024 || unit == HUMAN_SI) strcpy(size_label, "Size");
    else if (unit == BLOCK_512) strcpy(size_label, "512B-blocks");
    else if (unit == BLOCK_1M) strcpy(size_label, "1M-blocks");

    if (opt_T) {
        printf("%-20s %-10s %12s %12s %12s %5s %s\n",
            "Filesystem", "Type", size_label, "Used", "Available", "Use%", "Mounted on");
    } else {
        printf("%-20s %12s %12s %12s %5s %s\n",
            "Filesystem", size_label, "Used", "Available", "Use%", "Mounted on");
    }

    auto print_drive = [&](const char* root) {
        ULARGE_INTEGER freeBytesAvail, totalBytes, totalFreeBytes;
        if (!GetDiskFreeSpaceExA(root, &freeBytesAvail, &totalBytes, &totalFreeBytes))
            return;

        UINT dtype = GetDriveTypeA(root);
        if (opt_l && dtype == DRIVE_REMOTE) return;

        std::string fstype = get_fs_type_str(root);

        // filter by type
        if (!filter_types.empty()) {
            bool found = false;
            for (auto& t : filter_types)
                if (_stricmp(t.c_str(), fstype.c_str()) == 0) { found = true; break; }
            if (!found) return;
        }
        for (auto& t : exclude_types)
            if (_stricmp(t.c_str(), fstype.c_str()) == 0) return;

        unsigned long long total = totalBytes.QuadPart;
        unsigned long long avail = freeBytesAvail.QuadPart;
        unsigned long long total_free = totalFreeBytes.QuadPart;
        unsigned long long used  = total - total_free;
        int pct = (total > 0) ? (int)(((double)used / total) * 100.0 + 0.5) : 0;

        // Format filesystem name as drive letter path
        char fs_name[64];
        // Get volume name
        char vol_name[MAX_PATH] = {};
        char vol_serial[32] = {};
        DWORD serial = 0;
        GetVolumeInformationA(root, vol_name, sizeof(vol_name), &serial, NULL, NULL, NULL, 0);
        // Use root as filesystem identifier
        char root_trimmed[8];
        strncpy(root_trimmed, root, sizeof(root_trimmed)-1);
        // remove trailing backslash
        int rlen = (int)strlen(root_trimmed);
        if (rlen > 1 && root_trimmed[rlen-1] == '\\') root_trimmed[rlen-1] = 0;

        char size_str[32], used_str[32], avail_str[32];
        auto fmt = [&](char* buf, size_t blen, unsigned long long bytes) {
            if (unit == HUMAN_1024) human_size(buf, blen, bytes, false);
            else if (unit == HUMAN_SI) human_size(buf, blen, bytes, true);
            else snprintf(buf, blen, "%llu", to_blocks(bytes, unit));
        };
        fmt(size_str, sizeof(size_str), total);
        fmt(used_str, sizeof(used_str), used);
        fmt(avail_str, sizeof(avail_str), avail);

        char pct_str[16];
        snprintf(pct_str, sizeof(pct_str), "%d%%", pct);

        // Mount point: use root as-is
        char mount[8];
        strncpy(mount, root, sizeof(mount)-1);
        if (strlen(mount) > 1 && mount[strlen(mount)-1] == '\\') mount[strlen(mount)-1] = 0;

        if (opt_T) {
            printf("%-20s %-10s %12s %12s %12s %5s %s\n",
                root_trimmed, fstype.c_str(), size_str, used_str, avail_str, pct_str, mount);
        } else {
            printf("%-20s %12s %12s %12s %5s %s\n",
                root_trimmed, size_str, used_str, avail_str, pct_str, mount);
        }
    };

    if (!paths.empty()) {
        for (auto& p : paths) {
            char root[MAX_PATH] = {};
            GetVolumePathNameA(p.c_str(), root, sizeof(root));
            print_drive(root);
        }
    } else {
        // Enumerate all drives
        char drives[512] = {};
        GetLogicalDriveStringsA(sizeof(drives)-1, drives);
        for (char* d = drives; *d; d += strlen(d)+1) {
            print_drive(d);
        }
    }
    return 0;
}
