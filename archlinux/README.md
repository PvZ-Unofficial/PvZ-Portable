# Arch Linux Packaging & Testing Guide

This directory contains build scripts for Arch Linux to facilitate testing PvZ-Portable (a re-implementation engine) on various environments and configurations.

## ⚠️ Important Legal & Usage Disclaimer

**This package contains ENGINE CODE ONLY.**

To comply with copyright laws and the project's [license](../LICENSE), this build script **DOES NOT** include any game assets.
*   **You MUST own a legal copy** of *Plants vs. Zombies: Game of the Year Edition* (e.g., from Steam or EA's official website).
*   This project is for **educational and research purposes**, specifically to study cross-platform compatibility, OpenGL/SDL rendering techniques, and engine behavior on Linux architectures (x86_64, aarch64, riscv64, etc.).

## Testing Focus: Wayland vs. X11

The primary goal of this package is to test the engine's behavior under different display servers. Current known issues we are investigating:

*   **Fixed Aspect Ratio**: The engine enforces a 4:3 aspect ratio. On widescreen monitors, this results in black bars (letterboxing).
*   **~~XWayland Flickering~~** *(Fixed)*: Previously, running via XWayland could cause flickering in the black bar areas. This has been resolved by the OpenGL ES 2.0 backend refactor.
*   **Wayland Native**: Running natively on Wayland is recommended and works well across different compositors (KWin, Mutter, Hyprland, etc.).

## Build Instructions

Because we cannot distribute the game assets, you must provide them manually before building the package.

### Prerequisites

Install the base development tools:

```bash
sudo pacman -S --needed base-devel
```

### Prepare Game Assets

Locate your legally obtained English Plants vs. Zombies 1.0.0.1051 game data. You need two items:

> **Game version compatibility:** This branch uses **1.0.0.1051** as its behavior and resource baseline. GOTY / annual-edition data is not the reference and is not guaranteed to work.

1.  The file `main.pak`
2.  The folder `properties/`

Verify that the files come from the English 1.0.0.1051 release; current Steam and other GOTY installations normally contain annual-edition data instead.

**Packaging the Assets:**
Create a ZIP file named `Plants_vs._Zombies_1.0.0.1051_EN.zip` containing these items at the **root** of the archive.

```bash
# Example command if you are in the game directory:
7z a Plants_vs._Zombies_1.0.0.1051_EN.zip main.pak properties/
```

> **Note:** Do not put them inside a subfolder inside the zip. The `PKGBUILD` expects `main.pak` to be at the top level of the archive.

### Build and Install

1.  Copy the `Plants_vs._Zombies_1.0.0.1051_EN.zip` you just created into this `archlinux/` directory (alongside the `PKGBUILD` file).
2.  Run the build command in the `archlinux/` directory:

```bash
makepkg -si
```

This command will:
*   Compile the `PvZ-Portable` engine from source.
*   verify the integrity of your local resource zip (skipped for local files usually, but structure must match).
*   Combine the engine and your assets into an Arch Linux package.
*   Install it to your system.

### Running the Game

Launch from your application menu or terminal:

```bash
pvz-portable
```

Data is stored in `~/.local/share/io.github.wszqkzqk/PvZPortable/` by default.
