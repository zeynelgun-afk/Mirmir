#!/usr/bin/env python3
"""Stage a relocatable runtime from an already cooked Linux Shipping archive."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import stat
import time

ROOT=Path(__file__).resolve().parents[1]


def copy(src,dst):
    dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)


def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--runtime',type=Path,default=ROOT/'.local/packaging/runtime');p.add_argument('--version',default=(ROOT/'packaging/VERSION').read_text().strip());a=p.parse_args()
    package=json.loads((ROOT/'.local/package.json').read_text());source=Path(package['launcher']).parent
    if package.get('configuration')!='Shipping' or package.get('platform')!='Linux' or package.get('packaging_command_succeeded') is not True:raise SystemExit('A successful Linux Shipping package is required.')
    if package.get('project_version') != a.version:raise SystemExit('Installer version must match the version recorded by the successful Unreal cook.')
    binary=Path(package['binary']).resolve();binary.relative_to(source.resolve())
    if not os.access(binary,os.X_OK):raise SystemExit('Shipping binary is not executable.')
    if a.output.exists():raise SystemExit('Refusing to overwrite stage: '+str(a.output))
    if not (a.runtime/'python/bin/python3').exists():raise SystemExit('Prepare the portable runtime first.')
    a.output.mkdir(parents=True)
    def ignore(directory,names):
        return [name for name in names if name.endswith(('.debug','.sym','.pdb','.log')) or name in {'Manifest_DebugFiles_Linux.txt','__pycache__'}]
    shutil.copytree(source,a.output/'runtime',symlinks=True,ignore=ignore)
    shutil.copytree(a.runtime/'python',a.output/'python',symlinks=True,ignore=ignore)
    for module in (ROOT/'native-shell').glob('*.py'):
        copy(module,a.output/'native-shell'/module.name)
    for relative in ['native-shell/README.md','native-shell/LICENSE','LICENSE','ASSET-LICENSES.md']:
        item=ROOT/relative
        if item.is_file():copy(item,a.output/relative)
    copy(ROOT/'desktop-lab/icon.svg',a.output/'assets/icon.svg')
    copy(ROOT/'packaging/linux/launcher.sh',a.output/'mirmir');(a.output/'mirmir').chmod(0o755)
    copy(ROOT/'packaging/linux/diagnose.py',a.output/'diagnose.py')
    for filename in ['INSTALL.md','INSTALL-LINUX.md']:
        if (ROOT/'docs'/filename).is_file():copy(ROOT/'docs'/filename,a.output/filename)
    licenses=a.output/'licenses';licenses.mkdir()
    for source_file,name in [
        ('assets/personal-use/sources.json','cat-original-sources.json'),
        ('assets/provenance/cat-walk-personal/original/license.txt','cat-original-license.txt'),
        ('assets/provenance/tuxedo-motion/LICENSE.md','additional-motion-license.md'),
        ('assets/provenance/tuxedo-motion/source.json','additional-motion-sources.json'),
        ('assets/generated/fur-detail-v1.json','generated-fur-provenance.json')]:copy(ROOT/source_file,licenses/name)
    copy(ROOT/'packaging/runtime-lock.json',licenses/'runtime-lock.json')
    copy(ROOT/'packaging/requirements-linux.lock',licenses/'requirements-linux.lock')
    copy(ROOT/'packaging/THIRD-PARTY-NOTICES.md',licenses/'THIRD-PARTY-NOTICES.md')
    shutil.copytree(ROOT/'packaging',a.output/'source/packaging',ignore=ignore)
    (a.output/'.local').mkdir()
    manifest={'schema_version':1,'configuration':'Shipping','platform':'Linux','packaging_command_succeeded':True,'archive_directory':'runtime','launcher':'runtime/Mirmir.sh','binary':'runtime/'+str(binary.relative_to(source.resolve())),'map':package['map']}
    (a.output/'.local/package.json').write_text(json.dumps(manifest,indent=2)+'\n')
    # Runtime whitelist: UAT output, private interpreter, shell source, notices.
    forbidden=[str(p) for p in a.output.rglob('*') if p.name in {'UnrealEditor','UnrealEditor-Cmd','UnrealBuildTool','RunUAT.sh','.env'} or ('Engine/Source/' in str(p)) or ('Engine/Extras/ThirdPartyNotUE/' in str(p))]
    if forbidden:raise SystemExit('Forbidden SDK/private files in stage: '+str(forbidden[:5]))
    lock=json.loads((ROOT/'packaging/runtime-lock.json').read_text())
    source_metadata={key:package.get(key) for key in ['configuration','platform','project_version','packaged_at_utc','map','editor_required_for_runtime']}
    source_metadata['archive_name']=Path(package['archive_directory']).name
    record={'schema_version':1,'version':a.version,'platform':'Linux-x86_64','staged_at_unix':int(time.time()),'shipping_binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'native_shell_sha256':hashlib.sha256((a.output/'native-shell/mirmir_shell.py').read_bytes()).hexdigest(),'source_package_manifest':source_metadata,'minimum_glibc':lock['minimum_glibc'],'python_version':lock['python']['version'],'wheel_versions':{x['name']:x['version'] for x in lock['wheels']},'engine_sdk_included':False,'staged_runtime_launch_verified':False,'other_distro_install_verified':False}
    record['native_modules_sha256']={module.name:hashlib.sha256(module.read_bytes()).hexdigest()
        for module in sorted((a.output/'native-shell').glob('*.py'))}
    (a.output/'bundle-manifest.json').write_text(json.dumps(record,indent=2)+'\n')
    print(a.output)


if __name__=='__main__':main()
