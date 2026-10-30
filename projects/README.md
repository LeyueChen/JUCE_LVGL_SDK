# 产品工程

每个产品采用 `projects/<name>/project.json` 与 `projects/<name>/user/`。

SDK 分支保留示例和模板；产品分支 `project/<name>/main` 保存该产品及其 SDK 快照。只有当前检出分支包含的产品才会显示在这里。

使用 `.venv/Scripts/python.exe tools/sdk.py new-project <name> --sdk-ref <已验证的新布局SDK标签>` 创建产品分支。迁移期间尚未创建新的发布标签，原 `sdk/v0.1.0` 属于旧布局。

构建示例：`.venv/Scripts/python.exe tools/sdk.py build --project <name>`。VS Code 工作区由 `.venv/Scripts/python.exe tools/sdk.py workspace --project <name>` 生成。
