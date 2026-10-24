# Quickfish
`Quickfish` is a TUI file manager designed to replace the need for graphical alternatives. The goal of this project is to do everything a GUI file manager can do, and more.

Proudly a part of the ObsidianWater project!

## Features
- Everything is a command: every keybind runs a command line you can also type, like `trash 3 7` or `mark sel|all`
- Numbered entries you can address by index, name, `sel` or `cur` from the shell
- Keyboard-driven navigation with vim-style motions and counts
- Inline shell buffer with full system command support, and `sel`/`cur` expansion in system commands
- File selection, move marking, trash, and permanent delete
- Undo/redo support
- File and folder preview

## Keybinds

See [`keybinds.md`](docs/keybinds.md) for keybinds, commands and targets, press `F1` in-app, or type `help` in the shell.

## Dependencies
- `gcc`
- `make`
- `ncurses` (wide-character build, with panel support)
- `pkg-config` (optional, used to locate ncurses)

## Building From Source

**Development build:**
```bash
git clone https://github.com/seaslugr/quickfish
cd quickfish
make
# Binary placed in build/
```

**Release build (recommended):**
```bash
git clone https://github.com/seaslugr/quickfish
cd quickfish
make rel
# Binary placed in build/release/
```

## Installation

After a release build, install the binary and supporting files to your system:
```bash
sudo make install
```

To uninstall:
```bash
sudo make uninstall
```

The install prefix defaults to `/usr/local`. Override it with `PREFIX`:
```bash
sudo make install PREFIX=/usr
```

## Cleaning Build Artifacts

```bash
make clean
```

## Packaging
Quickfish is not yet in any distribution's repositories, but the `packaging/` directory has build files that track the latest git commit:

- **Arch:** `packaging/arch/PKGBUILD` (`quickfish-git`). Build it with `makepkg -si`.
- **Gentoo:** `packaging/gentoo/app-misc/quickfish/quickfish-9999.ebuild`. Copy `packaging/gentoo/app-misc` into a local overlay, then `emerge app-misc/quickfish`.

An AUR package is planned once the project reaches a stable state.

## License
[GPL 2.0](LICENSE)
