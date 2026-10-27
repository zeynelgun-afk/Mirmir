#!/usr/bin/env python3
"""Fetch SHA-pinned portable CPython/wheels; never install into the host OS."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import tarfile
import urllib.request

ROOT=Path(__file__).resolve().parents[1]


def fetch(item,cache):
    path=cache/item['filename']
    if not path.exists():
        temp=path.with_suffix(path.suffix+'.part')
        urllib.request.urlretrieve(item['url'],temp);temp.replace(path)
    if hashlib.sha256(path.read_bytes()).hexdigest()!=item['sha256']:
        raise SystemExit('Download SHA mismatch: '+str(path))
    return path


def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=ROOT/'.local/packaging/runtime');a=p.parse_args()
    lock=json.loads((ROOT/'packaging/runtime-lock.json').read_text());cache=ROOT/'.local/packaging/cache';cache.mkdir(parents=True,exist_ok=True)
    marker=a.output/'mirmir-runtime.json'
    if marker.exists():
        if json.loads(marker.read_text())!=lock:raise SystemExit('Runtime lock changed; use a fresh output directory.')
        print(a.output);return
    a.output.mkdir(parents=True,exist_ok=True)
    if not (a.output/'python/bin/python3').exists():
        with tarfile.open(fetch(lock['python'],cache)) as archive:
            # install_only archives contain relative internal interpreter symlinks.
            archive.extractall(a.output,filter='data')
    wheels=[fetch(item,cache) for item in lock['wheels']]
    subprocess.run([str(a.output/'python/bin/python3'),'-m','pip','install','--disable-pip-version-check','--no-index','--no-deps','--no-compile',*map(str,wheels)],check=True)
    marker.write_text(json.dumps(lock,indent=2)+'\n');print(a.output)


if __name__=='__main__':main()
