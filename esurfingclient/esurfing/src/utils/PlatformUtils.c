#include "utils/PlatformUtils.h"
#include "utils/Shutdown.h"
#include "utils/Logger.h"
#include "utils/cJSON.h"
#include "States.h"

#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32

#include <sysinfoapi.h>
#include <iphlpapi.h>

#endif

#ifdef __OPENWRT__
static const char config_file[] = "/etc/config/esurfingclient";
#else
#define DIALER_CONFIG_FILE "ESurfingClient.json"
static char config_file[PATH_MAX + 1 + sizeof(DIALER_CONFIG_FILE)];
#endif

#define MAX_CONFIG_FILE_SIZE (1024 * 1024)

typedef struct
{
    char ip[IP_LEN];
    char name[NAME_LENGTH];
} adapter_t;

static const char s_default_cfg[] = "{\n"
                                    "   \"enabled\": false,\n"
                                    "   \"log_lv\": 3,\n"
                                    "   \"accounts\": [\n"
                                    "       {\n"
                                    "           \"username\": \"\",\n"
                                    "           \"password\": \"\",\n"
                                    "           \"channel\": \"phone\",\n"
                                    "           \"mark\": \"\"\n"
                                    "       }\n"
                                    "   ]\n"
                                    "}\n";

static adapter_t* s_adaptor = NULL;
static uint8_t s_adaptor_count = 0;

static void get_adapters()
{
#ifdef _WIN32
    PIP_ADAPTER_INFO p_adapter_info = NULL;
    ULONG ul_out_buf_len = 0;
    if (GetAdaptersInfo(p_adapter_info, &ul_out_buf_len) == ERROR_BUFFER_OVERFLOW)
    {
        p_adapter_info = (PIP_ADAPTER_INFO)malloc(ul_out_buf_len);
        if (p_adapter_info && GetAdaptersInfo(p_adapter_info, &ul_out_buf_len) == NO_ERROR)
        {
            PIP_ADAPTER_INFO p_adapter = p_adapter_info;
            uint8_t cnt = 0;
            while (p_adapter)
            {
                adapter_t* new_adaptor = realloc(s_adaptor, sizeof(adapter_t) * (cnt + 1));
                if (!new_adaptor)
                {
                    LOG_ERROR("分配内存失败");
                    break;
                }
                s_adaptor = new_adaptor;
                snprintf(s_adaptor[cnt].name, NAME_LENGTH, "%s", p_adapter->Description);
                snprintf(s_adaptor[cnt].ip, IP_LEN, "%s", p_adapter->IpAddressList.IpAddress.String);
                LOG_VERBOSE("IP: %s", p_adapter->IpAddressList.IpAddress.String);
                p_adapter = p_adapter->Next;
                cnt++;
            }
            s_adaptor_count = cnt;
        }
    }
    if (p_adapter_info) free(p_adapter_info);
#else
    struct ifaddrs* ifaddrs_ptr, *ifa;
    if (getifaddrs(&ifaddrs_ptr) == 0)
    {
        uint8_t cnt = 0;
        for (ifa = ifaddrs_ptr; ifa; ifa = ifa->ifa_next)
        {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            if (strcmp(ifa->ifa_name, "lo") == 0) continue;
            char ip[INET_ADDRSTRLEN];
            struct sockaddr_in *addr = (struct sockaddr_in*)ifa->ifa_addr;
            if (inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip)))
            {
                adapter_t* new_adaptor = realloc(s_adaptor, sizeof(adapter_t) * (cnt + 1));
                if (!new_adaptor)
                {
                    LOG_ERROR("分配内存失败");
                    break;
                }
                s_adaptor = new_adaptor;
                snprintf(s_adaptor[cnt].name, NAME_LENGTH, "%s", ifa->ifa_name);
                snprintf(s_adaptor[cnt].ip, IP_LEN, "%s", ip);
                cnt++;
            }
        }
        s_adaptor_count = cnt;
        freeifaddrs(ifaddrs_ptr);
    }
#endif
}

char* get_adapters_json()
{
    get_adapters();
    cJSON* root = cJSON_CreateObject();
    cJSON* adapters = cJSON_CreateArray();
    for (uint8_t i = 0; i < s_adaptor_count; i++)
    {
        if (strlen(s_adaptor[i].name) == 0) break;
        cJSON* adapter = cJSON_CreateObject();
        cJSON_AddStringToObject(adapter, "name", s_adaptor[i].name);
        cJSON_AddStringToObject(adapter, "ip", s_adaptor[i].ip);
        cJSON_AddItemToArray(adapters, adapter);
    }
    cJSON_AddItemToObject(root, "adapters", adapters);
    cJSON_AddStringToObject(root, "school_network_symbol", g_school_network_symbol);
    char* json = cJSON_Print(root);
    cJSON_Delete(root);
    return json;
}

