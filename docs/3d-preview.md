# EmulationStation 3D Preview (experimental)

Work-in-progress extension of **christianhaitian/EmulationStation-fcamod**, tested on an R36T Max (aarch64, 720 × 720) running dArkOS4Clone. This fork retains the upstream project's existing README and attribution.

## Current features
- Textured glTF 2.0 `.glb` previews in the detailed game list
- CPU skin animation for the tested Link model, automatic rotation
- Orthographic camera and model origin as floor reference
- Per-game zoom through `model3dScale`
- 3D container aligned to the theme's `md_video` component where available

## Per-game metadata example
Add these elements **inside** the correct `<game>` element in your own `gamelist.xml`:

```xml
<model3d>./models/character.glb</model3d>
<model3dScale>2.0</model3dScale>
```

The path is resolved by EmulationStation, and zoom can be changed by restarting EmulationStation. Not every glTF feature or animation rig has been tested. GLB assets are not included in this repository.

## GLB textures and color compatibility (R36T Max testing)

The preview renderer intentionally supports only a subset of glTF 2.0. For the **current experimental build**, use **PNG images embedded inside the `.glb`** for predictable texture loading; external texture files and the `EXT_texture_webp` extension are not supported by the current texture lookup. PNG is a **compatibility recommendation for this renderer**, not a limitation of glTF itself.

### Case study: Super Mario 64

A Mario GLB initially rendered geometry but showed white eyes, moustache, hat emblem and other image-based details. Inspection found five textures with **no ordinary `textures[].source`**; instead each referenced an embedded `image/webp` via `textures[].extensions.EXT_texture_webp.source`. Converting all five WebP images to embedded PNG, setting the standard `textures[].source`, and removing the unused WebP extension restored the missing details. The original GLB was kept until the replacement was verified.

The remaining incorrect skin and clothing tones were traced separately to **color handling**:

- This GLB supplies `COLOR_0` on its primitives. Supporting vertex color attributes (including multiplication with `baseColorFactor`) is appropriate, but inspection showed **all vertex colors were white** for this Mario model; vertex colors were **not** the cause of its remaining color difference.
- Its `baseColorFactor` values are specified in **linear RGB**. On the tested fixed-function OpenGL ES rendering path, converting the final RGB color from linear to **sRGB** before submitting vertex colors corrected the visible appearance. Alpha must remain linear. Do not apply this conversion blindly on a shader-based pipeline that already handles color space correctly.
- Some textured Mario materials declare `alphaMode: BLEND`. The test implementation also enables blending for these materials; this is separate from decoding and locating PNG textures.

After the PNG conversion and local color-space fix, the Mario model displayed its image details and colors correctly on the R36T Max. The prebuilt beta archive/checksum documented below **predates these local fixes**; it should not be described as containing them. **The current GitHub source may also lag behind the tested local changes until a separate source commit is pushed.**

### Troubleshooting models that load but look wrong

- **White/missing facial details:** inspect `textures[].source`, `EXT_texture_webp`, `images[].mimeType`, and the material's `baseColorTexture.index`; use embedded PNG for this build.
- **Wrong colors after textures appear:** inspect `baseColorFactor`, `COLOR_0`, and linear/sRGB handling; PNG conversion alone does not fix color-space errors.
- **Transparent detail issues:** check material `alphaMode` and renderer blending. Full glTF transparency and render ordering have not been validated.
- **Scale issues:** adjust `<model3dScale>` for each game; the value appropriate for Link was too large for Mario.

These findings come from the local **Mario GLB test** and do not imply complete glTF 2.0 compliance.

## Tested build environment

The first ARM64 binary was built and run on an **R36T Max** using **dArkOS4Clone with Debian GNU/Linux 13.6 (Trixie)**, on a **720 × 720** display. It is an **AArch64 dynamically linked ELF** and uses SDL2, EGL/OpenGL ES, FreeImage, FreeType, SDL2_mixer, libcurl, VLC and ALSA, along with their transitive libraries. On the tested device, `ldd` reported no unresolved shared libraries. This does **not** guarantee ABI or graphics-driver compatibility on other dArkOS, ArkOS or ARM64 images.

The locally prepared, **not yet publicly released** archive is named `emulationstation-3d-r36tmax-arm64.tar.gz` with SHA-256 `6c31ef047feda893bbf354ee564c7f8784400e807187777d3eae247ba5a95213`. This checksum applies only to that exact test archive; do not assume a later Release asset has the same checksum.

## Build
From a clone of the `3d-preview` branch, with the upstream dependencies and submodules installed:

