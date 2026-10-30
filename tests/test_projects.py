"""Project boundaries, overlay precedence and IDE namespace contracts."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import ide
import projects
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


if __name__ == '__main__':
    unittest.main()
