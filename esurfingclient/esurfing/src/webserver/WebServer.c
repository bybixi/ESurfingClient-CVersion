#include "utils/PlatformUtils.h"
#include "webserver/WebServer.h"
#include "webserver/mongoose.h"
#include "utils/SimThread.h"
#include "utils/Shutdown.h"
#include "utils/Logger.h"
#include "utils/cJSON.h"
#include "NetClient.h"
#include "States.h"

static const char* listenAddr = "http://127.0.0.1:8888";
static sim_thread_t* web_thread;
static atomic_int s_web_start_state = ATOMIC_VAR_INIT(0);

static void fn(struct mg_connection *c, const int ev, void *ev_data)
{
    if (ev != MG_EV_HTTP_MSG) return;

    struct mg_http_message *hm = ev_data;
    struct mg_http_serve_opts opts = { .root_dir = "portal" };
    if (mg_strcmp(hm->method, mg_str("GET")) == 0)
    {
        if (mg_match(hm->uri, mg_str("/"), NULL))
        {
            mg_http_reply(c, 302, "Location: /index.html\r\n", "");
            return;
        }
        if (mg_match(hm->uri, mg_str("/api/status/auth"), NULL))
        {
            cJSON* auth = cJSON_CreateObject();
            if (!auth)
            {
                mg_http_reply(c, 500, "", "");
                return;
            }
            const bool is_authed = g_prog_status != NULL && g_prog_cnt > 0 &&
                                   g_prog_status[0].runtime_status.is_authed;
            cJSON_AddBoolToObject(auth, "status", is_authed);
            char* status_str = cJSON_PrintUnformatted(auth);
            cJSON_Delete(auth);
            if (!status_str)
            {
                mg_http_reply(c, 500, "", "");
                return;
            }
            mg_http_reply(c, 200,
                          "Content-Type: application/json\r\nCache-Control: no-store\r\n",
                          "%s", status_str);
            free(status_str);
            return;
        }
        if (mg_match(hm->uri, mg_str("/api/status/online"), NULL))
        {
            const NetworkStatus status = check_network_status();
            if (status == REQUEST_SUCCESS) mg_http_reply(c, 204, "", "");
            else if (status == REQUEST_REDIRECT) mg_http_reply(c, 302, "", "");
            else mg_http_reply(c, 503, "", "");
            return;
        }
        if (mg_match(hm->uri, mg_str("/api/getConfigs"), NULL))
        {
            cJSON* configs = cJSON_CreateObject();
            cJSON* accounts = cJSON_CreateArray();
            cJSON* account = cJSON_CreateObject();
            if (!configs || !accounts || !account || g_prog_status == NULL || g_prog_cnt <= 0)
            {
                cJSON_Delete(configs);
                cJSON_Delete(accounts);
                cJSON_Delete(account);
                mg_http_reply(c, 500, "", "");
                return;
            }
            cJSON_AddBoolToObject(configs, "enabled", g_prog_enabled);
            cJSON_AddNumberToObject(configs, "log_lv", get_logger_level());
            cJSON_AddStringToObject(account, "username", g_prog_status[0].login_cfg.usr);
            cJSON_AddStringToObject(account, "password", "");
            cJSON_AddStringToObject(account, "channel", g_prog_status[0].login_cfg.chn);
            cJSON_AddItemToArray(accounts, account);
            cJSON_AddItemToObject(configs, "accounts", accounts);
            char* config_str = cJSON_PrintUnformatted(configs);
            cJSON_Delete(configs);
            if (!config_str)
            {
                mg_http_reply(c, 500, "", "");
                return;
            }
            mg_http_reply(c, 200,
                          "Content-Type: application/json\r\nCache-Control: no-store\r\n",
                          "%s", config_str);
            free(config_str);
            return;
        }

        mg_http_serve_dir(c, hm, &opts);
        return;
    }

    mg_http_reply(c, 405, "Allow: GET\r\n", "");
}

