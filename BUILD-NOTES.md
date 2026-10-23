# Experimental Mali compatibility patch

The patch targets exactly the user-supplied `Sleeping-Dogs-v1.0.10-full-apkvision.apk`, SHA-256 `ac630d904182326f85f2bd15b7a5a8abdf42cd95967be09540ebc40d18e33ed0`. It is not a universal patch for other APK versions.

This source package contains the newly authored launcher/driver bridge and exact patch ledger. The original APK and copyrighted PC game files are not included in this source archive. The test APK is for the user-supplied installation.

## Findings and limits

- REA archive inventory: parent evidence `ev_72114efe6342d46e0cdbfbf8872ff4d8da04f9def8db8d784caecc77972e566c`; original archive hash above; see `rea-evidence.json`.
- REA Android decompilation was unavailable because its separate JADX MCP provider was not configured. Independent local Apktool/JADX/ELF/Capstone analysis was used. No claim of runtime execution is made.
- The original APK has ARM64 native game/recompiler code in `libnfsmw.so`, two DXVK pairs, and an Adreno Turnip driver. Game text references the PC Definitive Edition `sdhdship.exe` and external game folder. The APK by itself does not contain the full PC game.
- Native GPU-check function `_Z8CheckGpuPv` at RVA `0x5e58228`, size `0x1180`, queries actual device features. Its instructions remain unchanged. The original user-reported stock-driver failures were `VK_EXT_robustness2` and `shaderClipDistance`.
- The original `nfsmw_vkGetInstanceProcAddr` at RVA `0x5e5c59c`, size 488, used Turnip HAL and custom WSI. Its instance wrapper strips Android surface extensions. Direct PanVK dispatch avoids retaining that wrapper.
- Exact original `libvulkan.so` strings in five DSOs, and `libvulkan.so.1` in four DXVK DSOs, are replaced with an equal-length app-local bridge name. The bridge's own real system-driver load is unchanged. No Vulkan `DT_NEEDED` imports exist in the original DSOs.
- A tiny AArch64 thunk preserves arguments, callee-saved registers and stack alignment, forwards to the bridge through the original dlopen/dlsym PLT entries, and returns NULL on lookup failure.
- Selection happens before native game startup. After game dispatch begins, switching backends is rejected. Incomplete device dispatch and unknown completion of submitted probe work request a force-stop rather than permitting a retry with live work.
- Device test remains outstanding: Oppo Reno14 GPU node access, kbase/CSF compatibility, Android WSI, actual game rendering, compressed textures, input, performance and stability. Neither a compiled APK nor upstream testing on a different G615 device establishes these.

## Dependencies used

- Java JDK 17; Eclipse ECJ 3.43.0 for clean launcher compilation.
- Apktool 2.12.1; JADX CLI 1.5.6 for inspection.
- Android 4.1.1.4 API compile stubs (only old platform APIs used); actual APK minSDK remains 29, targetSDK 35.
- D8/R8 9.6.0-alpha07 for added DEX, with JDK 17 and Android API library inputs.
- Zig 0.13.0 Clang/LLD targeting `aarch64-linux-android`; freestanding C ABI declarations plus Android/Bionic link stubs. **The link stubs are not shipped in the APK.** Runtime dependencies are real `libc.so`, `libdl.so`, `liblog.so`.
- Vulkan-Headers v1.3.296. JNI C ABI header from the installed JDK with the unused stdio include removed; corresponding header notices retained.
- pyelftools 0.33 and Capstone 5.0.9 for ELF verification and thunk disassembly.
- uber-apk-signer 1.3.0 / zipalign 33.0.2, test/debug certificate. This certificate differs from the original publisher's.

## Driver provenance

The bundled library is upstream **PanVK-kbase RC3 Android/Bionic AArch64**, not the glibc variant. Release: https://github.com/zenithblue-oss/panvk-kbase-android/releases/tag/panvk-kbase-v0.1.0-rc3

SHA-256 `b46b9f0416f207a05482349dffb37b6f3d84c20762151c623774831ce3939445`; upstream Mesa commit `5a07217f034b3e50d8c7c7794f97a2df1742613b`. Both the standalone release asset and packaged driver agree on this digest. See included upstream `SOURCE.json`, `VALIDATION.json`, runtime feature matrix, license notices and release metadata. Their reported tests describe upstream's reference devices, not this APK or Reno14.

## Build sequence in this workspace

Paths in the Python scripts are tied to this workspace and original APK; adjust them for a separate checkout. Download the pinned tools into `work/android-tools`, the Zig package into `work/compiler`, and ELF Python dependencies into `work/python-libs`. Put the verified driver and metadata in `work/panvk`. Decode the original into `work/sleeping-dogs/decoded` using Apktool first.

1. `python work/mali-port/build_native.py`
2. Compile `driver_thunk.S` with Zig Clang for `aarch64-linux-android` to `driver_thunk.o`.
3. Compile Java sources using ECJ with `android.jar`, package classes to `ui.jar`, then run D8 with min-api 29 and both Android/JDK libraries to `dex/classes.dex`.
4. `python work/mali-port/patch_apk.py`; it checks the input APK/driver hashes and original function bytes before patching.
5. Use Apktool to build the modified manifest into `manifestbuild.apk`.
6. `python work/mali-port/assemble_apk.py`; it uses the original archive entries, new binary manifest, added DEX/driver/bridge and patched five DSOs. Original `classes.dex` and resource table are copied byte-for-byte.
7. Align and sign with the same test keystore for later test updates. Inspect `signature-verification-final.txt` for the provided APK's verification result.

Only the final signed APK is installable. The build has not been installed or executed on Android in this session.
