# Sleeping Dogs — Mali G615 experimental test build

This is a compatibility experiment for your Oppo Reno 14 (12 GB RAM). It is **not verified fully playable**. No phone or emulator was available for runtime testing.

## Install and test

1. Back up saves before changing installations. This test APK uses a different signing key from the original APK, so Android may refuse to install it over the original. Do not remove the original until your saves are backed up. Keep the original APK for rollback.
2. Copy `SleepingDogs-Mali-G615-TEST.apk` to your phone and install it. It retains package name `com.sdre.sleepingdogs` and requires Android 10 or newer.
3. Open **Sleeping Dogs Mali Test**. The experimental **PanVK RC3** driver is already bundled; no separate driver installation is required.
4. Leave **Bundled PanVK RC3** selected and tap **Check selected driver**. Wait for the report. The test loads the driver, enumerates the GPU, reads real Vulkan feature support, creates a device and attempts a GPU transfer/readback.
5. If the report has no errors and shows `VK_EXT_robustness2: YES`, `shaderClipDistance: YES` and `GPU transfer/readback smoke test: PASS`, try **Start game**. Your original game files must still be in `Internal storage/SleepingDogs`. The APK does not include or download those game files.
6. Use **Share diagnostics** to send the report back to this chat, and describe what happens after starting the game. If sharing is unavailable, select/copy the text or send a screenshot.

If a check crashes, reopen the launcher: it includes the last saved native diagnostic stage. If it hangs or requests a restart, **force-stop** the app in Android Settings, then reopen it. Before changing drivers after game startup, force-stop the app as well.

The **Phone system Vulkan driver** option is available for comparison. Your stock driver already reported missing robustness2 and clip-distance support, so that option is expected to retain the original incompatibility.

## What changed

- Added a driver-selector launcher and local diagnostics/sharing.
- Bundled the Android ARM64 PanVK-kbase RC3 driver, verified against upstream SHA-256.
- Redirected the game and both packaged DXVK versions through an app-local driver bridge, including their alternate library-loading paths.
- Replaced the bundled Turnip-specific dispatch entry with a direct bridge, so PanVK uses its own Vulkan presentation implementation.
- Preserved the original game DEX, resources, protection libraries and real GPU capability-check code. The checks have not been disabled or made to report fake features.

This does not replace your phone's system driver or require root. Whether Oppo permits this driver to access `/dev/mali0`, and whether its kernel interfaces match, must be established on the phone. A successful diagnostic probe still does not prove working surface presentation, game rendering, textures, input, performance or stability.

## Driver source

[PanVK-kbase Android RC3 release](https://github.com/zenithblue-oss/panvk-kbase-android/releases/tag/panvk-kbase-v0.1.0-rc3).

The upstream reference device is a Poco X6 Pro with Mali-G615 MC6. That result does not verify Reno 14 compatibility. Driver source/validation metadata and license notices are included in the APK and source archive.

The next step is your device test. Send the diagnostics and any new error exactly as displayed; further fixes should follow that evidence.
