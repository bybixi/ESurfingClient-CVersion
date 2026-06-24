 ESurfingClient-C (修复版)

()

## 放一张我路由器的使用情况
<img width="2560" height="1271" alt="image" src="https://github.com/user-attachments/assets/e54ad461-f731-48c4-9878-bdb98a41f679" />

 最新编译: ()

 与上游版本的区别

 断网修复（核心）

 问题  根因  修复 

 进程挂死无日志   卡在 DNS 解析，无连接超时   (5s) +  
 内存泄漏 OOM  5 个函数中  未释放  所有 / 返回后  
 重连认证失败  // 用过期值  重连前  清空 
  

 磁盘/DNS 修复

 问题  根因  修复 

  被日志写满   每秒 1 条，2 天写满 117MB  默认 WARN，256KB 轮转，最多 2 个文件 
  
 高频 CPU/网络  在线每秒   轮询间隔 1s → 5s 
 procd 日志刷屏 (r7)   所有日志重复输出到系统日志  改为  
 配置文件默认 VERBOSE (r7)  多处  默认值不一致 (4/5/6)  统一为 3 (WARN)：config.json、LuCI、代码 

 多线程优雅退出

 问题  修复 

   是  共享变量 → 数据竞争   
 watchdog  释放运行中的线程  设  等待自然退出 
  重试不检查退出标志   检查 
 
  线程句柄 + 数组泄漏   +  
   后死代码 (r7)  关闭 handle 后有无法执行的 rename 逻辑  清理 
   后死代码  清理 

 CI 修复

 问题  修复 

 
   为空  \|\| '默认值' fallback 


 安装方法




wget https://github.com/bybixi/ESurfingClient-CVersion/releases/download/v2.0.4-r7/esurfingclient_2.0.4-7_mediatek_filogic.ipk -O /tmp/esurfingclient.ipk




 验证修复


 确认是修复版（应输出 1）



 其它架构

()

 原项目说明

根据 Rsplwe 大佬的 Kotlin 源码编写的纯 C 版本天翼校园认证客户端。程序文件仅 2MB，跨平台跨架构，支持 OpenWrt / Windows / Linux / macOS。

 理论上只要是用天翼校园网客户端的学校都可以用，不论省份。

 支持的系统和架构

 系统  包管理器 
|:---:|:---:|:---:|





## 使用教程

- [Windows, Linux, macOS 环境](Desktop.md)
- [OpenWRT 环境](OpenWRT.md)
- [OpenWRT 进阶 - 多播](OpenWRT_mwan3.md)
- [自行编译指南](Compile.md)

## AI 辅助声明

本项目修复由 AI 辅助完成：

- **DeepSeek** / **MiMo** — 代码编写
- **ChatGPT** — 代码审查
- **Claude Code** / **Codex** 插件 — 代码编写与集成

所有 AI 产出均已人工审查确认后合并。

## 致谢

原作者 [BadGhost520](https://github.com/BadGhost520) 及所有贡献者
