# ESurfingClient-C (修复版)

> 本项目 fork 自 [BadGhost520/ESurfingClient-CVersion](https://github.com/BadGhost520/ESurfingClient-CVersion)，在此基础上修复了多个导致路由器断网的关键问题。

## 与上游版本的区别

### 断网修复（核心问题）

上游版本在路由器上运行几天后会断网且无法自动恢复。经分析日志和代码，定位到以下根因：

| 问题 | 原因 | 修复 |
|------|------|------|
| 进程挂死无日志 | `curl_easy_perform()` 卡在 DNS 解析，无连接超时 | 添加 `CURLOPT_CONNECTTIMEOUT` (5s) + `CURLOPT_NOSIGNAL` |
| 内存持续增长 | `check_network_status()` / `get_last_location()` / `term()` 的 `body_data` 未释放 | 所有 `get()`/`post()` 返回后 `free(body_data)` |
| 重连认证失败 | `s_school_id` / `s_domain` / `s_area` 重连后不更新，用过期参数发请求 | 每次重连前调用 `reset_network_state()` 清空 |
| 重定向 URL 过期 | `last_location_lock` 在线程重启后不重置 | `clean()` 中重置 `last_location_lock = false` |

### 多线程优雅退出

| 问题 | 修复 |
|------|------|
| `run()` 的 `retry_timeout` / `retry_auth` 是 `static` 共享变量，多线程竞争 | 改为 `static _Thread_local` |
| watchdog 超时后调用 `sim_thread_destroy()` 释放还在运行的线程句柄 | 改为设 `is_running = false` 等待自然退出 |
| `term()` 重试不检查退出标志，关机时阻塞 5 秒 | 加 `g_thread_keep_alive` + `is_running` 检查 |
| `get_last_location()` 重定向循环无退出条件 | 加退出信号检查，防止死循环 |
| `shut()` 不释放线程句柄和 `g_prog_status` | 加 `free(thread)` + `free(g_prog_status)` |

### CI 构建修复

| 问题 | 修复 |
|------|------|
| `scripts/feeds install esurfingclient` 失败 | 去掉，本地包不需要 feed install |
| `workflow_dispatch` 手动触发时 `inputs` 为空，版本号被清空 | 所有 `inputs` 引用加 `\|\| '默认值'` fallback |
| 缺少详细编译日志 | 加 `V=s` 输出 |

## 安装方法

### OpenWrt (mediatek_filogic)

```bash
# 下载
wget https://github.com/bybixi/ESurfingClient-CVersion/releases/download/v2.0.4-r6/esurfingclient_2.0.4-6_mediatek_filogic.ipk -O /tmp/esurfingclient.ipk

# 安装（覆盖旧版）
opkg install /tmp/esurfingclient.ipk --force-reinstall

# 重启服务
/etc/init.d/esurfingclient restart
```

### 其它架构

从 [Releases](https://github.com/bybixi/ESurfingClient-CVersion/releases) 页面下载对应架构的 ipk 包。

## 原项目说明

**根据 Rsplwe 大佬的 Kotlin 源码编写的纯 C 版本的天翼校园认证客户端**

使用了 [cJSON](https://github.com/DaveGamble/cJSON), [mongoose](https://github.com/cesanta/mongoose) 开源库

- 程序文件超级小（所有版本均仅占用 2MB 左右的储存空间）
- 跨平台跨架构能力强
- 支持 OpenWRT 15.05 到最新版的 LuCI 以及程序软件包

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

## 致谢

原作者 [BadGhost520](https://github.com/BadGhost520) 及所有贡献者
