"""Build copied audio/GUI folders with only C and no original SDK runtime."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from toolchain import build_env, compiler_arguments, tool


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--generated', type=Path, required=True)
    args = parser.parse_args()
    parent = ROOT / 'out/module-contracts'
    parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=parent) as temporary:
        fixture = Path(temporary)
        user = fixture / 'user'
        user.mkdir()
        for name in ('rakure_ea', 'rakure_gui'):
            shutil.copytree(ROOT / 'projects/audio/user' / name, user / name)
        (user / 'CMakeLists.txt').write_text(
            f'include("{ROOT.as_posix()}/cmake/UserModules.cmake")\n'
            'sdk_add_user_modules(copied_modules)\n', encoding='utf-8')
        (fixture / 'CMakeLists.txt').write_text(f'''
cmake_minimum_required(VERSION 3.25)
project(CopiedModules LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(MSVC AND DEFINED SDK_MSVC_INCLUDE_PREFIX)
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${{SDK_MSVC_INCLUDE_PREFIX}}")
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(BUILD_TESTING ON)
enable_testing()
set(REA_BUILD_SDK_PORT OFF CACHE BOOL "")
set(REA_ENABLE_RTNEURAL OFF CACHE BOOL "")
# The receiving project supplies LVGL's public target and configuration.
# No SDK runtime, FreeRTOS target or original app include path is provided.
add_library(lvgl INTERFACE)
target_include_directories(lvgl INTERFACE "{ROOT.as_posix()}/third_party/src/lvgl" "{ROOT.as_posix()}/third_party/src/lvgl/include" "{args.generated.as_posix()}")
target_compile_definitions(lvgl INTERFACE [=[LV_CONF_PATH="{args.generated.as_posix()}/lv_conf.h"]=] LV_KCONFIG_IGNORE)
add_subdirectory(user)
add_executable(copy_check main.c)
target_link_libraries(copy_check PRIVATE copied_modules)
''', encoding='utf-8')
        main_source = '''#include "rea/rea.h"
#include "rakure_gui/rakure_gui.h"
int main(void) {
    rakure_gui_status_t status = {0}; status.frames = 1;
    rea_chain_t *chain = rea_chain_create();
    if (!chain || rea_chain_append_gain(chain, 0.5f) < 0 || rea_chain_prepare(chain, 48000, 1)) return 1;
    float sample = 0.4f;
    int error = rea_chain_process(chain, &sample, 1, 1);
    rea_chain_destroy(chain);
    return error || sample < 0.199f || sample > 0.201f || status.frames != 1;
}
'''
        (fixture / 'main.c').write_text(main_source, encoding='utf-8')
        env = build_env()
        extra, _ = compiler_arguments(env)
        output = fixture / 'build'
        subprocess.run([tool('cmake'), '-S', str(fixture), '-B', str(output), '-G', 'Ninja',
                        '-DCMAKE_MAKE_PROGRAM=' + tool('ninja'), *extra], env=env, check=True)
        def build():
            subprocess.run([tool('cmake'), '--build', str(output), '--parallel', '4'], env=env, check=True)
            subprocess.run([str(output / 'copy_check.exe')], check=True)
        build()
        ctest = Path(tool('cmake')).with_name('ctest.exe' if sys.platform == 'win32' else 'ctest')
        subprocess.run([str(ctest), '--test-dir', str(output), '--output-on-failure'], env=env, check=True)
        # Copying a new registered module must be noticed by Ninja's glob check;
        # neither the project root nor its aggregate target is edited.
        extra_module = user / 'extra'
        extra_module.mkdir()
        (extra_module / 'CMakeLists.txt').write_text('''add_library(copy_extra STATIC extra.c)
target_include_directories(copy_extra PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_property(DIRECTORY PROPERTY SDK_USER_MODULE_TARGET copy_extra)
''', encoding='utf-8')
        (extra_module / 'extra.h').write_text('int copied_value(void);\n', encoding='utf-8')
        (extra_module / 'extra.c').write_text('#include "extra.h"\nint copied_value(void) { return 7; }\n', encoding='utf-8')
        (fixture / 'main.c').write_text('#include "extra.h"\n' + main_source.replace(
            'return error ||', 'return copied_value() != 7 || error ||'), encoding='utf-8')
        build()
        database = json.loads((output / 'compile_commands.json').read_text(encoding='utf-8'))
        for entry in database:
            if Path(entry['file']).resolve().is_relative_to(ROOT / 'projects'):
                raise RuntimeError('Copied build still compiles original app sources')
            if Path(entry['file']).suffix != '.c' or '/TP' in entry['command'] or '-x c++' in entry['command']:
                raise RuntimeError('C-only copied build unexpectedly compiles C++')
        cache = (output / 'CMakeCache.txt').read_text(encoding='utf-8')
        if any(line.startswith('CMAKE_CXX_COMPILER:') for line in cache.splitlines()):
            raise RuntimeError('C-only copied build unexpectedly enables a C++ compiler')
        if not any(Path(entry['file']).name == 'rakure_gui.c' for entry in database):
            raise RuntimeError('Copied GUI was not compiled')
        if not any(Path(entry['file']).name == 'rea_neural_stub.c' for entry in database):
            raise RuntimeError('C-only copied build did not use the C RTNeural stub')
        print('PASS C-only copied REA/GUI, effects, public includes and automatic module discovery')


if __name__ == '__main__':
    main()
