#define _CRT_SECURE_NO_WARNINGS

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")

#define DEFAULT_BIND        "127.0.0.1"
#define DEFAULT_PORT        80
#define RESPONSE_SIZE       16384
#define SERVICE_NAME        "RakLicense"

static const char kHead[] =
    "HTTP/1.1 200 OK\n"
    "Server: Apache\n"
    "Access-Control-Allow-Origin: *\n"
    "Content-Type: text/html; charset=UTF-8\n"
    "Content-Length: 12\n"
    "\n"
    "1:1:proverka";

static char g_response[RESPONSE_SIZE];
static char g_log_path[MAX_PATH];
static CRITICAL_SECTION g_log_lock;

static void build_response(void)
{
    memcpy(g_response, kHead, sizeof(kHead) - 1);
    memset(g_response + (sizeof(kHead) - 1), ' ',
           RESPONSE_SIZE - (sizeof(kHead) - 1));
}

static SERVICE_STATUS_HANDLE g_status_handle;
static SERVICE_STATUS g_status;
static volatile LONG g_stop;
static int g_argc;
static char **g_argv;

static void init_log_path(void)
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    char *slash;

    if (n == 0 || n >= MAX_PATH)
        return;

    slash = strrchr(path, '\\');
    if (slash == NULL)
        return;
    slash[1] = 0;

    if (strlen(path) + strlen("raklicense.log") + 1 >= MAX_PATH)
        return;
    strcat(path, "raklicense.log");
    strcpy(g_log_path, path);
}

static void log_line(const char *fmt, ...)
{
    char msg[1024];
    char line[1200];
    SYSTEMTIME st;
    va_list ap;
    HANDLE h;
    DWORD written;

    if (g_log_path[0] == 0)
        return;

    va_start(ap, fmt);
    _vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, ap);
    va_end(ap);

    GetLocalTime(&st);
    _snprintf_s(line, sizeof(line), _TRUNCATE,
                "%04d-%02d-%02d %02d:%02d:%02d [%lu] %s\r\n",
                st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond,
                GetCurrentProcessId(), msg);

    EnterCriticalSection(&g_log_lock);
    h = CreateFileA(g_log_path, FILE_APPEND_DATA,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        WriteFile(h, line, (DWORD)strlen(line), &written, NULL);
        CloseHandle(h);
    }
    LeaveCriticalSection(&g_log_lock);
}

static void set_status(DWORD state, DWORD exit_code)
{
    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState = state;
    g_status.dwControlsAccepted =
        (state == SERVICE_RUNNING) ? (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN) : 0;
    g_status.dwWin32ExitCode = exit_code;
    g_status.dwServiceSpecificExitCode = 0;
    g_status.dwCheckPoint = 0;
    g_status.dwWaitHint =
        (state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING) ? 5000 : 0;

    if (g_status_handle != NULL)
        SetServiceStatus(g_status_handle, &g_status);
}

static void usage(void)
{
    printf("raklicense.exe [--bind 127.0.0.1] [--port 80]\n");
    printf("  Binds loopback only by default. Do not expose port 80 publicly.\n");
}

static int parse_args(int argc, char **argv, const char **bind_addr, int *port)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
            *bind_addr = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            *port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage();
            return 1;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            usage();
            return -1;
        }
    }

    if (*port <= 0 || *port > 65535) {
        fprintf(stderr, "invalid port: %d\n", *port);
        return -1;
    }

    return 0;
}

static DWORD WINAPI client_thread(LPVOID param)
{
    SOCKET c = (SOCKET)(ULONG_PTR)param;
    char req[4096];
    int n = 0;
    int sent = 0;
    DWORD timeout = 3000;

    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO,
               (const char *)&timeout, sizeof(timeout));

    while (n < (int)sizeof(req) - 1) {
        int r = recv(c, req + n, (int)sizeof(req) - 1 - n, 0);
        if (r <= 0)
            break;
        n += r;
        req[n] = 0;
        if (strstr(req, "\r\n\r\n") != NULL || strstr(req, "\n\n") != NULL)
            break;
    }

    if (n > 0) {
        char *eol = strpbrk(req, "\r\n");
        if (eol)
            *eol = 0;
        log_line("request: %s", req);
    } else {
        log_line("request: <empty>");
    }

    while (sent < RESPONSE_SIZE) {
        int r = send(c, g_response + sent, RESPONSE_SIZE - sent, 0);
        if (r <= 0)
            break;
        sent += r;
    }

    shutdown(c, SD_SEND);
    closesocket(c);
    return 0;
}

