# 项目工程

完整应用/产品统一采用 projects/<name>/project.json 与 projects/<name>/user/。项目 ID 必须与 examples/ 中的 ID 保持唯一。

当前 audio 项目位于 projects/audio，内部 rakure_ea 和 rakure_gui 为独立模块，各自管理 CMakeLists、源码、公开头文件和依赖。user/CMakeLists 自动发现并组合已注册模块。

```powershell
.\.venv\Scripts\python.exe tools/sdk.py projects
.\.venv\Scripts\python.exe tools/sdk.py workspace --project audio
.\.venv\Scripts\python.exe tools/sdk.py build --project audio
.\.venv\Scripts\python.exe tools/sdk.py run --project audio
```

打开 workspaces/audio-windows_sim-debug.code-workspace 使用独立 build/clean/run、F5 和 C/C++ 索引。输出为 out/audio/<board>/<config>，数据为 .local/data/audio/<board>，符号缓存为 .local/intellisense/audio/<board>/<config>。

SDK 分支维护框架、示例、内置 audio 演示和模板。产品采用 project/<name>/main 及其功能分支；SDK 发布后可用 tools/sdk.py new-project <name> --sdk-ref <reviewed-tag> 从审阅标签创建产品分支，默认生成到 projects/<name>/user/，不自动提交。分支选择代码版本，--project 选择当前检出代码中的构建目标。
