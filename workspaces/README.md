# 项目专属 VS Code 工作区

运行 `.venv/Scripts/python.exe tools/sdk.py workspace --project <id>`，打开生成的
`workspaces/<id>-windows_sim-debug.code-workspace`。

每个工作区使用自己的 `out/<id>/<board>/<config>/compile_commands.json`、
`.local/intellisense/<id>/<board>/<config>/browse.vc.db` 与 `ipch/`。
索引路径来自该项目实际编译的源文件和头文件目录，不扫描所有产品。
每个项目的本地 `.vscode/c_cpp_properties.json` 以项目 ID 命名配置（例如 `audio`），通过 `${default}` 继承这些设置。旧的通用模板会自动迁移名称，自定义内容继续保留。
工作区只打开所选项目目录，SDK/第三方函数通过编译数据库和限定浏览路径跳转。
build/clean/run 任务和 F5 调试入口也包含在生成的工作区中。
切换专属工作区时打开相应文件；该方式的任务固定指向工作区所属项目。

直接打开仓库文件夹时，也可以在根目录的 C/C++ 配置选择器中选择 `audio`、`basic` 或其他工程 ID，build/clean/run 和 F5 统一读取 `${command:cpptools.activeConfigName}`。各配置拥有独立的编译数据库和符号数据库，根目录模式关闭共享的持久化 ipch 缓存，专属工作区的缓存配置继续保留。

新增完整项目或需要更新索引路径时，执行 **SDK: refresh projects**（命令行 `tools/sdk.py refresh-projects`）。它发现 examples/projects 下的工程并刷新根目录配置列表及标准项目配置，不构建程序。`workspace --project <id>` 生成专属工作区时也刷新列表；普通 build/clean/run 不改写 IDE 文件。项目选择只决定当前检出代码的构建目标，Git 分支仍单独管理。

工作区包含当前机器的编译器路径，因此由工具生成并忽略提交。
编译选项由 compile_commands.json 提供；请安装 Microsoft C/C++ 扩展。
