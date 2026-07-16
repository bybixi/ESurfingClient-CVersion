#ifndef ESURFINGCLIENT_SHUTDOWN_H
#define ESURFINGCLIENT_SHUTDOWN_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 关闭函数
 * @param exit_code 退出码
 */
void shut(int8_t exit_code);

/**
 * @brief 初始化关闭函数
 */
void init_shutdown_hook();

/**
 * @brief 请求主线程安全退出
 */
void request_shutdown(void);

/**
 * @brief 查询是否已收到退出请求
 */
bool is_shutdown_requested(void);

#endif //ESURFINGCLIENT_SHUTDOWN_H
