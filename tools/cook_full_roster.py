#!/usr/bin/env python3
"""Cook missing retail racers into a private overlay without changing shared assets.

The existing twelve-racer assets remain the baseline. Missing race archives are
read from the user's verified local disc and cooked with the same GMDL/DSKM
pipeline. The resulting manifest contains all 24 racers and twelve vehicles each;
only newly cooked payloads live in the overlay.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import shutil
import struct

from cook_roster import ROOT, ROSTER, data_partition, sha256, write_native_roster
from cook_garage import VEHICLES, VEHICLES_PER_DRIVER, EXTRA_NAMES, inspect_driver, inspect_kart, cook_choice, write_native_garage
from extract_disc import Disc
from unpack_szs import unpack


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'build/roster')
    parser.add_argument('--work', type=Path, default=ROOT/'build/roster-work')
    parser.add_argument('--jobs', type=int, default=3)
    parser.add_argument('--refresh-models', action='store_true', help='Re-export added geometry/materials, preserving existing textures and animation banks')
    args = parser.parse_args()
    args.output=args.output.resolve();args.work=args.work.resolve()
    for path in (args.output,args.work):
        if not path.is_relative_to(ROOT/'build'):
            raise ValueError('Roster output/work must stay inside the build directory')
    shared=ROOT/'sd-card/3ds/mkw-3ds'
    original=json.loads((shared/'models/garage.json').read_text())
    inventory=json.loads((ROOT/'data/extracted/disc-files.json').read_text())
    if inventory['identity']!=original['identity']:
        raise ValueError('Garage and local disc inventory have different source identities')
    files={r['name']:r for r in inventory['files']}
    display=(ROOT/'assets/common/kartDriverDispParam.bin').read_bytes()
    if struct.unpack_from('>II',display)!=(36,48):raise ValueError('Unexpected retail display table')
    write_native_roster(display)
    sources=args.work/'vehicles';exports=args.work/'parts';receipts=args.work/'receipts'
    for path in (sources,exports,receipts,args.output/'models'):path.mkdir(parents=True,exist_ok=True)
    originals={row['prefix']:row for row in original['choices']}
    cooked=dict(originals)
    disc=Disc(ROOT/inventory['identity']['source']);partition=data_partition(disc)
    pending=[]
    def cook_one(choice):
        record=cook_choice(choice,args.output,source_root=sources,export_root=exports,models_only=args.refresh_models)
        (receipts/(record['prefix']+'.json')).write_text(json.dumps(record,indent=2)+'\n')
        for name in record['models']:
            folder=exports/name
            if folder.exists():shutil.rmtree(folder)
        return record
    try:
        with ThreadPoolExecutor(max_workers=max(1,args.jobs)) as workers:
            for driver_index,(slug,name,character,code,size) in enumerate(ROSTER):
                for slot,(vehicle,vehicle_name,stem) in enumerate(VEHICLES[size]):
                    prefix=f'{slug}_{stem}_'
                    if prefix in originals:continue
                    previous=receipts/(prefix+'.json')
                    if previous.exists():
                        row=json.loads(previous.read_text())
                        if all((args.output/'models'/(n+'.gmdl')).is_file() for n in row['models']) and all((args.output/'models'/(n+'.dskm')).is_file() for n in row['animationBanks']):
                            if args.refresh_models:pending.append(workers.submit(cook_one,row))
                            else:cooked[prefix]=row
                            continue
                    archive_name=f'Race/Kart/{stem}-{code}.szs';entry=files[archive_name]
                    compressed=args.work/'archives'/Path(archive_name).name
                    compressed.parent.mkdir(parents=True,exist_ok=True)
                    compressed.write_bytes(partition.read(entry['offset'],entry['size']))
                    archive=sources/prefix.rstrip('_');unpack(compressed,archive)
                    driver=inspect_driver(archive);kart=inspect_kart(archive)
                    y,z=struct.unpack_from('>2f',display,8+(vehicle*48+character)*56)
                    choice={'driverIndex':driver_index,'slot':slot,'driver':name,'driverSlug':slug,
                            'name':vehicle_name,'prefix':prefix,'character':character,'vehicle':vehicle,
                            'bike':vehicle>=18,'seatY':y,'seatZ':z,'archive':archive_name,
                            'archive_sha256':sha256(compressed),'driver_sha256':sha256(archive/'driver_model.brres'),
                            'kart_sha256':sha256(archive/'kart_model.brres'),'clips':driver['chr0'],
                            'parts':[m['model'] for m in kart['nodes']]}
                    print(f'Extracted {name} / {vehicle_name}: {len(driver["chr0"])} clips',flush=True)
                    pending.append(workers.submit(cook_one,choice))
            for future in pending:
                row=future.result();cooked[row['prefix']]=row
    finally:
        disc.f.close()
    choices=sorted(cooked.values(),key=lambda row:(row['driverIndex'],row['slot']))
    if len(choices)!=len(ROSTER)*VEHICLES_PER_DRIVER:raise ValueError('Incomplete retail roster')
    manifest=dict(original,choices=choices,extraNames=EXTRA_NAMES)
    (args.output/'models/garage.json').write_text(json.dumps(manifest,indent=2)+'\n')
    base_roster=json.loads((shared/'models/roster.json').read_text())
    characters=list(base_roster['characters'])
    for index,(slug,name,character,_,size) in enumerate(ROSTER[len(characters):],start=len(characters)):
        row=choices[index*VEHICLES_PER_DRIVER]
        characters.append({'slug':slug,'name':name,'character_id':character,'vehicle_id':row['vehicle'],
                           'prefix':slug+'_','archive':row['archive'],'archive_sha256':row['archive_sha256'],
                           'driver_sha256':row['driver_sha256'],'kart_sha256':row['kart_sha256'],
                           'clips':row['clips'],'seatOffset':[0,row['seatY'],row['seatZ']]})
    (args.output/'models/roster.json').write_text(json.dumps(dict(base_roster,characters=characters),indent=2)+'\n')
    write_native_garage(choices)
    print(f'FULL ROSTER PASS: {len(characters)} drivers, {len(choices)} combinations',flush=True)


if __name__=='__main__':main()
