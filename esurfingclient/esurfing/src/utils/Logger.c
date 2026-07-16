#include "utils/PlatformUtils.h"
#include "utils/Logger.h"

#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#define LOGGER_LOCK_INIT SRWLOCK_INIT
typedef SRWLOCK logger_lock_t;
static void logger_lock(logger_lock_t* lock) { AcquireSRWLockExclusive(lock); }
static void logger_unlock(logger_lock_t* lock) { ReleaseSRWLockExclusive(lock); }
#else
#include <pthread.h>
#define LOGGER_LOCK_INIT PTHREAD_MUTEX_INITIALIZER
typedef pthread_mutex_t logger_lock_t;
static void logger_lock(logger_lock_t* lock) { pthread_mutex_lock(lock); }
static void logger_unlock(logger_lock_t* lock) { pthread_mutex_unlock(lock); }
#endif

static const char s_file_name[] = "run.log";
static logger_lock_t s_logger_lock = LOGGER_LOCK_INIT;

#ifdef __OPENWRT__
#define DEFAULT_MAX_LOG_LINES 2000
#define MAX_LOG_FILE_SIZE (256 * 1024)
#define MAX_ROTATED_LOG_FILES 2
#define LOG_TO_CONSOLE 0
#else
#define DEFAULT_MAX_LOG_LINES 10000
#define MAX_LOG_FILE_SIZE (1024 * 1024)
#define MAX_ROTATED_LOG_FILES 8
#define LOG_TO_CONSOLE 1
#endif

static log_cfg_t s_logger_cfg = {
    .lv = LOG_LEVEL_WARN,
    .log_dir = "",
    .log_file = "",
    .file_handle = NULL,
    .max_lines = DEFAULT_MAX_LOG_LINES,
    .cur_lines = 0
};

static const char* get_level_str(const LogLevel lv)
{
    switch (lv)
    {
    case LOG_LEVEL_VERBOSE: return "VERBOSE";
    case LOG_LEVEL_DEBUG:   return "DEBUG";
    case LOG_LEVEL_INFO:    return "INFO";
    case LOG_LEVEL_WARN:    return "WARN";
    case LOG_LEVEL_ERROR:   return "ERROR";
    case LOG_LEVEL_FATAL:   return "FATAL";
    default:                return "UNKNOWN";
    }
}

static void rotate()
{
    if (!s_logger_cfg.file_handle || strlen(s_logger_cfg.log_file) == 0) return;
    if (s_logger_cfg.cur_lines < s_logger_cfg.max_lines && s_logger_cfg.cur_bytes < MAX_LOG_FILE_SIZE) return;
    fclose(s_logger_cfg.file_handle);
    s_logger_cfg.file_handle = NULL;
    for (int i = MAX_ROTATED_LOG_FILES; i >= 1; i--)
    {
        char old_name[PATH_MAX];
        char new_name[PATH_MAX];
        if (i == 1)
        {
            snprintf(old_name, sizeof(old_name), "%s", s_logger_cfg.log_file);
        }
        else
        {
            snprintf(old_name, sizeof(old_name), "%s.%d", s_logger_cfg.log_file, i - 1);
        }
        snprintf(new_name, sizeof(new_name), "%s.%d", s_logger_cfg.log_file, i);
        if (i == MAX_ROTATED_LOG_FILES) remove(new_name);
        rename(old_name, new_name);
    }
    s_logger_cfg.cur_lines = 0;
    s_logger_cfg.cur_bytes = 0;
    s_logger_cfg.file_handle = fopen(s_logger_cfg.log_file, "a");
    if (s_logger_cfg.file_handle == NULL) fprintf(stderr, "[ERROR] failed to reopen log file after rotate: %s\n", s_logger_cfg.log_file);
    return;
}

static bool get_log_dir(char* out)
{
#ifdef _WIN32
    char dir[PATH_MAX];
    if (get_exec_dir(dir) == false) return false;
    const int len = snprintf(out, PATH_MAX, "%s%clogs", safe_str(dir), SEP);
    if (len < 0 || (size_t)len >= PATH_MAX) return false;
    if (!CreateDirectoryA(out, NULL))
    {
        const DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) return false;
    }
#else
    const char dir[] = "/var/log/esurfing";
    const int len = snprintf(out, PATH_MAX, "%s%clogs", dir, SEP);
    if (len < 0 || (size_t)len >= PATH_MAX) return false;
    struct stat st;
    if (stat(out, &st) != 0)
    {
        if (mkdir("/var", 0755) != 0 && errno != EEXIST) return false;
        if (mkdir("/var/log", 0755) != 0 && errno != EEXIST) return false;
        if (mkdir(dir, 0755) != 0 && errno != EEXIST) return false;
        if (mkdir(out, 0755) != 0 && errno != EEXIST) return false;
    }
    else if (!S_ISDIR(st.st_mode)) return false;
