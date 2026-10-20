#!/usr/bin/env python3
"""Every NW4R EF effect archive, converted ahead of the runtime that uses it (0.5.0).

The race table (tools/cook_race_effects.py, race-effects.bin) keeps the
emitters the race uses today. This step converts every effect archive on the
disc in full, so course, arena, battle and ceremony effects can arrive as
quick updates: RKRace.breff (all of it), each course's, arena's and stage's
effect folder, and the award trophy sparkle. Each archive becomes one or more
REFX v3 tables (the race table's format) under textures/fx/<archive>/, with
its textures beside them; models/effect-archives.json indexes them. An
archive larger than one table (255 emitters, 96 textures) is split along its
emitter references, so a parent and the children it spawns share a table.
"""
import os,argparse,hashlib,json,shutil,struct,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'vendor/gclib')]
import cook_race_effects as fx
OUT=ROOT/'sd-card/3ds/mkw-3ds'
# GX formats the race table never needed (course effects use them): decoded by
# gclib like the rest, stored as PICA RGBA8 / LA4. The race table is unchanged.
for gx,form in {2:('la4','IA4'),8:('rgba8','C4'),9:('rgba8','C8'),10:('rgba8','C14X2'),14:('rgba8','CMPR')}.items():
    fx.TEX_FORMATS.setdefault(gx,form)

def sources():
    """(key, breff) for every effect archive: common, courses/arenas/stages, award."""
    rows=[('common_'+p.stem.lower(),p) for p in sorted((ROOT/'assets/common/Effect').glob('*.breff'))]
    for p in sorted((ROOT/'assets/courses').glob('*/effect/**/*.breff')):
        course=p.relative_to(ROOT/'assets/courses').parts[0]
        rows.append((f'{course}_{p.stem}'.lower(),p))
    rows+=[('award_'+p.stem.lower(),p) for p in sorted((ROOT/'build/ceremony/Award/Effect').glob('*.breff'))]
    return [(''.join(c if c.isalnum() or c=='_' else '_' for c in k),p) for k,p in rows if not p.name.startswith('._')]

def groups(records,limit_emitters,limit_textures):
    """Emitters split into tables closed under their references."""
    parent={n:n for n in records}
    def find(n):
        while parent[n]!=n:parent[n]=parent[parent[n]];n=parent[n]
        return n
    for n,e in records.items():
        for r in e.references():
            if r in records:parent[find(r)]=find(n)
    components={}
    for n in records:components.setdefault(find(n),[]).append(n)
    tables=[]
    for comp in sorted(components.values(),key=len,reverse=True):
        tex={t for n in comp for t in records[n].textures if t}
        if len(comp)>limit_emitters or len(tex)>limit_textures:raise ValueError(f'one effect group alone exceeds a table ({len(comp)} emitters, {len(tex)} textures)')
        for t in tables:
            if len(t['emitters'])+len(comp)<=limit_emitters and len(t['textures']|tex)<=limit_textures:
                t['emitters']+=comp;t['textures']|=tex;break
        else:tables.append({'emitters':list(comp),'textures':set(tex)})
    return [t['emitters'] for t in tables]

