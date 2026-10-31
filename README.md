# LVGL JUCE Portable SDK

在 Windows / VS Code 中开发可移植的 LVGL / FreeRTOS 应用。JUCE 提供窗口与宿主设备；SDK 管理依赖、配置、代码生成和平台适配。当前实现 Windows x64 / MSVC，CPU0 运行真实 FreeRTOS，CPU1/2/3 保留入口。

## 开始使用

安装 Visual Studio 的 C++ 桌面开发及 CMake 工具、Git、Python 3.11+。脚本在仓库内创建虚拟环境，自动查找 VS 的 CMake/Ninja，无需更改全局 PATH。

```powershell
# 首次克隆时安装宿主依赖；已有 .venv 可跳过前两条
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install --cache-dir .cache/pip --require-hashes -r tools/requirements.txt
.\.venv\Scripts\python.exe tools/sdk.py sync
.\.venv\Scripts\python.exe tools/sdk.py workspace --project basic
```

VS Code 任务直接调用仓库 `.venv` 中的 Python，不依赖 PowerShell 脚本执行策略。仓库还保留 `tools/bootstrap.ps1` 和 `tools/sdk.ps1` 作为方便入口，使用它们时遵循本机现有策略。

在 VS Code 打开生成的 `workspaces/basic-windows_sim-debug.code-workspace`。`Ctrl+Shift+B` 构建，`F5` 调试；任务列表提供 `SDK: build`、`SDK: clean`、`SDK: run`、`SDK: test`、`SDK: menuconfig`。直接打开仓库文件夹时，根 `.vscode` 默认选择 basic 示例。

```powershell
.\.venv\Scripts\python.exe tools/sdk.py projects
.\.venv\Scripts\python.exe tools/sdk.py build --project basic
.\.venv\Scripts\python.exe tools/sdk.py clean --project basic
.\.venv\Scripts\python.exe tools/sdk.py run --project basic
.\.venv\Scripts\python.exe tools/sdk.py test --project basic
.\.venv\Scripts\python.exe tools/sdk.py menuconfig --project basic
.\.venv\Scripts\python.exe tools/sdk.py format --check
```

普通构建使用 `BUILD_TESTING=OFF`；`test` 在独立的 `out/<id>/<board>/Debug-tests`（或 `Release-tests`）中编译验证程序。`workspace` 显式生成 IDE 配置，保留用户自定义的 `c_cpp_properties.json`；build/clean/run 不改写索引设置。

普通构建不下载、不升级依赖。`clean` 只清理所选构建目录中的编译产物，保留配置、编译数据库、依赖、运行数据和旧缓存。

## 目录和项目隔离

```text
LVGL_JUCE/
├─ .gitmodules                     # 14 个直接依赖的上游 URL
├─ .clang-format                   # 用户提供的格式规则
├─ AGENTS.md                       # 先计划、确认后执行的工作流
├─ .vscode/                        # basic 默认任务、调试、C/C++ 索引入口
├─ sdk/                            # 公共接口、服务、OSAL、第三方配置与适配
├─ platforms/windows/              # JUCE、FreeRTOS port、宿主桥接及设备
├─ boards/windows_sim/             # 板级默认配置和核拓扑
├─ third_party/
│  ├─ components.json              # 名称、来源、能力状态和目录
│  ├─ src/<library>/               # 实际 Git submodule，gitlink 固定版本
│  └─ recipes/                     # 平台构建接入
├─ examples/basic/
│  ├─ project.json
│  └─ user/                        # SDK 的可运行示例
├─ projects/<product>/                 # 新应用/产品项目，audio 为音频演示
│  ├─ project.json
│  ├─ targets/windows_sim.conf     # 产品对平台的配置覆盖，可选
│  └─ user/                        # 可移植业务、UI、协议和核职责
├─ templates/project/              # 创建产品时复制的模板
├─ workspaces/                     # 生成的项目专属工作区，忽略提交
├─ cmake/  tools/  tests/
├─ docs/                           # 仅本地文档，Git 忽略，不随仓库分发
├─ out/<id>/<board>/<config>/      # 编译、生成配置、compile_commands.json
└─ .local/
   ├─ config/<id>/<board>.conf      # menuconfig 本机覆盖
   ├─ data/<id>/<board>/            # littlefs 镜像、FlashDB 数据
   └─ intellisense/<id>/<board>/<config>/ # browse.vc.db、ipch
```

