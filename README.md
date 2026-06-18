# ESurfingClient-C (修复版)

> 本项目 fork 自 [BadGhost520/ESurfingClient-CVersion](https://github.com/BadGhost520/ESurfingClient-CVersion)，修复了导致路由器断网、内存泄漏、磁盘写爆、DNS 崩溃等多个关键问题。

> **最新编译**: [v2.0.4-r7](https://github.com/bybixi/ESurfingClient-CVersion/releases/tag/v2.0.4-r7)

## 与上游版本的区别

### 断网修复（核心）

| 问题 | 根因 | 修复 |
|------|------|------|
| 进程挂死无日志 | `curl_easy_perform()` 卡在 DNS 解析，无连接超时 | `CURLOPT_CONNECTTIMEOUT` (5s) + `CURLOPT_NOSIGNAL` |
| 内存泄漏 OOM | 5 个函数中 `body_data` 未释放 | 所有 `get()`/`post()` 返回后 `free(body_data)` |
| 重连认证失败 | `s_school_id`/`s_domain`/`s_area` 用过期值 | 重连前 `reset_network_state()` 清空 |
| 重定向 URL 过期 | `last_location_lock` 不重置 | `clean()` 中重置 |

### 磁盘/DNS 修复

| 问题 | 根因 | 修复 |
|------|------|------|
| `/tmp` 被日志写满 | `log_lv=VERBOSE` 每秒 1 条，2 天写满 117MB | 默认 WARN，256KB 轮转，最多 2 个文件 |
| DNS 打挂 dnsmasq | 每秒 curl 无 DNS 缓存 | `CURLOPT_DNS_CACHE_TIMEOUT=300s` |
| 高频 CPU/网络 | 在线每秒 `check_network_status()` | 轮询间隔 1s → 5s |
| procd 日志刷屏 (r7) | `stdout 1/stderr 1` 所有日志重复输出到系统日志 | 改为 `stdout 0/stderr 0` |
| 配置文件默认 VERBOSE (r7) | 多处 `log_lv` 默认值不一致 (4/5/6) | 统一为 3 (WARN)：config.json、LuCI、代码 |

### 多线程优雅退出

| 问题 | 修复 |
|------|------|
| `run()` `retry_*` 是 `static` 共享变量 → 数据竞争 | `static _Thread_local` |
| watchdog `sim_thread_destroy()` 释放运行中的线程 | 设 `is_running=false` 等待自然退出 |
| `term()` 重试不检查退出标志 | `g_thread_keep_alive` 检查 |
| `get_last_location()` 重定向死循环 | 退出信号检查 |
| `shut()` 线程句柄 + 数组泄漏 | `free(thread)` + `free(g_prog_status)` |
| `clean_logger()` `return` 后死代码 (r7) | 关闭 handle 后有无法执行的 rename 逻辑 | 清理 |
| `clean_logger()` `return` 后死代码 | 清理 |

### CI 修复

| 问题 | 修复 |
|------|------|
| `feeds install esurfingclient` 失败 | 本地包不需要 |
| `workflow_dispatch` `inputs` 为空 | `\|\| '默认值'` fallback |
| SDK 缓存导致不重编译 | 先 `rm -rf` 缓存再 `cp` 源码 |

## 安装方法

### OpenWrt (mediatek_filogic)

```bash
wget https://github.com/bybixi/ESurfingClient-CVersion/releases/download/v2.0.4-r7/esurfingclient_2.0.4-7_mediatek_filogic.ipk -O /tmp/esurfingclient.ipk
opkg install /tmp/esurfingclient.ipk --force-reinstall
/etc/init.d/esurfingclient restart
```

### 验证修复

```bash
# 确认是修复版（应输出 1）
strings /usr/bin/esurfingclient | grep -c reset_network_state
```

### 其它架构

从 [Releases](https://github.com/bybixi/ESurfingClient-CVersion/releases) 下载。

## 原项目说明

根据 Rsplwe 大佬的 Kotlin 源码编写的纯 C 版本天翼校园认证客户端。程序文件仅 2MB，跨平台跨架构，支持 OpenWrt / Windows / Linux / macOS。

> 理论上只要是用天翼校园网客户端的学校都可以用，不论省份。

## 支持的系统和架构

| 系统 | 架构 | 包管理器 |
|:---:|:---:|:---:|
| Windows | x86_64 | / |
| Linux | x86_64 | / |
| macOS | x86_64 / arm64 | / |
| OpenWrt | x86_64 / ramips_mt7621 / qualcommax_ipq60xx / mediatek_filogic | opkg / apk |

## 使用教程

- [Windows, Linux, macOS 环境](Desktop.md)
- [OpenWRT 环境](OpenWRT.md)
- [OpenWRT 进阶 - 多播](OpenWRT_mwan3.md)
- [自行编译指南](Compile.md)

## AI 辅助声明

本项目修复由 AI 辅助完成：

- **DeepSeek** — 代码分析与修复方案设计
- **ChatGPT (Claude)** — 代码编写、编译调试、CI 工作流修复
- **MiMo** — 代码审查

所有 AI 产出均已人工审查确认后合并。

## 致谢

原作者 [BadGhost520](https://github.com/BadGhost520) 及所有贡献者