static int run_server(const char *bind_addr, int port, int is_service)
{
    WSADATA wsa;
    SOCKET s;
    struct sockaddr_in addr;
    int rc;

    build_response();

    rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (rc != 0) {
        log_line("WSAStartup failed: %d", rc);
        return 1;
    }

    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        log_line("socket failed: %d", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    if (InetPtonA(AF_INET, bind_addr, &addr.sin_addr) != 1) {
        log_line("invalid bind address: %s", bind_addr);
        closesocket(s);
        WSACleanup();
        return 1;
    }

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        int err = WSAGetLastError();
        log_line("bind %s:%d failed: %d", bind_addr, port, err);
        closesocket(s);
        WSACleanup();
        return 1;
    }

    if (listen(s, SOMAXCONN) != 0) {
        int err = WSAGetLastError();
        log_line("listen failed: %d", err);
        closesocket(s);
        WSACleanup();
        return 1;
    }

    log_line("listening on %s:%d", bind_addr, port);
    printf("raklicense: listening on %s:%d\n", bind_addr, port);

    if (is_service) {
        if (InterlockedCompareExchange(&g_stop, 0, 0) != 0) {
            closesocket(s);
            WSACleanup();
            return 0;
        }
        set_status(SERVICE_RUNNING, NO_ERROR);
    }

    for (;;) {
        fd_set rfds;
        struct timeval tv;
        int sel;
        SOCKET c;
        HANDLE th;

        if (InterlockedCompareExchange(&g_stop, 0, 0) != 0)
            break;

        FD_ZERO(&rfds);
        FD_SET(s, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 500000;

        sel = select(0, &rfds, NULL, NULL, &tv);
        if (sel == SOCKET_ERROR) {
            log_line("select failed: %d", WSAGetLastError());
            break;
        }
        if (sel == 0)
            continue;

        c = accept(s, NULL, NULL);
        if (c == INVALID_SOCKET) {
            log_line("accept failed: %d", WSAGetLastError());
            break;
        }

        th = CreateThread(NULL, 0, client_thread,
                          (LPVOID)(ULONG_PTR)c, 0, NULL);
        if (th != NULL) {
            CloseHandle(th);
        } else {
            client_thread((LPVOID)(ULONG_PTR)c);
        }
    }

    log_line("stopping listener");
    closesocket(s);
    WSACleanup();
    return 0;
}

static void WINAPI service_ctrl_handler(DWORD ctrl)
{
    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        if (g_status.dwCurrentState == SERVICE_RUNNING ||
            g_status.dwCurrentState == SERVICE_START_PENDING) {
            set_status(SERVICE_STOP_PENDING, NO_ERROR);
            InterlockedExchange(&g_stop, 1);
        }
        break;

    case SERVICE_CONTROL_INTERROGATE:
        set_status(g_status.dwCurrentState, NO_ERROR);
        break;

    default:
        break;
    }
}

static void WINAPI service_main(DWORD argc, LPSTR *argv)
{
    const char *bind_addr = DEFAULT_BIND;
    int port = DEFAULT_PORT;
    int rc;

    (void)argc;
    (void)argv;

    g_status_handle = RegisterServiceCtrlHandlerA(SERVICE_NAME, service_ctrl_handler);
    if (g_status_handle == NULL)
        return;

    set_status(SERVICE_START_PENDING, NO_ERROR);

    init_log_path();
    InitializeCriticalSection(&g_log_lock);
    log_line("service starting");

    if (parse_args(g_argc, g_argv, &bind_addr, &port) != 0) {
        log_line("invalid service arguments");
        set_status(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
        return;
    }

    rc = run_server(bind_addr, port, 1);
    log_line("service stopped, rc=%d", rc);
    set_status(SERVICE_STOPPED, rc == 0 ? NO_ERROR : ERROR_SERVICE_SPECIFIC_ERROR);
}

static BOOL WINAPI console_ctrl_handler(DWORD type)
{
    switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        InterlockedExchange(&g_stop, 1);
        return TRUE;

    default:
        return FALSE;
    }
}

int main(int argc, char **argv)
{
    SERVICE_TABLE_ENTRYA table[] = {
        { (LPSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTIONA)service_main },
        { NULL, NULL }
    };

    g_argc = argc;
    g_argv = argv;

    if (StartServiceCtrlDispatcherA(table))
        return 0;

    if (GetLastError() != ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
        fprintf(stderr, "StartServiceCtrlDispatcher failed: %lu\n", GetLastError());
        return 1;
    }

    {
        const char *bind_addr = DEFAULT_BIND;
        int port = DEFAULT_PORT;
        int rc;

        init_log_path();
        InitializeCriticalSection(&g_log_lock);

        rc = parse_args(argc, argv, &bind_addr, &port);
        if (rc != 0)
            return 2;

        SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
        return run_server(bind_addr, port, 0);
    }
}