每个 `project.json` 声明唯一 ID 和 `user_dir`。同一分支可包含多个项目，使用 `--project <id>` 选择；也可使用仓库内含 `project.json` 的目录。仅选中的 user 目录进入编译。

每个产品工作区只打开对应项目目录，生成自己的 `.vscode/c_cpp_properties.json`，通过 `${default}` 继承工作区的编译数据库、限定浏览路径与符号缓存。SDK/第三方函数通过该项目实际编译的源文件跳转，不扫描全部产品。项目工作区还携带独立的 build/clean/run 和 F5 路径，避免根文件夹设置覆盖。详见 [工作区说明](workspaces/README.md)。

配置覆盖顺序为：板卡 `defconfig` → `user/app.conf` → `targets/<board>.conf` → `.local/config/<id>/<board>.conf`。第三方配置头和运行时参数由 SDK 映射；用户区域不管理依赖下载或平台头文件路径。

## Git 分支和 submodule

完整应用/产品统一位于 `projects/<name>/user/`；SDK 稳定线为 `sdk/main`，通用功能使用 `sdk/feature/<topic>`；产品线为 `project/<name>/main`，产品功能使用 `project/<name>/feature/<topic>`。产品从审阅后的 SDK 发布标签建立，之后主动合并新的 SDK 标签。

SDK 完善继续在 `sdk/feature/submodule-project-layout` 开发，提交和发布由维护者手动执行。现有 `sdk/main` 和 `sdk/v0.1.0` 仍使用旧目录并跟踪 docs，不可作为新产品基点。先审阅、提交、测试，再发布新结构 SDK 标签，不改写已有标签。随后可运行：

```powershell
# 将占位符替换成正式发布的新结构 SDK 标签
.\.venv\Scripts\python.exe tools/sdk.py new-project panel-a --sdk-ref <new-sdk-release-tag>
.\.venv\Scripts\python.exe tools/sdk.py sync
.\.venv\Scripts\python.exe tools/sdk.py workspace --project panel-a
```

