"""Evaluate native configuration, target selection and nested-header changes."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from toolchain import build_env, compiler_arguments, tool


class LvglBuild(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.env = build_env()
        cls.extra, cls.compiler = compiler_arguments(cls.env)

    def setUp(self):
        parent = ROOT / 'out/lvgl-build-contracts'
        parent.mkdir(parents=True, exist_ok=True)
        temp = tempfile.TemporaryDirectory(prefix='project with spaces ', dir=parent)
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.config = self.root / 'project/config'
        self.config.mkdir(parents=True)
        self.helper = self.config / 'switches.h'
        self.switches(0, 1, 0)
        (self.config / 'lv_conf.h').write_text('''#define LV_CONF_H
#include "switches.h"
#define LV_BUILD_DEMOS TEST_DEMOS
#define LV_BUILD_EXAMPLES (TEST_EXAMPLES && !TEST_DEMOS && SDK_TEST_SWITCH)
#define LV_USE_THORVG TEST_THORVG
#define LV_USE_THORVG_INTERNAL 1
#define LV_USE_LOG 0
''', encoding='utf-8')
        (self.root / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.25)
project(LvglBuildContracts LANGUAGES C CXX)
add_compile_definitions(SDK_TEST_SWITCH=1)
find_package(Python COMPONENTS Interpreter REQUIRED)
set(SDK_LVGL_DIR "{ROOT.as_posix()}/third_party/src/lvgl")
set(SDK_GENERATED_DIR "${{CMAKE_BINARY_DIR}}/generated")
include("{ROOT.as_posix()}/cmake/LvglConfig.cmake")
sdk_configure_lvgl("${{CMAKE_SOURCE_DIR}}/project" "${{SDK_GENERATED_DIR}}")
sdk_configure_lvgl_build()
set(LV_BUILD_CONF_PATH "${{SDK_GENERATED_DIR}}/lv_conf.h" CACHE PATH "" FORCE)
set(LV_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
set(LV_FETCH_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(LV_USE_FIND_PACKAGE OFF CACHE BOOL "" FORCE)
set(LV_USE_PKG_CONFIG OFF CACHE BOOL "" FORCE)
add_subdirectory("${{SDK_LVGL_DIR}}" lvgl EXCLUDE_FROM_ALL)
target_include_directories(lvgl PUBLIC "${{SDK_LVGL_CONFIG_DIR}}")
set(selected "")
foreach(name lvgl_examples lvgl_demos lvgl_thorvg)
    if(TARGET ${{name}})
        string(APPEND selected "${{name}}\\n")
    endif()
endforeach()
file(WRITE "${{CMAKE_BINARY_DIR}}/selected.txt" "${{selected}}")
add_custom_target(config_ready)
add_executable(disabled_log "{ROOT.as_posix()}/tests/lvgl_log.c" "{ROOT.as_posix()}/sdk/adapters/lvgl/log.c")
target_include_directories(disabled_log PRIVATE "{ROOT.as_posix()}/sdk/include" "{ROOT.as_posix()}/sdk/adapters/lvgl" "${{SDK_LVGL_DIR}}" "${{SDK_LVGL_DIR}}/include")
target_compile_definitions(disabled_log PRIVATE LV_KCONFIG_IGNORE LV_CONF_PATH="${{SDK_GENERATED_DIR}}/lv_conf.h")
target_include_directories(disabled_log PRIVATE "${{SDK_LVGL_CONFIG_DIR}}")
''', encoding='utf-8')

    def switches(self, demos, examples, thorvg):
        self.helper.write_text(f'#define TEST_DEMOS {demos}\n#define TEST_EXAMPLES {examples}\n#define TEST_THORVG {thorvg}\n', encoding='utf-8')

    def configure(self, success=True):
        result = subprocess.run([tool('cmake'), '-S', str(self.root), '-B', str(self.root / 'build'), '-G', 'Ninja',
                                 '-DCMAKE_MAKE_PROGRAM=' + tool('ninja'), '-DPython_EXECUTABLE=' + sys.executable,
                                 '-DCMAKE_C_COMPILER=' + self.compiler, '-DCMAKE_CXX_COMPILER=' + self.compiler,
                                 '-DCMAKE_BUILD_TYPE=Debug', *self.extra], env=self.env,
                                capture_output=True, text=True, errors='replace')
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result.stdout + result.stderr

    def build(self, target):
        result = subprocess.run([tool('cmake'), '--build', str(self.root / 'build'), '--target', target],
                                env=self.env, capture_output=True, text=True, errors='replace')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_header_expressions_select_targets_and_nested_changes_reconfigure(self):
        self.configure()
        selected = self.root / 'build/selected.txt'
        self.assertEqual(selected.read_text().splitlines(), ['lvgl_examples'])
        self.switches(1, 1, 1)
        self.build('config_ready')
        self.assertEqual(selected.read_text().splitlines(), ['lvgl_demos', 'lvgl_thorvg'])
        self.switches(0, 0, 0)
        self.build('config_ready')
        self.assertEqual(selected.read_text(), '')

    def test_disabled_log_compiles_and_runs_without_logger_or_lvgl_library(self):
        self.configure()
        self.build('disabled_log')
        subprocess.run([str(self.root / 'build/disabled_log.exe')], check=True)

    def test_unprovided_dependency_fails_at_configuration(self):
        with (self.config / 'lv_conf.h').open('a') as stream:
            stream.write('#define LV_USE_FREETYPE 1\n')
        self.assertIn('LV_USE_FREETYPE=1', self.configure(success=False))

    def test_invalid_header_is_not_silently_treated_as_disabled(self):
        self.helper.write_text('#error broken project helper\n', encoding='utf-8')
        self.assertIn('broken project helper', self.configure(success=False))


if __name__ == '__main__':
    unittest.main()
