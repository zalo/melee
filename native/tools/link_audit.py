"""Report unresolved native symbols without replacing them with stubs."""
from pathlib import Path
import subprocess,collections,json
root=Path(__file__).resolve().parents[2]
objects=list((root/'build/native/CMakeFiles/melee_game.dir').rglob('*.o'))
libs=list((root/'build/native/aurora').glob('libaurora*.a'))
if not objects or not libs:
 raise SystemExit('Build the native game objects and Aurora libraries before auditing symbols')
defined=set();required=collections.defaultdict(list)
for paths,game in [(objects,True),(libs,False)]:
 for p in paths:
  output=subprocess.run(['nm','-g',str(p)],capture_output=True,text=True,check=True).stdout
  for line in output.splitlines():
   parts=line.split()
   if len(parts)>=2 and parts[-2]=='U':
    if game: required[parts[-1]].append(str(p.relative_to(root)))
   elif len(parts)>=3 and parts[-2] in {'T','D','B','S','C','W'}: defined.add(parts[-1])
missing={s:users for s,users in required.items() if s not in defined}
path=root/'build/native/unresolved.json';path.write_text(json.dumps(missing,indent=2))
print(len(objects),'compiled game objects;',len(missing),'unresolved symbols (includes host libc)')
for s in sorted(missing):print(s)
