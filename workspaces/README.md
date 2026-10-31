# 项目专属 VS Code 工作区

首次运行 `sdk.cmd setup --project <id>`（或 VS Code 的 SDK: setup），打开生成的
`workspaces/<id>-windows_sim-debug.code-workspace`。

每个工作区使用自己的 `out/<id>/<board>/<config>/compile_commands.json`、
`.local/intellisense/<id>/<board>/<config>/browse.vc.db` 与 `ipch/`。
索引路径来自该项目实际编译的源文件和头文件目录，不扫描所有产品。
每个项目的 `.vscode/c_cpp_properties.json` 以项目 ID 命名配置（例如 `audio`），是随 Git 跟踪的项目配置源。它通过 `${default}` 继承 SDK 默认值，支持 JSONC 注释、尾逗号和项目 env；刷新及 workspace 不会重写已有项目文件。
工作区只打开所选项目目录，SDK/第三方函数通过编译数据库和限定浏览路径跳转。
build/clean/run、固定到该项目的 SDK: setup 任务和 F5 调试入口也包含在生成的工作区中。
切换专属工作区时打开相应文件；该方式的任务固定指向工作区所属项目。

直接打开仓库文件夹时，也可以在根目录的 C/C++ 配置选择器中选择 `audio`、`basic` 或其他工程 ID，build/clean/run 和 F5 统一读取 `${command:cpptools.activeConfigName}`。各配置拥有独立的编译数据库和符号数据库，根目录模式关闭共享的持久化 ipch 缓存，专属工作区的缓存配置继续保留。

新增完整项目、修改项目 IDE 配置或切换产品分支后，执行 **SDK: refresh projects**（命令行 `tools/sdk.py refresh-projects`）。它发现 examples/projects 下的工程，读取各项目源配置和已有编译数据库，并生成根目录配置列表；缺少项目配置时才初始化标准文件。`workspace --project <id>` 生成专属工作区时也刷新列表；sdk.cmd 的 build/run/configure 在生成结果失效时补齐 IDE 文件。项目选择只决定当前检出代码的构建目标，Git 分支仍单独管理。

根目录中的 `${workspaceFolder}` 指向仓库，项目源配置中的同一变量指向项目目录，SDK 会在合成根配置时转换路径。项目 env 先在该项目范围展开，避免不同项目的同名变量串用。编译数据库和符号数据库由 SDK 按项目/板卡/构建配置计算，源文件使用 `${default}`；工具拒绝引用其他项目的数据库和索引路径。

项目的 c_cpp_properties.json 和模板进入版本管理；根 `.vscode/c_cpp_properties.json` 是忽略提交的本地生成结果。首次克隆时先执行 SDK: setup；它不依赖已有 .venv 或根索引配置。环境就绪后可用 SDK: refresh projects 只刷新配置列表。根目录受 SDK 管理的条目由项目源文件生成，直接修改这些生成条目会在下次刷新时被更新。

工作区的任务、调试、解释器、索引及缓存路径使用工作区变量，不保存 SDK 默认编译器的主机绝对路径。全部 SDK 任务通过根 sdk.cmd，依然固定选择该工作区所属项目。
编译选项由当前机器重新生成的 compile_commands.json 提供；未匹配的文件由 Microsoft C/C++ 扩展发现本机编译器。工作区仍由工具生成并忽略提交。
