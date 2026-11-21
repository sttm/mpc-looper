#!/usr/bin/env python3
from pathlib import Path
import sys,json,shutil,subprocess,zipfile,hashlib
ROOT=Path(__file__).resolve().parents[1];SDK=ROOT.parent.parent/'vendor/mpc-plugin-builder-sdk'
sys.path.insert(0,str(SDK/'tools'));from build import write_version_xml
meta=json.loads((ROOT/'plugin.json').read_text());out=ROOT/'build/sttm - VST - MPC Looper';out.mkdir(parents=True,exist_ok=True)
# Native bank assignments must match the four-column controls.
ql=json.loads((ROOT/'skins/Q-Links.json').read_text())
expected={f'Q-Link {q}':p for n in range(4) for q,p in ((1+n*4,90+n),(2+n*4,18+n*10),(3+n*4,60+n),(4+n*4,-1))}
assert ql['Program Mode Q-Links']==expected
for entry in ql['Screen Mode Q-Links']['map'][:2]:assert entry['Q-Links']==expected
shutil.copy2(ROOT/'plugin.json',out/'plugin.json')
if (out/'Plugin Skins').exists():shutil.rmtree(out/'Plugin Skins')
shutil.copytree(ROOT/'skins',out/'Plugin Skins')
for folder in ('Exports','Sessions'):(out/'Content'/folder).mkdir(parents=True,exist_ok=True)
for name in ('README.md','LICENSE','THIRD_PARTY.md','VALIDATION.md','TEMPO-DESIGN.md','MEMORY-DESIGN.md','TRANSPORT-DESIGN.md'):shutil.copy2(ROOT/name,out/name)
shutil.copy2(ROOT.parent.parent/'assets/fonts/Barlow-OFL.txt',out/'ARTWORK-FONT-LICENSE.txt')
shutil.copy2(SDK/'LICENSE',out/'SDK-LICENSE.txt')
for lib in ('signalsmith-stretch','signalsmith-linear'):
 shutil.copy2(ROOT/'vendor'/lib/'LICENSE.txt',out/(lib.upper()+'-LICENSE.txt'))
write_version_xml(str(out),meta,True)
subprocess.run([sys.executable,str(SDK/'tools/validate.py'),str(out),'--params',str(ROOT/'build/params.json')],check=True)
# Never package user audio or saved sessions if rebuilding an installed tree.
archive=ROOT/f'dist/mpc-looper-{meta["version"]}-mpc.zip';archive.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
 for p in sorted(out.rglob('*')):
  if p.is_file() and 'Content' not in p.relative_to(out).parts:z.write(p,Path('VST2 Plugins')/out.name/p.relative_to(out))
 z.write(ROOT/'README.md','INSTALL.md')
sha=hashlib.sha256(archive.read_bytes()).hexdigest();archive.with_suffix('.zip.sha256').write_text(sha+'  '+archive.name+'\n')
print('Package:',archive,'\nSHA256:',sha)
