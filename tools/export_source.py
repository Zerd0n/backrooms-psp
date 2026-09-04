#!/usr/bin/env python3
"""Export all build/runtime source, including unchanged texture arrays, to Markdown."""
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
LANG={'.c':'c','.h':'c','.py':'python','.sh':'sh','.json':'json','.ini':'ini'}

def main():
    files=[ROOT/'Makefile',ROOT/'.env.example',ROOT/'.gitignore']
    for folder in ('src','tools','tests','config'):
        files.extend(p for p in (ROOT/folder).rglob('*')
                     if p.is_file() and p.suffix in LANG and '__pycache__' not in p.parts)
    output=ROOT/'docs/SOURCE.md';output.parent.mkdir(exist_ok=True)
    with output.open('w',encoding='utf-8') as f:
        f.write('# Backrooms PSP Rebuilt — полный исходный код\n\n')
        f.write('Новая реализация и инструменты. Заголовки `src/generated` содержат сохранённые ресурсы; их массивы включены без изменений. Инструкция: [README](../README.md).\n\n')
        for path in sorted(set(files)):
            relative=path.relative_to(ROOT)
            language='makefile' if path.name=='Makefile' else LANG.get(path.suffix,'text')
            f.write(f'## {relative}\n\n```{language}\n')
            f.write(path.read_text(encoding='utf-8').rstrip()+'\n```\n\n')
    print(f'Exported {len(set(files))} files: {output}')

if __name__=='__main__':main()
