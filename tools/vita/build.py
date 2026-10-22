#!/usr/bin/env python3
"""Mario Kart Wii for PS Vita: one command from your disc to a VPK and game files.

    python tools/vita/build.py "Mario Kart Wii (USA).wbfs"

Steps, each skipped when already done (rerun to resume):
  1. tools    VitaSDK (VITASDK) with vitaGL from vdpm; CMake and Ninja (pip);
              tex3ds (devkitPro's texture converter, fetched into .toolchain)
  2. sources  pinned Kinoko, citro3d, libctru, noclip, gclib (tools/bootstrap.py)
  3. assets   your disc -> build/vita/data/mkw (tools/vita/build_assets.py, about an hour)
  4. vpk      the game -> build/vita/mkw.vpk, or mkw-retro-rewind.vpk with --rr (CMake)

--assets-only stops after step 3 and needs no VitaSDK: for a VPK downloaded
from the releases (Build Game Files.bat).

Only public tools and sources are downloaded. The disc and everything made from
it stay on this computer.
"""
import argparse, hashlib, io, os, shutil, subprocess, sys, tarfile, urllib.request
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
TOOLCHAIN = ROOT / '.toolchain'
# devkitPro's Windows tex3ds package (tex3ds 2.3.0-4), checked by SHA-256.
TEX3DS = ('https://pkg.devkitpro.org/packages/windows/x86_64/tex3ds-2.3.0-4-x86_64.pkg.tar.zst',
          'a000c670bf49c5af203b688ff4c0ba0beaaf2a5b0cf4356f3da8c0ebb68848b4')


def log(*a):
    print('[vita]', *a, flush=True)


def run(cmd, **kw):
    log('$', ' '.join(str(c) for c in cmd))
    subprocess.run([str(c) for c in cmd], cwd=ROOT, check=True, **kw)


def vitasdk():
    sdk = os.environ.get('VITASDK')
    if not sdk or not (Path(sdk) / 'share/vita.cmake').is_file():
        raise SystemExit('Set VITASDK to your VitaSDK folder (https://vitasdk.org), e.g. C:\\vitasdk')
    return Path(sdk)


def ensure_vitagl(sdk):
    if (sdk / 'arm-vita-eabi/lib/libvitaGL.a').is_file():
        return
    vdpm = sdk / 'bin' / ('vdpm.exe' if os.name == 'nt' else 'vdpm')
    log('installing vitaGL with vdpm')
    run([vdpm, 'pacman', '--', '-S', '--noconfirm', 'vitaGL', 'zlib', 'libpng'])


def ensure_cmake():
    import sysconfig
    venv = sys.prefix != sys.base_prefix
    # pip's console scripts may not be on PATH yet.
    scripts = Path(sys.executable).parent if venv else Path(sysconfig.get_path('scripts', f'{os.name}_user'))
    os.environ['PATH'] = str(scripts) + os.pathsep + os.environ['PATH']
    missing = [m for m in ('cmake', 'ninja') if not shutil.which(m)]
    if missing:
        run([sys.executable, '-m', 'pip', 'install', *([] if venv else ['--user']), *missing])


def ensure_tex3ds():
    from devkitpro import tool
    if tool('tex3ds').exists():
        return
    target = TOOLCHAIN / 'devkitpro'
    exe = target / 'tools/bin' / ('tex3ds.exe' if os.name == 'nt' else 'tex3ds')
    os.environ['DEVKITPRO_WIN' if os.name == 'nt' else 'DEVKITPRO'] = str(target)
    if exe.exists():
        return
    if os.name != 'nt':
        raise SystemExit('Install tex3ds from devkitPro (dkp-pacman -S tex3ds) and set DEVKITPRO')
    log('fetching tex3ds')
    # devkitPro's server expects a pacman client.
    request = urllib.request.Request(TEX3DS[0], headers={'User-Agent': 'pacman/6.0.2 (MSYS_NT-10.0 x86_64) libalpm/13.0.2'})
    data = urllib.request.urlopen(request).read()
    if hashlib.sha256(data).hexdigest() != TEX3DS[1]:
        raise SystemExit('tex3ds download does not match its checksum')
    import zstandard
    tar = zstandard.ZstdDecompressor().stream_reader(io.BytesIO(data)).read()
    with tarfile.open(fileobj=io.BytesIO(tar)) as t:
        for member in t.getmembers():
            if member.isfile() and member.name.startswith('opt/devkitpro/tools/bin/'):
                out = target / 'tools/bin' / Path(member.name).name
                out.parent.mkdir(parents=True, exist_ok=True)
                out.write_bytes(t.extractfile(member).read())


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('disc', nargs='?', type=Path, help='Mario Kart Wii (USA) ISO or WBFS (not needed once cooked)')
    p.add_argument('--skip-assets', action='store_true', help='only build the VPK')
    p.add_argument('--assets-only', action='store_true',
                   help='only build the game files, for a downloaded VPK (no VitaSDK needed)')
    p.add_argument('--release', action='store_true', help='no test hooks or runtime log')
    p.add_argument('--mipmaps', action='store_true', help='experimental texture mipmaps (see tools/vita/build_assets.py)')
    p.add_argument('--force-cook', action='store_true', help='cook every asset again')
    p.add_argument('--rr', type=Path, help='Retro Rewind folder (RetroRewind6): builds the separate Retro Rewind edition')
    p.add_argument('--rr-cups', default='all', help='Retro Rewind cups to cook: all (default), or e.g. RT:0 or RT:0-3,CT:1')
    p.add_argument('--rr-skins', default='all', help='Retro Rewind skins to cook: all (default), or character codes (mr,lg)')
    a = p.parse_args()
    data = 'mkwrr' if a.rr else 'mkw'
    if a.assets_only and a.skip_assets:
        raise SystemExit('--assets-only and --skip-assets leave nothing to build')
    sys.path[:0] = [str(ROOT / 'tools')]
    if not a.assets_only:
        sdk = vitasdk()
        ensure_vitagl(sdk)
        ensure_cmake()
    ensure_tex3ds()
    run([sys.executable, 'tools/bootstrap.py'])
    if not a.skip_assets:
        run([sys.executable, 'tools/vita/build_assets.py', *([a.disc] if a.disc else []),
             *(['--mipmaps'] if a.mipmaps else []), *(['--force-cook'] if a.force_cook else []),
             *(['--rr', a.rr, '--rr-cups', a.rr_cups, '--rr-skins', a.rr_skins] if a.rr else [])])
    build = ROOT / 'build/vita'
    if a.assets_only:
        log('done:', build / 'data' / data)
        log(f'Copy the contents of that folder to ux0:data/{data} on your Vita, and install the VPK from the release.')
        return
    if a.rr:  # the content stamp (tools/vita/gen_headers.py) counts the Retro Rewind cook steps
        os.environ.update(MKW_RR=str(a.rr.resolve()), MKW_RR_CUPS=a.rr_cups, MKW_RR_SKINS=a.rr_skins)
    run(['cmake', '-G', 'Ninja', '-S', '.', '-B', build, '-DCMAKE_BUILD_TYPE=Release', f'-DMKW_RELEASE={"ON" if a.release else "OFF"}',
         f'-DMKW_VARIANT={"rr" if a.rr else "retail"}'])
    run(['cmake', '--build', build])
    log('done:', build / ('mkw-retro-rewind.vpk' if a.rr else 'mkw.vpk'))
    log('Install it with VitaShell, then copy the contents of', build / 'data' / data, f'to ux0:data/{data} on your Vita.')


if __name__ == '__main__':
    main()
