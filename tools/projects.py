"""Project discovery and paths shared by CLI, CMake and IDE generation."""
from dataclasses import dataclass
import json
from pathlib import Path
import re
import shutil


def valid_id(name):
    return isinstance(name, str) and re.fullmatch(r'[a-z0-9]+(?:[-_][a-z0-9]+)*', name) is not None


@dataclass(frozen=True)
class Project:
    id: str
    directory: Path
    user: Path
    display_name: str


def load(directory: Path) -> Project:
    directory = directory.resolve()
    metadata = json.loads((directory / 'project.json').read_text(encoding='utf-8'))
    name = metadata.get('id', '')
    if metadata.get('schema') != 1 or not valid_id(name):
        raise RuntimeError(f'Invalid project metadata: {directory}')
    user = (directory / metadata.get('user_dir', 'user')).resolve()
    if not user.is_relative_to(directory) or not (user / 'app.conf').is_file():
        raise RuntimeError(f'Invalid portable user directory: {directory}')
    return Project(name, directory, user, metadata.get('display_name', name))


def discover(root: Path) -> list[Project]:
    result = []
    for category in ('examples', 'projects'):
        for metadata in sorted((root / category).glob('*/project.json')):
            result.append(load(metadata.parent))
    ids = [project.id for project in result]
    if len(set(ids)) != len(ids):
        raise RuntimeError('Project IDs must be unique across examples/ and projects/')
    return result


def resolve(root: Path, value: str) -> Project:
    registered = discover(root)
    for project in registered:
        if project.id == value:
            return project
    candidate = (root / value).resolve()
    if candidate.is_relative_to(root) and (candidate / 'project.json').is_file():
        project = load(candidate)
        if any(p.id == project.id and p.directory != project.directory for p in registered):
            raise RuntimeError('Project ID collides with another project in this repository')
        return project
    raise RuntimeError(f'Unknown project {value!r}; use the projects command to list this branch')


def output_dir(root: Path, project: Project, board: str, configuration: str) -> Path:
    return root / 'out' / project.id / board / configuration


def data_dir(root: Path, project: Project, board: str) -> Path:
    return root / '.local/data' / project.id / board


def scaffold(root: Path, name: str, destination: Path | None = None) -> Project:
    if not valid_id(name):
        raise RuntimeError('Project names use lowercase letters, digits, hyphens and underscores')
    if name in [project.id for project in discover(root)]:
        raise RuntimeError('Project ID already exists')
    destination = (destination or root / 'projects' / name).resolve()
    if not destination.is_relative_to(root.resolve()) or destination.exists():
        raise RuntimeError('Project destination must be a new directory inside the repository')
    shutil.copytree(root / 'templates/project', destination)
    metadata = json.loads((destination / 'project.json').read_text(encoding='utf-8'))
    metadata.update(id=name, display_name=name)
    (destination / 'project.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    return load(destination)
