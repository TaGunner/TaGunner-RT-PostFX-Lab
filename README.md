# TaGunner RT + PostFX Lab v0.3

Standalone offline Android GPU lab for **iQOO 13** (and other ARM64 Android Vulkan devices).

## What the app tests
- **Vulkan driver scan:** GPU, API version, extensions, device features for `VK_KHR_ray_query`, `VK_KHR_acceleration_structure`, `VK_KHR_ray_tracing_pipeline`, buffer device address, and sample counts.
- **Active Ray Query device test:** If supported, calls `vkCreateDevice` with ray query and acceleration structure enabled and reports SUCCESS/FAILED.
- **GPU PostFX preview:** Live OpenGL ES 3.0 shader adjusting sharpen, vibrance, bloom and exposure on an image; choose a Roblox screenshot and compare before/after.
- **Copy report:** Copy the Vulkan result and paste into ChatGPT.

It **does not** inject into Roblox, bypass device checks, enable Roblox's Ray Tracing, or render ray-traced shadows yet. Even SUCCESS from `vkCreateDevice` demonstrates Vulkan feature support only, not a working ray-traced rendering pipeline.

## Download APK using only your phone
1. Open **[GitHub Actions](https://github.com/TaGunner/TaGunner-RT-PostFX-Lab/actions/workflows/build-android-apk.yml)**.
2. Open latest successful run, select artifact **TaGunner-RT-PostFX-debug** and download the ZIP.
3. Extract ZIP; install `app-debug.apk` on Android (debug signing).
4. Tap **Vulkan / Ray Tracing**, run probe, copy the output back into the chat.
5. Use **GPU PostFX** to select any image and experiment with the shader.

This project targets Android 8+ ARM64. Code and builds are experimental. No app data or screenshots are uploaded by this app.

## Build configuration
- Android Gradle Plugin 8.7.3, Gradle 8.9, Android SDK 35
- NDK 27.2.12479018, CMake 3.22.1, Java 17
- Main repo branch is `main`; a push triggers the APK build. You can also use **Run workflow**.

**Developer caution:** APK compilation and device runtime must be validated by GitHub Actions and physical Android respectively.
