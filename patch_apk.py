from pathlib import Path
import sys,struct,json,hashlib,zipfile,xml.etree.ElementTree as ET,shutil
root=Path(__file__).resolve().parent
sys.path.insert(0,str(root.parent/'python-libs'))
from elftools.elf.elffile import ELFFile
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
apk=Path('C:/Users/ritis/Downloads/Sleeping-Dogs-v1.0.10-full-apkvision.apk')
original_hash='ac630d904182326f85f2bd15b7a5a8abdf42cd95967be09540ebc40d18e33ed0'
assert hashlib.sha256(apk.read_bytes()).hexdigest()==original_hash
assert hashlib.sha256((root.parent/'panvk/libvulkan_panfrost.so').read_bytes()).hexdigest()=='b46b9f0416f207a05482349dffb37b6f3d84c20762151c623774831ce3939445'
decoded=root.parent/'sleeping-dogs/decoded'
android='http://schemas.android.com/apk/res/android';ET.register_namespace('android',android)
manifest=ET.parse(decoded/'AndroidManifest.xml');application=manifest.getroot().find('application')
game=next(a for a in application.findall('activity') if a.get('{'+android+'}name')=='com.mod.MainActivity')
for intent in list(game.findall('intent-filter')):game.remove(intent)
game.set('{'+android+'}exported','false')
name='com.sdre.mali.MaliLauncherActivity'
for a in list(application.findall('activity')):
    if a.get('{'+android+'}name')==name:application.remove(a)
launcher=ET.SubElement(application,'activity',{
    '{'+android+'}name':name,'{'+android+'}exported':'true',
    '{'+android+'}label':'Sleeping Dogs Mali Test',
    '{'+android+'}configChanges':'keyboardHidden|orientation|screenSize',
    '{'+android+'}theme':'@android:style/Theme.Holo.NoActionBar'})
intent=ET.SubElement(launcher,'intent-filter')
ET.SubElement(intent,'action',{'{'+android+'}name':'android.intent.action.MAIN'})
ET.SubElement(intent,'category',{'{'+android+'}name':'android.intent.category.LAUNCHER'})
manifest.write(decoded/'AndroidManifest.xml',encoding='utf-8',xml_declaration=True)

with (root/'driver_thunk.o').open('rb') as f:
    e=ELFFile(f);thunk=bytearray(e.get_section_by_name('.text').data())
    symbols={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
base=0x5e5c59c
for label,target in [('bridge_dlopen',0x5fe9730),('bridge_dlsym',0x5fe9740)]:
    pos=symbols[label];delta=target-(base+pos);assert delta%4==0 and -(1<<27)<=delta<(1<<27)
    struct.pack_into('<I',thunk,pos,0x94000000|((delta//4)&0x3ffffff))
assert len(thunk)<=488
cs=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN)
(root/'thunk-disassembly.txt').write_text('\n'.join(f'{i.address:x}: {i.mnemonic} {i.op_str}' for i in cs.disasm(thunk[:symbols['bridge_library']],base)))
patched=root/'patched-libs';patched.mkdir(exist_ok=True)
ledger=[]
with zipfile.ZipFile(apk) as z:
    for name in ['libnfsmw.so','libdxvk_d3d11.so','libdxvk_dxgi.so','libdxvk2_d3d11.so','libdxvk2_dxgi.so']:
        data=z.read('lib/arm64-v8a/'+name);before=hashlib.sha256(data).hexdigest()
        changes=[]
        for old,new in [(b'libvulkan.so\0',b'libgpudrv.so\0'),(b'libvulkan.so.1\0',b'libgpudrv.so\0\0\0')]:
            assert len(old)==len(new)
            start=0
            while (pos:=data.find(old,start))>=0:
                changes.append({'offset':hex(pos),'original':old.decode().rstrip('\0'),'replacement':'libgpudrv.so'})
                data=data[:pos]+new+data[pos+len(old):];start=pos+len(new)
        if name=='libnfsmw.so':
            # This build maps this .text RVA directly to the same file offset; verify against ELF.
            from io import BytesIO
            elf=ELFFile(BytesIO(data));offset=None
            for s in elf.iter_segments():
                if s['p_type']=='PT_LOAD' and s['p_vaddr']<=base<s['p_vaddr']+s['p_filesz']:offset=s['p_offset']+base-s['p_vaddr']
            assert offset is not None
            assert data[offset:offset+16].hex()=='ff4301d1fd7b01a9f71300f9f65703a9'
            data=data[:offset]+bytes(thunk)+b'\0'*(488-len(thunk))+data[offset+488:]
            changes.append({'offset':hex(offset),'rva':hex(base),'length':488,'replacement':'checked bridge thunk; no feature-check bypass'})
        assert len(data)==len(z.read('lib/arm64-v8a/'+name))
        (patched/name).write_bytes(data)
        ledger.append({'file':name,'original_sha256':before,'patched_sha256':hashlib.sha256(data).hexdigest(),'changes':changes})
(root/'patch-ledger.json').write_text(json.dumps({'original_apk_sha256':original_hash,'libraries':ledger},indent=2))
print('Prepared manifest and native patches. Original DEX and other content will be preserved.')
