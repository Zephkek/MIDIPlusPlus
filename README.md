
<div align="center">

# MIDI++ for Roblox

*Advanced MIDI Playback for Roblox Pianos*

[![Release](https://img.shields.io/github/v/release/Zephkek/MIDIPlusPlus?style=for-the-badge&logo=github&color=blue)](https://github.com/Zephkek/MIDIPlusPlus/releases)
[![License](https://img.shields.io/badge/License-GPLv3-blue.svg?style=for-the-badge)](https://www.gnu.org/licenses/gpl-3.0)
[![Stars](https://img.shields.io/github/stars/Zephkek/MIDIPlusPlus?style=for-the-badge&logo=github)](https://github.com/Zephkek/MIDIPlusPlus/stargazers)
[![Issues](https://img.shields.io/github/issues/Zephkek/MIDIPlusPlus?style=for-the-badge&logo=github)](https://github.com/Zephkek/MIDIPlusPlus/issues)
[![Download Now](https://img.shields.io/badge/Download%20Now-▼-brightgreen?style=for-the-badge&logo=github&logoColor=white)](https://github.com/Zephkek/MIDIPlusPlus/releases/latest)


</div>

---

## Overview

MIDI++ is a high-performance C++ application designed for playing MIDI files on Roblox pianos with exceptional accuracy and speed. It offers unparalleled precision and advanced features for music enthusiasts and Roblox players.

## Key Features
- **Powerful MIDI Parser**: Battle tested parser, against all kinds of fucked up midi files that Gene comes up with, shout out to him.
- **Transpose Engine**: Intelligent key detection, genre-specific transposition, and advanced harmonic analysis (for whoever even uses this anyway)
- **Performance Optimization**: C++, over optimized as shit.
- **MIDI-to-QWERTY Conversion**: Yeah simple conversion, straight forward no spghetti qwerty emulation code like some shitty code out there.
- **Tempo Management**: gets the tempo of the midi files in real time properly, no more typing what tempo you need!
- **Volume Control**: Velocity-sensitive playback and dynamic volume modulation to try and simulate depth on sh*t pianos that don't even handle any of that properly.
- **Authenticity Mode**: Basic "human like behaviour" (not really)
- **Extended Functionality**: Sustain pedal support, 88-key mode, automatic MIDI file error correction
- **System Optimization**: Efficient event handling, custom memory pools, lock-free queues, manual vectorization in critical parts of the code to ensure these parts are always optimized because guess what? auto vectorization is not always guranteed, what a shock!.
- **Customization**:  customize keys using clean, dynamic mappings with proper data structures (ever heard of constexpr maps or std::array?), unlike the brute-force hardcoded disaster dogwater code that's out there for midi to qwerty conversion.
## Technical Superiority

- Utilizes advanced data structures and algorithms for optimal performance
- Features custom memory management for reduced overhead
- Employs lock-free programming techniques for enhanced concurrency
- Optimizied to the max with branch prediction and PGO
  
## Showcase:
| [![Stress Test N°1 7m Notes](https://img.youtube.com/vi/aAmGCW7o55c/0.jpg)](https://www.youtube.com/watch?v=aAmGCW7o55c) | [![Features Showcase](https://img.youtube.com/vi/ajsBSaO1icQ/0.jpg)](https://www.youtube.com/watch?v=ajsBSaO1icQ) |
|---|---|
| Stress Test N°1 7m Notes (Loud) | Transposition Engine Test |

## Installation

1. Install Microsoft Visual C++ Redistributable (x64) from [here](https://aka.ms/vs/17/release/vc_redist.x64.exe)
2. Download MIDI++ from the [Release Page](https://github.com/Zephkek/MIDIPlusPlus/releases/tag/v1.0.2)
3. Extract the package and place MIDI files in the "midi" folder
4. Launch MIDI++ to begin

## System Requirements

- Windows 10 or later (64-bit)
- AVX2 supported CPU (Generally any CPU that's not older than 2013)
- Microsoft Visual C++ Redistributable 2022 (x64)
- 100MB free disk space

## Features Usage Guide:


The following table explains the settings used in the `LEGIT_MODE_SETTINGS` JSON object:

| Setting | Type | Description |
|---------|------|-------------|
| ENABLED | Boolean | • Activates/deactivates Authenticity Mode<br>• When true, all other settings become active, and will load the MIDI file in legit mode |
| TIMING_VARIATION | Float<br>(0.0 to 1.0) | • Introduces timing deviations to each note<br>• 0.1 means ±10% variation in note timing<br>• Example: A note at 1.000s might play between 0.900s and 1.100s |
| NOTE_SKIP_CHANCE | Float<br>(0.0 to 1.0) | • Sets probability of missing a note<br>• 0.02 means 2% chance to skip any given note<br>• Simulates human errors in fast or complex passages |
| EXTRA_DELAY_CHANCE | Float<br>(0.0 to 1.0) | • Likelihood of inserting a pause between notes<br>• 0.05 means 5% chance of adding delay after a note<br>• Mimics human hesitation or breaths |
| EXTRA_DELAY_MIN | Float<br>(seconds) | • Minimum duration for random delays<br>• Should be ≤ EXTRA_DELAY_MAX |
| EXTRA_DELAY_MAX | Float<br>(seconds) | • Maximum duration for random delays<br>• Defines upper limit for added pauses |

## Example Configuration

```json
{
  "LEGIT_MODE_SETTINGS": {
    "ENABLED": true,
    "TIMING_VARIATION": 0.1,
    "NOTE_SKIP_CHANCE": 0.02,
    "EXTRA_DELAY_CHANCE": 0.05,
    "EXTRA_DELAY_MIN": 0.05,
    "EXTRA_DELAY_MAX": 0.2
  }
}
```
# Volume Settings

| Setting | Value | Description |
|---------|-------|-------------|
| MIN_VOLUME | 10 | • Minimum allowed volume level<br>• Represents 10% of maximum volume |
| MAX_VOLUME | 200 | • Maximum allowed volume level in your game<br>• Represents 200% of normal volume by default |
| INITIAL_VOLUME | 100 | • Starting volume level on initialization<br>• Represents 100% or "normal" volume |
| VOLUME_STEP | 10 | • Increment/decrement step for volume adjustments<br>• Each adjustment changes volume by 10 units by default |
| ADJUSTMENT_INTERVAL_MS | 50 | • Minimum time between volume adjustments<br>• Prevents changes more frequent than every 50 milliseconds by default|

## JSON Configuration

```json
{
    "VOLUME_SETTINGS": {
        "MIN_VOLUME": 10,
        "MAX_VOLUME": 200,
        "INITIAL_VOLUME": 100,
        "VOLUME_STEP": 10,
        "ADJUSTMENT_INTERVAL_MS": 50
    }
}
```

### External Libraries

MIDI++ leverages the power of these outstanding open-source libraries:

<table style="width: 100%; border-collapse: separate; border-spacing: 0 15px; font-family: Arial, sans-serif;">
  <tr>
    <th style="text-align: left; padding: 10px 15px; color: #2c3e50; font-size: 18px; font-weight: bold;">Library</th>
    <th style="text-align: left; padding: 10px 15px; color: #2c3e50; font-size: 18px; font-weight: bold;">Description</th>
  </tr>
  <tr style="background-color: #f8f9fa; box-shadow: 0 2px 5px rgba(0,0,0,0.1);">
    <td style="padding: 20px; border-left: 4px solid #3498db; font-weight: bold;">
      <a href="https://github.com/cameron314/concurrentqueue" style="color: #3498db; text-decoration: none; font-size: 16px;">moodycamel Concurrent Queue</a>
    </td>
    <td style="padding: 20px; color: #34495e; font-size: 14px;">
      A fast multi-producer, multi-consumer lock-free concurrent queue for C++11
    </td>
  </tr>
  <tr style="background-color: #f8f9fa; box-shadow: 0 2px 5px rgba(0,0,0,0.1);">
    <td style="padding: 20px; border-left: 4px solid #3498db; font-weight: bold;">
      <a href="https://github.com/nlohmann/json" style="color: #3498db; text-decoration: none; font-size: 16px;">nlohmann's JSON for Modern C++</a>
    </td>
    <td style="padding: 20px; color: #34495e; font-size: 14px;">
      A JSON library for Modern C++
    </td>
  </tr>
</table>

These high-quality libraries have significantly enhanced MIDI++'s functionality and performance, Kudos to their creators.

## License

This project is licensed under the GNU General Public License v3.0 (GPLv3).

This license allows you to:
- Use the software for any purpose
- Change the software to suit your needs
- Share the software with your friends and neighbors

This license requires you to:
- Share the source code when you distribute the software
- License any modifications under GPLv3
- Keep intact all copyright, license, and disclaimer notices

This license explicitly forbids:
- Distributing the software without the source code
- Using the software as part of a proprietary product

For more details, see the [LICENSE](LICENSE) file in this repository or visit [GNU GPLv3](https://www.gnu.org/licenses/gpl-3.0.en.html).
---

</div>
