#!/usr/bin/env python3
"""Check package extraction/relocation and optionally launch an isolated cook.

No package-manager installation or command to the user's normal app occurs.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import time
import uuid
import zlib


def capture(command,env=None):
    return subprocess.check_output([str(x) for x in command],text=True,stderr=subprocess.PIPE,env=env)


def main():
    p=argparse.ArgumentParser();p.add_argument('--stage',type=Path,required=True);p.add_argument('--artifacts',type=Path,required=True);p.add_argument('--launch',action='store_true');a=p.parse_args()
    stage=a.stage.resolve();out=a.artifacts.resolve();record={'schema_version':1,'package_manager_install_performed':False,'normal_user_app_touched':False}
    record['bundle_diagnostic']=json.loads(capture([stage/'mirmir','--diagnose']))
    env=os.environ.copy();env['QT_QPA_PLATFORM']='offscreen'
    record['private_qt_selftest']=capture([stage/'python/bin/python3',stage/'native-shell/mirmir_shell.py','--self-test'],env)
    for line in (out/'SHA256SUMS').read_text().splitlines():
        expected,name=line.split(None,1);name=name.strip()
        if Path(name).name!=name or hashlib.sha256((out/name).read_bytes()).hexdigest()!=expected:raise SystemExit('Artifact SHA256 mismatch')
    record['artifact_sha256_verified']=True
    source=json.loads((stage/'bundle-manifest.json').read_text());expected_binary=source['shipping_binary_sha256'];version=source['version']
    deb=out/'extracted-deb/opt/mirmir';record['deb_extracted_binary_sha256']=hashlib.sha256((deb/'runtime/Mirmir/Binaries/Linux/Mirmir-Linux-Shipping').read_bytes()).hexdigest()
    if record['deb_extracted_binary_sha256']!=expected_binary:raise SystemExit('Debian extracted Shipping mismatch')
    record['deb_relocated_diagnostic']=json.loads(capture([deb/'mirmir','--diagnose']))
    with tempfile.TemporaryDirectory(prefix='mirmir-extract-',dir=out.parent) as directory:
        target=Path(directory)
        capture(['bsdtar','-xf',out/f'mirmir-{version}-1.x86_64.rpm','-C',target])
        binary=target/'opt/mirmir/runtime/Mirmir/Binaries/Linux/Mirmir-Linux-Shipping'
        if hashlib.sha256(binary.read_bytes()).hexdigest()!=expected_binary:raise SystemExit('RPM extracted Shipping mismatch')
        record['rpm_extraction_verified']=True
        record['rpm_relocated_diagnostic']=json.loads(capture([target/'opt/mirmir/mirmir','--diagnose']))
    record['arch_metadata']=capture(['pacman','-Qip',out/f'mirmir-{version}-1-x86_64.pkg.tar.zst']) if shutil.which('pacman') else capture(['bsdtar','-xOf',out/f'mirmir-{version}-1-x86_64.pkg.tar.zst','.PKGINFO'])
    appenv=os.environ.copy();appenv['APPIMAGE_EXTRACT_AND_RUN']='1'
    record['appimage_extracted_diagnostic']=json.loads(capture([out/f'Mirmir-{version}-x86_64.AppImage','--diagnose'],appenv))
    record['actual_shipping_launch_verified']=False
    if a.launch:
        scratch=out.parent/('smoke-'+uuid.uuid4().hex);scratch.mkdir();frames=Path('/dev/shm')/('mirmir-package-'+uuid.uuid4().hex);frames.mkdir(mode=0o700)
        testenv=os.environ.copy();testenv['XDG_DATA_HOME']=str(scratch/'data');testenv['XDG_STATE_HOME']=str(scratch/'state')
        testenv['SDL_AUDIODRIVER']='dummy';testenv['SDL_AUDIO_DRIVER']='dummy'
        with (scratch/'shell.log').open('w') as log:
            child=subprocess.Popen([str(stage/'mirmir'),'--frame-dir',str(frames),'--no-cursor-interest'],env=testenv,stdout=log,stderr=subprocess.STDOUT)
            try:
                deadline=time.monotonic()+45;matched=None
                while time.monotonic()<deadline and child.poll() is None:
                    try:
                        metadata=json.loads((frames/'runtime.json').read_text());png=(frames/'frame.png').read_bytes();shell=json.loads((frames/'shell-status.json').read_text())
                        if (metadata.get('frame_seq',0)>=10 and metadata.get('ready') is True and metadata.get('running') is True
                                and shell.get('ready') is True and shell.get('engine_kind')=='shipping' and shell.get('test_mode') is False
                                and shell.get('session_id')==metadata.get('session_id')
                                and (tuple(map(int,version.split('.'))) < (0,4,0) or shell.get('props_ready') is True)
                                and len(png)==metadata.get('png_bytes') and zlib.crc32(png)&0xffffffff==metadata.get('png_crc32')):
                            matched=metadata;break
                    except (OSError,ValueError):pass
                    time.sleep(.15)
                if matched is None:raise RuntimeError('Isolated staged Shipping did not publish a matching native frame; see '+str(scratch))
                record['actual_shipping_launch_verified']=True
                record['desktop_window_map_verified']=None
                try:
                    from Xlib import display as xdisplay, X
                    connection=xdisplay.Display()
                    try:
                        window=connection.create_resource_object('window',shell['window_id'])
                        record['desktop_window_map_state']=window.get_attributes().map_state
                        record['desktop_window_map_verified']=record['desktop_window_map_state']==X.IsViewable
                    finally:
                        connection.close()
                except (ImportError,KeyError,OSError):
                    record['desktop_window_map_note']='Own X11 window map state was not measured.'
                if record['desktop_window_map_verified'] is False:
                    record['desktop_window_map_note']='Own window remained unmapped; repeat visibility verification in an unlocked session. No session/window-manager setting was changed.'
                record['isolated_runtime_sample']={k:matched.get(k) for k in ['producer_pid','session_id','frame_seq','width','height','png_crc32','appearance']}
                record['isolated_runtime_sample']['stance_valid_legs']=matched.get('motion',{}).get('stance_valid_legs')
                if tuple(map(int,version.split('.'))) >= (0,4,0):
                    props=matched.get('props',{})
                    name=props.get('frame_file')
                    if props.get('ready') is not True or not isinstance(name,str) or Path(name).name!=name:
                        raise RuntimeError('0.4 requires a ready fixed habitat capture.')
                    habitat=(frames/name).read_bytes()
                    if len(habitat)!=props.get('png_bytes') or zlib.crc32(habitat)!=props.get('png_crc32'):
                        raise RuntimeError('Habitat frame/metadata mismatch.')
                    record['habitat_crc_verified']=True
            finally:
                subprocess.run([str(stage/'mirmir'),'--stop','--frame-dir',str(frames)],env=testenv,stdout=log,stderr=subprocess.STDOUT,timeout=20,check=True)
                child.wait(timeout=20)
        shutil.rmtree(frames)
        record['isolated_stop_exit_code']=child.returncode
    record['other_distro_install_verified']=False
    (out/'verification.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n')
    manifest=out/'build-manifest.json';data=json.loads(manifest.read_text());data['staged_runtime_launch_verified']=record['actual_shipping_launch_verified'];data['extraction_and_relocation_verified']=True;data['verification_file']='verification.json';manifest.write_text(json.dumps(data,indent=2)+'\n')
    print(json.dumps({'diagnostic':True,'private_qt_selftest':True,'extraction_and_relocation':True,'actual_shipping_launch':record['actual_shipping_launch_verified'],'other_distro_install':False},indent=2))


if __name__=='__main__':main()