bool get_exec_dir(char* dir_array)
{
#ifdef _WIN32
    char path[MAX_PATH];
    const DWORD len_d = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (len_d == 0 || len_d >= MAX_PATH) return false;
    char* last = strrchr(path, SEP);
    if (!last) return false;
    *last = '\0';
    const uint16_t len = snprintf(dir_array, PATH_MAX, "%s", safe_str(path));
    if ((size_t)len >= PATH_MAX) return false;
    return true;
#elif __linux__
    char path[PATH_MAX];
    const ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len <= 0 || len >= (ssize_t)sizeof(path)) return false;
    path[len] = '\0';
    char* last = strrchr(path, '/');
    if (!last) return false;
    *last = '\0';
    const uint16_t n = snprintf(dir_array, PATH_MAX, "%s", path);
    if ((size_t)n >= PATH_MAX) return false;
    return true;
#elif defined(__APPLE__)
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) return false;
    char* resolved = realpath(path, NULL);
    if (!resolved) return false;
    char* last = strrchr(resolved, '/');
    if (!last) { free(resolved); return false; }
    *last = '\0';
    const uint16_t n = snprintf(dir_array, PATH_MAX, "%s", resolved);
    free(resolved);
    if ((size_t)n >= PATH_MAX) return false;
    return true;
#else
    (void)dir_array;
    return false;
#endif
}

char* xml_parser(const char* xml_data, const char* tag)
{
    if (xml_data == NULL || tag == NULL) return NULL;

    char start_tag[256];
    snprintf(start_tag, sizeof(start_tag), "<%s>", tag);

    char end_tag[256];
    snprintf(end_tag, sizeof(end_tag), "</%s>", tag);

    const char* start_pos = strstr(xml_data, start_tag);
    if (!start_pos) return NULL;
    start_pos += strlen(start_tag);

    const char* end_pos = strstr(start_pos, end_tag);
    if (!end_pos) return NULL;

    const size_t content_length = end_pos - start_pos;
    if (content_length <= 0) return NULL;

    char* content = malloc(content_length + 1);
    if (!content) return NULL;

    strncpy(content, start_pos, content_length);
    content[content_length] = '\0';
    return content;
}

bytes_t str2bytes(const char* str)
{
    bytes_t ba = {0};
    if (!str) return ba;
    ba.length = strlen(str);
    ba.data = (uint8_t*)malloc(ba.length);
    if (ba.data) memcpy(ba.data, str, ba.length);
    return ba;
}

uint64_t str2uint64(const char* str)
{
    if (!str) return 0;
    while (isspace((unsigned char)*str)) str++;
    if (*str == '\0' || *str == '-') return 0;
    char* end_ptr;
    errno = 0;
    const uint64_t value = strtoull(str, &end_ptr, 10);
    if (errno == ERANGE) return 0;
    if (end_ptr == str) return 0;
    while (isspace((unsigned char)*end_ptr)) end_ptr++;
    if (*end_ptr != '\0') return 0;
    return value;
}

#ifdef __OPENWRT__
static bool parse_hex_uint32(const char* str, uint32_t* value)
{
    if (!str || !value || *str == '\0' || *str == '-' || *str == '+') return false;
    char* end_ptr;
    errno = 0;
    const unsigned long long parsed = strtoull(str, &end_ptr, 16);
    if (errno == ERANGE || end_ptr == str || *end_ptr != '\0' || parsed > UINT32_MAX) return false;
    *value = (uint32_t)parsed;
    return true;
}
#endif

char* uint642str(const uint64_t num)
{
    char* result = malloc(22);
    if (!result) return NULL;
    snprintf(result, 22, "%" PRIu64, num);
    return result;
}

uint64_t get_cur_tm_ms()
{
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER uli;
    GetSystemTimeAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart / 10000LL - 11644473600000LL;
#else
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) return 0;
    return tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
#endif
}

