#include "utils/PlatformUtils.h"
#include "utils/Shutdown.h"
#include "utils/Logger.h"
#include "NetClient.h"
#include "States.h"

#include <signal.h>
#include <stdlib.h>

static volatile sig_atomic_t s_signal_shutdown_requested = 0;
static atomic_bool s_thread_shutdown_requested = ATOMIC_VAR_INIT(false);

void request_shutdown(void)
{
    atomic_store(&s_thread_shutdown_requested, true);
}

bool is_shutdown_requested(void)
{
    return s_signal_shutdown_requested != 0 || atomic_load(&s_thread_shutdown_requested);
}

#ifndef __OPENWRT__
extern void stop_web_server();
#endif

#ifdef _WIN32
#include <windows.h>
extern bool get_service_mode();
#endif

void shut(const int8_t exit_code)
{
    g_need_exit = true;
    LOG_INFO("主程序正在关闭");

#ifndef __OPENWRT__
    LOG_INFO("关闭 Web 服务器");
    stop_web_server();
#endif

    if (g_thread_keep_alive)
    {
        LOG_INFO("关闭线程守护");
        g_thread_keep_alive = false;
    }
    LOG_INFO("清理资源中");
    LOG_DEBUG("关闭线程");
    for (uint8_t i = 0; g_prog_status != NULL && i < g_prog_cnt; i++)
    {
        int result_code = 0;
        g_prog_status[i].runtime_status.is_running = false;
        if (g_prog_status[i].thread)
        {
            sim_thread_join(g_prog_status[i].thread, &result_code);
            free(g_prog_status[i].thread);
        }
        g_prog_status[i].thread = NULL;
        LOG_DEBUG("认证线程退出, 退出码: %d", result_code);
    }
    free(g_prog_status);
    g_prog_status = NULL;
    clean_net_client();
    LOG_INFO("退出程序, 退出码: %" PRIu8, exit_code);
    clean_logger();

#ifdef _WIN32
    if (get_service_mode())
    {
        longjmp(g_exit_jmp, 1);
    }
#endif

    exit(exit_code);
}

#ifdef _WIN32
// Windows 控制台事件处理
static BOOL WINAPI console_handler(const DWORD ctrlType)
{
    switch(ctrlType)
    {
    case CTRL_C_EVENT:
        request_shutdown();
        return TRUE;
    case CTRL_BREAK_EVENT:
        request_shutdown();
        return TRUE;
    case CTRL_CLOSE_EVENT:
        request_shutdown();
        return TRUE;
    case CTRL_LOGOFF_EVENT:
        request_shutdown();
        return TRUE;
    case CTRL_SHUTDOWN_EVENT:
        request_shutdown();
        return TRUE;
    default:
        return FALSE;
    }
}

#else
// Linux/Unix 信号处理
static void signal_handler(const int sig)
{
    (void)sig;
    s_signal_shutdown_requested = 1;
}

#endif

void init_shutdown_hook()
{
#ifdef _WIN32
    if (SetConsoleCtrlHandler(console_handler, TRUE) == 0)
    {
        fprintf(stderr, "[ERROR] 设置控制台事件处理失败\n");
        exit(1);
    }
#else
    if (signal(SIGINT, signal_handler) == SIG_ERR)
    {
        fprintf(stderr, "[ERROR] 信号 SIGINT 设置失败\n");
        exit(1);
    }
    if (signal(SIGTERM, signal_handler) == SIG_ERR)
    {
        fprintf(stderr, "[ERROR] 信号 SIGTERM 设置失败\n");
        exit(1);
    }
    if (signal(SIGHUP, signal_handler) == SIG_ERR)
    {
        fprintf(stderr, "[ERROR] 信号 SIGHUP 设置失败\n");
        exit(1);
    }
    if (signal(SIGQUIT, signal_handler) == SIG_ERR)
    {
        fprintf(stderr, "[ERROR] 信号 SIGQUIT 设置失败\n");
        exit(1);
    }
#endif
}
