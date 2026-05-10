# -*- mode: python ; coding: utf-8 -*-
from PyInstaller.utils.hooks import collect_all, collect_data_files, collect_submodules

# Core packages that need full collection
packages = ['rich', 'websockets', 'zeroconf', 'msgpack', 'pynvml', 'accelerate']
datas = [('main.cu', '.'), ('include', 'include'), ('src', 'src')]
binaries = []
hiddenimports = []

for pkg in packages:
    tmp_ret = collect_all(pkg)
    datas += tmp_ret[0]
    binaries += tmp_ret[1]
    hiddenimports += tmp_ret[2]

# Targeted collection for heavy weights to avoid redundant analysis
datas += collect_data_files('torch')
datas += collect_data_files('transformers')
hiddenimports += collect_submodules('torch')
hiddenimports += collect_submodules('transformers')

# Aggressive exclusion of unused heavy dependencies
excluded_modules = [
    'tensorflow', 'tensorboard', 'keras', 'matplotlib', 'pandas', 'scipy', 
    'sklearn', 'grpc', 'boto3', 'botocore', 'IPython', 'PIL', 'cv2',
    'openvino', 'onnxruntime', 'tcl', 'tk', 'tkinter', 'notebook', 'jedi'
]

a = Analysis(
    ['UniAccelHost.py'],
    pathex=[],
    binaries=binaries,
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=excluded_modules,
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='UniAccelHost',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False, # UPX is very slow for large ML binaries
    console=True,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)

coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=False,
    upx_exclude=[],
    name='UniAccelHost',
)
