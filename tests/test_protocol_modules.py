"""Project protocol modules own generation without leaking into SDK targets."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from toolchain import build_env, compiler_arguments, tool


class ProtocolModules(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.env = build_env()
        cls.extra, cls.compiler = compiler_arguments(cls.env)

    def check_project(self, project, enabled=True, empty=False):
        parent = ROOT / 'out/protocol-module-contracts'
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='isolated project ', dir=parent) as temp:
            root = Path(temp)
            shutil.copytree(ROOT / project / 'user/protocol', root / 'user/protocol')
            if empty:
                for path in (root / 'user/protocol').glob('*.proto'):
                    path.unlink()  # This test's own copied fixture only.
            active = enabled and not empty
            (root / 'user/CMakeLists.txt').write_text(
                f'include("{ROOT.as_posix()}/cmake/UserModules.cmake")\nsdk_add_user_modules(sdk_user)\n', encoding='utf-8')
            (root / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.25)
project(ProtocolModules LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(MSVC AND DEFINED SDK_MSVC_INCLUDE_PREFIX)
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${{SDK_MSVC_INCLUDE_PREFIX}}")
endif()
find_package(Python COMPONENTS Interpreter REQUIRED)
set(CONFIG_SDK_NANOPB {"y" if enabled else "n"})
set(SDK_NANOPB_DIR "{ROOT.as_posix()}/third_party/src/nanopb")
add_library(sdk_components INTERFACE)
add_library(sdk_runtime INTERFACE)
target_link_libraries(sdk_runtime INTERFACE sdk_components)
if(CONFIG_SDK_NANOPB STREQUAL "y")
    add_library(sdk_nanopb STATIC "${{SDK_NANOPB_DIR}}/pb_common.c" "${{SDK_NANOPB_DIR}}/pb_encode.c" "${{SDK_NANOPB_DIR}}/pb_decode.c")
    target_include_directories(sdk_nanopb PUBLIC "${{SDK_NANOPB_DIR}}")
    target_link_libraries(sdk_components INTERFACE sdk_nanopb)
endif()
include("{ROOT.as_posix()}/cmake/Protocol.cmake")
if(TARGET sdk_user_protocol)
    message(FATAL_ERROR "SDK created a product protocol before adding user modules")
endif()
add_subdirectory(user)
get_target_property(component_deps sdk_components INTERFACE_LINK_LIBRARIES)
get_target_property(runtime_deps sdk_runtime INTERFACE_LINK_LIBRARIES)
if("${{component_deps}};${{runtime_deps}}" MATCHES "sdk_user_protocol")
    message(FATAL_ERROR "Product protocol leaked into SDK dependencies")
endif()
add_executable(sdk_only "{ROOT.as_posix()}/tests/protocol_sdk_only.c")
target_link_libraries(sdk_only PRIVATE sdk_runtime)
if({"TRUE" if active else "FALSE"})
    if(NOT TARGET sdk_user_protocol)
        message(FATAL_ERROR "Protocol module failed to register its target")
    endif()
    add_executable(consumer "{ROOT.as_posix()}/tests/protocol_consumer.c")
    target_link_libraries(consumer PRIVATE sdk_runtime sdk_user)
elseif(TARGET sdk_user_protocol)
    message(FATAL_ERROR "Disabled/empty protocol module created a target")
endif()
''', encoding='utf-8')
            output = root / 'build'
            result = subprocess.run([tool('cmake'), '-S', str(root), '-B', str(output), '-G', 'Ninja',
                                     '-DCMAKE_MAKE_PROGRAM=' + tool('ninja'), '-DCMAKE_C_COMPILER=' + self.compiler,
                                     '-DPython_EXECUTABLE=' + sys.executable, *self.extra], env=self.env,
                                    capture_output=True, text=True, errors='replace')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            # Building only an SDK consumer must not run the product generator.
            self.build(output, 'sdk_only')
            self.assertFalse((output / 'user/protocol/generated/heartbeat.pb.c').exists())
            database = json.loads((output / 'compile_commands.json').read_text())
            sdk_command = next(item['command'] for item in database if Path(item['file']).name == 'protocol_sdk_only.c')
            self.assertNotIn('protocol/generated', sdk_command.replace('\\', '/'))
            if active:
                self.build(output, 'consumer')
                self.assertTrue((output / 'user/protocol/generated/heartbeat.pb.c').exists())
                subprocess.run([str(output / 'consumer.exe')], check=True)

    def build(self, output, target):
        result = subprocess.run([tool('cmake'), '--build', str(output), '--target', target], env=self.env,
                                capture_output=True, text=True, errors='replace')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_audio_basic_and_template_protocols_remain_project_owned(self):
        for project in ('projects/audio', 'examples/basic', 'templates/project'):
            with self.subTest(project=project):
                self.check_project(project)

    def test_nanopb_disabled_and_empty_protocol_directory(self):
        self.check_project('templates/project', enabled=False)
        self.check_project('templates/project', empty=True)


if __name__ == '__main__':
    unittest.main()
