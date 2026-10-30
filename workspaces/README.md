# 项目专属 VS Code 工作区

运行 `.venv/Scripts/python.exe tools/sdk.py workspace --project <id>`，打开生成的
`workspaces/<id>-windows_sim-debug.code-workspace`。

每个工作区使用自己的 `out/<id>/<board>/<config>/compile_commands.json`、
`.local/intellisense/<id>/<board>/<config>/browse.vc.db` 与 `ipch/`。
索引路径来自该项目实际编译的源文件和头文件目录，不扫描所有产品。
每个项目的本地 `.vscode/c_cpp_properties.json` 通过 `${default}` 继承这些设置。
工作区只打开所选项目目录，SDK/第三方函数通过编译数据库和限定浏览路径跳转。
这样可避免仓库根目录 `.vscode/settings.json` 的 basic 设置覆盖产品工作区。
build/clean/run 任务和 F5 调试入口也包含在生成的工作区中。
切换产品时打开相应工作区；直接打开仓库文件夹时默认使用 basic 示例。

工作区包含当前机器的编译器路径，因此由工具生成并忽略提交。
编译选项由 compile_commands.json 提供；请安装 Microsoft C/C++ 扩展。
