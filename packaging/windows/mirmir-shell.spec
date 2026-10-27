# Run on Windows with the pinned packaging/requirements-windows.txt environment.
from pathlib import Path
root=Path(SPECPATH).resolve().parents[1]
a=Analysis([str(root/'native-shell/mirmir_shell.py')],pathex=[str(root/'native-shell')],
           binaries=[],datas=[],hiddenimports=['numpy'],hookspath=[],hooksconfig={},
           runtime_hooks=[],excludes=[],noarchive=False)
pyz=PYZ(a.pure)
exe=EXE(pyz,a.scripts,[],exclude_binaries=True,name='MirmirShell',debug=False,
        bootloader_ignore_signals=False,strip=False,upx=False,console=False)
coll=COLLECT(exe,a.binaries,a.datas,strip=False,upx=False,name='MirmirShell')
