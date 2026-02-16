# Wallow

**Wallow** is a lightweight, multi-tab social media browser for Windows. It allows you to keep your messaging and social apps (WhatsApp, Telegram, Instagram, etc.) in one place without the overhead of running multiple full browser instances or Electron apps.

## Features

- **Multi-Tab Interface**: Switch between WhatsApp, Telegram, Instagram, TikTok, LinkedIn, Facebook, and custom tabs easily.
- **Resource Efficient**:
  - **Aggressive Memory Optimization**: Inactive tabs are automatically suspended and their memory is trimmed, reducing RAM usage significantly.
  - **Single Instance**: Prevents multiple copies of the app from running; focuses the existing window instead.
- **Customizable**:
  - Add your own custom tabs for any website.
  - toggle visibility of built-in apps.
  - configure zoom levels and user agents (Standard or WebKit spoofing).
- **System Tray Integration**: Minimize to tray to keep it running in the background.
- **Privacy & Storage**:
  - Configurable storage location for browser data.
  - Option to clear cache and browsing data per app.

## Building from Source

### Prerequisites

- **CMake** (3.8 or newer)
- **Visual Studio 2022** (or newer) with C++ Desktop Development workload.
- **WebView2 Runtime** (usually pre-installed on Windows 10/11).

### Build Instructions

1.  Clone the repository:
    ```bash
    git clone https://github.com/BimsaraU/Wallow.git
    cd Wallow
    ```

2.  Configure and build using CMake:
    ```bash
    cmake -B build
    cmake --build build --config Release
    ```

3.  The executable will be located in `build/Release/Wallow.exe`.

## Usage

1.  Run `Wallow.exe`.
2.  Use the top toolbar to switch between apps.
3.  Click **Settings** to:
    - Enable/Disable specific apps.
    - Add custom tabs.
    - Monitor memory usage.
    - Configure startup behavior.

## License

MIT License
