# 项目工程

完整应用/产品统一采用 projects/<name>/project.json 与 projects/<name>/user/。项目 ID 必须与 examples/ 中的 ID 保持唯一，支持小写字母、数字、连字符和下划线，例如 rakure_gui。

各项目的 .vscode/c_cpp_properties.json 为可跟踪的 IDE 配置源，配置名与项目 ID 一致。SDK 读取该文件和真实编译数据库来生成根目录配置；项目源文件保留注释和自定义内容，不在刷新时重写。根 c_cpp_properties.json、生成工作区、out 和 .local 保持本地。

当前 audio 项目位于 projects/audio，内部 rakure_ea 和 rakure_gui 为独立模块，各自管理 CMakeLists、源码、公开头文件和依赖。user/CMakeLists 自动发现并组合已注册模块。

每个项目必须提供自己的 config/lv_conf.h，维护原生 LVGL 宏；audio 的文件为 projects/audio/config/lv_conf.h。新项目从 templates/project/config/lv_conf.h 复制初始内容，之后独立维护。构建不会重写项目源配置，也不会在缺失时使用 SDK 或其他项目的配置。所选文件复制到当前项目的 generated/lv_conf.h，config 目录接入编译和索引；修改后重新构建会自动更新。分辨率等 CONFIG_SDK_* 选项继续在 user/app.conf 中维护。

项目配置使用当前 LVGL v9.6.0 的完整官方 lv_conf_template.h，保留全部选项和注释。初始内容启用配置，并保留 ARGB8888、16 ms 刷新周期和 SDK 内存映射等现有设置；各项目在对应原生选项处独立修改。

```powershell
.\sdk.cmd setup --project audio
.\.venv\Scripts\python.exe tools/sdk.py projects
.\.venv\Scripts\python.exe tools/sdk.py refresh-projects
.\.venv\Scripts\python.exe tools/sdk.py workspace --project audio
.\.venv\Scripts\python.exe tools/sdk.py build --project audio
.\.venv\Scripts\python.exe tools/sdk.py run --project audio
```

打开 workspaces/audio-windows_sim-debug.code-workspace 使用独立 build/clean/run、F5 和 C/C++ 索引。输出为 out/audio/<board>/<config>，数据为 .local/data/audio/<board>，符号缓存为 .local/intellisense/audio/<board>/<config>。

也可以直接打开仓库根目录，在 C/C++ 配置选择器中选择 audio 后执行 SDK: run；任务会先编译再运行 audio，F5 使用同一选择。新增完整产品工程后执行 SDK: refresh projects，其项目 ID 会成为新的可选配置。当前 audio/user/rakure_gui 仍是 audio 内的 GUI 模块，若建立独立的 projects/rakure_gui 工程，需要自己的项目元数据、配置和 user 构建入口。

SDK 分支维护框架、示例、内置 audio 演示和模板。产品采用 project/<name>/main 及其功能分支；SDK 发布后可用 tools/sdk.py new-project <name> --sdk-ref <reviewed-tag> 从审阅标签创建产品分支，默认生成到 projects/<name>/user/，不自动提交。分支选择代码版本，--project 选择当前检出代码中的构建目标。
