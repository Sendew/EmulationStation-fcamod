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

## Attribution
This work builds on [christianhaitian/EmulationStation-fcamod](https://github.com/christianhaitian/EmulationStation-fcamod) and the upstream contributors acknowledged there. Review and comply with the applicable upstream licenses before redistributing binaries. No game ROMs, personal `gamelist.xml` files, credentials, or GLB assets are distributed.