static void logFn(const char ch, void *param)
{
    (void)param;
    static char buffer[512];
    static size_t pos = 0;
    if (ch == '\n' || pos >= sizeof(buffer) - 1)
    {
        const size_t line_length = pos;
        buffer[line_length] = '\0';
        pos = 0;
        if (line_length > 0)
        {
            const char* web_log_level = strchr(buffer, ' ');
            if (!web_log_level || strlen(web_log_level) < 4)
            {
                LOG_WARN("未知的 Web 日志: %s", buffer);
                return;
            }
            const char* file_start = web_log_level + 3;
            const char* file_end = strchr(file_start, ':');
            if (!file_end)
            {
                LOG_WARN("未知的 Web 日志: %s", buffer);
                return;
            }
            const size_t file_length = (size_t)(file_end - file_start);
            char file[512];
            if (file_length >= sizeof(file)) return;
            memcpy(file, file_start, file_length);
            file[file_length] = '\0';
            const char* file_line_start = file_end + 1;
            const char* file_line_end = strchr(file_line_start, ':');
            if (!file_line_end)
            {
                LOG_WARN("未知的 Web 日志: %s", buffer);
                return;
            }
            const size_t file_line_length = (size_t)(file_line_end - file_line_start);
            char file_line_str[32];
            if (file_line_length >= sizeof(file_line_str)) return;
            memcpy(file_line_str, file_line_start, file_line_length);
            file_line_str[file_line_length] = '\0';
            const uint64_t parsed_file_line = str2uint64(file_line_str);
            if (parsed_file_line > UINT32_MAX) return;
            const uint32_t file_line = (uint32_t)parsed_file_line;
            const char* msg = file_line_end + 1;
            switch(web_log_level[1])
            {
            case '1':
                LOG_WEB_ERROR(file, file_line, "%s", msg);
                break;
            case '2':
                LOG_WEB_INFO(file, file_line, "%s", msg);
                break;
            case '3':
            case '4':
                LOG_WEB_VERBOSE(file, file_line, "%s", msg);
                break;
            default:
                LOG_WARN("未知等级的 Web 日志: %s", msg);
            }
        }
    }
    else if (ch != '\r')
    {
        buffer[pos++] = ch;
    }
}

int web_server(void* arg)
{
    tl_thread_idx = (int8_t)(intptr_t)arg;
    struct mg_mgr mgr;
    mg_log_level = MG_LL_VERBOSE;
    mg_log_set_fn(logFn, NULL);
    mg_mgr_init(&mgr);

    if (mg_http_listen(&mgr, listenAddr, fn, NULL) == NULL)
    {
        LOG_ERROR("Web 服务器监听失败: %s", listenAddr);
        atomic_store(&s_web_start_state, -1);
        mg_mgr_free(&mgr);
        return 1;
    }
    int expected_state = 0;
    if (!atomic_compare_exchange_strong(&s_web_start_state, &expected_state, 1))
    {
        mg_mgr_free(&mgr);
        return 1;
    }
    g_is_webserver_running = 1;
    LOG_INFO("Web 服务器已启动 (Web 前端尚未完工), 后台访问地址: http://127.0.0.1:8888/");
    while (g_is_webserver_running && !g_need_exit && !is_shutdown_requested())
    {
        mg_mgr_poll(&mgr, 1000);
    }
    mg_mgr_free(&mgr);
    LOG_INFO("Web 服务器已停止");
    return 0;
}

bool start_web_server()
{
    atomic_store(&s_web_start_state, 0);
    web_thread = sim_thread_create(web_server, (void*)(intptr_t)-2);

    uint8_t retry = 1;
    while (web_thread == NULL)
    {
        if (retry > 5)
        {
            LOG_FATAL("超过重试次数");
            return false;
        }
        LOG_ERROR("Web 服务器线程创建失败, 重试中, 重试次数: %" PRIu8 ", 最多 5 次", retry);
        web_thread = sim_thread_create(web_server, (void*)(intptr_t)-2);
        retry++;
    }

    for (uint8_t wait_count = 0;
         wait_count < 50 && atomic_load(&s_web_start_state) == 0;
         wait_count++)
    {
        sleep_ms(100, true);
    }
    int expected_state = 0;
    atomic_compare_exchange_strong(&s_web_start_state, &expected_state, -1);
    if (atomic_load(&s_web_start_state) != 1)
    {
        int result_code = 0;
        sim_thread_join(web_thread, &result_code);
        free(web_thread);
        web_thread = NULL;
        return false;
    }
    return true;
}

void stop_web_server()
{
    if (!web_thread) return;
    g_is_webserver_running = 0;
    int result_code = 0;
    sim_thread_join(web_thread, &result_code);
    free(web_thread);
    web_thread = NULL;
    LOG_DEBUG("Web 服务器线程退出, 退出码: %d", result_code);
}
