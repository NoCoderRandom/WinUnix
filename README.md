# WinUnix

**Linux/Unix command-line tools natively compiled for Windows 11**

WinUnix brings the most-used Linux process and system tools to Windows — built from scratch in C++ using the native Windows API. No WSL, no Cygwin, no dependencies. Just drop the `.exe` files in your PATH and go.

```
> uname -a
Windows DESKTOP 10.0.26100 #1 SMP (Windows Build 26100) x86_64 x86_64 x86_64 Windows_NT

> free -h
               total        used        free      shared  buff/cache   available
Mem:           15.7Gi       8.4Gi       7.3Gi          0B          0B       7.3Gi
Swap:           8.5Gi       3.4Gi       5.1Gi

> pstree -p
explorer(8432)
├─Copilot(7812)
├─ms-teams(14200)
│ └─msedgewebview2(14380)
│   └─4*[msedgewebview2]
└─WindowsTerminal(14472)
  └─powershell(13472)
```

---

## Tools

| Tool | Description | Key flags |
|------|-------------|-----------|
| `ps` | Snapshot of running processes | `-e` `-f` `-ef` `aux` `-l` `-C` `-p` `--sort` |
| `pstree` | Process tree with Unicode/ASCII drawing | `-p` `-n` `-u` `-A` `-c` `-h` `-H` |
| `top` | Live process monitor (batch & interactive) | `-b` `-n` `-d` `-o` `-p` `-u` `-i` |
| `htop` | Color interactive process viewer | `-d` `-u` `-p` `-s` `-t` `--sort-key` |
| `df` | Disk space usage by drive | `-h` `-H` `-T` `-t` `-x` `-l` `-k` `-P` |
| `du` | Directory/file size on disk | `-sh` `-a` `-c` `-d` `-h` `-b` `-k` `-m` `--exclude` |
| `free` | RAM and swap usage | `-h` `-b` `-k` `-m` `-g` `-t` `-w` `-s` `-c` |
| `uname` | OS and hardware information | `-a` `-s` `-n` `-r` `-v` `-m` `-p` `-i` `-o` |

All tools accept the same flags as their Linux counterparts.

---

## Installation (from release)

**1. Download** `WinUnix-v1.0.zip` from the [Releases](../../releases/latest) page.

**2. Create a tools folder and extract:**
```cmd
mkdir C:\tools
```
Extract all `.exe` files from the zip into `C:\tools`.

**3. Add to PATH permanently** (run as Administrator):
```cmd
setx /M PATH "%PATH%;C:\tools"
```

**4. Restart your terminal**, then verify:
```cmd
uname -a
free -h
ps aux
```

> The `/M` flag sets the PATH system-wide. Omit `/M` to set it for the current user only.

---

## Building from Source

Requires **Visual Studio 2019 or 2022** with the *"Desktop development with C++"* workload.

```cmd
git clone https://github.com/NoCoderRandom/WinUnix.git
cd WinUnix
build.bat
```

Compiled binaries are placed in `bin\`. The build script auto-detects your VS installation and compiles with `/MT` (statically linked CRT — no runtime DLL dependencies).

---

## Usage Examples

```cmd
# System info
uname -a
uname -r

# Memory
free -h
free -h -t
free -s 2          # refresh every 2 seconds

# Disk
df -h
df -h -T           # include filesystem type
du -sh C:\Windows
du -h --max-depth=1 .

# Processes
ps aux
ps -ef
ps aux --sort -%cpu
ps -C explorer     # find by name

# Process tree
pstree
pstree -p          # with PIDs
pstree -A          # ASCII drawing characters
pstree -n          # sorted by PID

# Live monitor (batch)
top -b -n 1
top -b -n 5 -d 2 -o mem

# Interactive
top                # press q to quit, P/M/T/N to sort
htop               # press q to quit, F6 to sort
htop -t            # start in tree view
htop --sort-key=CPU
```

---

## Notes

- **No dependencies** — statically compiled, works on any Windows 10/11 machine.
- **TTY column** shows `?` — Windows has no TTY/pts concept.
- **buff/cache and shared** show `0` in `free` — Windows does not expose these kernel buffers separately.
- **df** shows drive letters (`C:`, `D:`) instead of `/dev/sda1` — this is the Windows equivalent.
- Run your terminal **as Administrator** to see all system processes in `ps`, `top`, and `htop`.

---

## License

MIT License — see [LICENSE](LICENSE) for details.
