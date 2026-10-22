#!/usr/bin/env python3
"""Restore pinned public source dependencies; never fetch game files."""
import json,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];deps=json.loads((root/'dependencies.json').read_text())
for name in ('kinoko','noclip','gclib','citro3d','libctru'):
 dest=root/'vendor'/name;entry=deps[name]
 if dest.exists():
  actual=subprocess.check_output(['git','-C',str(dest),'rev-parse','HEAD'],text=True).strip()
  if actual!=entry['commit']:raise SystemExit(f'{name}: unexpected revision; refusing to overwrite existing checkout')
  if name=='citro3d':
   subprocess.run(['git','-C',str(dest),'apply','--reverse','--check',str(root/entry['patch'])],check=True)
  if name=='kinoko':
   # An update may change the port patch: restore the pinned sources (the
   # patch only edits tracked files) and apply the current one.
   patch=str(root/'patches/kinoko-arm11.patch')
   if subprocess.run(['git','-C',str(dest),'apply','--reverse','--check',patch],capture_output=True).returncode:
    print('kinoko: applying the updated port patch')
    subprocess.run(['git','-C',str(dest),'checkout','--','.'],check=True)
    # The patch also adds files: remove the old copies before applying the new one.
    for line in Path(patch).read_text(encoding='utf-8').splitlines():
     if line.startswith('+++ b/') and (dest/line[6:]).is_file() and subprocess.run(['git','-C',str(dest),'ls-files','--error-unmatch',line[6:]],capture_output=True).returncode:(dest/line[6:]).unlink()
    subprocess.run(['git','-C',str(dest),'apply',patch],check=True)
  continue
 subprocess.run(['git','clone','--no-checkout',entry['url'],str(dest)],check=True)
 subprocess.run(['git','-C',str(dest),'checkout','--detach',entry['commit']],check=True)
 if name=='kinoko':subprocess.run(['git','-C',str(dest),'apply',str(root/'patches/kinoko-arm11.patch')],check=True)
 if name=='citro3d':subprocess.run(['git','-C',str(dest),'apply',str(root/entry['patch'])],check=True)
print('Source dependencies ready. Run npm ci and install requirements.txt into a project virtualenv.')
