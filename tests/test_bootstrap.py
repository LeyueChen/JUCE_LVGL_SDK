"""First-use contracts; no real package install, submodule sync, build or GUI."""
from contextlib import ExitStack, redirect_stdout
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import bootstrap
import ide
import projects
import sdk
from config.generate import generate


class BootstrapContracts(unittest.TestCase):
    def setUp(self):
        parent = ROOT / 'out/bootstrap-contracts'
        parent.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix='checkout with spaces ', dir=parent)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        shutil.copytree(ROOT / 'templates', self.root / 'templates')
        self.project = projects.scaffold(self.root, 'audio')
        (self.root / 'tools').mkdir()
        (self.root / 'tools/requirements.txt').write_text('kconfiglib==14.1.0 --hash=sha256:example\n')
        self.python = self.root / '.venv/Scripts/python.exe'
        self.ready = {'ready': True, 'exists': True, 'python': str(self.python), 'pip': False}

    def test_first_use_creates_local_environment_and_installs_only_lock(self):
        missing = {'ready': False, 'exists': False}
        pending = dict(self.ready, ready=False)
        with patch.object(bootstrap, 'environment_status', side_effect=[missing, pending, self.ready]), \
                patch.object(bootstrap.venv, 'EnvBuilder') as builder, patch.object(bootstrap, 'run') as run:
            self.assertEqual(bootstrap.ensure_environment(self.root, False), self.python)
        builder.assert_called_once_with(with_pip=True)
        builder.return_value.create.assert_called_once_with(self.root / '.venv')
        self.assertEqual(run.call_args_list[0].args[0], [self.python, '-I', '-m', 'ensurepip'])
        command = run.call_args_list[1].args[0]
        self.assertEqual(command, [self.python, '-I', '-m', 'pip', 'install', '--cache-dir',
                                  self.root / '.cache/pip', '--require-hashes', '-r', self.root / 'tools/requirements.txt'])
        self.assertEqual(run.call_args_list[1].kwargs['cwd'], self.root)

    def test_ready_environment_is_reused_without_network_or_pip(self):
        with patch.object(bootstrap, 'environment_status', return_value=self.ready), \
                patch.object(bootstrap.venv, 'EnvBuilder') as builder, patch.object(bootstrap, 'run') as run:
            bootstrap.ensure_environment(self.root, False)
            marker = self.root / '.local/bootstrap/environment.json'
            before = marker.stat().st_mtime_ns
            bootstrap.ensure_environment(self.root, False)
            self.assertEqual(marker.stat().st_mtime_ns, before)
        builder.assert_not_called()
        run.assert_not_called()

    def test_prepared_environment_drift_requires_explicit_setup(self):
        marker = self.root / '.local/bootstrap/environment.json'
        marker.parent.mkdir(parents=True)
        marker.write_text('{}')
        pending = dict(self.ready, ready=False, pip=True)
        with patch.object(bootstrap, 'environment_status', return_value=pending), patch.object(bootstrap, 'run') as run:
            with self.assertRaisesRegex(RuntimeError, 'setup explicitly'):
                bootstrap.ensure_environment(self.root, False)
            run.assert_not_called()
        with patch.object(bootstrap, 'environment_status', side_effect=[pending, self.ready]), \
                patch.object(bootstrap, 'run') as run:
            bootstrap.ensure_environment(self.root, True)
            self.assertEqual(run.call_count, 1)

    def test_foreign_environment_and_build_cache_are_preserved(self):
        self.python.parent.mkdir(parents=True)
        self.python.write_bytes(b'existing environment')
        values = {'prefix': str(self.root / 'previous-checkout/.venv'), 'version': [3, 14, 0],
                  'bits': 64, 'packages': {'kconfiglib': '14.1.0'}, 'pip': True}
        with patch.object(bootstrap.subprocess, 'check_output', return_value=json.dumps(values)):
            with self.assertRaisesRegex(RuntimeError, 'wrong location'):
                bootstrap.environment_status(self.root)
        self.assertEqual(self.python.read_bytes(), b'existing environment')
        output = projects.output_dir(self.root, self.project, 'windows_sim', 'Debug')
        output.mkdir(parents=True)
        cache = output / 'CMakeCache.txt'
        cache.write_text('CMAKE_HOME_DIRECTORY:INTERNAL=C:/previous-checkout\n')
        before = cache.read_bytes()
        with self.assertRaisesRegex(RuntimeError, 'another checkout/project'):
            bootstrap.ide_ready(self.root, self.project, 'windows_sim', 'Debug')
        self.assertEqual(cache.read_bytes(), before)

    def test_configuration_preview_honors_overrides_without_generating_files(self):
        shutil.copyfile(ROOT / 'Kconfig', self.root / 'Kconfig')
        shutil.copytree(ROOT / 'boards/windows_sim', self.root / 'boards/windows_sim')
        target = self.project.directory / 'targets/windows_sim.conf'
        target.parent.mkdir(exist_ok=True)
        target.write_text('CONFIG_SDK_CJSON=n\n')
        local = self.root / '.local/config/audio/windows_sim.conf'
        local.parent.mkdir(parents=True)
        local.write_text('CONFIG_SDK_CJSON=y\nCONFIG_SDK_NANOPB=n\n')
        before = {str(p): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        with patch.dict(os.environ):
            values = generate(self.root, self.project.user, self.root / 'boards/windows_sim', self.root / 'preview',
                              project_id='audio', extra_fragments=[target], write_outputs=False)
            names = bootstrap.selected_dependencies(self.root, self.project, 'windows_sim')
        self.assertEqual(values['SDK_CJSON'], 'y')
        self.assertIn('cjson', names)
        self.assertNotIn('nanopb', names)
        self.assertEqual(before, {str(p): p.read_bytes() for p in self.root.rglob('*') if p.is_file()})

    def test_only_missing_selected_submodules_are_prepared(self):
        deps = [{'name': 'juce', 'path': 'third_party/src/juce'},
                {'name': 'rtneural', 'path': 'third_party/src/rtneural'},
                {'name': 'cherryusb', 'path': 'third_party/src/cherryusb'}]
        rt = self.root / deps[1]['path']
        rt.mkdir(parents=True)
        (rt / '.git').write_text('gitdir: unused')
        nested = '-abc third_party/src/rtneural/modules/Eigen\n'
        with patch.object(bootstrap.dependencies, 'components', return_value=deps), \
                patch.object(bootstrap.dependencies, 'inspect') as inspect, \
                patch.object(bootstrap.subprocess, 'check_output', return_value=nested):
            self.assertEqual(bootstrap.missing_dependencies(self.root, {'juce', 'rtneural'}), {'juce', 'rtneural'})
            inspect.assert_called_once_with(self.root, deps[1])
        with patch.object(bootstrap.dependencies, 'components', return_value=deps), \
                patch.object(bootstrap.dependencies, 'inspect', side_effect=RuntimeError('tracked modifications')), \
                patch.object(bootstrap.dependencies, 'sync') as sync:
            with self.assertRaisesRegex(RuntimeError, 'tracked modifications'):
                bootstrap.missing_dependencies(self.root, {'rtneural'})
            sync.assert_not_called()

    def main_mocks(self, stack, ready=False, missing=()):
        stack.enter_context(patch.object(bootstrap, 'ROOT', self.root))
        stack.enter_context(patch.object(sys, 'prefix', str(self.root / '.venv')))
        stack.enter_context(patch.object(bootstrap, 'system_tools', return_value={'git': 'git'}))
        status = stack.enter_context(patch.object(bootstrap, 'environment_status', return_value=self.ready))
        environment = stack.enter_context(patch.object(bootstrap, 'ensure_environment', return_value=self.python))
        stack.enter_context(patch.object(bootstrap, 'selected_dependencies', return_value={'juce'}))
        stack.enter_context(patch.object(bootstrap, 'missing_dependencies', return_value=set(missing)))
        stack.enter_context(patch.object(bootstrap, 'ide_ready', return_value=ready))
        sync = stack.enter_context(patch.object(bootstrap.dependencies, 'sync'))
        dispatch = stack.enter_context(patch.object(bootstrap.sdk, 'main'))
        stack.enter_context(redirect_stdout(io.StringIO()))
        return status, environment, sync, dispatch

    def test_setup_checks_cannot_install_sync_configure_or_launch(self):
        with ExitStack() as stack:
            status, environment, sync, dispatch = self.main_mocks(stack, missing={'juce'})
            self.assertEqual(bootstrap.main(['setup', '--check']), 1)
            status.return_value = {'ready': False, 'exists': False}
            self.assertEqual(bootstrap.main(['setup', '--check']), 1)
            environment.assert_not_called()
            sync.assert_not_called()
            dispatch.assert_not_called()
        self.assertFalse((self.root / '.venv').exists())

    def test_setup_uses_selected_workspace_without_building_or_running(self):
        with ExitStack() as stack:
            _, environment, sync, dispatch = self.main_mocks(stack, missing={'juce'})
            self.assertEqual(bootstrap.main(['setup', '--project', 'audio', '--config', 'Release']), 0)
            environment.assert_called_once_with(self.root, True)
            sync.assert_called_once_with(self.root, [], ['juce'])
            dispatch.assert_called_once_with(['workspace', '--project', 'audio', '--board', 'windows_sim', '--config', 'Release'])

    def test_run_preserves_arguments_and_initializes_ide_only_when_needed(self):
        for ready in (False, True):
            with self.subTest(ready=ready), ExitStack() as stack:
                _, _, sync, dispatch = self.main_mocks(stack, ready=ready)
                args = ['run', '--project=audio', '--config', 'Release', '--no-audio']
                self.assertEqual(bootstrap.main(args), 0)
                dispatch.assert_called_once_with(args, initialize_ide=not ready)
                sync.assert_not_called()

    def test_first_run_configures_once_before_ide_build_and_launch_dispatch(self):
        output = projects.output_dir(self.root, self.project, 'windows_sim', 'Debug')
        with patch.object(sdk, 'ROOT', self.root), \
                patch.object(sdk, 'configure', return_value=(self.project, output)) as configure, \
                patch.object(sdk.ide, 'generate') as index, patch.object(sdk.ide, 'refresh_projects') as refresh, \
                patch.object(sdk, 'tool', return_value='cmake'), patch.object(sdk, 'build_env', return_value={}), \
                patch.object(sdk, 'run') as run:
            sdk.main(['run', '--project', 'audio', '--no-audio'], initialize_ide=True)
            configure.assert_called_once()
            index.assert_called_once_with(self.root, self.project, 'windows_sim', 'Debug')
            refresh.assert_called_once_with(self.root, 'windows_sim', 'Debug', repair=True)
            self.assertEqual(run.call_args_list[0].args[0][:2], ['cmake', '--build'])
            self.assertEqual(run.call_args_list[1].args[0][0], output / 'bin/lvgl_juce.exe')
            self.assertIn('--no-audio', run.call_args_list[1].args[0])

    def test_missing_system_tools_give_install_instructions(self):
        with patch.object(bootstrap.shutil, 'which', return_value=None):
            with self.assertRaisesRegex(RuntimeError, 'Install Git for Windows'):
                bootstrap.system_tools()
        with patch.object(bootstrap.shutil, 'which', return_value='git'), \
                patch.object(bootstrap.toolchain, 'vs_root', side_effect=FileNotFoundError('vswhere')):
            with self.assertRaisesRegex(RuntimeError, r'Desktop development with C\+\+'):
                bootstrap.system_tools()

    def test_root_and_dedicated_setup_do_not_require_an_existing_environment(self):
        root_tasks = json.loads((ROOT / '.vscode/tasks.json').read_text())['tasks']
        workspace = json.loads(ide.generate(self.root, self.project).read_text())
        for tasks in (root_tasks, workspace['tasks']['tasks']):
            task = next(t for t in tasks if t['label'] == 'SDK: setup')
            self.assertTrue(task['command'].endswith('sdk.cmd'))
            self.assertEqual(task['args'][0], 'setup')
            self.assertNotIn('.venv', json.dumps(task))
            self.assertNotIn('cpptools.activeConfigName', json.dumps(task))
            self.assertEqual(task['options']['shell']['args'], ['/d', '/c'])
        setup = next(t for t in workspace['tasks']['tasks'] if t['label'] == 'SDK: setup')
        self.assertEqual(setup['args'][2], '${workspaceFolder}/../../projects/audio')

    @unittest.skipUnless(os.name == 'nt', 'Windows command launcher')
    def test_command_launcher_forwards_space_paths_and_exit_code_without_venv(self):
        shutil.copyfile(ROOT / 'sdk.cmd', self.root / 'sdk.cmd')
        (self.root / 'tools/bootstrap.py').write_text('import json, sys\nprint(json.dumps(sys.argv[1:]))\nsys.exit(7)\n')
        command = f'"{os.environ["COMSPEC"]}" /d /s /c ""{self.root / "sdk.cmd"}" run --project "project with spaces""'
        for discovery in ('override', 'path'):
            with self.subTest(discovery=discovery):
                env = {key.upper(): value for key, value in os.environ.items()}
                env.pop('SDK_PYTHON', None)
                if discovery == 'override':
                    env['SDK_PYTHON'] = sys.executable
                else:
                    env['PATH'] = str(Path(sys.executable).parent) + os.pathsep + str(Path(os.environ['SystemRoot']) / 'System32')
                result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=15)
                self.assertEqual(result.returncode, 7, result.stderr)
                self.assertEqual(json.loads(result.stdout), ['run', '--project', 'project with spaces'])


if __name__ == '__main__':
    unittest.main()
