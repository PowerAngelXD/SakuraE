# Copy from https://github.com/XtherDevTeam/hoshi-lang

import pathlib

code_lines = {}

def traverse(path: pathlib.Path = pathlib.Path(".")) -> None:
    global code_lines
    for child in path.iterdir():
        if child.is_dir() and not child.name.startswith(".") and child.name not in ["build", "cmake-build-debug", "cmake-build-release", "mimalloc"]:
            traverse(child)
        elif child.is_file():
            if child.suffix in ['.cpp', '.h', '.hpp', '.txt']:
                code_lines[child.name] = len(child.read_text().split('\n'))


if __name__ == '__main__':
    traverse()
    for file_name, line_count in code_lines.items():
        print(f"{file_name}: {line_count} lines")
    print(f"Total lines of code: {sum(code_lines.values())}")