#endif
    return true;
}

static void write_2_console(const char* msg)
{
#if LOG_TO_CONSOLE
    printf("%s", msg);
    fflush(stdout);
#else
    (void)msg;
#endif
}

static void write_2_file(const char* msg)
{
    if (s_logger_cfg.file_handle)
    {
        const int written = fprintf(s_logger_cfg.file_handle, "%s", msg);
        fflush(s_logger_cfg.file_handle);
        if (written > 0) s_logger_cfg.cur_bytes += (size_t)written;
    }
}

static const char* get_thread_str(char* buf, const size_t buf_size)
{
    if (tl_thread_idx >= 0)
    {
        snprintf(buf, buf_size, "%" PRId8, tl_thread_idx);
        return buf;
    }
    if (tl_thread_idx == -1)
    {
        return "Main";
    }
    return "WebServer";
}

void log_out(const LogLevel level, const char* file, const uint32_t line, const char* fmt, ...)
{
    logger_lock(&s_logger_lock);
    if (level > s_logger_cfg.lv)
    {
        logger_unlock(&s_logger_lock);
        return;
    }
    if (!s_logger_cfg.file_handle)
    {
        fprintf(stderr, "[ERROR] 日志系统未打开, 无法输出日志\n");
        logger_unlock(&s_logger_lock);
        return;
    }
    va_list local_args;
    char ts[32] = {0};
    char msg[2048];
    char final_msg[2560];
    char thread_str[16];
    get_fmt_time(ts, CONSOLE_FORMAT);
    va_start(local_args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, local_args);
    va_end(local_args);
    snprintf(final_msg, sizeof(final_msg),
        "[%s] [TID %" PRIu64 "] [T-%s] [%s] [%s:%" PRIu32 "] %s\n",
        safe_str(ts),
        sim_thread_cur_id(),
        get_thread_str(thread_str, sizeof(thread_str)),
        get_level_str(level),
        strrchr(file, '/') ? strrchr(file, '/') + 1 : strrchr(file, '\\') ? strrchr(file, '\\') + 1 : file,
        line,
        safe_str(msg));
    write_2_console(final_msg);
    write_2_file(final_msg);
    s_logger_cfg.cur_lines++;
    rotate();
    logger_unlock(&s_logger_lock);
}

LogLevel get_logger_level()
{
    logger_lock(&s_logger_lock);
    const LogLevel level = s_logger_cfg.lv;
    logger_unlock(&s_logger_lock);
    return level;
}

void set_logger_level(const LogLevel lv)
{
    bool changed = false;
    logger_lock(&s_logger_lock);
    if (s_logger_cfg.lv != lv)
    {
        s_logger_cfg.lv = lv;
        changed = true;
    }
    logger_unlock(&s_logger_lock);
    if (changed) LOG_INFO("设置日志等级为 [%s]", get_level_str(lv));
}

bool init_logger()
{
    if (get_log_dir(s_logger_cfg.log_dir) == false)
    {
        fprintf(stderr, "[ERROR] 无法准备日志目录\n");
        return false;
    }
    const int len = snprintf(s_logger_cfg.log_file, sizeof(s_logger_cfg.log_file), "%s%c%s", safe_str(s_logger_cfg.log_dir), SEP, s_file_name);
    if (len < 0 || (size_t)len >= sizeof(s_logger_cfg.log_file))
    {
        fprintf(stderr, "[ERROR] 日志文件路径太长 (最大 %zu)\n", sizeof(s_logger_cfg.log_file));
        return false;
    }
    logger_lock(&s_logger_lock);
    s_logger_cfg.file_handle = fopen(s_logger_cfg.log_file, "a");
    if (!s_logger_cfg.file_handle)
    {
        logger_unlock(&s_logger_lock);
        fprintf(stderr, "[ERROR] 无法打开日志文件 %s, 如果是 Linux 系统请使用 sudo 运行程序\n", s_logger_cfg.log_file);
        return false;
    }
    logger_unlock(&s_logger_lock);
    LOG_DEBUG("日志系统初始化完成");
    LOG_DEBUG("日志等级: %s", get_level_str(s_logger_cfg.lv));
    return true;
}

void clean_logger()
{
    logger_lock(&s_logger_lock);
    if (s_logger_cfg.file_handle)
    {
        fclose(s_logger_cfg.file_handle);
        s_logger_cfg.file_handle = NULL;
    }
    logger_unlock(&s_logger_lock);
}
