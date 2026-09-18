#include "../syscall_platform.h"

#if !defined(_WIN32)

#include "uasm/syscall_ids.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <dirent.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <sys/sysinfo.h>
#endif

extern char** environ;

namespace uasm {

namespace {

char* bufAt(uint8_t* mem, uint64_t memSize, int64_t offset, int64_t len) {
    if (offset < 0 || len < 0) return 0;
    if (static_cast<uint64_t>(offset) + static_cast<uint64_t>(len) > memSize) return 0;
    return reinterpret_cast<char*>(mem) + offset;
}

const char* cstrAt(uint8_t* mem, uint64_t memSize, int64_t offset) {
    if (offset < 0 || static_cast<uint64_t>(offset) >= memSize) return 0;
    return reinterpret_cast<const char*>(mem) + offset;
}

int64_t writeResult(char* buf, int64_t bufLen, const std::string& s) {
    if (!buf) return -1;
    size_t n = s.size();
    if (static_cast<int64_t>(n) > bufLen) n = static_cast<size_t>(bufLen);
    std::memcpy(buf, s.data(), n);
    if (static_cast<int64_t>(n) < bufLen) buf[n] = 0;
    return static_cast<int64_t>(n);
}

}

int64_t platformSyscall(int64_t id, int64_t a0, int64_t a1, int64_t a2, uint8_t* mem, uint64_t memSize) {
    switch (id) {
        case SyscallId::Exit:
            std::exit(static_cast<int>(a0));
            return 0;

        case SyscallId::Write: {
            char* buf = bufAt(mem, memSize, a1, a2);
            if (!buf) return -1;
            return static_cast<int64_t>(::write(static_cast<int>(a0), buf, static_cast<size_t>(a2)));
        }
        case SyscallId::Read: {
            char* buf = bufAt(mem, memSize, a1, a2);
            if (!buf) return -1;
            return static_cast<int64_t>(::read(static_cast<int>(a0), buf, static_cast<size_t>(a2)));
        }
        case SyscallId::Open: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return -1;
            return ::open(path, static_cast<int>(a1), static_cast<mode_t>(a2));
        }
        case SyscallId::Close:
            return ::close(static_cast<int>(a0));
        case SyscallId::Seek:
            return static_cast<int64_t>(::lseek(static_cast<int>(a0), static_cast<off_t>(a1), static_cast<int>(a2)));
        case SyscallId::FileSize: {
            struct stat st;
            if (::fstat(static_cast<int>(a0), &st) != 0) return -1;
            return static_cast<int64_t>(st.st_size);
        }
        case SyscallId::RemoveFile: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return -1;
            return ::unlink(path);
        }
        case SyscallId::RenameFile: {
            const char* oldPath = cstrAt(mem, memSize, a0);
            const char* newPath = cstrAt(mem, memSize, a1);
            if (!oldPath || !newPath) return -1;
            return ::rename(oldPath, newPath);
        }
        case SyscallId::Mkdir: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return -1;
            return ::mkdir(path, 0755);
        }
        case SyscallId::Rmdir: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return -1;
            return ::rmdir(path);
        }
        case SyscallId::Getcwd: {
            char* buf = bufAt(mem, memSize, a0, a1);
            if (!buf) return -1;
            if (::getcwd(buf, static_cast<size_t>(a1)) == 0) return -1;
            return static_cast<int64_t>(std::strlen(buf));
        }
        case SyscallId::Chdir: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return -1;
            return ::chdir(path);
        }
        case SyscallId::FileExists: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return 0;
            return ::access(path, F_OK) == 0 ? 1 : 0;
        }
        case SyscallId::Realpath: {
            const char* path = cstrAt(mem, memSize, a0);
            char* buf = bufAt(mem, memSize, a1, a2);
            if (!path || !buf) return -1;
            char resolved[4096];
            if (::realpath(path, resolved) == 0) return -1;
            return writeResult(buf, a2, resolved);
        }
        case SyscallId::TimeUnixSeconds:
            return static_cast<int64_t>(::time(0));
        case SyscallId::TimeUnixMillis: {
            struct timeval tv;
            ::gettimeofday(&tv, 0);
            return static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
        }
        case SyscallId::TickCountMs: {
            struct timespec ts;
            ::clock_gettime(CLOCK_MONOTONIC, &ts);
            return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
        }
        case SyscallId::SleepMillis: {
            struct timespec ts;
            ts.tv_sec = a0 / 1000;
            ts.tv_nsec = (a0 % 1000) * 1000000;
            ::nanosleep(&ts, 0);
            return 0;
        }
        case SyscallId::GetPid:
            return static_cast<int64_t>(::getpid());
        case SyscallId::CpuCount:
            return static_cast<int64_t>(::sysconf(_SC_NPROCESSORS_ONLN));
        case SyscallId::PageSize:
            return static_cast<int64_t>(::sysconf(_SC_PAGESIZE));
        case SyscallId::GetEnv: {
            const char* name = cstrAt(mem, memSize, a0);
            char* buf = bufAt(mem, memSize, a1, a2);
            if (!name || !buf) return -1;
            const char* value = std::getenv(name);
            if (!value) return -1;
            return writeResult(buf, a2, value);
        }
        case SyscallId::SetEnv: {
            const char* name = cstrAt(mem, memSize, a0);
            const char* value = cstrAt(mem, memSize, a1);
            if (!name || !value) return -1;
            return ::setenv(name, value, 1);
        }
        case SyscallId::Argc:
            return getProcessArgc();
        case SyscallId::Argv: {
            char* buf = bufAt(mem, memSize, a1, a2);
            if (!buf) return -1;
            return writeResult(buf, a2, getProcessArgv(static_cast<int>(a0)));
        }
        case SyscallId::RandomBytes: {
            char* buf = bufAt(mem, memSize, a0, a1);
            if (!buf) return -1;
            for (int64_t i = 0; i < a1; ++i) buf[i] = static_cast<char>(::arc4random() & 0xFF);
            return 0;
        }
        case SyscallId::IsATty:
            return ::isatty(static_cast<int>(a0));
        case SyscallId::Flush:
            return ::fsync(static_cast<int>(a0));
        case SyscallId::StdinAvailable: {
            int n = 0;
            if (::ioctl(0, FIONREAD, &n) != 0) return 0;
            return n > 0 ? 1 : 0;
        }

        case SyscallId::DirOpen: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return -1;
            DIR* d = ::opendir(path);
            return reinterpret_cast<int64_t>(d);
        }
        case SyscallId::DirRead: {
            DIR* d = reinterpret_cast<DIR*>(a0);
            char* buf = bufAt(mem, memSize, a1, a2);
            if (!d || !buf) return -1;
            struct dirent* entry = ::readdir(d);
            if (!entry) return 0;
            return writeResult(buf, a2, entry->d_name);
        }
        case SyscallId::DirClose: {
            DIR* d = reinterpret_cast<DIR*>(a0);
            if (!d) return -1;
            return ::closedir(d);
        }
        case SyscallId::FileIsDir: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return 0;
            struct stat st;
            if (::stat(path, &st) != 0) return 0;
            return S_ISDIR(st.st_mode) ? 1 : 0;
        }
        case SyscallId::FileMtime: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return -1;
            struct stat st;
            if (::stat(path, &st) != 0) return -1;
            return static_cast<int64_t>(st.st_mtime);
        }
        case SyscallId::FilePermsGet: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return -1;
            struct stat st;
            if (::stat(path, &st) != 0) return -1;
            return static_cast<int64_t>(st.st_mode & 0777);
        }
        case SyscallId::FilePermsSet: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return -1;
            return ::chmod(path, static_cast<mode_t>(a1));
        }
        case SyscallId::CopyFile: {
            const char* src = cstrAt(mem, memSize, a0);
            const char* dst = cstrAt(mem, memSize, a1);
            if (!src || !dst) return -1;
            FILE* in = std::fopen(src, "rb");
            if (!in) return -1;
            FILE* out = std::fopen(dst, "wb");
            if (!out) {
                std::fclose(in);
                return -1;
            }
            char chunk[4096];
            size_t n;
            while ((n = std::fread(chunk, 1, sizeof(chunk), in)) > 0) std::fwrite(chunk, 1, n, out);
            std::fclose(in);
            std::fclose(out);
            return 0;
        }
        case SyscallId::DiskFreeSpace: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return -1;
            struct statvfs st;
            if (::statvfs(path, &st) != 0) return -1;
            return static_cast<int64_t>(st.f_bavail) * static_cast<int64_t>(st.f_frsize);
        }
        case SyscallId::GetTempDir: {
            char* buf = bufAt(mem, memSize, a0, a1);
            if (!buf) return -1;
            const char* tmp = std::getenv("TMPDIR");
            if (!tmp) tmp = "/tmp";
            return writeResult(buf, a1, tmp);
        }
        case SyscallId::GetExePath: {
            char* buf = bufAt(mem, memSize, a0, a1);
            if (!buf) return -1;
#if defined(__APPLE__)
            char path[4096];
            uint32_t size = sizeof(path);
            if (_NSGetExecutablePath(path, &size) != 0) return -1;
            return writeResult(buf, a1, path);
#else
            char path[4096];
            ssize_t n = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
            if (n < 0) return -1;
            path[n] = 0;
            return writeResult(buf, a1, path);
#endif
        }
        case SyscallId::GetHostname: {
            char* buf = bufAt(mem, memSize, a0, a1);
            if (!buf) return -1;
            if (::gethostname(buf, static_cast<size_t>(a1)) != 0) return -1;
            return static_cast<int64_t>(std::strlen(buf));
        }
        case SyscallId::EnvCount: {
            int64_t n = 0;
            while (::environ[n] != 0) ++n;
            return n;
        }
        case SyscallId::EnvGet: {
            char* buf = bufAt(mem, memSize, a1, a2);
            if (!buf) return -1;
            int64_t idx = 0;
            for (; ::environ[idx] != 0; ++idx) {
                if (idx == a0) return writeResult(buf, a2, ::environ[idx]);
            }
            return -1;
        }
        case SyscallId::TermColumns:
        case SyscallId::TermRows: {
            struct winsize ws;
            if (::ioctl(1, TIOCGWINSZ, &ws) != 0) return -1;
            return id == SyscallId::TermColumns ? ws.ws_col : ws.ws_row;
        }
        case SyscallId::FileLock:
            return ::flock(static_cast<int>(a0), LOCK_EX);
        case SyscallId::FileUnlock:
            return ::flock(static_cast<int>(a0), LOCK_UN);

        case SyscallId::SocketCreate:
            return ::socket(static_cast<int>(a0), static_cast<int>(a1), 0);
        case SyscallId::SocketConnect: {
            const char* host = cstrAt(mem, memSize, a1);
            if (!host) return -1;
            struct sockaddr_in addr;
            std::memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(static_cast<uint16_t>(a2));
            if (::inet_pton(AF_INET, host, &addr.sin_addr) != 1) return -1;
            return ::connect(static_cast<int>(a0), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        }
        case SyscallId::SocketBind: {
            const char* host = cstrAt(mem, memSize, a1);
            struct sockaddr_in addr;
            std::memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(static_cast<uint16_t>(a2));
            if (!host || host[0] == 0) addr.sin_addr.s_addr = INADDR_ANY;
            else if (::inet_pton(AF_INET, host, &addr.sin_addr) != 1) return -1;
            return ::bind(static_cast<int>(a0), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        }
        case SyscallId::SocketListen:
            return ::listen(static_cast<int>(a0), static_cast<int>(a1));
        case SyscallId::SocketAccept:
            return ::accept(static_cast<int>(a0), 0, 0);
        case SyscallId::SocketSend: {
            char* buf = bufAt(mem, memSize, a1, a2);
            if (!buf) return -1;
            return static_cast<int64_t>(::send(static_cast<int>(a0), buf, static_cast<size_t>(a2), 0));
        }
        case SyscallId::SocketRecv: {
            char* buf = bufAt(mem, memSize, a1, a2);
            if (!buf) return -1;
            return static_cast<int64_t>(::recv(static_cast<int>(a0), buf, static_cast<size_t>(a2), 0));
        }
        case SyscallId::SocketClose:
            return ::close(static_cast<int>(a0));
        case SyscallId::DnsResolve: {
            const char* host = cstrAt(mem, memSize, a0);
            char* buf = bufAt(mem, memSize, a1, a2);
            if (!host || !buf) return -1;
            struct addrinfo hints;
            std::memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;
            struct addrinfo* res = 0;
            if (::getaddrinfo(host, 0, &hints, &res) != 0 || !res) return -1;
            char ip[64];
            ::inet_ntop(AF_INET, &reinterpret_cast<struct sockaddr_in*>(res->ai_addr)->sin_addr, ip, sizeof(ip));
            ::freeaddrinfo(res);
            return writeResult(buf, a2, ip);
        }

        case SyscallId::MemPageAlloc: {
            void* p = ::mmap(0, static_cast<size_t>(a0), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
            if (p == MAP_FAILED) return 0;
            return reinterpret_cast<int64_t>(p);
        }
        case SyscallId::MemPageProtect: {
            int prot = 0;
            if (a2 & 1) prot |= PROT_READ;
            if (a2 & 2) prot |= PROT_WRITE;
            if (a2 & 4) prot |= PROT_EXEC;
            return ::mprotect(reinterpret_cast<void*>(a0), static_cast<size_t>(a1), prot);
        }
        case SyscallId::CpuArchId:
#if defined(__x86_64__)
            return 1;
#elif defined(__aarch64__)
            return 3;
#elif defined(__arm__)
            return 2;
#else
            return 0;
#endif

        default:
            return -1;
    }
}

}

#endif