void get_rand_bytes(uint8_t* buf, const size_t len)
{
    if (!buf || len == 0) return;
    memset(buf, 0, len);
#ifdef _WIN32
    if (len > UINT32_MAX) return;
    HCRYPTPROV h_crypt_prov;
    if (!CryptAcquireContext(&h_crypt_prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) return;
    CryptGenRandom(h_crypt_prov, (DWORD)len, buf);
    CryptReleaseContext(h_crypt_prov, 0);
#else
    const int fd = open("/dev/urandom", O_RDONLY);
    if (fd == -1) return;
    size_t offset = 0;
    while (offset < len)
    {
        const ssize_t bytes_read = read(fd, buf + offset, len - offset);
        if (bytes_read > 0)
        {
            offset += (size_t)bytes_read;
            continue;
        }
        if (bytes_read < 0 && errno == EINTR) continue;
        break;
    }
    close(fd);
#endif
}

void sleep_ms(const uint64_t ms, const bool can_stop)
{
    if (ms == 0) return;

    uint64_t elapsed = 0;

    while (elapsed < ms && (can_stop ? g_thread_keep_alive : true))
    {
        if (tl_thread_idx >= 0 && tl_thread_idx < g_prog_cnt && g_prog_status != NULL)
        {
            if (g_prog_status[tl_thread_idx].runtime_status.is_running == false || g_prog_status[tl_thread_idx].runtime_status.is_need_reset)
            {
                return;
            }
        }
        else
        {
            if (g_need_exit || is_shutdown_requested())
            {
                return;
            }
        }
        const uint64_t SEGMENT_MS = 100;
        const uint64_t sleep_time = ms - elapsed < SEGMENT_MS ? ms - elapsed : SEGMENT_MS;

#ifdef _WIN32
        Sleep(sleep_time);
#else
        usleep(sleep_time * 1000);
#endif
        elapsed += sleep_time;
    }
}

void get_fmt_time(char* buf, const TimeFormat fmt)
{
    time_t raw_tm;
    if (time(&raw_tm) == (time_t) - 1)
    {
        fprintf(stderr, "ERROR: 获取系统时间失败\n");
        return;
    }
    struct tm local_tm;
#ifdef _WIN32
    if (localtime_s(&local_tm, &raw_tm) != 0)
    {
        fprintf(stderr, "ERROR: 时间转换失败\n");
        return;
    }
#else
    if (localtime_r(&raw_tm, &local_tm) == NULL)
    {
        fprintf(stderr, "ERROR: 时间转换失败\n");
        return;
    }
#endif
    switch (fmt)
    {
    case CONSOLE_FORMAT:
        if (strftime(buf, 32, "%Y-%m-%d %H:%M:%S", &local_tm) == 0)
        {
            fprintf(stderr, "ERROR: 格式化时间失败\n");
            return;
        }
        return;
    case FILE_FORMAT:
        if (strftime(buf, 32, "%Y%m%d-%H%M%S", &local_tm) == 0)
        {
            fprintf(stderr, "ERROR: 格式化时间失败\n");
        }
    }
}

const char* safe_str(const char* str)
{
    return str ? str : "";
}

char* create_xml_payload(const XmlChoose choose)
{
    char cur_tm[32] = {0};
    get_fmt_time(cur_tm, CONSOLE_FORMAT);
    static _Thread_local char xml[XML_BUFFER_SIZE];
    LOG_DEBUG("XML 选择代码: %d", choose);
    uint16_t xml_len = 0;
    switch (choose)
    {
    case GET_TICKET:
        xml_len = snprintf(xml, XML_BUFFER_SIZE,
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<request>\n"
            "    <user-agent>%s</user-agent>\n"
            "    <client-id>%s</client-id>\n"
            "    <local-time>%s</local-time>\n"
            "    <host-name>%s</host-name>\n"
            "    <ipv4>%s</ipv4>\n"
            "    <ipv6></ipv6>\n"
            "    <mac>%s</mac>\n"
            "    <ostag>%s</ostag>\n"
            "    <gwip>%s</gwip>\n"
            "</request>\n",
            safe_str(g_prog_status[tl_thread_idx].login_cfg.user_agent),
            safe_str(g_prog_status[tl_thread_idx].auth_cfg.client_id),
            safe_str(cur_tm),
            safe_str(g_prog_status[tl_thread_idx].auth_cfg.host_name),
            safe_str(g_prog_status[tl_thread_idx].auth_cfg.client_ip),
            safe_str(g_prog_status[tl_thread_idx].auth_cfg.mac_addr),
            safe_str(g_prog_status[tl_thread_idx].auth_cfg.host_name),
            safe_str(g_prog_status[tl_thread_idx].auth_cfg.ac_ip)
        );
        break;
    case LOGIN:
        xml_len = snprintf(xml, XML_BUFFER_SIZE,
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<request>\n"
            "    <user-agent>%s</user-agent>\n"
            "    <client-id>%s</client-id>\n"
            "    <ticket>%s</ticket>\n"
            "    <local-time>%s</local-time>\n"
            "    <userid>%s</userid>\n"
            "    <passwd>%s</passwd>\n"
            "</request>\n",
            safe_str(g_prog_status[tl_thread_idx].login_cfg.user_agent),
            safe_str(g_prog_status[tl_thread_idx].auth_cfg.client_id),
            safe_str(g_prog_status[tl_thread_idx].auth_cfg.ticket),
            safe_str(cur_tm),
            safe_str(g_prog_status[tl_thread_idx].login_cfg.usr),
            safe_str(g_prog_status[tl_thread_idx].login_cfg.pwd)
        );
        break;
    case HEART_BEAT:
    case TERM:
        xml_len = snprintf(xml, XML_BUFFER_SIZE,
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<request>\n"
            "    <user-agent>%s</user-agent>\n"
            "    <client-id>%s</client-id>\n"
            "    <local-time>%s</local-time>\n"
            "    <host-name>%s</host-name>\n"
            "    <ipv4>%s</ipv4>\n"
            "    <ticket>%s</ticket>\n"
            "    <ipv6></ipv6>\n"
            "    <mac>%s</mac>\n"
            "    <ostag>%s</ostag>\n"
            "</request>\n",
            safe_str(g_prog_status[tl_thread_idx].login_cfg.user_agent),
            safe_str(g_prog_status[tl_thread_idx].auth_cfg.client_id),
            safe_str(cur_tm),
            safe_str(g_prog_status[tl_thread_idx].auth_cfg.host_name),
            safe_str(g_prog_status[tl_thread_idx].auth_cfg.client_ip),
            safe_str(g_prog_status[tl_thread_idx].auth_cfg.ticket),
            safe_str(g_prog_status[tl_thread_idx].auth_cfg.mac_addr),
            safe_str(g_prog_status[tl_thread_idx].auth_cfg.host_name)
        );
        break;
    default:
        LOG_ERROR("XML 选择代码错误");
        return NULL;
    }
    if (xml_len <= 0)
    {
        LOG_ERROR("XML 创建失败");
        return NULL;
    }
    if (xml_len >= XML_BUFFER_SIZE)
    {
        LOG_ERROR("XML 内容过长 (需要 %d 字节，但缓冲区只有 %d 字节)", xml_len + 1, XML_BUFFER_SIZE);
        return NULL;
    }
    LOG_DEBUG("创建 XML 完成");
    LOG_VERBOSE("XML 内容为:\n%s", xml);
    return xml;
}

char* extract_between_tags(const char* text, const char* start_tag, const char* end_tag)
{
    if (!text)
    {
        LOG_ERROR("传入文本为空");
        return NULL;
    }
    char* start = strstr(text, start_tag);
    if (!start)
    {
        LOG_ERROR("未找到开头标签: %s", start_tag);
        return NULL;
    }
    start += strlen(start_tag);
    char* end = strstr(start, end_tag);
    if (!end)
    {
        LOG_WARN("未找到结尾标签: %s, 返回", end_tag);
        return NULL;
    }
    const size_t len = end - start;
    if (len == 0) LOG_WARN("提取到空内容 (标签: %s...%s)", start_tag, end_tag);
    char* result = malloc(len + 1);
    if (!result)
    {
        LOG_ERROR("分配内存失败");
        return NULL;
    }
    memcpy(result, start, len);
    result[len] = '\0';
    return result;
}

char* clean_CDATA(const char* text)
{
    return extract_between_tags(text, "<![CDATA[", "]]>");
}

bool save_cfg(char* configs_str)
{
    (void)configs_str;
    LOG_INFO("保存配置中");
    LOG_INFO("仅会保存第一个可用配置");

    cJSON* cfg_json = cJSON_CreateObject();
    cJSON* accounts = cJSON_CreateArray();
    cJSON* account = cJSON_CreateObject();
    if (!cfg_json || !accounts || !account || !g_prog_status || g_prog_cnt <= 0)
    {
        cJSON_Delete(cfg_json);
        cJSON_Delete(accounts);
        cJSON_Delete(account);
        LOG_ERROR("保存配置时无法创建 JSON 数据");
        return false;
    }

    cJSON_AddBoolToObject(cfg_json, "enabled", g_prog_enabled);
    cJSON_AddNumberToObject(cfg_json, "log_lv", get_logger_level());
    cJSON_AddStringToObject(account, "username", g_prog_status[0].login_cfg.usr);
    cJSON_AddStringToObject(account, "password", g_prog_status[0].login_cfg.pwd);
    cJSON_AddStringToObject(account, "channel", g_prog_status[0].login_cfg.chn);
    cJSON_AddItemToArray(accounts, account);
    cJSON_AddItemToObject(cfg_json, "accounts", accounts);

    char* json = cJSON_Print(cfg_json);
    cJSON_Delete(cfg_json);
    if (!json)
    {
        LOG_ERROR("保存配置时无法序列化 JSON 数据");
        return false;
    }

    FILE* cfg_file = fopen(config_file, "w");
    if (!cfg_file)
    {
        LOG_ERROR("无法生成文件: %s", config_file);
        free(json);
        return false;
    }
    const bool write_ok = fputs(json, cfg_file) != EOF && fflush(cfg_file) == 0;
    const bool close_ok = fclose(cfg_file) == 0;

    free(json);
    if (!write_ok || !close_ok)
    {
        LOG_ERROR("配置文件写入失败: %s", config_file);
        return false;
    }
    return true;
}

bool load_cfg()
{
#ifndef __OPENWRT__

    char dir[PATH_MAX];
    if (get_exec_dir(dir) == false)
    {
        LOG_ERROR("获取可执行文件路径失败, 请检查权限后重启");
        while (true)
        {
            if (g_need_exit || is_shutdown_requested())
            {
                return false;
            }
            sleep_ms(10000, true);
        }
    }
    snprintf(config_file, PATH_MAX + 1 + sizeof(DIALER_CONFIG_FILE), "%s%c%s", safe_str(dir), SEP, DIALER_CONFIG_FILE);

#endif

    FILE* cfg_file = fopen(config_file, "r");
    bool create_default = cfg_file == NULL;
    if (cfg_file != NULL && fgetc(cfg_file) == EOF)
    {
        fclose(cfg_file);
        cfg_file = NULL;
        create_default = true;
    }
    if (create_default)
    {
        LOG_ERROR("无法打开配置文件或配置文件为空: %s", config_file);
        LOG_INFO("创建新的默认配置文件");
        FILE* new_cfg = fopen(config_file, "w");
        if (!new_cfg)
        {
            LOG_FATAL("无法生成文件: %s, 请检查权限后重启", config_file);
            while (true)
            {
                if (g_need_exit || is_shutdown_requested())
                {
                    return false;
                }
                sleep_ms(10000, true);
            }
        }
        fprintf(new_cfg, "%s", s_default_cfg);
        fclose(new_cfg);
        LOG_INFO("创建完成, 请在 %s 填写账号数据, 然后重启", config_file);
        while (true)
        {
            if (g_need_exit || is_shutdown_requested())
            {
                return false;
            }
            sleep_ms(10000, true);
        }
    }

    if (fseek(cfg_file, 0, SEEK_END) != 0)
    {
        LOG_FATAL("无法定位配置文件末尾");
        fclose(cfg_file);
        return false;
    }
    const long len = ftell(cfg_file);
    if (len < 0 || len > MAX_CONFIG_FILE_SIZE)
    {
        LOG_FATAL("配置文件大小无效或超过 %d 字节", MAX_CONFIG_FILE_SIZE);
        fclose(cfg_file);
        return false;
    }
    if (fseek(cfg_file, 0, SEEK_SET) != 0)
    {
        LOG_FATAL("无法回到配置文件开头");
        fclose(cfg_file);
        return false;
    }

    char* cfg_data = malloc(len + 1);
    if (!cfg_data)
    {
        LOG_FATAL("读取配置文件时分配内存失败");
        fclose(cfg_file);
        return false;
    }
    const size_t bytes_read = fread(cfg_data, 1, (size_t)len, cfg_file);
    if (bytes_read != (size_t)len)
    {
        LOG_FATAL("配置文件读取不完整");
        free(cfg_data);
        fclose(cfg_file);
        return false;
    }
    cfg_data[len] = '\0';
    fclose(cfg_file);

    cJSON* cfg_json = cJSON_Parse(cfg_data);
    free(cfg_data);
    if (!cfg_json)
    {
        LOG_FATAL("JSON 解析失败, 请检查后重启");
        while (true)
        {
            if (g_need_exit || is_shutdown_requested())
            {
                return false;
            }
            sleep_ms(10000, true);
        }
    }

    const cJSON* log_lv = cJSON_GetObjectItem(cfg_json, "log_lv");
    if (log_lv && cJSON_IsNumber(log_lv) &&
        log_lv->valueint >= LOG_LEVEL_NONE && log_lv->valueint <= LOG_LEVEL_VERBOSE)
    {
        set_logger_level(log_lv->valueint);
    }
    else
    {
        set_logger_level(LOG_LEVEL_WARN);
        LOG_WARN("log_lv 参数不存在, 使用默认等级 (WARN)");
    }

    const cJSON* enabled = cJSON_GetObjectItem(cfg_json, "enabled");
    if (enabled == NULL || !cJSON_IsBool(enabled))
    {
        LOG_WARN("enabled 参数不存在, 请填写后重启程序");
        cJSON_Delete(cfg_json);
        while (true)
        {
            if (g_need_exit || is_shutdown_requested())
            {
                return false;
            }
            g_prog_enabled = false;
            sleep_ms(10000, true);
        }
    }
    if (cJSON_IsFalse(enabled))
    {
        LOG_WARN("配置文件中禁用了程序启动, 请开启后重启程序");
        cJSON_Delete(cfg_json);
        while (true)
        {
            if (g_need_exit || is_shutdown_requested())
            {
                return false;
            }
            g_prog_enabled = false;
            sleep_ms(10000, true);
        }
    }
    g_prog_enabled = true;

    const cJSON* accounts = cJSON_GetObjectItem(cfg_json, "accounts");
    if (accounts == NULL || cJSON_IsArray(accounts) == false || cJSON_GetArraySize(accounts) == 0)
    {
        LOG_FATAL("没有找到账号数据, 请添加后重启程序");
        cJSON_Delete(cfg_json);
        while (true)
        {
            if (g_need_exit || is_shutdown_requested())
            {
                return false;
            }
            sleep_ms(10000, true);
        }
    }

    const int account_count = cJSON_GetArraySize(accounts);
    if (account_count > INT8_MAX)
    {
        LOG_FATAL("账号配置数量超过上限: %d", INT8_MAX);
        cJSON_Delete(cfg_json);
        return false;
    }
    const uint8_t cnt = (uint8_t)account_count;

    int8_t valid_cnt = 0;

#ifdef __OPENWRT__
    LOG_INFO("OpenWRT 环境, 会尝试加载所有有效配置");

    prog_status_t* new_prog_status = calloc(cnt, sizeof(prog_status_t));
    if (new_prog_status)
    {
        free(g_prog_status);
        g_prog_status = new_prog_status;
        for (uint8_t i = 0; i < cnt; i++)
        {
            atomic_init(&g_prog_status[i].runtime_status.is_initialized, false);
            atomic_init(&g_prog_status[i].runtime_status.is_running, false);
            atomic_init(&g_prog_status[i].runtime_status.is_authed, false);
            atomic_init(&g_prog_status[i].runtime_status.is_need_reset, false);
        }
    }
    else
    {
        LOG_FATAL("重分配内存失败");
        cJSON_Delete(cfg_json);
        return false;
    }

    bool use_cus_mark = false;

    for (uint8_t i = 0, valid_i = 0; i < cnt; i++)
    {
        const cJSON* account = cJSON_GetArrayItem(accounts, i);

        const cJSON* usr = cJSON_GetObjectItem(account, "username");
        const cJSON* pwd = cJSON_GetObjectItem(account, "password");
        const cJSON* chn = cJSON_GetObjectItem(account, "channel");
        const cJSON* mark = cJSON_GetObjectItem(account, "mark");

        // 检查账号
        if (!cJSON_IsString(usr) || usr->valuestring == NULL)
        {
            LOG_WARN("配置 %" PRIu8 " username 参数不存在, 跳过当前配置", i + 1);
            continue;
        }
        if (usr->valuestring[0] == '\0')
        {
            LOG_WARN("配置 %" PRIu8 " username 参数为空, 跳过当前配置", i + 1);
            continue;
        }
        if (strlen(usr->valuestring) >= USR_LEN)
        {
            LOG_WARN("配置 %" PRIu8 " username 参数过长, 跳过当前配置", i + 1);
            continue;
        }

        // 检查密码
        if (!cJSON_IsString(pwd) || pwd->valuestring == NULL)
        {
            LOG_WARN("配置 %" PRIu8 " password 参数不存在, 跳过当前配置", i + 1);
            continue;
        }
        if (pwd->valuestring[0] == '\0')
        {
            LOG_WARN("配置 %" PRIu8 " password 参数为空, 跳过当前配置", i + 1);
            continue;
        }
        if (strlen(pwd->valuestring) >= PWD_LEN)
        {
            LOG_WARN("配置 %" PRIu8 " password 参数过长, 跳过当前配置", i + 1);
            continue;
        }

        snprintf(g_prog_status[valid_i].login_cfg.usr, USR_LEN, "%s", safe_str(usr->valuestring));
        snprintf(g_prog_status[valid_i].login_cfg.pwd, PWD_LEN, "%s", safe_str(pwd->valuestring));

        // 检查通道
        if (!cJSON_IsString(chn) || chn->valuestring == NULL)
        {
            LOG_WARN("配置 %" PRIu8 " channel 参数不存在, 使用默认通道", i + 1);
            snprintf(g_prog_status[valid_i].login_cfg.chn, CHN_LEN, "%s", "phone");
        }
        else if (chn->valuestring[0] == '\0' || strlen(chn->valuestring) >= CHN_LEN)
        {
            LOG_WARN("配置 %" PRIu8 " channel 参数为空或过长, 使用默认通道", i + 1);
            snprintf(g_prog_status[valid_i].login_cfg.chn, CHN_LEN, "%s", "phone");
        }
        else
        {
            snprintf(g_prog_status[valid_i].login_cfg.chn, CHN_LEN, "%s", safe_str(chn->valuestring));
        }

        // 转化成 UA
        if (strcmp(g_prog_status[valid_i].login_cfg.chn, "pc") == 0)
        {
            snprintf(g_prog_status[valid_i].login_cfg.user_agent, USER_AGENT_LEN, "CCTP/Linux64/1003");
            LOG_DEBUG("使用 UA: %s", g_prog_status[valid_i].login_cfg.user_agent);
            LOG_DEBUG("当前使用下标: %" PRIu8, valid_i);
        }
        else
        {
            snprintf(g_prog_status[valid_i].login_cfg.user_agent, USER_AGENT_LEN, "CCTP/android64_vpn/2093");
            LOG_DEBUG("使用 UA: %s", g_prog_status[valid_i].login_cfg.user_agent);
            LOG_DEBUG("当前使用下标: %" PRIu8, valid_i);
        }

        // 检查标记值
        if (!cJSON_IsString(mark) || mark->valuestring == NULL)
        {
            if (use_cus_mark)
            {
                LOG_WARN("其它配置使用了自定义标记值, 但配置 %" PRIu8 " 未填写, 将跳过该配置", i + 1);
                continue;
            }
            g_prog_status[valid_i].login_cfg.mark = 0x100 + valid_i * 0x100;
            LOG_DEBUG("使用自动标记值: %" PRIu32 " (0x%x)", g_prog_status[valid_i].login_cfg.mark, g_prog_status[valid_i].login_cfg.mark);
            LOG_DEBUG("当前使用下标: %" PRIu8, valid_i);
        }
        else
        {
            if (use_cus_mark && mark->valuestring[0] == '\0')
            {
                LOG_WARN("其它配置使用了自定义标记值, 但配置 %" PRIu8 " 未填写, 将跳过该配置", i + 1);
                continue;
            }
            if (mark->valuestring[0] != '\0')
            {
                uint32_t parsed_mark;
                if (!parse_hex_uint32(mark->valuestring, &parsed_mark))
                {
                    LOG_WARN("配置 %" PRIu8 " mark 参数无效, 将跳过该配置", i + 1);
                    continue;
                }
                g_prog_status[valid_i].login_cfg.mark = parsed_mark;
                g_prog_status[valid_i].login_cfg.use_cus_mark = true;
                use_cus_mark = true;
                LOG_DEBUG("使用自定义标记值: %" PRIu32 " (0x%x)", g_prog_status[valid_i].login_cfg.mark, g_prog_status[valid_i].login_cfg.mark);
                LOG_DEBUG("当前使用下标: %" PRIu8, valid_i);
            }
            else
            {
                g_prog_status[valid_i].login_cfg.mark = 0x100 + valid_i * 0x100;
                LOG_DEBUG("使用自动标记值: %" PRIu32 " (0x%x)", g_prog_status[valid_i].login_cfg.mark, g_prog_status[valid_i].login_cfg.mark);
                LOG_DEBUG("当前使用下标: %" PRIu8, valid_i);
            }
        }

        // g_prog_status[valid_i].login_cfg.auto_start = auto_start->valueint;

        g_prog_status[valid_i].login_cfg.idx = i + 1;
        LOG_INFO("配置 %" PRIu8 " 可用, 将会尝试使用", i + 1);
        valid_cnt++;
        valid_i++;
    }

#else

    LOG_INFO("非 OpenWRT 环境, 仅会尝试加载第一个有效配置");

    for (uint8_t i = 0; i < cnt; i++)
    {
        const cJSON* account = cJSON_GetArrayItem(accounts, i);

        const cJSON* usr = cJSON_GetObjectItem(account, "username");
        const cJSON* pwd = cJSON_GetObjectItem(account, "password");
        const cJSON* chn = cJSON_GetObjectItem(account, "channel");

        // 检查账号
        if (!cJSON_IsString(usr) || usr->valuestring == NULL)
        {
            LOG_WARN("配置 %" PRIu8 " username 参数不存在, 跳过当前配置", i + 1);
            continue;
        }
        if (usr->valuestring[0] == '\0')
        {
            LOG_WARN("配置 %" PRIu8 " username 参数为空, 跳过当前配置", i + 1);
            continue;
        }
        if (strlen(usr->valuestring) >= USR_LEN)
        {
            LOG_WARN("配置 %" PRIu8 " username 参数过长, 跳过当前配置", i + 1);
            continue;
        }

        // 检查密码
        if (!cJSON_IsString(pwd) || pwd->valuestring == NULL)
        {
            LOG_WARN("配置 %" PRIu8 " password 参数不存在, 跳过当前配置", i + 1);
            continue;
        }
        if (pwd->valuestring[0] == '\0')
        {
            LOG_WARN("配置 %" PRIu8 " password 参数为空, 跳过当前配置", i + 1);
            continue;
        }
        if (strlen(pwd->valuestring) >= PWD_LEN)
        {
            LOG_WARN("配置 %" PRIu8 " password 参数过长, 跳过当前配置", i + 1);
            continue;
        }

        snprintf(g_prog_status[0].login_cfg.usr, USR_LEN, "%s", safe_str(usr->valuestring));
        snprintf(g_prog_status[0].login_cfg.pwd, PWD_LEN, "%s", safe_str(pwd->valuestring));

        // 检查通道
        if (!cJSON_IsString(chn) || chn->valuestring == NULL)
        {
            LOG_WARN("配置 %" PRIu8 " channel 参数不存在, 使用默认通道", i + 1);
            snprintf(g_prog_status[0].login_cfg.chn, CHN_LEN, "%s", "phone");
        }
        else if (chn->valuestring[0] == '\0' || strlen(chn->valuestring) >= CHN_LEN)
        {
            LOG_WARN("配置 %" PRIu8 " channel 参数为空或过长, 使用默认通道", i + 1);
            snprintf(g_prog_status[0].login_cfg.chn, CHN_LEN, "%s", "phone");
        }
        else
        {
            snprintf(g_prog_status[0].login_cfg.chn, CHN_LEN, "%s", safe_str(chn->valuestring));
        }

        // 转化成 UA
        if (strcmp(g_prog_status[0].login_cfg.chn, "pc") == 0)
        {
            snprintf(g_prog_status[0].login_cfg.user_agent, USER_AGENT_LEN, "CCTP/Linux64/1003");
            LOG_DEBUG("使用 UA: %s", g_prog_status[0].login_cfg.user_agent);
            LOG_DEBUG("当前使用下标: 0");
        }
        else
        {
            snprintf(g_prog_status[0].login_cfg.user_agent, USER_AGENT_LEN, "CCTP/android64_vpn/2093");
            LOG_DEBUG("使用 UA: %s", g_prog_status[0].login_cfg.user_agent);
            LOG_DEBUG("当前使用下标: 0");
        }

        g_prog_status[0].login_cfg.idx = 1;
        LOG_INFO("配置 %" PRIu8 " 可用, 将会尝试使用", i + 1);
        valid_cnt++;
        break;
    }

#endif

    cJSON_Delete(cfg_json);

    if (valid_cnt == 0)
    {
        LOG_FATAL("无可用配置, 请检查后重启程序");
        while (true)
        {
            if (g_need_exit || is_shutdown_requested())
            {
                return false;
            }
            sleep_ms(10000, true);
        }
    }

    g_prog_cnt = valid_cnt;

    return true;
}