该命令要求工作区干净，在当前 checkout 创建产品分支及 `projects/panel-a/user/`，不自动提交。切换产品分支后执行 `sync`，再打开对应工作区。Git 官方说明多 checkout worktree 的 submodule 支持尚不完整，因此默认使用同一 checkout 切换分支；需要同时开发多个产品时使用独立 clone。参考 [Git worktree 限制](https://git-scm.com/docs/git-worktree#_bugs)。

依赖版本的唯一依据是父仓库的 submodule gitlink；`components.json` 的版本文字仅供阅读。`sync` 执行固定版本的 `git submodule update --init --recursive`，不跟踪上游最新分支。默认同步 Windows、参考算法和推理依赖；`sync --profile embedded esp` 可同步 CherryUSB/ESP-GMF；`sync --only lvgl` 可单独同步一个库。更新依赖时需另行计划、审阅 gitlink、重跑验证并在 SDK 分支发布。

切换已有分支使用 `tools/sdk.py switch <branch>`：检查工作区干净、目标布局与 docs 策略，并使用 Git 的 `--no-overwrite-ignore` 保护本地忽略文件。目标仍跟踪 docs 时拒绝切换，避免旧主线覆盖本地文档；切换后再执行 `sync` 和 `workspace --project <id>`。

提交后运行 `tools/sdk.py release-check`，只读检查新结构、干净状态、docs 忽略规则和全部直接依赖 gitlink。它不创建提交/标签，不合并、不推送，也不替代 `test`。维护者手动推进尚未迁移的旧主线时，可在已审阅的新结构分支中执行以下操作，避免检出旧文件：

```powershell
# 完成提交、test 和 release-check 后；第一条返回非 0 时停止，另行审阅合并方案
git merge-base --is-ancestor sdk/main HEAD
# 没有 + 或 --force，仅允许快进更新主线引用，不检出旧文件
git fetch . HEAD:sdk/main
.\.venv\Scripts\python.exe tools/sdk.py switch sdk/main
# 新 sdk/vX.Y.Z 标签及推送由维护者决定
```

## 窗口显示

LVGL 保持配置的原始分辨率，JUCE 窗口调整时等比缩放、居中留边，鼠标按同一变换映射。留边不接受触摸；拖出、缩放和失焦会取消按压。原始画面和显示区域截图分别保存为构建目录中的 `snapshot.png` 与 `viewport.png`。

## 音频与 MIDI

主窗口顶部 **Audio / MIDI settings** 可选择音频输入/输出、通道、采样率、缓冲大小，以及 MIDI 输入和默认输出。VS Code 的 **SDK: device settings** 任务也可直接打开。设备配置按项目保存在 `.local/data/<id>/<board>/media-devices.xml`。首次运行只请求默认双通道输出，保持静音；audio 项目的音频监听开关默认关闭。无设备时仍能启动。

```powershell
.\.venv\Scripts\python.exe tools/sdk.py run --project basic --show-devices
.\.venv\Scripts\python.exe tools/sdk.py run --project basic --no-audio
```

user 通过 `sdk/audio.h` 和 `sdk/midi.h` 使用处理回调、PCM 队列及 MIDI 收发；音频回调归 HOST，CPU0 通过有界桥接交互。接口约定以公共头文件注释为准；音频回调中避免阻塞、文件 I/O、LVGL 和 FreeRTOS 调用。

音频效果链在独立项目 **projects/audio** 中，项目显式创建 AudioTask。启用监听后，PCM 经 SDK 队列送到 CPU0，交给 `rea_` 效果链处理，再送回 JUCE，初始 gain 为 0.15。演示包含 gain、soft clip、RTNeural Dense JSON、参数命令和预设；它是可修改的学习实现，用户后续引擎设计不受这些接口限制。

```powershell
.\.venv\Scripts\python.exe tools/sdk.py workspace --project audio
.\.venv\Scripts\python.exe tools/sdk.py build --project audio
.\.venv\Scripts\python.exe tools/sdk.py run --project audio --show-devices
.\.venv\Scripts\python.exe tools/sdk.py test --project audio
```

打开 `workspaces/audio-windows_sim-debug.code-workspace`，使用其 build/clean/run 和 F5。选择输入设备后才能处理输入音频；监听默认关闭。点击画布获取键盘焦点，A 保存预设、B 读取，MIDI CC7 调整首个 gain。预设优先 FlashDB，未启用时回退 littlefs。basic 和新建项目模板仅保留通用 SDK 示例及 JUCE 设备设置。

AudioTask 运行时独占 PCM 队列，参数命令在块边界执行。模型解析/加载属于控制路径，可能造成欠载；Windows 仿真不保证硬实时。当前 RTNeural 示例只适配 mono-in/mono-out Dense 网络及 tanh/relu/sigmoid/linear，其他模型由后续项目扩展。

## 可移植边界与验证

`core_cpu_0()` 在 FreeRTOS BootTask 中调用；`core_cpu_1/2/3()` 保留且不执行。CPU0 的任务、Tick/模拟中断线程绑定至同一允许使用的宿主处理器，JUCE 与文件 I/O 留在 HOST 上下文。`SDK_HOST_PROCESSOR` 可选择当前进程允许集合中的处理器编号；这验证职责和线程亲和性，不提供芯片时序仿真。

用户依赖 SDK C 接口及允许使用的库公共 API。LVGL 对象归 UiTask 所有，异步存储由 StorageTask 和宿主 I/O 桥接完成；存储回调不能直接修改 LVGL。SDK 配置 cJSON 分配器、nanopb 生成工具及 littlefs/FlashDB 后端。将 user 移植到已有嵌入式 SDK，仍需实现兼容的 OSAL/HAL、配置与单一依赖提供者；ESP32-S3/P4、RK3506 的实机后端尚未实现。

Windows 已接入 LVGL 9.6.0、FreeRTOS、cJSON、littlefs、FlashDB、nanopb、MultiButton、EasyLogger、RTNeural STL，以及 ESP-DSP 点积和 ESP-NN ReLU6 的参考子集。CherryUSB 控制器和 ESP-GMF 的 ESP 集成尚待完成。`test` 命令覆盖组件、配置、项目隔离、音频/MIDI 桥接、设备设置、窗口比例和运行时；外接 MIDI 的物理收发仍需连接设备后验收。

`docs/` 中的架构、分析、验证记录和子目录文档仅保存在本地，不纳入 Git 跟踪，也不随仓库克隆分发。

第一方 C/C++ 统一遵守根 `.clang-format`；VS Code 保存时使用 clang-format。第三方、生成物不参与格式化。迁移只给用户原文件末尾补了必要换行，没有改变任何格式选项。

## FreeRTOS、界面与逻辑按键

OSAL 是操作系统适配层，HAL 是设备适配层。当前都用于 Windows 开发。user 可以直接使用 `FreeRTOS.h` 及 task/queue/semphr/event_groups/timers/stream_buffer/message_buffer 等公共 API，也可使用 `sdk/os.h` 的便捷接口。SDK 管理内核生命周期、配置与 CPU0 上下文；原生动态/静态任务、Idle 和 TimerTask 均正确标记 CPU0。

已启用抢占/时间片、静态/动态分配、队列/队列集、互斥/递归/计数信号量、3 组任务通知、事件组、软件定时器、流/消息缓冲及任务/堆/运行时间统计。`xTaskCreate` 的堆栈参数是 StackType_t 数量，`sdk_task_create` 是字节。Windows 实际栈由宿主管理，FreeRTOS 水位不能推算嵌入式实际栈占用；运行统计包含宿主调度影响。

HOST 的 JUCE/音频/文件线程不调用内核 API。`FromISR` 只在 FreeRTOS 模拟中断处理函数中使用；普通 JUCE 回调不属于 ISR。CPU1/2/3 继续保留。

LVGL 由 UiTask 单一持有。其他 CPU0 任务用 `sdk_ui_post()` 投递回调及最多 64 字节的复制负载；回调在 UiTask 执行，不阻塞，context 必须保持有效直到投递完成。队列满时调用者决定重试或合并。

点击 LVGL 区域获取键盘焦点，方向键、Enter、Escape、A、B 映射为 `sdk/input.h` 的逻辑键。JUCE 发布电平，ButtonTask 驱动 MultiButton，单一应用消费者通过 `sdk_button_read()` 读取事件。键盘自动重复不产生重复按下；失焦会取消手势。未来 GPIO provider 只需实现 `sdk/input_backend.h`。扫描/消抖/点击/长按阈值由 Kconfig 管理；扫描间隔必须是整数个 RTOS tick。

## 存储和资源

`sdk/storage.h` 提供复制请求的异步服务，StorageTask 排队、HOST worker 执行 FlashDB/littlefs 操作。支持 KV set/get/size/delete，文件读写、偏移读写、大小、目录、删除和重命名。每次最多 256 字节，大模型、IR、预设集合通过 offset 分块传输；写完临时文件后 rename 原子替换资源。KV 空间不足时报告错误，不静默截断。

默认 littlefs 为 4096 × 128（512 KiB）模拟 NOR，FlashDB 为 4096 × 16（64 KiB），可通过 menuconfig 调整。已有镜像必须匹配容量，变化需另行迁移；SDK 不自动格式化已有损坏/尺寸不符的 littlefs 镜像，也不清理运行数据。存储回调的结果只在回调期间有效，回调不得阻塞或修改 LVGL。

CI 指提交/推送后在服务器自动构建和测试，目前暂不配置。实际硬件、CherryUSB/ESP 新适配暂缓；压力测试、热插拔和实机后端随后续开发逐步完善。

## 可复制的应用模块

```text
projects/audio/user/
├─ CMakeLists.txt              # 发现和组合模块，不登记内部源文件
├─ app/                       # SDK 生命周期、MIDI/按键/存储与模块的接线
│  └─ CMakeLists.txt
├─ cores/
│  └─ CMakeLists.txt
├─ protocol/
│  └─ CMakeLists.txt
├─ rakure_ea/
│  ├─ CMakeLists.txt
│  ├─ include/rea/rea.h        # rea_ 函数/类型，REA_ 宏；通用链处理接口
│  ├─ include/rea/rea_audio.h  # SDK provider 的音频任务/控制接口
│  ├─ src/                    # C 效果链和禁用 RTNeural 时的 C stub
│  ├─ ports/sdk/              # FreeRTOS/SDK 音频任务、预设与参数接线
│  ├─ ports/rtneural/         # 必要的 C++ 库适配，通过 rea.h 提供 C 接口
│  └─ tests/CMakeLists.txt
└─ rakure_gui/
   ├─ CMakeLists.txt
   ├─ include/rakure_gui/
   └─ src/                    # 仅依赖 LVGL，用 actions 回调与应用交互
```

每个独立模块在根目录维护 CMakeLists，使用当前模块的相对路径列出源码，通过 `target_include_directories(... PUBLIC ...)` 导出公共头文件，通过 target 链接声明依赖。内部 src/include 文件夹由模块的构建入口管理。

SDK 项目的 user/CMakeLists 使用：

```cmake
include("${CMAKE_SOURCE_DIR}/cmake/UserModules.cmake")
sdk_add_user_modules(sdk_user)
```

发现范围仅为 `user/*/CMakeLists.txt`，模块用目录属性声明需要组合的目标：

```cmake
add_library(my_module STATIC src/my_module.c)
target_include_directories(my_module PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
set_property(DIRECTORY PROPERTY SDK_USER_MODULE_TARGET my_module)
```

复制整个模块文件夹到另一个使用此约定的项目 user/ 后，重新 build/configure 即可；Ninja 也会检测新增模块入口。父项目不需要逐个登记文件。新增源码时只修改该模块自己的 CMakeLists。自动接入负责构建和索引；运行时仍由应用连接模块入口，例如在 user_app_start 调用 rea_audio_start，在 UiTask 的 user_ui_create 调用 rakure_gui_create，具体接线可参考 audio 的 app/app.c。模块依赖必须由接收项目提供；目标名不能重复，移除模块前先解除应用对它的直接调用。

`rakure::ea` 为通用处理目标，`rakure::ea_sdk` 为可选 SDK provider，`rakure::gui` 为 LVGL GUI。在普通 CMake 工程中可用 add_subdirectory 接入：设置 `REA_BUILD_SDK_PORT=OFF`，关闭 RTNeural 或通过 `REA_RTNEURAL_TARGET` 提供目标；GUI 通过 `RAKURE_GUI_LVGL_TARGET` 接收 LVGL 目标。迁移到其他 SDK 时对接 provider，通用链/GUI 不直接依赖 JUCE 或 Windows。

项目、示例、模板及其测试中能够用 C 实现的第一方源码统一使用 `.c`，以 C11 为基础。效果链、GUI、FreeRTOS 音频任务、应用和 CPU 入口均按 C 编译。公共头文件兼容 C；C++ 调用方使用 `extern "C"` 保护。JUCE、系统适配以及 RTNeural 等 C++ 库的必要封装保留 `.cpp`，并集中在适配目录，不向业务接口暴露 C++ 类型。

复制通用效果链和 GUI 到仅启用 C 的工程时，设置 `REA_BUILD_SDK_PORT=OFF`、`REA_ENABLE_RTNEURAL=OFF`，接收工程可使用 `project(MyApp LANGUAGES C)`。启用 RTNeural 时接收工程需同时启用 CXX；由 C 编写的应用仍通过原有 `rea_` 接口调用，最终链接包含 C++ 运行库。SDK 音频任务通过 C11 原子操作保持 CPU0/HOST 并发安全，当前 MSVC 由模块单独添加 `/experimental:c11atomics`；该选项不施加到通用链、GUI 或第三方源码。通用链在控制路径使用 C 标准堆分配，音频处理路径不分配内存。

VS Code 索引来自接收项目的实际编译数据库。复制模块后若需要刷新浏览路径，可执行 `workspace --project <id>`；生成器保留已有自定义 c_cpp_properties.json。各项目继续使用独立编译目录、数据和符号缓存。
