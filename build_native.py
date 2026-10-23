import os,subprocess,pathlib
root=pathlib.Path(__file__).resolve().parent
zig=root.parent/'compiler/ziglang/zig.exe'
stubs=root/'link-stubs';stubs.mkdir(exist_ok=True)
env=os.environ.copy();env['ZIG_GLOBAL_CACHE_DIR']=str(root/'zig-cache');env['ZIG_LOCAL_CACHE_DIR']=str(root/'zig-local-cache')
common=[str(zig),'cc','-target','aarch64-linux-android','-nostdlib','-shared','-fPIC','-fno-stack-protector','-ffreestanding','-O2']
libraries={'libc.so':['setenv','snprintf','vsnprintf','strcmp','strlen','memset','malloc','free','open','close','write','getuid','__errno'], 'libdl.so':['dlopen','dlsym','dlerror'], 'liblog.so':['__android_log_print']}
for library,symbols in libraries.items():
    source=stubs/(library+'.c');source.write_text('\n'.join('void '+name+'(void) {}' for name in symbols))
    subprocess.run(common+[str(source),'-Wl,-soname,'+library,'-o',str(stubs/library)],env=env,check=True)
subprocess.run(common+[str(root/'gpudrv.c'),'-I'+str(root/'headers'),'-L'+str(stubs),'-Wl,--no-as-needed',str(stubs/'libc.so'),str(stubs/'libdl.so'),str(stubs/'liblog.so'),'-Wl,-soname,libgpudrv.so','-Wl,-z,max-page-size=16384','-Wl,-z,defs','-o',str(root/'libgpudrv.so')],env=env,check=True)
print('Built',root/'libgpudrv.so')
