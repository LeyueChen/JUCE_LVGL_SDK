from pathlib import Path
import argparse
from config.generate import generate
from projects import load, lvgl_config
root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--project-dir', required=True)
parser.add_argument('--board', default='windows_sim')
parser.add_argument('--output', required=True)
args = parser.parse_args()
project = load(Path(args.project_dir))
lvgl_config(project)
target = project.directory / 'targets' / (args.board + '.conf')
generate(root, project.user, root / 'boards' / args.board, Path(args.output),
         project_id=project.id, extra_fragments=[target] if target.exists() else [])