```bash
cmake -S . -B build-3d -DGLES=ON -DGL=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-3d -j1
```

For the tested system this produces the `emulationstation` executable in the source root. Build success does **not** establish compatibility with other handhelds or firmware versions.

## Install / remove (experimental)
There is **no public precompiled release yet**. The scripts currently require a compiled local executable:

```bash
sudo bash scripts/3d-preview/install.sh /path/to/compiled/emulationstation
sudo bash scripts/3d-preview/update.sh /path/to/newly/compiled/emulationstation
sudo bash scripts/3d-preview/uninstall.sh
```

The installer refuses to overwrite an existing experimental 3D setup, creates a separate launcher and systemd override, and keeps the original binary untouched. Before using it, back up your system. **Do not run the installer over the author's existing manual test installation**, which already has `emulationstation-3d` and `3d-preview.conf`; that setup requires a separate migration step.

Scripts have not yet been validated on a clean device. Do not treat this as a general-purpose dArkOS update. A tested packaged Release and compatibility checks will follow.

## What is the .tar.gz file?

`emulationstation-3d-r36tmax-arm64.tar.gz` is a compressed archive (similar to a ZIP). It contains **only one file**, the modified ARM64 executable named `emulationstation-3d`. It is **not** a ROM, firmware image, or SD-card image. Do not flash it, and do not extract it into the ROMs directory.

## Automatic installation (prepared, NOT YET released)

The new `scripts/3d-preview/install-release.sh` downloads the exact `v0.1.0-beta` archive from GitHub Releases, verifies its pinned SHA-256, extracts it temporarily, and invokes `install.sh`. The original EmulationStation executable remains untouched. If startup fails, the installer attempts to restore the original service.

**Do not execute this on the developer's existing R36T Max.** That console already has a manual 3D installation, and this installer intentionally refuses to overwrite it.

Once the **Release is public and tested on a clean compatible device**, the intended commands are:

```bash
curl -fL https://raw.githubusercontent.com/Sendew/EmulationStation-fcamod/3d-preview/scripts/3d-preview/install-release.sh -o install-release.sh
# Review the downloaded script before running it.
sudo bash install-release.sh
```

Until the Release is public, the download will fail safely. These beta installers have **not** passed an end-to-end install/uninstall test. They are not an official dArkOS update.
## Manual beta installation (NOT YET TESTED ON A CLEAN DEVICE)

**Prerequisites:** a compatible R36T Max running the same dArkOS4Clone / Debian 13 ARM64 environment, SSH access, a system backup, the release archive downloaded to a Windows PC, and the `3d-preview` installer scripts. The installer will **refuse** an existing manual 3D installation; **do not run it on the development R36T Max** until a migration procedure is ready.

1. Transfer the archive to the handheld from Windows PowerShell (adapt the IP):

   ```powershell
   scp "$env:USERPROFILE\\Downloads\\emulationstation-3d-r36tmax-arm64.tar.gz" ark@192.168.1.97:/home/ark/
   ```

2. Connect by SSH and validate the archive. The SHA below corresponds **only** to the original beta test archive:

   ```bash
   cd /home/ark
   echo '6c31ef047feda893bbf354ee564c7f8784400e807187777d3eae247ba5a95213  emulationstation-3d-r36tmax-arm64.tar.gz' | sha256sum -c -
   mkdir -p /home/ark/es3d-install
   tar -xzf emulationstation-3d-r36tmax-arm64.tar.gz -C /home/ark/es3d-install
   ```

3. Download the scripts from the project repository, then install using the **extracted executable**, not the `.tar.gz` file:

   ```bash
   git clone -b 3d-preview --depth 1 https://github.com/Sendew/EmulationStation-fcamod.git /home/ark/es3d-installer
   sudo bash /home/ark/es3d-installer/scripts/3d-preview/install.sh /home/ark/es3d-install/emulationstation-3d
   ```

4. To uninstall **only if the mod was installed by that script**:

   ```bash
   sudo bash /home/ark/es3d-installer/scripts/3d-preview/uninstall.sh
   ```

These steps are documentation for a future controlled test, **not a verified one-command installer**. The current scripts have only passed Bash syntax checks, not a complete install/uninstall/rollback test. The mod does not include copyrighted game assets or GLB models.

## Attribution
This work builds on [christianhaitian/EmulationStation-fcamod](https://github.com/christianhaitian/EmulationStation-fcamod) and the upstream contributors acknowledged there. Review and comply with the applicable upstream licenses before redistributing binaries. No game ROMs, personal `gamelist.xml` files, credentials, or GLB assets are distributed.
