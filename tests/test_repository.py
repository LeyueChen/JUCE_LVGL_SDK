"""Protect ignored local docs across both old and current-layout branches."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import repository


class RepositoryContracts(unittest.TestCase):
    def test_old_branch_cannot_replace_local_documents(self):
        parent = ROOT / 'out/repository-contracts'
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent, ignore_cleanup_errors=True) as temp:
            root = Path(temp)
            def git(*args):
                return repository.git(root, *args)
            git('init', '-b', 'legacy')
            git('config', 'user.name', 'SDK isolated test')
            git('config', 'user.email', 'sdk-test@localhost.invalid')
            (root / 'docs').mkdir()
            (root / 'docs/note.md').write_text('old tracked text')
            (root / 'examples/basic').mkdir(parents=True)
            (root / 'examples/basic/project.json').write_text('{}')
            git('add', '.')
            git('commit', '-qm', 'legacy fixture')
            git('switch', '-c', 'sdk-test')
            git('rm', '--cached', 'docs/note.md')
            (root / '.gitignore').write_text('/docs\n')
            git('add', '.gitignore')
            git('commit', '-qm', 'local-only docs fixture')
            local = root / 'docs/note.md'
            local.write_text('local private text that must survive')
            with self.assertRaisesRegex(RuntimeError, 'Target tracks docs'):
                repository.switch(root, 'legacy')
            self.assertEqual(git('branch', '--show-current'), 'sdk-test')
            self.assertEqual(local.read_text(), 'local private text that must survive')
            git('branch', 'safe')
            repository.switch(root, 'safe')
            self.assertEqual(git('branch', '--show-current'), 'safe')
            self.assertEqual(local.read_text(), 'local private text that must survive')


if __name__ == '__main__':
    unittest.main()
