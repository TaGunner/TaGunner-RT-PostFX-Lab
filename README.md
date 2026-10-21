# TaGunner RT + PostFX Lab v0.7

Standalone offline Android GPU lab for **iQOO 13** (and other ARM64 Android Vulkan devices).

## v0.7 Moving-light RT query test + 1080p
- Ray Query shader now accepts a frame index through a Vulkan push constant and rotates the directional light per GPU dispatch. The floor and cube are **static**; BLAS/TLAS are NOT dynamically rebuilt in this version.
- Two "Shadow A/B" preview buttons render the same objects with two different light angles to visually check **dynamic ray-traced shadows**.
- Add a 1920×1080×4 compute-pass benchmark.
- Benchmark timings measure GPU dispatches for moving light, not whole frames, app FPS, game FPS, continuous animation, or scene geometry updates.
- The benchmark is experimental and may heat the phone. Charge before using high resolution.

## v0.6 Vulkan Ray Query benchmark
- Adds two benchmark buttons: 480×300 × 16 dispatches, and 1280×720 × 8 dispatches.
- Measures GPU execution via Vulkan timestamp queries (when supported) and CPU submit + queue-wait time.
- Displays GPU milliseconds per dispatch and a **theoretical GPU-only** rate, NOT Roblox/game FPS or sustained temperature-tested FPS.
- Render uses the same BLAS/TLAS, static floor and cube, and primary/shadow rayQueryEXT shader as v0.5.
- Tests on a background thread and offers a copyable benchmark report.
- Benchmark repeatedly renders the SAME STATIC SCENE, with no camera/object animation, no frame presentation, no game CPU/physics; **DO NOT interpret the theoretical rate as actual game FPS**.
- Charge battery sufficiently, use cooling as needed, and stop if overheating. Test the 480x300 preset before 720p.

## v0.5 experimental hardware ray-traced scene
- New **เรนเดอร์ 3D ด้วย Hardware Ray Query** button creates a triangle/cube + floor BLAS, TLAS, compute pipeline and GPU storage buffer, then executes a `rayQueryEXT` shader for primary visibility and cast shadows.
- Displays the resulting Vulkan GPU image in the app and displays a detailed error if the device fails.
- The Vulkan SDK's `glslangValidator` compiles `shaders/rt_scene.comp` into an APK asset during GitHub Actions build.
- **Not yet device-tested**. Success must be verified from the actual rendered image, not from compile alone.
- Does NOT modify or enable Ray Tracing inside Roblox. Requires renderer/game integration as a separate project.

## Fixes introduced in v0.4
- Correct flipped Roblox screenshot preview in OpenGL ES.
- Respect Android 15/16 edge-to-edge status/navigation bars.
- Keep PostFX preview near the controls instead of leaving large vertical gaps.

## Device test evidence (iQOO 13, user supplied)
- Vulkan 1.3.284, Adreno 830; VK_KHR_ray_query and VK_KHR_acceleration_structure supported; vkCreateDevice succeeded.
- VK_KHR_ray_tracing_pipeline NOT supported by this driver. This does not block a compute-shader Ray Query implementation.
- Driver-reported maximum anisotropic filtering: 16x; Vulkan framebufferColorSampleCounts mask 7 corresponds to 1x/2x/4x.
- **Next milestone:** create BLAS/TLAS and dispatch a shader with rayQueryEXT, then independently confirm actual ray traversal on the device. This APK does not yet do that.

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
