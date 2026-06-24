#include "sixlift/platform.h"

#if defined(_WIN32)

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Paths are kept space-free to avoid schtasks/quoting pain. */
#define BIN      "C:\\sixlift\\sixlift.exe"
#define STATEDIR "C:\\sixlift"
#define STATEF   "C:\\sixlift\\enabled"
#define LOGF     "C:\\sixlift\\sixlift.log"
#define TASK     "sixlift-watchdog"

const char *plat_name(void)       { return "Windows"; }
const char *plat_bin_path(void)   { return BIN; }
const char *plat_state_path(void) { return STATEF; }
const char *plat_state_dir(void)  { return STATEDIR; }
const char *plat_log_path(void)   { return LOGF; }

/* Run a command line, hidden. If out != NULL, capture stdout. Returns exit code. */
static int win_run(const char *cmdline, char *out, size_t outlen)
{
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE rd = NULL, wr = NULL, nul = INVALID_HANDLE_VALUE;

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    if (out) {
        if (!CreatePipe(&rd, &wr, &sa, 0))
            return -1;
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        si.hStdOutput = wr;
        si.hStdError = wr;
    } else {
        nul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa,
                          OPEN_EXISTING, 0, NULL);
        si.hStdOutput = nul;
        si.hStdError = nul;
    }

    char *cl = malloc(strlen(cmdline) + 1);
    if (!cl) {
        if (wr) CloseHandle(wr);
        if (rd) CloseHandle(rd);
        if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
        return -1;
    }
    strcpy(cl, cmdline);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    BOOL ok = CreateProcessA(NULL, cl, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                             NULL, NULL, &si, &pi);
    free(cl);
    if (wr) CloseHandle(wr);

    if (!ok) {
        if (rd) CloseHandle(rd);
        if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
        return -1;
    }

    if (out) {
        size_t off = 0;
        DWORD n;
        char tmp[512];
        while (off + 1 < outlen &&
               ReadFile(rd, tmp, sizeof(tmp), &n, NULL) && n > 0) {
            size_t room = outlen - 1 - off;
            size_t cpy = (n < room) ? n : room;
            memcpy(out + off, tmp, cpy);
            off += cpy;
        }
        out[off] = '\0';
        CloseHandle(rd);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (nul != INVALID_HANDLE_VALUE)
        CloseHandle(nul);
    return (int)code;
}

int plat_is_admin(void)
{
    BOOL is_admin = FALSE;
    PSID admins = NULL;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                 &admins)) {
        if (!CheckTokenMembership(NULL, admins, &is_admin))
            is_admin = FALSE;
        FreeSid(admins);
    }
    return is_admin ? 1 : 0;
}

int plat_self_path(char *buf, size_t buflen)
{
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)buflen);
    return (n > 0 && n < buflen) ? 0 : -1;
}

int plat_mkdir(const char *path)
{
    if (CreateDirectoryA(path, NULL))
        return 0;
    return (GetLastError() == ERROR_ALREADY_EXISTS) ? 0 : -1;
}

int plat_exists(const char *path)
{
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

int plat_copy_exec(const char *src, const char *dst)
{
    CreateDirectoryA(STATEDIR, NULL); /* ensure target dir */
    return CopyFileA(src, dst, FALSE) ? 0 : -1;
}

/* First "up" non-loopback adapter that has a gateway = the path to the net. */
int plat_active_connection(char *conn, size_t conn_len, char *dev, size_t dev_len)
{
    ULONG size = 15000;
    IP_ADAPTER_ADDRESSES *aa = malloc(size);
    if (!aa)
        return -1;

    ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_MULTICAST |
                  GAA_FLAG_SKIP_DNS_SERVER;
    ULONG ret = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, aa, &size);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        free(aa);
        aa = malloc(size);
        if (!aa)
            return -1;
        ret = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, aa, &size);
    }
    if (ret != NO_ERROR) {
        free(aa);
        return -1;
    }

    int rc = -1;
    for (IP_ADAPTER_ADDRESSES *a = aa; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp)
            continue;
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        if (!a->FirstGatewayAddress)
            continue;
        WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1,
                            conn, (int)conn_len, NULL, NULL);
        snprintf(dev, dev_len, "%s", conn);
        rc = 0;
        break;
    }
    free(aa);
    return rc;
}

int plat_set_dns64(const char *conn, const char *dev,
                   const char *const *servers, int count)
{
    (void)conn;
    char list[512];
    size_t off = 0;
    list[0] = '\0';
    for (int i = 0; i < count; i++) {
        int w = snprintf(list + off, sizeof(list) - off, "%s'%s'",
                         i ? "," : "", servers[i]);
        if (w < 0 || (size_t)w >= sizeof(list) - off)
            break;
        off += (size_t)w;
    }
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "powershell -NoProfile -NonInteractive -Command "
             "\"Set-DnsClientServerAddress -InterfaceAlias '%s' "
             "-ServerAddresses (%s)\"",
             dev, list);
    win_run(cmd, NULL, 0);
    win_run("ipconfig /flushdns", NULL, 0);
    return 0;
}

int plat_clear_dns(const char *conn, const char *dev)
{
    (void)conn;
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "powershell -NoProfile -NonInteractive -Command "
             "\"Set-DnsClientServerAddress -InterfaceAlias '%s' "
             "-ResetServerAddresses\"",
             dev);
    win_run(cmd, NULL, 0);
    win_run("ipconfig /flushdns", NULL, 0);
    return 0;
}

int plat_reconnect(const char *conn)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "powershell -NoProfile -NonInteractive -Command "
             "\"Restart-NetAdapter -InterfaceAlias '%s' -Confirm:$false\"",
             conn);
    int rc = win_run(cmd, NULL, 0);
    Sleep(4000);
    return rc;
}

int plat_remove_ipv6_blackhole(void)
{
    /* The IPv6 blackhole kill-switch is a Linux/NetworkManager artifact;
     * it does not occur on Windows. Nothing to do. */
    return 0;
}

int plat_watchdog_install(const char *self_bin)
{
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "schtasks /Create /TN " TASK " /TR \"%s heal\" "
             "/SC MINUTE /MO 2 /RU SYSTEM /RL HIGHEST /F",
             self_bin);
    int rc = win_run(cmd, NULL, 0);
    /* Created enabled by default; match the model (install != on). */
    win_run("schtasks /Change /TN " TASK " /DISABLE", NULL, 0);
    return rc == 0 ? 0 : -1;
}

int plat_watchdog_remove(void)
{
    win_run("schtasks /Delete /TN " TASK " /F", NULL, 0);
    return 0;
}

int plat_watchdog_set(int enabled)
{
    return win_run(enabled
                       ? "schtasks /Change /TN " TASK " /ENABLE"
                       : "schtasks /Change /TN " TASK " /DISABLE",
                   NULL, 0);
}

int plat_watchdog_installed(void)
{
    return win_run("schtasks /Query /TN " TASK, NULL, 0) == 0;
}

int plat_watchdog_active(void)
{
    char out[4096];
    if (win_run("schtasks /Query /TN " TASK " /FO LIST", out, sizeof(out)) != 0)
        return 0;
    return strstr(out, "Disabled") ? 0 : 1;
}

#endif /* _WIN32 */
