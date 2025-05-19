<div align="center">

![MIDI++ Logo](https://github.com/user-attachments/assets/07ee6238-43bc-4080-8514-84848d499851 "MIDI++ Logo")

# MIDI++

[![Stars](https://img.shields.io/github/stars/Zephkek/MIDIPlusPlus?style=for-the-badge&logo=github)](https://github.com/Zephkek/MIDIPlusPlus/stargazers)
[![Issues](https://img.shields.io/github/issues/Zephkek/MIDIPlusPlus?style=for-the-badge&logo=github)](https://github.com/Zephkek/MIDIPlusPlus/issues)
[![Download Now](https://img.shields.io/badge/Download%20Now-▼-brightgreen?style=for-the-badge&logo=github&logoColor=white)](https://github.com/Zephkek/MIDIPlusPlus/releases/latest)
[![Users Love Us](https://img.shields.io/badge/SourceForge-Users%20Love%20Us-orange?style=for-the-badge&logo=sourceforge)](https://sourceforge.net/projects/midiplusplus/)
[![Rising Star](https://img.shields.io/badge/SourceForge-Rising%20Star-orange?style=for-the-badge&logo=sourceforge)](https://sourceforge.net/projects/midiplusplus/)
</div>

**MIDI++** is an advanced MIDI Autoplayer and MIDI-to-QWERTY conversion tool designed specifically for virtual piano applications, providing seamless integration for both live play and automated performances.


## Key Features
* **Zero Latency:** Precise MIDI-to-QWERTY conversion with less than 0.5ms latency.
* **Custom Velocity Mapping:** 32-step velocity mapping system for fine control.
* **Advanced Track Management:** Real-time mute and solo capabilities.
* **Remappable Keys:** Easily adapt controls for different games or applications.

## Performance Highlights
* Handles up to 128 simultaneous notes.
* Extremely low memory and CPU usage.
* Accurate timing even under heavy load.

## Interface & Demonstrations

### Main UI Components
<div align="center">

#### UI Interface
![Track Management](https://github.com/user-attachments/assets/fc2d3a44-b7ab-4e7b-a49c-82dc9189be8a)

*Real-time track muting, soloing, and visualization*

#### Velocity Curve Editor
![Velocity Curve Editor](https://github.com/user-attachments/assets/8c061d9c-ad1d-4b06-984b-1f3583d34907)

*32-step precision velocity mapping for perfect expression control*

</div>

### Video Demonstrations

| Feature | Description | Demo |
|---------|-------------|------|
| **Zero Latency** | Experience input-to-output latency of less than 0.5ms | [Watch Video](https://github.com/user-attachments/assets/3b567203-da44-4a30-969d-6831ef1c6067) |
| **Remappable Keys** | Customize key assignments for any virtual piano interface | [Watch Video](https://github.com/user-attachments/assets/f5f9809a-259f-46b8-8ee2-dbd527d0baef) |


## System Requirements
* Windows 10/11 (64-bit)
* Visual C++ Redistributable 2022

## Build Instructions

### Prerequisites
* Visual Studio 2022 (Community, Professional, or Enterprise)
* Windows 10/11 SDK
* C++ Desktop Development Workload

### Building with Visual Studio 2022
1. Clone the repository:
   ```
   git clone https://github.com/Zephkek/MIDIPlusPlus.git
   cd MIDIPlusPlus
   ```

2. Open the solution file:
   * Navigate to the `MIDI++` folder
   * Open `MIDI++.sln` with Visual Studio 2022

3. Select the build configuration:
   * For normal use: `Release x64`
   * For debugging: `Debug x64`

4. Build the solution:
   * Press `Ctrl+Shift+B` or
   * Select `Build > Build Solution` from the menu

5. Run the application:
   * Press `F5` or
   * Navigate to `bin/x64/Release/` and run `MIDI++.exe`

### Common Build Issues
* If you encounter missing dependencies, ensure you have the Visual C++ Desktop Development workload installed via the Visual Studio Installer
* For Windows SDK related errors, install the latest Windows 10/11 SDK via the Visual Studio Installer

## Quick Commands
```
Load    - MIDI loading
Play    - Start playback
Stop    - Stop playback
Restart - Reset playback
Skip    - Navigate through tracks
Speed   - Adjust playback speed
```

## Acknowledgements
Special thanks to **Gene** and the MIDI++ community for invaluable feedback and support.

## License
Please see the [LICENSE](LICENSE) file for details.