def write_table(br,reft,idx,tindex,order,output,name):
    """REFX v3 like cook_race_effects.cook, for the given emitters of one archive."""
    records={n:fx.Emitter(br,idx,n) for n in order}
    notes=[]
    # Features the runtime has not ported yet stay in the stored descriptors
    # (desc/ptcl bytes); they are listed, not refused, so a later code update
    # can draw them without new content.
    for n in order:
        try:fx.check(records[n],notes)
        except ValueError as e:notes.append(f'not yet drawable: {e}')
    textures,files={},[]
    def texture_index(t):
        if not t:return fx.NO_INDEX
        if t not in tindex:notes.append(f'texture {t} missing');return fx.NO_INDEX
        if t not in textures:
            textures[t]=len(files);files.append(fx.texture(reft,tindex,t,output,name+'_'))
        return textures[t]
    emitter_index={n:i for i,n in enumerate(order)}
    def emitter_lookup(n):
        if n not in emitter_index:notes.append(f'emitter {n} outside the table');return fx.NO_INDEX
        return emitter_index[n]
    blob=bytearray()
    def put(data):
        while len(blob)%4:blob.append(0)
        at=len(blob);blob.extend(data);return at
    table=[]
    for n in order:
        e=records[n];tex=[texture_index(t) for t in e.textures];curves=[]
        for c in e.ptcl_curves:
            kind=c[2]
            lookup=texture_index if kind==4 else emitter_lookup if kind in (2,5) else (lambda x:fx.NO_INDEX)
            curves.append(put(fx.resolve(c,lookup)+bytes(0x20)))
        for c in e.emit_curves:curves.append(put(bytes(c)+bytes(0x20)))
        desc_at,ptcl_at=put(e.desc),put(e.ptcl)
        list_at=put(struct.pack('<%dI'%len(curves),*curves)) if curves else 0
        table.append(struct.pack('<32sII3H7HI',n.encode()[:31],desc_at,ptcl_at,*[t&0xffff for t in tex],len(e.ptcl_curves),e.ptcl_init,len(e.emit_curves),e.emit_init,len(e.ptcl),0,0,list_at))
    if len(files)>fx.MAX_TEXTURES:raise ValueError(f'{name}: too many textures')
    head=struct.pack('<4sIIII',b'REFX',fx.VERSION,len(order),len(files),len(blob))
    tex_table=b''.join(struct.pack('<32sHHHH',f[0].encode(),f[1],f[2],f[4],f[5]) for f in files)
    (output/(name+'.refx')).write_bytes(head+tex_table+b''.join(table)+bytes(blob))
    for png in output.glob('*.png'):png.unlink()
    return {'table':name+'.refx','emitters':order,'textures':[f[0] for f in files],'notes':notes}

def main():
    argparse.ArgumentParser().parse_args()
    root=OUT/'textures/fx'
    if root.exists():shutil.rmtree(root)
    index={'version':1,'archives':[]}
    for key,breff in sources():
      try:
        br,reft=breff.read_bytes(),breff.with_suffix('.breft').read_bytes()
        idx,tindex=fx.entries(br),fx.entries(reft)
        # Mipmapped textures keep their full-size level (stored first).
        reft=bytearray(reft);mipmapped=[n for n,(pos,_) in tindex.items() if reft[pos+0x14]>1]
        for n in mipmapped:reft[tindex[n][0]+0x14]=1
        reft=bytes(reft)
        records={n:fx.Emitter(br,idx,n) for n in idx}
        output=root/key;output.mkdir(parents=True,exist_ok=True)
        tables=[write_table(br,reft,idx,tindex,order,output,f'{key}_{i}') for i,order in enumerate(groups(records,fx.MAX_EMITTERS,fx.MAX_TEXTURES))]
      except (ValueError,KeyError,FileNotFoundError) as error:
        # Retro Rewind tracks' effect archives in another format (older emitter
        # descriptors): that track goes without them; retail stays strict.
        if not os.environ.get('MKW_RR'):raise
        print(f'{key}: effect archive left out ({str(error)[:120]})',flush=True)
        shutil.rmtree(root/key,ignore_errors=True)
        continue
      else:
          index['archives'].append({'key':key,'source':breff.relative_to(ROOT).as_posix(),'sha256':hashlib.sha256(br).hexdigest(),
              'folder':f'textures/fx/{key}','mipmapsDropped':mipmapped,'tables':[{**t,'table':f'textures/fx/{key}/{t["table"]}'} for t in tables]})
    (OUT/'models/effect-archives.json').write_text(json.dumps(index,indent=1)+'\n')
    emitters=sum(len(t['emitters']) for a in index['archives'] for t in a['tables'])
    print(f"effect archives: {len(index['archives'])} archives, {sum(len(a['tables']) for a in index['archives'])} tables, {emitters} emitters")

if __name__=='__main__':main()
