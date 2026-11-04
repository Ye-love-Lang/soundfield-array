#!/usr/bin/env python3
"""Create a working copy with original tool-required file-name casing.
Usage: python engineering/build/prepare_workspace.py <new_working_directory>
The submitted folder remains unchanged. Requires Python 3.10+ only.
"""
from pathlib import Path
import argparse,json,shutil,hashlib

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('target',type=Path)
 args=parser.parse_args()
 source=Path(__file__).resolve().parents[2]
 target=args.target.resolve()
 if target==source or target.is_relative_to(source) or source.is_relative_to(target):
  parser.error('Choose a new working directory outside the submission directory.')
 if target.exists():parser.error('Target already exists; choose a new directory.')
 index=json.loads((source/'engineering/case_map.json').read_text(encoding='utf8'))
 mapping={'engineering/'+e['packaged']:'engineering/'+e['working'] for e in index['files']}
 mapping.update({e['packaged']:e['working'] for e in index.get('other_files',[])})
 files=[p for p in source.rglob('*') if p.is_file() and not p.name.startswith('~$')]
 if any(p.is_symlink() for p in files):parser.error('Symbolic links are not supported.')
 target.mkdir(parents=True)
 for p in files:
  relative=p.relative_to(source).as_posix()
  restored=Path(mapping.get(relative,relative))
  if restored.is_absolute() or '..' in restored.parts:raise ValueError('Invalid path in case map')
  q=target/restored;q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q)
  if hashlib.sha256(p.read_bytes()).digest()!=hashlib.sha256(q.read_bytes()).digest():raise OSError('Copy verification failed: '+relative)
 print('WORKSPACE_READY',target,'files='+str(len(files)))
 print('Open the engineering directory and follow the root readme.md.')

if __name__=='__main__':main()
