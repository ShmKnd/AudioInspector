# AudioInspector

OBS Studio plugin for visualizing and inspecting audio sources and global audio devices.

## Features

- **Active Sources Tab**: Displays only currently playing audio sources in real-time
- **Global Devices Tab**: Lists all global audio devices (Desktop Audio 1-2, Mic/Aux 1-4)
  - Shows device status (Active/Disabled)
  - Displays audio mixer bus assignments (1-6)
  - Quick device information view
- **Audio Map Tab**: Complete hierarchical view of all audio sources
  - Organized by scenes
  - Shows source types, mute status, and monitor settings
  - Indicates shared sources across multiple scenes
  - JSON export to clipboard functionality
- **Audio Information Display**: Shows current audio configuration
  - Sample rate (Hz)
  - Audio driver type (CoreAudio, WASAPI, PulseAudio)
  - Channel count
  - OBS version info
- **Auto-refresh**: Updates every second to reflect current state

## UI Overview

The plugin adds a dock window to OBS Studio with three tabs:

1. **Active**: Lists audio sources currently outputting sound
2. **Global**: Shows global audio device configuration
3. **Map**: Complete audio routing map with JSON export

## Building

This plugin is built using the [OBS Plugin Template](https://github.com/obsproject/obs-plugintemplate) build system.

### Prerequisites

| Platform  | Tool   |
|-----------|--------|
| macOS     | XCode 16.0+ |
| macOS     | CMake 3.30.5+ |
| macOS     | Homebrew (recommended) |

### Build Instructions

#### Method 1: Using CMake Presets (Recommended)

```bash
# Clone and navigate to the repository
cd AudioInspector

# Configure using preset
cmake --preset macos

# Build
cmake --build build_macos --config Release
```

#### Method 2: Manual CMake Configuration

```bash
# Create build directory
mkdir build && cd build

# Configure with Xcode generator
cmake -G Xcode ..

# Build
cmake --build . --config Release
```

Or build using Xcode directly:

```bash
cd build
xcodebuild -configuration Release
```

### Installation

After building, the plugin binary will be located in the build output directory. Copy it to your OBS Studio plugins folder:

```bash
# macOS
cp -r build_macos/Release/AudioInspector.plugin ~/Library/Application\ Support/obs-studio/plugins/
```

## Development

### Project Structure

```
AudioInspector/
├── src/
│   ├── plugin-main.cpp              # Plugin entry point
│   ├── audio_inspector_core.h/cpp   # Core audio inspection logic
│   └── audio_inspector_widget.h/cpp # Qt UI implementation
├── data/
│   └── locale/
│       └── en-US.ini                # Localization strings
├── cmake/                           # Build system configuration
└── CMakeLists.txt                   # Main CMake configuration
```

### Key Components

- **AudioInspectorCore**: Interfaces with OBS libobs to query audio sources and devices
- **AudioInspectorWidget**: Qt-based UI widget with three-tab interface
- **Dynamic API Loading**: Uses `dlsym()` to safely load OBS frontend APIs at runtime

## License

See [LICENSE](LICENSE) file for details.

## Based on OBS Plugin Template

This plugin uses the official [OBS Plugin Template](https://github.com/obsproject/obs-plugintemplate) build system and project structure. For more information about the build system:

- [Getting Started](https://github.com/obsproject/obs-plugintemplate/wiki/Getting-Started)
- [Build System Requirements](https://github.com/obsproject/obs-plugintemplate/wiki/Build-System-Requirements)
- [CMake Build Options](https://github.com/obsproject/obs-plugintemplate/wiki/CMake-Build-System-Options)
