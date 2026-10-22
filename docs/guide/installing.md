# Installing SculptForge

This chapter covers what SculptForge needs, which package to download for your system, what to expect the
first time you open it, and how to build it from source.

## System requirements

- **Operating system**: Windows 10 or 11 (x64), macOS 11 or later (Apple Silicon or Intel), or a 64-bit Linux
  distribution (x86_64 or ARM64) with X11 or Wayland.
- **Graphics**: a GPU and driver that support **OpenGL 3.3** (core profile). Any graphics card from the last
  ten years qualifies, including integrated Intel, AMD and Apple GPUs. SculptForge asks for 4x multisampling
  and falls back to none if the driver refuses it.
- **Input**: a three-button mouse works for everything. A pen tablet adds pressure (see [Sculpting and
  brushes](sculpting.md)).
- **Network**: not needed for sculpting. The free AI features (Brush Lab and image or text to 3D) and the
  community brush gallery need an internet connection unless you run models on your own machine.

> **Note:** SculptForge is a 64-bit desktop application. There is no web, iPad or Android version.

## Download

Download the latest release from the [releases
page](https://github.com/islandmed/SculptForge/releases/latest). Pick the file for your system:

| System | File | How to install |
|---|---|---|
| Windows 10/11 (x64) | `SculptForge-<version>-windows-x86_64.exe` | Run the installer (adds a Start menu shortcut) |
| Windows, portable | `SculptForge-<version>-windows-x86_64.zip` | Unzip anywhere and run `bin\sculptforge.exe` |
| macOS 11+ (Apple Silicon and Intel) | `SculptForge-<version>-darwin-universal.dmg` | Open the disk image and drag SculptForge to Applications |
| Ubuntu, Debian, Mint, Pop!_OS | `sculptforge_<version>_amd64.deb` or `..._arm64.deb` | `sudo apt install ./sculptforge_*.deb` |
| Fedora, RHEL, Rocky, Alma 8+ | `sculptforge-<version>...x86_64.rpm` or `...aarch64.rpm` | `sudo dnf install ./sculptforge-*.rpm` |
| Any Linux | `SculptForge-<version>-linux-x86_64.AppImage` or `...-arm64.AppImage` | `chmod +x` the file, then run it |
| Arch Linux | `packaging/arch/PKGBUILD` in the source | `makepkg -si` in that folder (an AUR package is planned) |
| Studio pipelines | `SculptForge-<version>-...-vfx2026.tar.gz` or `...-vfx2027.tar.gz` | See [SculptForge for studios](studios.md) |

Every release also has a `SHA256SUMS.txt` file with the checksum of each download, so you can confirm a file
arrived intact.

Each package contains two programs: **SculptForge**, the desktop application, and **sculptforge-cli**, the
headless command-line tool for scripts, build farms and AI agents (see [Scripting and
automation](scripting.md)). On macOS the command-line tool sits inside the app bundle, at
`SculptForge.app/Contents/MacOS/sculptforge-cli`.

## Opening SculptForge for the first time

The builds are not code-signed yet, so your operating system may warn you the first time.

### macOS: Gatekeeper

macOS may say that SculptForge "cannot be opened because the developer cannot be verified".

1. In Finder, open the Applications folder.
2. Right-click (or <kbd>Ctrl</kbd>+click) **SculptForge** and choose **Open**.
3. Click **Open** in the dialog that appears.

You only need to do this once. On recent macOS versions you may instead have to go to **System Settings >
Privacy & Security** and click **Open Anyway** next to the message about SculptForge.

### Windows: SmartScreen

Windows may show "Windows protected your PC".

1. Click **More info**.
2. Click **Run anyway**.

### Linux

The `.deb` and `.rpm` packages add SculptForge to your desktop's application menu. An AppImage needs to be
made executable first (`chmod +x SculptForge-*.AppImage`, or tick "Allow executing file as program" in your
file manager's properties).

## What you see at start-up

SculptForge opens a 1600 x 960 window with a sphere ready to sculpt and the **LightBox** open along the top of
the canvas on its **Project** tab, so you can reopen recent work with a double-click. Press <kbd>,</kbd>
(comma) to close it, or turn off **Preferences > LightBox > Show at startup**. The first start also creates
your library folder (**Documents/SculptForge**, with Projects, Tools, Brushes, Alphas, Images, Splats and
QuickSave sub-folders), which the LightBox shows as **User**.

You can open a file directly from the command line, too: `sculptforge model.obj` (or a `.glb`, `.sfproj`,
splat capture or image). `sculptforge --fullscreen` starts filling the screen; `sculptforge --help` lists
every option.

The next chapter, [The interface](interface.md), explains what everything on screen does.

## Updating and uninstalling

SculptForge doesn't update itself. To update, download the new release and install it over the old one. Your
settings, brushes and library are kept: they live in your user folders, not next to the program (see
[Troubleshooting and FAQ](troubleshooting.md) for where).

To uninstall, use your system's usual method (Settings > Apps on Windows, dragging the app to the Bin on
macOS, `sudo apt remove sculptforge` or `sudo dnf remove sculptforge` on Linux, or delete the AppImage).
Delete the settings and library folders as well if you want to remove everything.

## Building from source

You need CMake 3.24 or later and a C++20 compiler: GCC 11+, Clang 14+, Xcode 15+ or Visual Studio 2022. Dear
ImGui, GLM and (if it isn't installed already) SDL3 are downloaded automatically. AI features use libcurl on
Linux and macOS and the built-in WinHTTP on Windows.

```sh
git clone https://github.com/islandmed/SculptForge.git
cd SculptForge

# Linux only: compilers and X11/Wayland/GL/curl headers (Debian/Ubuntu or Fedora/RHEL)
sudo ./packaging/linux/install-build-deps.sh

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/sculptforge
```

On Windows the program is `build\Release\sculptforge.exe`; on macOS run `open build/SculptForge.app`.

Useful CMake options:

| Option | Default | What it does |
|---|---|---|
| `SCULPTFORGE_BUILD_APP` | `ON` | Build the desktop application (turn off for a headless build of `sculptforge-cli` only) |
| `SCULPTFORGE_BUILD_PYTHON` | `OFF` | Build the Python module (`import sculptforge`); needs Python development headers |
| `SCULPTFORGE_BUILD_TESTS` | `ON` | Build the unit and UI tests |
| `SCULPTFORGE_USE_SYSTEM_SDL` | `ON` | Use an installed SDL3 if one is found |

Running `cpack` in the build folder makes the platform's packages (`.deb`, `.rpm` and `.tar.gz` on Linux, a
`.dmg` on macOS, an installer and a `.zip` on Windows).

## See also

- [The interface](interface.md)
- [Troubleshooting and FAQ](troubleshooting.md)
- [SculptForge for studios](studios.md)
