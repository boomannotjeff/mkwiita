#!/usr/bin/env python3
"""Refresh all 288 race choices from existing verified archives, no disc extraction."""
import copy,json,shutil
from pathlib import Path
from concurrent.futures import ProcessPoolExecutor
from cook_garage import ROOT,cook_choice
from cook_roster import sha256

OUTPUT=ROOT/'build/roster';SHARED=ROOT/'sd-card/3ds/mkw-3ds'

def one(original):
 row=copy.deepcopy(original);folder=row['prefix'].rstrip('_')
 sources=ROOT/'assets/vehicles'
 if not (sources/folder).is_dir():sources=ROOT/'build/roster-work/vehicles'
 for name,field in [('driver_model.brres','driver_sha256'),('kart_model.brres','kart_sha256')]:
  if sha256(sources/folder/name)!=row[field]:raise ValueError('Changed verified archive: '+folder)
 for name in row['animationBanks']:
  target=OUTPUT/'models'/(name+'.dskm')
  if not target.is_file():shutil.copyfile(SHARED/'models'/target.name,target)
 result=cook_choice(row,OUTPUT,source_root=sources,export_root=ROOT/'build/racer-lighting-exports',models_only=True)
 for name in result['animationBanks']:
  import struct
  bank=(OUTPUT/'models'/(name+'.dskm')).read_bytes()
  model=(OUTPUT/'models'/(name+'.gmdl')).read_bytes()
  if struct.unpack_from('<I',bank,8)!=struct.unpack_from('<I',model,8):raise ValueError('Bank vertex mismatch: '+name)
 (ROOT/'build/roster-work/receipts'/(row['prefix']+'.json')).write_text(json.dumps(result,indent=2)+'\n')
 return result

def main():
 import argparse
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--jobs',type=int,default=3);jobs=p.parse_args().jobs
 manifest=json.loads((OUTPUT/'models/garage.json').read_text())
 # Processes, not threads: cooking is Python-bound under the GIL (360+ choices with Retro Rewind skins).
 with ProcessPoolExecutor(max_workers=max(1,jobs)) as executor:manifest['choices']=list(executor.map(one,manifest['choices']))
 (OUTPUT/'models/garage.json').write_text(json.dumps(manifest,indent=2)+'\n')
 print('Refreshed and bank-verified all',len(manifest['choices']),'race choices',flush=True)
if __name__=='__main__':main()
