# 实现与验证状态

2026-10-31，Windows x64 / MSVC 19.51 / Visual Studio 18 / Python 3.14。

## 已实现

- 同仓库 `sdk/main` 基线及产品 worktree 创建命令。
- 宿主工具虚拟环境、依赖固定提交和归档 SHA-256 校验、离线构建。
- Kconfig：产品片段、板卡片段、本机 menuconfig 覆盖、严格配置校验。
- CMake/Ninja、VS Code 构建/测试任务、调试启动配置。
- CPU0 的实际 FreeRTOS Windows port；辅助核入口存在但不启动。
- CPU0 所有关联任务线程亲和性设置与实际读取校验；宿主 CPU 可选择。
- JUCE 窗口、LVGL ARGB8888 帧交换、鼠标输入、UI 唯一所有者。
- 基础 OSAL：任务创建、字节栈参数转换、延时、扩展 tick 计时。
- 有界日志队列与 EasyLogger 单任务输出；宿主桥接不阻塞仿真任务。
- 异步存储请求：StorageTask 排序、宿主工作线程执行文件操作。
- littlefs 的文件支持 NOR 模拟设备，验证擦除/编程约束与重挂载持久化。
- FlashDB 原生 LIBC 文件后端的 KV 持久化。当前尚未使用 FAL 模拟 NOR 后端。
- nanopb 用户协议自动生成与运行库；cJSON、MultiButton、RTNeural STL。
- 运行时将 cJSON 的进程级内存钩子绑定到 FreeRTOS 堆；HOST 使用 JUCE 自己的 JSON 实现。用户示例在 AppTask 中实际创建、序列化和释放 JSON。
- ESP-DSP `dotprod_f32_ansi` 与 ESP-NN `relu6_s8_ansi` 的 SDK C 接口与参考计算测试。

## 验证方式

`tools/sdk.ps1 test` 执行：

1. 通用组件：JSON 正反例、protobuf wire 与生成 schema 往返、按键消抖、日志适配、RTNeural Dense 计算、两项 ESP 参考算法、littlefs/FlashDB 关闭重开后的数据恢复。
2. 配置契约：关闭组件、拒绝未知选项、拒绝启用预留核、用户代码不引用平台头文件。
3. 运行冒烟：真实窗口和内核，自动注入一次按下/释放，确认 LVGL 事件、两项存储写入、CPU0 入口一次、辅助核零次、亲和性失败为零、内核能够停止。

运行报告与截图由程序实际生成。数据恢复测试不等同于掉电故障注入或 Flash 寿命验证。

## 仍需按平台扩展

- FreeRTOS SMP、多内核 AMP、辅助核线程与业务、跨域 IPC、BSP 核启动。
- ESP-IDF、Linux/POSIX、其他 MCU 的完整 OSAL/HAL 与构建桥接。
- 真实目标板验证；Windows 仿真不验证实机栈水位或实时性能。
- CherryUSB 的控制器、DMA 与协议时序；ESP-GMF 的 ESP 目标后端。
- ESP-DSP/ESP-NN 的其他算法与芯片优化实现；RTNeural 的模型容量和性能评估。
- 完整 OSAL 队列/互斥锁/ISR 公共接口、外设驱动、音频和多媒体产品服务。
- FlashDB FAL 模式、设备分区统一仲裁、故障注入测试。

## 上游适配

- FreeRTOS：CMake 在生成目录应用三处亲和性替换，强制核对替换位置数量，保留原始源码缓存。
- EasyLogger：生成配置头与头文件覆盖目录，避免上游同目录配置覆盖 SDK 配置。
- ESP-NN：生成一个 MSVC 兼容公共头，保留原始头并限定到当前验证的 ReLU6 接口。
- MSVC：SDK 子进程使用嵌入式调试信息 `/Z7`，避免对共享编译 PDB 服务的依赖；探测本机 include 输出前缀供 Ninja 追踪头文件。

所有版本来源以 `third_party/deps.lock.json` 为准；未将依赖源码、虚拟环境或构建结果加入 Git。
