# Building WSJT-Z on Linux

This document describes how to build WSJT-Z from source on Linux. The upstream project is a Qt 5 / CMake / Fortran application derived from WSJT-X.

Pre-built Windows installers are published on the [Releases](https://github.com/sq9fve/wsjt-z/releases) page. Use these instructions when you want a local build (development, patches, or Linux packages).

## What you get

A successful build produces at least:

| Artifact | Role |
|----------|------|
| `build/wsjtx` | Main GUI application |
| `build/jt9` | Decoder engine (FT8, JT modes, etc.) |
| `build/wsprd` | WSPR decoder |
| `build/fmtave`, `build/fcal`, `build/fmeasure` | Audio / calibration helpers |

The GUI expects these programs in the same directory as `wsjtx` (or on `PATH`). Building the `wsjtx` CMake target alone is enough for a quick link check; install or copy the decoder binaries alongside the GUI for real operation.

## Requirements

### Toolchain

- **CMake** ≥ 3.7.2 (3.20+ recommended)
- **GCC** with C, C++, and **Fortran** (`g++`, `gfortran`)
- **GNU Make** (or Ninja, if you prefer `-G Ninja`)
- **OpenMP** (usually provided by GCC)

### Libraries (development packages)

| Component | Purpose |
|-----------|---------|
| **Qt 5** (≥ 5.12) | GUI: Widgets, SerialPort, Multimedia, PrintSupport, Sql, WebSockets, LinguistTools |
| **FFTW3** (single precision + threads) | `libfftw3-dev`, `libfftw3f` |
| **Hamlib** | Rig control |
| **Boost** (≥ 1.62) | log, log_setup |
| **PortAudio** | Audio I/O |
| **libusb-1.0** | USB rigs / Hamlib backends |
| **libudev** | Required by Hamlib on Linux |

Optional for full packaging: **asciidoc** (man pages and generated docs). The recipes below disable docs and man pages to avoid that dependency.

### Ubuntu / Debian (with `sudo`)

On Ubuntu 24.04 (Noble) or similar:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential gfortran cmake \
  qtbase5-dev qtmultimedia5-dev libqt5serialport5-dev \
  libqt5websockets5-dev qttools5-dev qttools5-dev-tools qt5-qmake \
  libfftw3-dev libhamlib-dev libboost-log-dev libboost-log-setup-dev \
  portaudio19-dev libusb-1.0-0-dev libudev-dev
```

If `qmake` is not found after install, ensure `qt5-qmake` / `qt5-qmake-bin` is installed and that `/usr/lib/qt5/bin` or `/usr/lib/x86_64-linux-gnu/qt5/bin` is on your `PATH`.

## Standard build (recommended)

From the repository root:

```bash
mkdir -p build && cd build

cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DWSJT_SKIP_MANPAGES=ON \
  -DWSJT_GENERATE_DOCS=OFF \
  ..

cmake --build . -j"$(nproc)"
```

The main binary is `build/wsjtx`.

Build decoder helpers as well:

```bash
cmake --build . -j"$(nproc)" --target wsjtx jt9 wsprd fmtave fcal fmeasure
```

### Install to a prefix (optional)

```bash
cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local" \
  -DWSJT_SKIP_MANPAGES=ON \
  -DWSJT_GENERATE_DOCS=OFF \
  ..

cmake --build . -j"$(nproc)"
cmake --build . --target install
```

## Build without root (`sudo`)

If you cannot install `-dev` packages system-wide, you can unpack `.deb` files into a local **sysroot** under the repo. This approach was used successfully on a machine with runtime Qt 5 libraries but no `qtbase5-dev`.

### 1. Download development packages

```bash
mkdir -p .deps
cd .deps

apt-get download \
  qtbase5-dev qtmultimedia5-dev libqt5serialport5-dev libqt5websockets5-dev \
  qttools5-dev qttools5-dev-tools qt5-qmake qt5-qmake-bin \
  libqt5websockets5 libqt5concurrent5t64 libudev-dev libudev1
```

You still need system packages for FFTW, Hamlib, Boost, PortAudio, GCC, and CMake (often already present on a developer machine).

### 2. Extract into `.sysroot`

```bash
cd ..   # repository root
rm -rf .sysroot && mkdir -p .sysroot

for deb in .deps/*.deb; do
  dpkg-deb -x "$deb" .sysroot
done
```

### 3. Wire Qt libraries and tools

Dev packages ship linker scripts that point at versioned `.so` files. Link against the copies already on the system:

```bash
SYSROOT="$(pwd)/.sysroot/usr/lib/x86_64-linux-gnu"
USR="/usr/lib/x86_64-linux-gnu"

# Versioned shared libraries
for f in "$USR"/libQt5*.so.5.15.13; do
  base=$(basename "$f")
  [ -e "$SYSROOT/$base" ] || ln -sf "$f" "$SYSROOT/$base"
done

# qmake (CMake expects this path)
mkdir -p "$SYSROOT/qt5/bin"
ln -sf "$(pwd)/.sysroot/usr/lib/qt5/bin/qmake" "$SYSROOT/qt5/bin/qmake"

# Qt platform plugins (input contexts, etc.)
mkdir -p "$SYSROOT/qt5/plugins"
for sub in "$USR"/../qt5/plugins/*/; do
  name=$(basename "$sub")
  dest="$SYSROOT/qt5/plugins/$name"
  [ -d "$dest" ] || ln -sfn "$(readlink -f "$sub")" "$dest"
done
```

Adjust the `5.15.13` suffix if your distribution ships a different Qt 5 minor version (`ls "$USR"/libQt5Core.so.*`).

### 4. Configure and build

```bash
mkdir -p build && cd build

cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(pwd)/../.sysroot/usr" \
  -DCMAKE_LIBRARY_PATH="$(pwd)/../.sysroot/usr/lib/x86_64-linux-gnu" \
  -DCMAKE_EXE_LINKER_FLAGS="-L$(pwd)/../.sysroot/usr/lib/x86_64-linux-gnu" \
  -DWSJT_SKIP_MANPAGES=ON \
  -DWSJT_GENERATE_DOCS=OFF \
  ..

cmake --build . -j"$(nproc)" --target wsjtx jt9 wsprd fmtave fcal fmeasure
```

Add `.sysroot`, `.deps`, and `build` to `.gitignore` (they are local artifacts).

## Useful CMake options

| Option | Default | Notes |
|--------|---------|-------|
| `CMAKE_BUILD_TYPE` | `Release` | Use `Debug` for development |
| `WSJT_SKIP_MANPAGES` | `OFF` | Set `ON` to skip `asciidoc` man pages |
| `WSJT_GENERATE_DOCS` | `ON` | Set `OFF` to skip documentation generation |
| `CMAKE_INSTALL_PREFIX` | `/usr/local` | Install location |
| `CMAKE_PREFIX_PATH` | — | Prefixes for Hamlib/Qt built elsewhere |
| `WSJT_BUILD_UTILS` | `ON` | Simulators and test programs |
| `BUILD_SHARED_LIBS` | `OFF` | Static internal libraries |

See `INSTALL` in the repository root for Hamlib-from-source, Windows (JTSDK), and macOS (MacPorts) instructions.

## Running the binary

`wsjtx` is a Qt GUI and needs a display (or an offscreen platform plugin for smoke tests):

```bash
./build/wsjtx
```

Headless check (no window):

```bash
QT_QPA_PLATFORM=offscreen ./build/wsjtx --version
```

If you see **“Could not load the Qt platform plugin xcb”**, install Qt platform plugins (`libqt5gui5`, `qt5-gtk-platformtheme`) and ensure you have a running X11 or Wayland session, or use `QT_QPA_PLATFORM=offscreen` only for non-interactive tests.

Verify shared libraries:

```bash
ldd ./build/wsjtx | grep 'not found'
```

No output means all dynamic dependencies resolved.

## Hamlib from source (optional)

If distribution Hamlib is too old, build static Hamlib and point CMake at it:

```bash
mkdir -p ~/hamlib-prefix/build && cd ~/hamlib-prefix
git clone https://github.com/Hamlib/Hamlib src
cd src && git checkout integration && ./bootstrap
mkdir ../build && cd ../build
../src/configure --prefix="$HOME/hamlib-prefix" \
  --disable-shared --enable-static \
  --without-cxx-binding --disable-winradio \
  CFLAGS="-g -O2 -fdata-sections -ffunction-sections" \
  LDFLAGS="-Wl,--gc-sections"
make && make install-strip
```

Then add `-DCMAKE_PREFIX_PATH="$HOME/hamlib-prefix"` to the `cmake` invocation.

## Troubleshooting

### `Could not find Qt5`

Install Qt 5 development packages (see above) or set `CMAKE_PREFIX_PATH` to a Qt 5 prefix (local sysroot or `~/Qt/5.x.x/gcc_64` from the Qt online installer — **Qt 6 is not supported** by this codebase).

### `cannot find -ludev`

Install `libudev-dev`, or extract `libudev-dev` and `libudev1` into your sysroot and pass `-L` / `CMAKE_LIBRARY_PATH` as in the no-root section.

### `The imported target "Qt5::Core" references the file ... but this file does not exist`

Usually missing versioned `.so` files or `qmake` / `mkspecs` / plugins. Install `qt5-qmake` and runtime Qt 5 packages, or symlink from `/usr/lib/x86_64-linux-gnu` into your sysroot as described above.

### Fortran / OpenMP errors

Ensure `gfortran` is installed and matches your GCC version (`gfortran --version`).

### Build time and memory

A full release build uses many parallel Fortran compilations and can take several minutes and several GB of RAM. Reduce parallelism if needed: `cmake --build . -j2`.

## Tests

When built with default options, CTest targets may be available:

```bash
cd build
ctest --output-on-failure
```

Some tests require Qt and may need a display or `QT_QPA_PLATFORM=offscreen`.

## Related files

- `INSTALL` — upstream WSJT-X install notes (Hamlib, Windows, macOS)
- `README.md` — project overview and features
- `CMakeLists.txt` — full build definition
