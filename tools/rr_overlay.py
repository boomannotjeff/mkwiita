#!/usr/bin/env python3
"""Lays the user's Retro Rewind files over data/extracted, as its Riivolution
patch (xml/RetroRewind6.xml) lays them over the disc. A cook step of the Retro
Rewind edition only (tools/release/build_cia.py RR_STEP); settings come from
tools/vita/build_assets.py:

    MKW_RR        the RetroRewind6 folder
    MKW_RR_CUPS   cups whose tracks are cooked (RT:0, RT:0-3,CT:1, all)
    MKW_RR_SKINS  characters whose skins are cooked (mr,lg or all)

Copied: the chosen tracks and their music, the menu archives, UIAssets (cup
icons) and the chosen skins. Not copied: Race/Common (keeps retail physics),
the Pulsar code and its other asset archives (features this port lacks).
"""
import json,os,re,shutil,sys
from pathlib import Path
from rr_catalog import catalog
from unpack_szs import unpack
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'data/extracted'
UI=('Title','Title_U','Award','Globe','MenuSingle','MenuMulti','Channel','Font','Race','Race_U')
# Skin folders and where the patch mounts them.
SKINS={'Character':'Race/Kart','Character/AllKart':'Scene/Model/Kart','Character/Driver':'Scene/Model/Driver',
       'Character/Map':'Race/Map','Character/Portrait':'Race/Portrait','Character/Sound':'sound'}

def copy(src,dst):
    dst.parent.mkdir(parents=True,exist_ok=True)
    if not dst.is_file() or dst.stat().st_size!=src.stat().st_size or dst.stat().st_mtime<src.stat().st_mtime:shutil.copy2(src,dst)

def main():
    rr=os.environ.get('MKW_RR')
    if not rr:raise SystemExit('MKW_RR is not set: run tools/vita/build_assets.py --rr <RetroRewind6 folder>')
    rr=Path(rr);data=catalog(rr,os.environ.get('MKW_RR_CUPS','RT:0'))
    tracks=music=0
    for pack in data['packs']:
        for cup in pack['cups']:
            for t in cup['tracks']:
                if not t['cooked']:continue
                copy(rr/pack['folder']/f"{t['file']}.szs",OUT/f"Race/Course/{t['file']}.szs");tracks+=1
                for suffix in ('_N','_Final'):
                    if (s:=rr/f"strm/{t['file']}{suffix}.brstm").is_file():copy(s,OUT/f'sound/strm/{s.name}');music+=1
    menus=[n for n in UI if (rr/f'UI/{n}.szs').is_file()]
    for n in menus:copy(rr/f'UI/{n}.szs',OUT/f'Scene/UI/{n}.szs')
    copy(rr/'Assets/UIAssets.szs',OUT/'UIAssets.szs')
    want=os.environ.get('MKW_RR_SKINS','mr')
    codes=None if want=='all' else [c for c in want.split(',') if c]
    pattern=re.compile(r'(?:^|[-_.])('+('|'.join(map(re.escape,codes)) if codes else r'[a-z]+')+r')-\d+',re.I)
    skins=0
    for folder,target in SKINS.items():
        for f in (rr/folder).glob('*'):
            if f.is_file() and pattern.search(f.name):copy(f,OUT/target/f.name);skins+=1
    # Retail courses whose objects and effects later steps read (cook_object_sounds,
    # cook_race_effects): the retail importer unpacks them, this edition does not import them.
    for key in ('old_mario_gc','farm_course','old_sherbet_64','old_peach_gc'):
        if not (ROOT/'assets/courses'/key).is_dir():unpack(OUT/f'Race/Course/{key}.szs',ROOT/'assets/courses'/key)
    (OUT/'rr-catalog.json').write_text(json.dumps(data,indent=1,ensure_ascii=False)+'\n',encoding='utf-8')
    print(f'Retro Rewind: {tracks} tracks, {music} music files, menus {", ".join(menus)}, {skins} skin files',flush=True)

if __name__=='__main__':main()
