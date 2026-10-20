#!/usr/bin/env python3
"""Retro Rewind's cup and track list, read from the user's own Pulsar configs.

    python tools/rr_catalog.py <RetroRewind6 folder> [--cups RT:0] [--out file.json]

Binaries/Config{RT,CT,BT}.pul (PULS v3): a CUPS table (4 tracks per cup: course
slot = the retail course the track runs as, music slot, variant count, CRC32),
a BMG with the track names (ids 0x20000+track) and a FILE text table
(track index in hex = archive name, 'cup?icon_NNN.png').
--cups picks the tracks the build cooks; every other track stays listed with
cooked=false, and the menus show it as not available.
"""
import argparse,json,re,struct,sys
from pathlib import Path
PACKS=('RT','CT','BT')

def bmg_names(b,base):
    """{message id: [(text, colour), ...]} of a BMG at base: runs split by colour
    escapes (1A 08 00 00 01 00 cc cc; colour 0 is the pane's own), other escapes dropped."""
    if b[base:base+8]!=b'MESGbmg1':raise ValueError('no BMG')
    count=struct.unpack_from('>I',b,base+12)[0];pos=base+0x20;inf=dat=mid=None
    for _ in range(count):
        tag=b[pos:pos+4];size=struct.unpack_from('>I',b,pos+4)[0]
        if tag==b'INF1':inf=pos
        elif tag==b'DAT1':dat=pos+8
        elif tag==b'MID1':mid=pos
        pos+=size
    n,stride=struct.unpack_from('>HH',b,inf+8);ids=struct.unpack_from(f'>{n}I',b,mid+16)
    out={}
    for i in range(n):
        p=dat+struct.unpack_from('>I',b,inf+16+i*stride)[0];runs=[['',0]]
        while True:
            u=struct.unpack_from('>H',b,p)[0]
            if u==0:break
            if u==0x1A:
                if b[p+2]==8 and b[p+3:p+6]==bytes((0,0,1)):runs.append(['',struct.unpack_from('>H',b,p+6)[0]])
                p+=b[p+2];continue # escape: its size byte counts the whole sequence
            runs[-1][0]+=chr(u);p+=2
        out[ids[i]]=[r for r in runs if r[0]]
    return out

def parse_config(path):
    b=path.read_bytes()
    if b[:4]!=b'PULS':raise ValueError(f'{path}: not a Pulsar config')
    cups,bmg=struct.unpack_from('>II',b,12)
    if b[cups:cups+4]!=b'CUPS':raise ValueError(f'{path}: no CUPS section')
    cup_count=struct.unpack_from('>H',b,cups+12)[0]
    entries=[struct.unpack_from('>BBHI',b,cups+0x1C+8*i) for i in range(cup_count*4)]
    names=bmg_names(b,bmg)
    files,icons={},{}
    table=b[bmg+struct.unpack_from('>I',b,bmg+8)[0]:]
    if not table.startswith(b'FILE'):raise ValueError(f'{path}: no FILE table')
    for line in table[4:].decode('latin-1').splitlines():
        if m:=re.fullmatch(r'([0-9A-Fa-f]+)\?(\S+?)(?:\.png)?',line):icons[int(m[1],16)]=m[2]
        elif m:=re.fullmatch(r'([0-9A-Fa-f]+)=([^|]*)\|.*',line):files[int(m[1],16)]=m[2]
    return entries,names,files,icons

def selected(spec):
    """'RT:0-3,CT:1' or 'all' -> function(pack, cup) -> bool."""
    if spec=='all':return lambda pack,cup:True
    want=set()
    for part in filter(None,spec.split(',')):
        pack,_,cups=part.partition(':');lo,_,hi=cups.partition('-')
        if pack not in PACKS or not lo.isdigit() or (hi and not hi.isdigit()):raise SystemExit(f'bad cup selection {part!r} (e.g. RT:0 or RT:0-3,CT:1)')
        want|={(pack,c) for c in range(int(lo),int(hi or lo)+1)}
    return lambda pack,cup:(pack,cup) in want

def catalog(rr,cups='RT:0'):
    rr=Path(rr);pick=selected(cups);packs=[]
    folders={'RT':rr/'Tracks','CT':rr/'CT/Tracks','BT':rr/'BT/Tracks'}
    for pack in PACKS:
        entries,names,files,icons=parse_config(rr/f'Binaries/Config{pack}.pul')
        n=len(entries)//4
        # Pulsar pads an odd cup count with a copy of cup 0.
        if n>1 and n%2==0 and [e[3] for e in entries[-4:]]==[e[3] for e in entries[:4]]:n-=1
        listing={p.name.lower():p.name for p in folders[pack].glob('*.szs')}
        result=[]
        for c in range(n):
            tracks=[]
            for t in range(c*4,c*4+4):
                slot,music,variants,crc=entries[t];file=files.get(t,'')
                found=listing.get(f'{file}.szs'.lower()) # BT lists GCNBC for gcnBC.szs
                runs=names.get(0x20000+t,[[file,0]]);name=''.join(r[0] for r in runs).strip()
                # A coloured console prefix ("SNES" in yellow): its text and colour code.
                prefix=runs[0][0].strip() if len(runs)>1 and runs[0][1] else ''
                tracks.append({'index':t,'file':found[:-4] if found else file,'name':name,'prefix':prefix,'color':runs[0][1] if prefix else 0,
                               'slot':slot,'music':music,'crc':f'{crc:08X}','present':bool(found),'cooked':bool(found) and pick(pack,c)})
            result.append({'index':c,'icon':icons.get(c,''),'tracks':tracks})
        packs.append({'pack':pack,'folder':str(folders[pack].relative_to(rr)).replace('\\','/'),'cups':result})
    return {'version':1,'source':str(rr),'cups':cups,'packs':packs}

def main():
    p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('rr',type=Path);p.add_argument('--cups',default='RT:0');p.add_argument('--out',type=Path)
    a=p.parse_args();data=catalog(a.rr,a.cups)
    for pack in data['packs']:
        tracks=[t for c in pack['cups'] for t in c['tracks']]
        missing=[t['file'] for t in tracks if not t['present']]
        print(f"{pack['pack']}: {len(pack['cups'])} cups, {len(tracks)} tracks, {sum(t['cooked'] for t in tracks)} to cook"+(f", missing {missing}" if missing else ''),file=sys.stderr)
    text=json.dumps(data,indent=1,ensure_ascii=False)+'\n'
    if a.out:a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(text,encoding='utf-8')
    else:sys.stdout.write(text)

if __name__=='__main__':main()
