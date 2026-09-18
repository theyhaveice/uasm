#include "../syscall_platform.h"

#if defined(_WIN32)

#include "uasm/syscall_ids.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <io.h>

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

bool ensureWinsock() {
    static bool started = false;
    if (!started) {
        WSADATA data;
        started = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    return started;
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
            return _write(static_cast<int>(a0), buf, static_cast<unsigned int>(a2));
        }
        case SyscallId::Read: {
            char* buf = bufAt(mem, memSize, a1, a2);
            if (!buf) return -1;
            return _read(static_cast<int>(a0), buf, static_cast<unsigned int>(a2));
        }
        case SyscallId::Open: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return -1;
            return _open(path, static_cast<int>(a1), static_cast<int>(a2));
        }
        case SyscallId::Close:
            return _close(static_cast<int>(a0));
        case SyscallId::Seek:
            return _lseeki64(static_cast<int>(a0), a1, static_cast<int>(a2));
        case SyscallId::FileSize: {
            int64_t cur = _telli64(static_cast<int>(a0));
            int64_t end = _lseeki64(static_cast<int>(a0), 0, 2);
            _lseeki64(static_cast<int>(a0), cur, 0);
            return end;
        }
        case SyscallId::RemoveFile: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return -1;
            return ::DeleteFileA(path) ? 0 : -1;
        }
        case SyscallId::RenameFile: {
            const char* oldPath = cstrAt(mem, memSize, a0);
            const char* newPath = cstrAt(mem, memSize, a1);
            if (!oldPath || !newPath) return -1;
            return ::MoveFileA(oldPath, newPath) ? 0 : -1;
        }
        case SyscallId::Mkdir: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return -1;
            return ::CreateDirectoryA(path, 0) ? 0 : -1;
        }
        case SyscallId::Rmdir: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return -1;
            return ::RemoveDirectoryA(path) ? 0 : -1;
        }
        case SyscallId::Getcwd: {
            char* buf = bufAt(mem, memSize, a0, a1);
            if (!buf) return -1;
            DWORD n = ::GetCurrentDirectoryA(static_cast<DWORD>(a1), buf);
            return n;
        }
        case SyscallId::Chdir: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return -1;
            return ::SetCurrentDirectoryA(path) ? 0 : -1;
        }
        case SyscallId::FileExists: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return 0;
            return ::GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES ? 1 : 0;
        }
        case SyscallId::Realpath: {
            const char* path = cstrAt(mem, memSize, a0);
            char* buf = bufAt(mem, memSize, a1, a2);
            if (!path || !buf) return -1;
            char resolved[4096];
            DWORD n = ::GetFullPathNameA(path, sizeof(resolved), resolved, 0);
            if (n == 0) return -1;
            return writeResult(buf, a2, resolved);
        }
        case SyscallId::TimeUnixSeconds:
            return static_cast<int64_t>(std::time(0));
        case SyscallId::TimeUnixMillis: {
            FILETIME ft;
            ::GetSystemTimeAsFileTime(&ft);
            uint64_t t = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
            return static_cast<int64_t>(t / 10000 - 11644473600000ULL);
        }
        case SyscallId::TickCountMs:
            return static_cast<int64_t>(::GetTickCount64());
        case SyscallId::SleepMillis:
            ::Sleep(static_cast<DWORD>(a0));
            return 0;
        case SyscallId::GetPid:
            return static_cast<int64_t>(::GetCurrentProcessId());
        case SyscallId::CpuCount: {
            SYSTEM_INFO info;
            ::GetSystemInfo(&info);
            return static_cast<int64_t>(info.dwNumberOfProcessors);
        }
        case SyscallId::PageSize: {
            SYSTEM_INFO info;
            ::GetSystemInfo(&info);
            return static_cast<int64_t>(info.dwPageSize);
        }
        case SyscallId::GetEnv: {
            const char* name = cstrAt(mem, memSize, a0);
            char* buf = bufAt(mem, memSize, a1, a2);
            if (!name || !buf) return -1;
            DWORD n = ::GetEnvironmentVariableA(name, buf, static_cast<DWORD>(a2));
            return n == 0 ? -1 : static_cast<int64_t>(n);
        }
        case SyscallId::SetEnv: {
            const char* name = cstrAt(mem, memSize, a0);
            const char* value = cstrAt(mem, memSize, a1);
            if (!name || !value) return -1;
            return ::SetEnvironmentVariableA(name, value) ? 0 : -1;
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
            for (int64_t i = 0; i < a1; ++i) buf[i] = static_cast<char>(::rand() & 0xFF);
            return 0;
        }
        case SyscallId::IsATty:
            return _isatty(static_cast<int>(a0));
        case SyscallId::Flush:
            return _commit(static_cast<int>(a0));
        case SyscallId::StdinAvailable:
            return 0;

        case SyscallId::DirOpen: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return 0;
            std::string pattern = std::string(path) + "\\*";
            WIN32_FIND_DATAA* data = new WIN32_FIND_DATAA();
            HANDLE h = ::FindFirstFileA(pattern.c_str(), data);
            if (h == INVALID_HANDLE_VALUE) {
                delete data;
                return 0;
            }
            return reinterpret_cast<int64_t>(h);
        }
        case SyscallId::DirRead: {
            HANDLE h = reinterpret_cast<HANDLE>(a0);
            char* buf = bufAt(mem, memSize, a1, a2);
            if (!h || !buf) return -1;
            WIN32_FIND_DATAA data;
            if (!::FindNextFileA(h, &data)) return 0;
            return writeResult(buf, a2, data.cFileName);
        }
        case SyscallId::DirClose: {
            HANDLE h = reinterpret_cast<HANDLE>(a0);
            if (!h) return -1;
            return ::FindClose(h) ? 0 : -1;
        }
        case SyscallId::FileIsDir: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return 0;
            DWORD attrs = ::GetFileAttributesA(path);
            return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
        }
        case SyscallId::FileMtime: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return -1;
            struct _stat st;
            if (_stat(path, &st) != 0) return -1;
            return static_cast<int64_t>(st.st_mtime);
        }
        case SyscallId::FilePermsGet: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return -1;
            struct _stat st;
            if (_stat(path, &st) != 0) return -1;
            return static_cast<int64_t>(st.st_mode & 0777);
        }
        case SyscallId::FilePermsSet: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return -1;
            return _chmod(path, static_cast<int>(a1));
        }
        case SyscallId::CopyFile:
        {
            const char* src = cstrAt(mem, memSize, a0);
            const char* dst = cstrAt(mem, memSize, a1);
            if (!src || !dst) return -1;
            return ::CopyFileA(src, dst, FALSE) ? 0 : -1;
        }
        case SyscallId::DiskFreeSpace: {
            const char* path = cstrAt(mem, memSize, a0);
            if (!path) return -1;
            ULARGE_INTEGER freeBytes;
            if (!::GetDiskFreeSpaceExA(path, &freeBytes, 0, 0)) return -1;
            return static_cast<int64_t>(freeBytes.QuadPart);
        }
        case SyscallId::GetTempDir: {
            char* buf = bufAt(mem, memSize, a0, a1);
            if (!buf) return -1;
            char tmp[MAX_PATH];
            DWORD n = ::GetTempPathA(sizeof(tmp), tmp);
            if (n == 0) return -1;
            return writeResult(buf, a1, tmp);
        }
        case SyscallId::GetExePath: {
            char* buf = bufAt(mem, memSize, a0, a1);
            if (!buf) return -1;
            char path[MAX_PATH];
            DWORD n = ::GetModuleFileNameA(0, path, sizeof(path));
            if (n == 0) return -1;
            return writeResult(buf, a1, path);
        }
        case SyscallId::GetHostname: {
            char* buf = bufAt(mem, memSize, a0, a1);
            if (!buf) return -1;
            DWORD len = static_cast<DWORD>(a1);
            return ::GetComputerNameA(buf, &len) ? static_cast<int64_t>(len) : -1;
        }
        case SyscallId::EnvCount: {
            LPCH env = ::GetEnvironmentStringsA();
            if (!env) return 0;
            int64_t n = 0;
            for (LPCH p = env; *p; p += std::strlen(p) + 1) ++n;
            ::FreeEnvironmentStringsA(env);
            return n;
        }
        case SyscallId::EnvGet: {
            char* buf = bufAt(mem, memSize, a1, a2);
            if (!buf) return -1;
            LPCH env = ::GetEnvironmentStringsA();
            if (!env) return -1;
            int64_t idx = 0;
            int64_t result = -1;
            for (LPCH p = env; *p; p += std::strlen(p) + 1) {
                if (idx == a0) {
                    result = writeResult(buf, a2, p);
                    break;
                }
                ++idx;
            }
            ::FreeEnvironmentStringsA(env);
            return result;
        }
        case SyscallId::TermColumns:
        case SyscallId::TermRows: {
            CONSOLE_SCREEN_BUFFER_INFO info;
            if (!::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE), &info)) return -1;
            return id == SyscallId::TermColumns ? info.dwSize.X : info.dwSize.Y;
        }
        case SyscallId::FileLock:
        case SyscallId::FileUnlock:
            return 0;

        case SyscallId::SocketCreate:
            if (!ensureWinsock()) return -1;
            return static_cast<int64_t>(::socket(static_cast<int>(a0), static_cast<int>(a1), 0));
        case SyscallId::SocketConnect: {
            const char* host = cstrAt(mem, memSize, a1);
            if (!host) return -1;
            struct sockaddr_in addr;
            std::memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(static_cast<u_short>(a2));
            addr.sin_addr.s_addr = ::inet_addr(host);
            return ::connect(static_cast<SOCKET>(a0), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        }
        case SyscallId::SocketBind: {
            struct sockaddr_in addr;
            std::memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(static_cast<u_short>(a2));
            addr.sin_addr.s_addr = INADDR_ANY;
            return ::bind(static_cast<SOCKET>(a0), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        }
        case SyscallId::SocketListen:
            return ::listen(static_cast<SOCKET>(a0), static_cast<int>(a1));
        case SyscallId::SocketAccept:
            return static_cast<int64_t>(::accept(static_cast<SOCKET>(a0), 0, 0));
        case SyscallId::SocketSend: {
            char* buf = bufAt(mem, memSize, a1, a2);
            if (!buf) return -1;
            return ::send(static_cast<SOCKET>(a0), buf, static_cast<int>(a2), 0);
        }
        case SyscallId::SocketRecv: {
            char* buf = bufAt(mem, memSize, a1, a2);
            if (!buf) return -1;
            return ::recv(static_cast<SOCKET>(a0), buf, static_cast<int>(a2), 0);
        }
        case SyscallId::SocketClose:
            return ::closesocket(static_cast<SOCKET>(a0));
        case SyscallId::DnsResolve: {
            const char* host = cstrAt(mem, memSize, a0);
            char* buf = bufAt(mem, memSize, a1, a2);
            if (!host || !buf) return -1;
            if (!ensureWinsock()) return -1;
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
            void* p = ::VirtualAlloc(0, static_cast<SIZE_T>(a0), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            return reinterpret_cast<int64_t>(p);
        }
        case SyscallId::MemPageProtect: {
            DWORD newProtect = PAGE_NOACCESS;
            bool r = (a2 & 1) != 0;
            bool w = (a2 & 2) != 0;
            bool x = (a2 & 4) != 0;
            if (x && w) newProtect = PAGE_EXECUTE_READWRITE;
            else if (x && r) newProtect = PAGE_EXECUTE_READ;
            else if (x) newProtect = PAGE_EXECUTE;
            else if (w) newProtect = PAGE_READWRITE;
            else if (r) newProtect = PAGE_READONLY;
            DWORD oldProtect;
            return ::VirtualProtect(reinterpret_cast<void*>(a0), static_cast<SIZE_T>(a1), newProtect, &oldProtect) ? 0 : -1;
        }
        case SyscallId::CpuArchId: {
            SYSTEM_INFO info;
            ::GetNativeSystemInfo(&info);
            switch (info.wProcessorArchitecture) {
                case PROCESSOR_ARCHITECTURE_AMD64: return 1;
                case PROCESSOR_ARCHITECTURE_ARM64: return 3;
                case PROCESSOR_ARCHITECTURE_ARM: return 2;
                default: return 0;
            }
        }

        default:
            return -1;
    }
}

}

#endif
