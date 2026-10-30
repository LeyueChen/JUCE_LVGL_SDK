# LVGL JUCE Portable SDK

Windows 上的 LVGL / FreeRTOS 开发基线。JUCE 承载桌面窗口与宿主设备，产品代码集中在 `user/`。

当前支持 Windows x64、MSVC、CPU0 的实际 FreeRTOS Windows port。CPU1/2/3 的入口已保留，默认不启动。嵌入式板卡、SMP/AMP、USB 控制器及 GMF 后端尚未实现，详见 [实现状态](docs/implementation-status.md) 和 [架构](docs/architecture.md)。

## 快速开始

需要 Visual Studio 的“使用 C++ 的桌面开发”及 CMake 工具、Git、Python 3.11+。`SDK_PYTHON` 可以指定 Python 解释器；本机无需将 VS 自带 CMake/Ninja 加入全局 PATH。

在仓库根目录使用 PowerShell：

```powershell
.\tools\bootstrap.ps1
.\tools\sdk.ps1 sync
.\tools\sdk.ps1 build
.\tools\sdk.ps1 run
```

`bootstrap.ps1` 只在仓库 `.venv/` 安装哈希锁定的宿主工具。`sync` 下载固定提交并核对归档 SHA-256；后续构建不下载依赖。首次下载需要联网。下载缓存、临时目录与构建产物均位于仓库忽略目录。

```powershell
.\tools\sdk.ps1 doctor
.\tools\sdk.ps1 menuconfig
.\tools\sdk.ps1 test
.\tools\sdk.ps1 run --smoke-seconds 4
```

默认输出：

- `out/windows_sim/user/Debug/bin/lvgl_juce.exe`
- `out/windows_sim/user/Debug/generated/`：SDK/第三方配置及生成的 protobuf
- `out/windows_sim/user/Debug/runtime-report.json`：实际入口次数、亲和性、帧和存储统计
- `out/windows_sim/user/Debug/snapshot.png`：实际 LVGL 画面
- `out/windows_sim/user/Debug/data/`：littlefs 镜像及 FlashDB 文件

VS Code：`Ctrl+Shift+B` 构建，`F5` 使用提供的 Windows 调试配置。任务统一调用 SDK 脚本。CMake Presets 也已提供；直接使用预设时需要已初始化的 MSVC/Ninja 环境。

## 产品开发

`user/app.conf` 描述产品功能，`user/cores/core_roles.c` 指定职责入口。实际核/处理器映射、内核启动、库配置及宿主 I/O 均属于 SDK。

`core_cpu_0()` 由已经启动的 FreeRTOS BootTask 调用。`core_cpu_1()`、`core_cpu_2()`、`core_cpu_3()` 为预留接口，当前启用辅助核配置会明确报错。

产品使用 SDK C 接口及 LVGL 公共 API，避免直接引用 Windows、JUCE 或 FreeRTOS 头文件。`user/protocol/` 中的 `.proto` 和 `.options` 自动生成到构建目录；业务文件不需要手工管理生成器路径。

UI 示例中点击按钮会异步写入 FlashDB 和 littlefs。请求在 StorageTask 串行处理，经宿主工作线程完成实际文件 I/O；完成回调运行于 StorageTask，不能在回调中直接修改 LVGL 对象。

## 逻辑核与宿主亲和性

逻辑 CPU0 默认映射到当前进程允许使用的第一个宿主处理器。FreeRTOS 的所有任务线程及两个调度辅助上下文都应用并验证该映射。JUCE、文件 I/O 等宿主线程不被当作目标 CPU0 业务。

可在本次启动中选择当前处理器组内的另一个允许处理器：

```powershell
$env:SDK_HOST_PROCESSOR = '1'
.\tools\sdk.ps1 run --smoke-seconds 4
Remove-Item Env:SDK_HOST_PROCESSOR
```

不可用的处理器会使启动失败。该功能验证职责与线程亲和性，不模拟芯片指令、缓存、中断或实机实时性。

## Git 工作流

SDK 基线位于 `sdk/main`，已验证的首版标签为 `sdk/v0.1.0`。创建产品分支和 worktree：

```powershell
.\tools\sdk.ps1 new-project panel-a
```

默认创建相邻目录 `LVGL_JUCE-panel-a` 和 `project/panel-a/main`。它共享当前仓库历史，并保留独立工作文件、虚拟环境和构建目录。新目录执行 bootstrap、sync 后即可开发自己的 `user/`。

后续产品在工作区干净时主动合并新的 SDK 标签；通用修复进入 SDK 分支。当前不配置远程地址，也不自动推送。

## 依赖与移植范围

直接依赖版本见 [锁文件](third_party/deps.lock.json)，宿主 Python 工具见 `tools/requirements.txt`。当前 Windows 配置包括 LVGL 9.6.0、FreeRTOS、cJSON、littlefs、FlashDB、nanopb、MultiButton、EasyLogger、RTNeural STL，以及 ESP-DSP 点积 / ESP-NN ReLU6 的 ANSI 参考接口。

CherryUSB 已锁定来源，控制器适配需目标板。ESP-GMF 为 ESP 专用可选依赖，不加入通用 Windows 构建，其原始许可证链接记录在锁定仓库中。下载仅在明确选择 `sync --profile esp` 时进行，首次登记归档摘要由维护者使用 `--record-hashes` 完成。

移植到已有 SDK 时需要复用或接入公共接口、OSAL、显示输入、存储和构建桥接，确保第三方库只有一个提供者且配置兼容。目前尚未进行真实 ESP32-S3、ESP32-P4 或 RK3506 构建验证。
