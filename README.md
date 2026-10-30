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
├─ projects/<product>/             # 对应产品分支中的实际项目
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

SDK 稳定线为 `sdk/main`，通用功能使用 `sdk/feature/<topic>`；产品线为 `project/<name>/main`，产品功能使用 `project/<name>/feature/<topic>`。产品从审阅后的 SDK 发布标签建立，之后主动合并新的 SDK 标签。

当前迁移在 `sdk/feature/submodule-project-layout`，改动保留供审阅。现有 `sdk/v0.1.0` 仍是旧目录结构，不能作为新产品创建基点；本次没有创建新提交、发布标签或产品分支。待迁移审阅并正式发布新标签后，可运行：

```powershell
# 将占位符替换成正式发布的新结构 SDK 标签
.\.venv\Scripts\python.exe tools/sdk.py new-project panel-a --sdk-ref <new-sdk-release-tag>
.\.venv\Scripts\python.exe tools/sdk.py sync
.\.venv\Scripts\python.exe tools/sdk.py workspace --project panel-a
```

该命令要求工作区干净，在当前 checkout 创建产品分支及 `projects/panel-a/user/`，不自动提交。切换产品分支后执行 `sync`，再打开对应工作区。Git 官方说明多 checkout worktree 的 submodule 支持尚不完整，因此默认使用同一 checkout 切换分支；需要同时开发多个产品时使用独立 clone。参考 [Git worktree 限制](https://git-scm.com/docs/git-worktree#_bugs)。

依赖版本的唯一依据是父仓库的 submodule gitlink；`components.json` 的版本文字仅供阅读。`sync` 执行固定版本的 `git submodule update --init --recursive`，不跟踪上游最新分支。默认同步 Windows、参考算法和推理依赖；`sync --profile embedded esp` 可同步 CherryUSB/ESP-GMF；`sync --only lvgl` 可单独同步一个库。更新依赖时需另行计划、审阅 gitlink、重跑验证并在 SDK 分支发布。

## 窗口显示

LVGL 保持配置的原始分辨率，JUCE 窗口调整时等比缩放、居中留边，鼠标按同一变换映射。留边不接受触摸；拖出、缩放和失焦会取消按压。原始画面和显示区域截图分别保存为构建目录中的 `snapshot.png` 与 `viewport.png`。

## 音频与 MIDI

主窗口顶部 **Audio / MIDI settings** 可选择音频输入/输出、通道、采样率、缓冲大小，以及 MIDI 输入和默认输出。VS Code 的 **SDK: device settings** 任务也可直接打开。设备配置按项目保存在 `.local/data/<id>/<board>/media-devices.xml`。首次运行只请求默认双通道输出，保持静音；basic 的音频监听开关默认关闭。无设备时仍能启动。

```powershell
.\.venv\Scripts\python.exe tools/sdk.py run --project basic --show-devices
.\.venv\Scripts\python.exe tools/sdk.py run --project basic --no-audio
```

user 通过 `sdk/audio.h` 和 `sdk/midi.h` 使用处理回调、PCM 队列及 MIDI 收发；音频回调归 HOST，CPU0 通过有界桥接交互。接口约定以公共头文件注释为准；音频回调中避免阻塞、文件 I/O、LVGL 和 FreeRTOS 调用。

## 可移植边界与验证

`core_cpu_0()` 在 FreeRTOS BootTask 中调用；`core_cpu_1/2/3()` 保留且不执行。CPU0 的任务、Tick/模拟中断线程绑定至同一允许使用的宿主处理器，JUCE 与文件 I/O 留在 HOST 上下文。`SDK_HOST_PROCESSOR` 可选择当前进程允许集合中的处理器编号；这验证职责和线程亲和性，不提供芯片时序仿真。

用户依赖 SDK C 接口及允许使用的库公共 API。LVGL 对象归 UiTask 所有，异步存储由 StorageTask 和宿主 I/O 桥接完成；存储回调不能直接修改 LVGL。SDK 配置 cJSON 分配器、nanopb 生成工具及 littlefs/FlashDB 后端。将 user 移植到已有嵌入式 SDK，仍需实现兼容的 OSAL/HAL、配置与单一依赖提供者；ESP32-S3/P4、RK3506 的实机后端尚未实现。

Windows 已接入 LVGL 9.6.0、FreeRTOS、cJSON、littlefs、FlashDB、nanopb、MultiButton、EasyLogger、RTNeural STL，以及 ESP-DSP 点积和 ESP-NN ReLU6 的参考子集。CherryUSB 控制器和 ESP-GMF 的 ESP 集成尚待完成。`test` 命令覆盖组件、配置、项目隔离、音频/MIDI 桥接、设备设置、窗口比例和运行时；外接 MIDI 的物理收发仍需连接设备后验收。

`docs/` 中的架构、分析、验证记录和子目录文档仅保存在本地，不纳入 Git 跟踪，也不随仓库克隆分发。

第一方 C/C++ 统一遵守根 `.clang-format`；VS Code 保存时使用 clang-format。第三方、生成物不参与格式化。迁移只给用户原文件末尾补了必要换行，没有改变任何格式选项。
