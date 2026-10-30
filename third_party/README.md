# 第三方依赖管理

14 个直接依赖位于 `src/`，通过根 `.gitmodules` 声明 submodule URL，通过父仓库的 gitlink 固定精确提交。`components.json` 保存名称、目录、版本说明、同步分组和能力状态，不再保存另一份提交锁。构建时验证已初始化、HEAD 与 gitlink 一致、没有已跟踪源码修改，并在构建目录生成 `generated/dependencies.json` 来源报告。

```powershell
.\.venv\Scripts\python.exe tools/sdk.py sync
.\.venv\Scripts\python.exe tools/sdk.py sync --profile embedded esp
.\.venv\Scripts\python.exe tools/sdk.py sync --only lvgl
```

新 clone 可先运行 `git submodule update --init --recursive`，或使用上述 SDK 命令按组同步。SDK 普通构建不联网、不移动版本。升级只在 SDK 维护流程中审阅更新 gitlink，并验证配置、API 与许可。原 `.cache/sources`、归档及迁移备份保留在本机，当前构建不再读取它们。

| 目录 | 上游 | 当前 Windows 范围 |
| --- | --- | --- |
| juce | juce-framework/JUCE | 窗口、宿主消息与绘制 |
| freertos | FreeRTOS/FreeRTOS-Kernel | CPU0 / Windows MSVC port |
| lvgl | lvgl/lvgl | v9.6.0 核心、显示与输入 |
| cjson | DaveGamble/cJSON | 解析与 FreeRTOS 分配器钩子 |
| littlefs | littlefs-project/littlefs | 文件支持的模拟 NOR |
| flashdb | armink/FlashDB | LIBC 文件后端 KV |
| cherryusb | cherry-embedded/CherryUSB | 源码已取回，板级集成待做 |
| nanopb | nanopb/nanopb | 运行库与宿主协议生成 |
| multibutton | 0x1abin/MultiButton | 扫描与事件 |
| easylogger | armink/EasyLogger | SDK 输出适配 |
| esp-dsp | espressif/esp-dsp | ANSI 点积参考子集 |
| esp-nn | espressif/esp-nn | ANSI ReLU6 参考子集 |
| esp-gmf | espressif/esp-gmf | 源码已取回，ESP 后端待做 |
| rtneural | jatinchowdhury18/RTNeural | STL Dense 子集 |

构建接入在 `cmake/Dependencies.cmake`、`Components.cmake`、`Protocol.cmake` 及 `recipes/esp-reference.cmake`。每个 target 只公开需要的 include 目录，不把整个 src 作为公共搜索路径。

FreeRTOS 亲和性覆盖和 ESP-NN 的兼容头在各自构建目录生成；nanopb generator 复制到各项目的 `host-tools/` 再运行，避免修改共享 submodule 或不同项目争写 Python/protobuf 缓存。格式化不进入 src。

原始许可证保留在各 submodule 中。CherryUSB 需要目标控制器、DMA 和 BSP；ESP-GMF 遵循其 Espressif 产品许可范围，不编入 Windows/RK 通用目标。目标已有内核/依赖时由移植桥接选择外部提供者，避免重复链接和配置不一致。
