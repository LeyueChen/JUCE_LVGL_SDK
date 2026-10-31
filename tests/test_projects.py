"""Project boundaries, overlay precedence and IDE namespace contracts."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import ide
import projects
import sdk
from config.generate import generate


class ProjectContracts(unittest.TestCase):
    def setUp(self):
        parent = ROOT / 'out/project-contracts'
        parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'templates').mkdir()
        import shutil
        shutil.copytree(ROOT / 'templates/project', self.root / 'templates/project')

    def test_scaffolds_are_independent_and_duplicate_ids_rejected(self):
        a = projects.scaffold(self.root, 'panel-a')
        b = projects.scaffold(self.root, 'panel-b')
        self.assertEqual([p.id for p in projects.discover(self.root)], ['panel-a', 'panel-b'])
        self.assertNotEqual(a.user, b.user)
        self.assertEqual(projects.resolve(self.root, 'panel-b'), b)
        with self.assertRaisesRegex(RuntimeError, 'already exists'):
            projects.scaffold(self.root, 'panel-a')

    def test_user_cannot_escape_project(self):
        a = projects.scaffold(self.root, 'panel-a')
        metadata = a.directory / 'project.json'
        data = json.loads(metadata.read_text())
        data['user_dir'] = '../../elsewhere'
        metadata.write_text(json.dumps(data))
        with self.assertRaisesRegex(RuntimeError, 'Invalid portable'):
            projects.load(a.directory)

    def test_projects_default_and_examples_are_both_discovered(self):
        a = projects.scaffold(self.root, 'new-app')
        b = projects.scaffold(self.root, 'reference', self.root / 'examples/reference')
        self.assertEqual(a.directory, self.root / 'projects/new-app')
        self.assertEqual(projects.resolve(self.root, 'reference'), b)
        self.assertEqual({p.id for p in projects.discover(self.root)}, {'new-app', 'reference'})

    def test_each_workspace_has_own_compile_database_and_symbol_cache(self):
        settings = []
        for name in ('panel-a', 'panel-b'):
            project = projects.scaffold(self.root, name)
            output = projects.output_dir(self.root, project, 'windows_sim', 'Debug')
            output.mkdir(parents=True)
            source = project.user / 'app/app.c'
            database = [{'directory': str(output), 'file': str(source),
                         'arguments': ['cl.exe', '/I' + str(project.user), '/DPROJECT_' + name, str(source)]}]
            (output / 'compile_commands.json').write_text(json.dumps(database))
            workspace = ide.generate(self.root, project)
            workspace_data = json.loads(workspace.read_text())
            opened = (workspace.parent / workspace_data['folders'][0]['path']).resolve()
            self.assertEqual(opened, project.directory)
            self.assertNotEqual(opened, self.root)
            self.assertTrue((opened / '.vscode/c_cpp_properties.json').exists())
            setting = workspace_data['settings']
            settings.append(setting)
            other = 'panel-b' if name == 'panel-a' else 'panel-a'
            self.assertNotIn(other, json.dumps(setting))
            self.assertNotIn('/**', json.dumps(setting))
            self.assertTrue(setting['C_Cpp.default.browse.limitSymbolsToIncludedHeaders'])
        for key in ('C_Cpp.default.compileCommands', 'C_Cpp.default.browse.databaseFilename', 'C_Cpp.intelliSenseCachePath'):
            self.assertNotEqual(settings[0][key], settings[1][key])

    def test_overlay_order_and_local_configuration_are_project_specific(self):
        # Use real Kconfig/board with local overlays confined to the temporary root.
        import shutil
        shutil.copy2(ROOT / 'Kconfig', self.root / 'Kconfig')
        shutil.copytree(ROOT / 'boards', self.root / 'boards')
        a = projects.scaffold(self.root, 'panel-a')
        b = projects.scaffold(self.root, 'panel-b')
        (a.user / 'app.conf').write_text('CONFIG_SDK_SCREEN_WIDTH=640\n')
        target = a.directory / 'targets/windows_sim.conf'
        target.write_text('CONFIG_SDK_SCREEN_WIDTH=720\n')
        local = self.root / '.local/config/panel-a/windows_sim.conf'
        local.parent.mkdir(parents=True)
        local.write_text('CONFIG_SDK_SCREEN_WIDTH=960\n')
        a_out = self.root / 'out/a'
        b_out = self.root / 'out/b'
        generate(self.root, a.user, self.root / 'boards/windows_sim', a_out,
                 project_id=a.id, extra_fragments=[target])
        generate(self.root, b.user, self.root / 'boards/windows_sim', b_out, project_id=b.id)
        self.assertEqual(json.loads((a_out / 'config.json').read_text())['SDK_SCREEN_WIDTH'], '960')
        self.assertEqual(json.loads((b_out / 'config.json').read_text())['SDK_SCREEN_WIDTH'], '800')

    def test_custom_index_properties_survive_regeneration(self):
        project = projects.scaffold(self.root, 'custom')
        ide.generate(self.root, project)
        path = project.directory / '.vscode/c_cpp_properties.json'
        value = json.loads(path.read_text())
        value['configurations'][0]['compilerPath'] = 'my-cl.exe'
        path.write_text(json.dumps(value))
        before = path.read_bytes()
        ide.generate(self.root, project)
        self.assertEqual(path.read_bytes(), before)

    def test_root_profiles_discover_underscore_projects_and_isolate_indexes(self):
        audio = projects.scaffold(self.root, 'audio')
        gui = projects.scaffold(self.root, 'rakure_gui')
        for project in (audio, gui):
            output = projects.output_dir(self.root, project, 'windows_sim', 'Debug')
            output.mkdir(parents=True)
            database = [{'directory': str(output), 'file': str(project.user / 'app/app.c'),
                         'arguments': ['cl.exe', '/I' + str(project.user / 'app'), str(project.user / 'app/app.c')]}]
            ide.write_json(output / 'compile_commands.json', database)
        settings = {'sdk.project': 'basic', 'files.exclude': {'**/.git': False},
                    'editor.tabSize': 8, 'C_Cpp.default.compileCommands': ['old-basic.json']}
        ide.write_json(self.root / '.vscode/settings.json', settings)
        legacy = ide.project_properties('Windows SDK (selected project)')
        legacy['env'] = {'USER_INCLUDE': '/keep/this'}
        ide.write_json(self.root / '.vscode/c_cpp_properties.json', legacy)
        self.assertEqual(ide.refresh_projects(self.root), ['audio', 'rakure_gui'])
        result = json.loads((self.root / '.vscode/c_cpp_properties.json').read_text())
        self.assertEqual(result['env'], legacy['env'])
        profiles = {item['name']: item for item in result['configurations']}
        for project, other in ((audio, gui), (gui, audio)):
            profile = profiles[project.id]
            text = json.dumps(profile)
            self.assertIn('/out/' + project.id + '/', profile['compileCommands'][0])
            self.assertIn('/intellisense/' + project.id + '/', profile['browse']['databaseFilename'])
            self.assertNotIn(other.id, text)
            self.assertNotIn('/**', text)
            self.assertTrue(profile['browse']['limitSymbolsToIncludedHeaders'])
            local = json.loads((project.directory / '.vscode/c_cpp_properties.json').read_text())
            self.assertEqual(local['configurations'][0]['name'], project.id)
            dedicated = json.loads(ide.generate(self.root, project).read_text())
            root_database = profile['browse']['databaseFilename'].replace('${workspaceFolder}', self.root.as_posix()).replace(
                '${config:sdk.board}', 'windows_sim').replace('${config:sdk.configuration}', 'Debug')
            self.assertNotEqual(root_database, dedicated['settings']['C_Cpp.default.browse.databaseFilename'])
            self.assertTrue(root_database.endswith('/root/browse.vc.db'))
            self.assertNotIn('C_Cpp.intelliSenseCacheSize', dedicated['settings'])
            self.assertTrue(any(task['label'] == 'SDK: refresh projects' for task in dedicated['tasks']['tasks']))
        updated = json.loads((self.root / '.vscode/settings.json').read_text())
        self.assertEqual(updated['files.exclude'], settings['files.exclude'])
        self.assertEqual(updated['editor.tabSize'], 8)
        self.assertNotIn('sdk.project', updated)
        self.assertNotIn('C_Cpp.default.compileCommands', updated)
        self.assertEqual(updated['C_Cpp.intelliSenseCacheSize'], 0)

    def test_root_refresh_preserves_customizations_and_only_adds_new_projects(self):
        project = projects.scaffold(self.root, 'audio')
        legacy = ide.project_properties('Windows SDK (selected project)')
        ide.write_json(project.directory / '.vscode/c_cpp_properties.json', legacy)
        ide.refresh_projects(self.root)
        path = self.root / '.vscode/c_cpp_properties.json'
        properties = json.loads(path.read_text())
        properties['configurations'][0]['compilerPath'] = 'my-cl.exe'
        properties['configurations'][0]['compilerArgs'] = ['/DKEEP_USER_OPTIONS']
        properties['configurations'][0]['customConfigurationVariables']['USER_KEEP'] = 'custom-value'
        custom = {'name': 'custom-toolchain', 'compilerPath': 'custom-cc.exe'}
        properties['configurations'].append(custom)
        ide.write_json(path, properties)
        projects.scaffold(self.root, 'rakure_gui')
        ide.refresh_projects(self.root)
        updated = json.loads(path.read_text())
        self.assertEqual([p['name'] for p in updated['configurations']], ['audio', 'custom-toolchain', 'rakure_gui'])
        self.assertEqual(updated['configurations'][0]['compilerPath'], 'my-cl.exe')
        self.assertEqual(updated['configurations'][0]['compilerArgs'], ['/DKEEP_USER_OPTIONS'])
        self.assertEqual(updated['configurations'][0]['customConfigurationVariables']['USER_KEEP'], 'custom-value')
        self.assertEqual(updated['configurations'][1], custom)
        original = path.read_bytes()
        ide.refresh_projects(self.root)
        self.assertEqual(path.read_bytes(), original)
        for invalid in ('../escape', 'UPPER', '-leading', 'trailing_', 'two__parts'):
            with self.assertRaisesRegex(RuntimeError, 'Project names'):
                projects.scaffold(self.root, invalid)

    def test_active_configuration_routes_run_build_and_debug_to_same_project(self):
        for name in ('audio', 'rakure_gui'):
            projects.scaffold(self.root, name)
        tasks = json.loads((ROOT / '.vscode/tasks.json').read_text())['tasks']
        launch = json.loads((ROOT / '.vscode/launch.json').read_text())['configurations'][0]
        active = '${command:cpptools.activeConfigName}'
        for task in tasks:
            if '--project' in task.get('args', []):
                self.assertEqual(task['args'][task['args'].index('--project') + 1], active)

        def resolve(text, name):
            for variable, value in ((active, name), ('${workspaceFolder}', str(self.root)),
                                    ('${config:sdk.board}', 'windows_sim'), ('${config:sdk.configuration}', 'Debug')):
                text = text.replace(variable, value)
            return text

        def configured(args, interactive=False):
            project = projects.resolve(self.root, args.project)
            return project, projects.output_dir(self.root, project, args.board, args.config)

        task = next(task for task in tasks if task['label'] == 'SDK: run')
        for name in ('audio', 'rakure_gui'):
            arguments = [resolve(arg, name) for arg in task['args']]
            with patch.object(sys, 'argv', arguments), patch.object(sdk, 'ROOT', self.root), \
                 patch.object(sdk, 'configure', side_effect=configured), patch.object(sdk, 'build_env', return_value={}), \
                 patch.object(sdk, 'tool', return_value='cmake.exe'), patch.object(sdk, 'run') as run:
                sdk.main()
                self.assertEqual(run.call_count, 2)
                build, start = [call.args[0] for call in run.call_args_list]
                expected = self.root / 'out' / name / 'windows_sim/Debug'
                self.assertEqual(build[1:3], ['--build', expected])
                self.assertEqual(start, [expected / 'bin/lvgl_juce.exe', '--data-dir', self.root / '.local/data' / name / 'windows_sim'])
                self.assertEqual(Path(resolve(launch['program'], name)), start[0])
                self.assertEqual(Path(resolve(launch['cwd'], name)), expected)
                self.assertEqual(Path(resolve(launch['args'][1], name)), start[2])
                self.assertEqual(launch['preLaunchTask'], 'SDK: build')

    def test_root_refresh_rejects_compile_database_from_another_product(self):
        audio = projects.scaffold(self.root, 'audio')
        gui = projects.scaffold(self.root, 'rakure_gui')
        output = projects.output_dir(self.root, audio, 'windows_sim', 'Debug')
        ide.write_json(output / 'compile_commands.json', [{'directory': str(output),
                       'file': str(gui.user / 'app/app.c'), 'arguments': ['cl.exe']}])
        with self.assertRaisesRegex(RuntimeError, 'another project'):
            ide.refresh_projects(self.root)

    def test_refresh_command_does_not_configure_build_or_launch(self):
        projects.scaffold(self.root, 'audio')
        with patch.object(sys, 'argv', ['sdk.py', 'refresh-projects']), patch.object(sdk, 'ROOT', self.root), \
             patch.object(sdk, 'configure') as configure, patch.object(sdk, 'run') as run:
            sdk.main()
            configure.assert_not_called()
            run.assert_not_called()
        result = json.loads((self.root / '.vscode/c_cpp_properties.json').read_text())
        self.assertEqual(result['configurations'][0]['name'], 'audio')


if __name__ == '__main__':
    unittest.main()
