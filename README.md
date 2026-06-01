# Tiwut Launcher

A modern, high-performance, and beautifully crafted application manager and compiler for Linux. Tiwut Launcher combines the robustness of **Qt6 (C++)** with the fluid, dynamic aesthetics of a web frontend powered by **Qt WebEngine** to deliver a premium, blur-glass visual experience.

Tiwut Launcher automates the entire process of discovering, downloading, compiling, launching, and managing Linux applications (both binary releases and source code) directly in a non-root, user-space environment.

---

## Features

- **Blur Glass Modern UI**: Transparent window overlay with smooth hover effects, micro-animations, full CSS glassmorphism, dynamic console logs, and built-in tabbed navigation.
- **Subprocess Downloader with Fail-safes**: Downloads remote files asynchronously. If the native Qt network manager encounters a problem, it automatically drops through robust fallbacks:
  1. Native Qt network download
  2. `curl` subprocess download
  3. `wget` subprocess download
- **Non-Root Extractor Pipeline**: Installs a wide variety of Linux application packages locally without needing sudo/root access:
  - **AppImages**: Configures execution permissions and performs automatic squashing/extraction if FUSE (`libfuse2`) is unavailable on the host machine.
  - **Debian Packages (`.deb`)**: Automatically extracts data archives using `dpkg-deb` or falls back to standard `ar` + `tar` pipelines.
  - **RedHat Packages (`.rpm`)**: Extracts packages using `rpm2cpio` + `cpio` or falls back to `7z`.
  - **Flatpak Bundles**: Installs `.flatpak` bundles silently in the user-registry context (`flatpak install --user`).
  - **Compressed Archives (`.zip`, `.tar.gz`, `.tar.xz`, `.tgz`)**: Dynamically extracts archives using standard system utilities or custom Python-based zip/tarball extractors.
- **Asynchronous Compiler State Machine**: Clones repositories from GitHub (or fetches branch ZIP files if git cloning fails) and automatically determines the best compile path:
  - **CMake**: Automatically configures and compiles targets (`cmake -B build` followed by `cmake --build`).
  - **Shell Scripts**: Detects and runs executable build/install scripts (e.g., `build.sh`, `install.sh`, `compile.sh`).
  - **Makefiles**: Resolves parallel building dynamically based on system thread count (`make -jN`).
- **Detached Launch & Deep Clean Uninstaller**: Launches applications fully detached from the main launcher process, terminates running instances gracefully via system signals (`pkill`/`pgrep`/`kill`/`killall`), and completely purges local files and binary caches.

---

## Architecture & Tech Stack

Tiwut Launcher utilizes a dual-engine architecture:

1. **Core Engine (C++ / Qt6)**: Handles OS interaction, network management, asynchronous process control (`QProcess`), dynamic file system scanning, extraction, and local compilation.
2. **Frontend UI (HTML5 / Vanilla CSS / JavaScript)**: A modern reactive visual layer rendered inside `QWebEngineView` with a bridge that triggers system RPC methods seamlessly.

---

## Building & Running

### Prerequisites

Ensure you have a modern C++17 compiler and Qt6 development libraries installed on your Linux distribution. 

#### On Debian/Ubuntu:
```bash
sudo apt install build-essential cmake qt6-base-dev qt6-webengine-dev
```

#### On Fedora:
```bash
sudo dnf install cmake gcc-c++ qt6-qtbase-devel qt6-qtwebengine-devel
```

#### On Arch Linux:
```bash
sudo pacman -S cmake gcc qt6-base qt6-webengine
```

### Installation & Build

1. Clone this repository to your local machine:
   ```bash
   git clone https://github.com/tiwut/Tiwut-Launcher-Linux.git
   cd Tiwut-Launcher-Linux
   ```

2. Configure the build directory:
   ```bash
   cmake -B build -S .
   ```

3. Compile the application:
   ```bash
   cmake --build build
   ```

4. Launch the binary:
   ```bash
   ./build/TiwutLauncher
   ```

---

## Project Directory Structure

```lis
├── CMakeLists.txt         # Port-ready CMake configuration
├── src
│   ├── main.cpp           # App initialization and WebEngine configuration
│   ├── mainwindow.h       # Controller definitions and async states
│   ├── mainwindow.cpp     # System RPC layer, subprocess pipelines, and resolvers
│   └── index.html         # Rich Liquid-Glass UI & frontend application logic
└── build/                 # Created build outputs
```

---

## How Portability is Achieved

The launcher is designed to run out-of-the-box on different Linux machines without local directory assumptions:
- **No Hardcoded User Paths**: Absolute paths to developer directories have been replaced by dynamic application path search methods (`QCoreApplication::applicationDirPath()`).
- **Dynamic Binary Resolution**: All external helper tools (like `git`, `cmake`, `curl`, etc.) are resolved through the host machine's environment `PATH` variable instead of rigid absolute system paths (e.g. `setProgram("git")` instead of `/usr/bin/git`).
- **Multiple Execution Layouts**: Features intelligent parent directory lookups (`../src/index.html` and `../../src/index.html`) so it can be launched directly from standard `build/` directories during development or installed in standard system paths (e.g., `/usr/bin/`) portably.

---

## License

This project is open-source and available under the MIT License.
