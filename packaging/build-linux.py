#!/usr/bin/env python3
"""Produce Linux distro/portable artifacts from an existing Shipping cook."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import platform
import re
import subprocess
import time
import urllib.request

ROOT=Path(__file__).resolve().parents[1]


def run(*command,**kwargs):subprocess.run([str(x) for x in command],check=True,**kwargs)


def finalize_artifacts(stage, artifacts, version):
    expected = [f'Mirmir-{version}-linux-x86_64.tar.zst',
                f'Mirmir-{version}-x86_64.AppImage',
                f'mirmir-{version}-1-x86_64.pkg.tar.zst',
                f'mirmir-{version}-1.x86_64.rpm',
                f'mirmir_{version}-1_amd64.deb']
    files = [artifacts / name for name in expected]
    if any(not file.is_file() or file.stat().st_size == 0 for file in files):
        raise SystemExit('All five nonempty Linux artifacts are required.')
    if not (artifacts / 'deb-metadata.txt').is_file() or not (artifacts / 'rpm-metadata.txt').is_file():
        raise SystemExit('Package metadata checks are missing.')
    records = [{'file':file.name,'bytes':file.stat().st_size,
                'sha256':hashlib.file_digest(file.open('rb'), 'sha256').hexdigest()}
               for file in files]
    (artifacts/'SHA256SUMS').write_text(''.join(x['sha256']+'  '+x['file']+'\n' for x in records))
    staged = json.loads((stage/'bundle-manifest.json').read_text())
    manifest = {'schema_version':1,'version':version,'host':platform.platform(),
                'stage_relative':'../Mirmir','files':records,
                'shipping_binary_sha256':staged['shipping_binary_sha256'],
                'docker_image':subprocess.check_output(['docker','image','inspect','mirmir-packaging:bookworm','--format','{{.Id}}'],text=True).strip(),
                'runtime_lock_sha256':hashlib.sha256((ROOT/'packaging/runtime-lock.json').read_bytes()).hexdigest(),
                'package_metadata_verified':True,'staged_runtime_launch_verified':False,
                'other_distro_install_verified':False,'engine_sdk_included':False,
                'windows_installer_built':False}
    (artifacts/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')


def main():
    p=argparse.ArgumentParser();p.add_argument('--version',default=(ROOT/'packaging/VERSION').read_text().strip());p.add_argument('--runtime',type=Path,default=ROOT/'.local/packaging/runtime');p.add_argument('--output',type=Path);p.add_argument('--stage-only',action='store_true');a=p.parse_args()
    if not re.fullmatch(r'\d+\.\d+\.\d+',a.version):raise SystemExit('Version must be MAJOR.MINOR.PATCH.')
    output=a.output or ROOT/'.local/dist/installers'/('build-'+a.version+'-'+time.strftime('%Y%m%dT%H%M%SZ',time.gmtime()))
    output=output.resolve();output.mkdir(parents=True,exist_ok=False);stage=output/'Mirmir'
    run('python',ROOT/'packaging/stage-linux.py','--output',stage,'--runtime',a.runtime,'--version',a.version)
    if a.stage_only:print(stage);return
    cache=ROOT/'.local/packaging/cache';cache.mkdir(parents=True,exist_ok=True)
    lock=json.loads((ROOT/'packaging/runtime-lock.json').read_text());tool=lock['appimage_runtime'];runtime=cache/'appimage-runtime'
    if not runtime.exists():urllib.request.urlretrieve(tool['url'],runtime)
    if hashlib.sha256(runtime.read_bytes()).hexdigest()!=tool['sha256']:raise SystemExit('AppImage runtime SHA mismatch')
    run('docker','build','-t','mirmir-packaging:bookworm','-f',ROOT/'packaging/linux/Dockerfile',ROOT/'packaging/linux')
    artifacts=output/'artifacts';artifacts.mkdir()
    run('docker','run','--rm','--user',f'{os.getuid()}:{os.getgid()}','--cpus','2',
        '-e','VERSION='+a.version,'-v',f'{stage}:/stage:ro','-v',f'{ROOT / "packaging"}:/recipes:ro',
        '-v',f'{cache}:/tools:ro','-v',f'{artifacts}:/out','mirmir-packaging:bookworm','sh','/recipes/linux/build-in-container.sh')
    finalize_artifacts(stage,artifacts,a.version);print(artifacts)


if __name__=='__main__':main()
