from pathlib import Path
import zipfile,hashlib,json
root=Path(__file__).resolve().parent
original=Path('C:/Users/ritis/Downloads/Sleeping-Dogs-v1.0.10-full-apkvision.apk')
target=root/'SleepingDogs-Mali-G615-experimental-unsigned.apk'
with zipfile.ZipFile(root/'manifestbuild.apk') as m:manifest=m.read('AndroidManifest.xml')
replacements={'AndroidManifest.xml':manifest,
              'classes2.dex':(root/'dex/classes.dex').read_bytes(),
              'lib/arm64-v8a/libgpudrv.so':(root/'libgpudrv.so').read_bytes(),
              'lib/arm64-v8a/libvulkan_panfrost.so':(root.parent/'panvk/libvulkan_panfrost.so').read_bytes()}
for p in (root/'patched-libs').glob('*.so'):replacements['lib/arm64-v8a/'+p.name]=p.read_bytes()
for p in (root.parent/'panvk/LICENSES').glob('*'):
    if p.is_file():replacements['assets/mali-driver-licenses/'+p.name]=p.read_bytes()
replacements['assets/mali-driver-licenses/NOTICE.md']=(root.parent/'panvk/NOTICE.md').read_bytes()
for p in (root.parent/'panvk/package-metadata').glob('*'):
    if p.is_file():replacements['assets/mali-driver-provenance/'+p.name]=p.read_bytes()
with zipfile.ZipFile(original) as source,zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as output:
    for info in source.infolist():
        if info.filename.startswith('META-INF/'):continue
        output.writestr(info,replacements.pop(info.filename,source.read(info.filename)))
    for name,data in replacements.items():output.writestr(name,data)
with zipfile.ZipFile(original) as source,zipfile.ZipFile(target) as output:
    assert output.testzip() is None
    assert source.read('classes.dex')==output.read('classes.dex')
    assert source.read('resources.arsc')==output.read('resources.arsc')
    for name in ['libAPKVISION.so','libstub.so','libhardware.so','libvulkan_freedreno.so']:
        assert source.read('lib/arm64-v8a/'+name)==output.read('lib/arm64-v8a/'+name)
    for info in output.infolist():
        if info.filename.startswith('lib/arm64-v8a/') and any(s in info.filename for s in ['libnfsmw','libdxvk']):
            assert b'libvulkan.so\0' not in output.read(info.filename)
            assert b'libvulkan.so.1\0' not in output.read(info.filename)
    # Native C++ sret function and full real-feature checks are preserved byte-for-byte.
    a=source.read('lib/arm64-v8a/libnfsmw.so');b=output.read('lib/arm64-v8a/libnfsmw.so')
    assert a[0x5e58228:0x5e593a8]==b[0x5e58228:0x5e593a8]
print(target, 'size',target.stat().st_size)
print('Original DEX/resources/protection libraries and GPU checks preserved; ZIP integrity passed.')
