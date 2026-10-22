#!/usr/bin/env python3
"""Retro Rewind character skins: each skin's twelve vehicles as extra garage choices.

A cook step of the Retro Rewind edition only (tools/release/build_cia.py), after
the full roster. tools/rr_overlay.py copied the chosen skins' archives beside the
retail ones: Race/Kart/<vehicle>-<driver>-<skin>.szs. Each is cooked like a retail
choice (cook_garage.cook_choice) under the prefix <driver>_<letter>_<vehicle>_
(skin 1 = b, as prefixes are letters only) and appended to build/roster's
garage.json, so the racer lighting and shadow steps that follow cook them too.
include/mkw/garage_skins.hpp lists them for the menu and the race (main.cpp).
"""
import json,os,struct
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
from cook_roster import ROOT,ROSTER,sha256
from cook_garage import VEHICLES,cook_choice,inspect_driver,inspect_kart
from unpack_szs import unpack

OUTPUT=ROOT/'build/roster';WORK=ROOT/'build/roster-work'
MAX_SKINS=9

def header(rows,counts,masks):
    """include/mkw/garage_skins.hpp: the cooked skins and how many each driver has."""
    boolean=lambda v:'true' if v else 'false'
    lines=['#pragma once','#include <mkw/garage.hpp>','namespace mkw {',
           '// Retro Rewind character skins (tools/cook_rr_skins.py): driver (roster index), skin (1+), vehicle slot.',
           'struct GarageSkin {unsigned driver,skin,slot;GarageChoice choice;};',
           f'inline constexpr std::array<GarageSkin,{len(rows)}> GarageSkins{{{{']
    for r in rows:
        lines.append('    {%d,%d,%d,{%s,%s,%d,%d,%.9ff,%.9ff,%s,%d,%s,%d}},'%(r['driverIndex'],r['skin'],r['slot'],json.dumps(r['name']),json.dumps(r['prefix']),
                     r['character'],r['vehicle'],r['seatY'],r['seatZ'],boolean(r['bike']),r['extrasMask'],boolean(r['bodyAnimation']),r['extraAnimationMask']))
    lines+=['}};',f'inline constexpr std::array<unsigned,{len(counts)}> GarageSkinCounts{{{{'+','.join(map(str,counts))+'}};',
            '// Bit s: skin s cooked (a skin some vehicle of which did not convert is left out).',
            f'inline constexpr std::array<unsigned,{len(masks)}> GarageSkinMask{{{{'+','.join(map(str,masks))+'}};',
            'inline const GarageChoice* garageSkin(unsigned driver,unsigned skin,unsigned slot){',
            '    for(const auto& s:GarageSkins)if(s.driver==driver&&s.skin==skin&&s.slot==slot)return &s.choice;',
            '    return nullptr;','}','}','']
    from course_catalog import generated
    generated('garage_skins.hpp',write=True).write_text('\n'.join(lines))

def cook_skin(job):
    """One skin vehicle: unpack, inspect and cook it (a worker process: all Python-bound).
    A vehicle the converter does not take leaves the whole skin out (main)."""
    driver_index,skin,slot,vehicle,vehicle_name,stem,archive_name=job
    slug,name,character,code,size=ROSTER[driver_index]
    try:
        return cook_skin_vehicle(job)
    except Exception as error:
        print(f'{name} skin {skin} / {vehicle_name}: left out ({str(error)[:120]})',flush=True)
        return {'driverIndex':driver_index,'skin':skin,'failed':True}

def cook_skin_vehicle(job):
    driver_index,skin,slot,vehicle,vehicle_name,stem,archive_name=job
    slug,name,character,code,size=ROSTER[driver_index]
    prefix=f'{slug}_{chr(ord("a")+skin)}_{stem}_'
    # Already cooked from this same archive (this step's or the racer lighting's receipt): reuse it.
    receipt=WORK/'receipts'/(prefix+'.json')
    if receipt.is_file():
        row=json.loads(receipt.read_text())
        if row.get('archive_sha256')==sha256(archive_name) and all((OUTPUT/'models'/(n+'.gmdl')).is_file() for n in row['models']) and all((OUTPUT/'models'/(n+'.dskm')).is_file() for n in row['animationBanks']):
            return row
    archive=WORK/'vehicles'/prefix.rstrip('_');unpack(archive_name,archive)
    driver=inspect_driver(archive);kart=inspect_kart(archive)
    display=(ROOT/'assets/common/kartDriverDispParam.bin').read_bytes()
    y,z=struct.unpack_from('>2f',display,8+(vehicle*48+character)*56)
    choice={'driverIndex':driver_index,'slot':slot,'skin':skin,'driver':name,'driverSlug':slug,
            'name':vehicle_name,'prefix':prefix,'character':character,'vehicle':vehicle,
            'bike':vehicle>=18,'seatY':y,'seatZ':z,'archive':archive_name.relative_to(ROOT).as_posix(),
            'archive_sha256':sha256(archive_name),'driver_sha256':sha256(archive/'driver_model.brres'),
            'kart_sha256':sha256(archive/'kart_model.brres'),'clips':driver['chr0'],
            'parts':[m['model'] for m in kart['nodes']]}
    row=cook_choice(choice,OUTPUT,source_root=WORK/'vehicles',export_root=WORK/'parts')
    receipt.parent.mkdir(parents=True,exist_ok=True);receipt.write_text(json.dumps(row,indent=2)+'\n')
    return row

def main():
    import argparse
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--jobs',type=int,default=3);jobs=p.parse_args().jobs
    want=os.environ.get('MKW_RR_SKINS','mr')
    codes=None if want=='all' else set(filter(None,want.split(',')))
    karts=ROOT/'data/extracted/Race/Kart'
    garage=OUTPUT/'models/garage.json';manifest=json.loads(garage.read_text())
    manifest['choices']=[c for c in manifest['choices'] if not c.get('skin')] # this step's own, from a previous run
    jobs_list=[];counts=[0]*len(ROSTER)
    for driver_index,(slug,name,character,code,size) in enumerate(ROSTER):
        if codes is not None and code not in codes:continue
        for skin in range(1,MAX_SKINS+1):
            archives=[karts/f'{stem}-{code}-{skin}.szs' for _,_,stem in VEHICLES[size]]
            if not all(a.is_file() for a in archives):break
            counts[driver_index]=skin
            for slot,((vehicle,vehicle_name,stem),archive_name) in enumerate(zip(VEHICLES[size],archives)):
                jobs_list.append((driver_index,skin,slot,vehicle,vehicle_name,stem,archive_name))
    # Processes, not threads: unpacking (pure-Python Yaz0) and cooking are Python-bound under the GIL.
    with ProcessPoolExecutor(max_workers=max(1,jobs)) as workers:
        cooked=list(workers.map(cook_skin,jobs_list))
    # A skin needs all twelve vehicles: one that failed leaves the whole skin out.
    failed={(c['driverIndex'],c['skin']) for c in cooked if c.get('failed')}
    cooked=[c for c in cooked if (c['driverIndex'],c['skin']) not in failed]
    masks=[0]*len(ROSTER)
    for c in cooked:masks[c['driverIndex']]|=1<<c['skin']
    counts=[m.bit_length()-1 if m else 0 for m in masks]
    if failed:print('Skins left out:',sorted(failed),flush=True)
    manifest['choices']+=cooked
    garage.write_text(json.dumps(manifest,indent=2)+'\n')
    header(cooked,counts,masks)
    print(f'Retro Rewind skins: {sum(counts)} skins, {len(cooked)} choices',flush=True)

if __name__=='__main__':main()
