# Dependency ownership

`deps.lock.json` pins repository commits and downloaded archive hashes. `tools/sdk.ps1 sync` obtains the default Windows, inference and ANSI-reference dependencies into `.cache/sources/` and checks the archives. `--record-hashes` is a maintainer operation used only when introducing or updating a dependency.

The Windows build recipes are in `cmake/Components.cmake`, `cmake/Dependencies.cmake`, `cmake/Protocol.cmake`, and `recipes/esp-reference.cmake`.

CherryUSB is registered for embedded integration; controller and board selection are not implemented. ESP-GMF is an explicitly selected ESP-only package and is not compiled into the generic host. Platform recipes must preserve each upstream license and account for transitive dependencies.

Upstream caches are not a public include search path. Each target exposes only the include directories needed by the selected component. Generated overlays are in the build directory and do not replace original upstream source files.
