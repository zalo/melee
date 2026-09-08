from pathlib import Path
import argparse,json,hashlib
from disc import Disc
parser=argparse.ArgumentParser(description='Extract Melee US 1.02 assets for the native port')
parser.add_argument('disc', type=Path)
parser.add_argument('output', type=Path)
a=parser.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
manifest_path = a.output / 'manifest.json'
if manifest_path.is_symlink():
    raise ValueError('Refusing to write manifest through a symlink')
manifest={}
with Disc(a.disc) as disc:
    for name,(offset,size) in disc.files.items():
        dest=a.output/name
        if dest.is_symlink() or any(p.is_symlink() for p in dest.parents):
            raise ValueError('Refusing extraction through a symlink')
        data=disc.read(offset,size)
        dest.parent.mkdir(parents=True,exist_ok=True)
        dest.write_bytes(data)
        manifest[name]={'size':size,'sha256':hashlib.sha256(data).hexdigest()}
manifest_path.write_text(json.dumps(manifest,indent=2)+'\n')
print(f'Extracted {len(manifest)} asset files from the verified US 1.02 disc to {a.output}')
