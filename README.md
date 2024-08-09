<div align="center">

# MIDI++ for Roblox

[LOGO PLACEHOLDER]

*Advanced MIDI Playback for Roblox Pianos*

</div>

---

## Overview

MIDI++ is a high-performance C++ application designed for playing MIDI files on Roblox pianos with exceptional accuracy and speed. It offers unparalleled precision and advanced features for music enthusiasts and Roblox players.

## Key Features
- **Powerful MIDI Parser**: Rapidly processes MIDI files, handling complex structures and corrupted data with ease for reliable playback.
- **Transpose Engine**: Intelligent key detection, genre-specific transposition, and advanced harmonic analysis
- **Performance Optimization**: C++ architecture, high-capacity data handling, nanosecond precision timing
- **MIDI-to-QWERTY Conversion**: Seamless Roblox piano integration with customizable key mappings
- **Tempo Management**: Real-time adjustments with smooth transition handling
- **Volume Control**: Velocity-sensitive playback and dynamic volume modulation
- **Authenticity Mode**: Simulated human-like performance with adjustable realism settings
- **Extended Functionality**: Sustain pedal support, 88-key mode, automatic MIDI file error correction
- **Advanced Analysis**: Chord and rhythm pattern recognition, harmonic structure analysis
- **System Optimization**: Efficient event handling, custom memory pools, lock-free queues, SIMD acceleration
- **Customization**: Flexible configuration options and performance-enhancing hotkeys

## Technical Superiority

- Built in C++ for maximum speed and efficiency
- Utilizes advanced data structures and algorithms for optimal performance
- Implements SIMD instructions for parallel processing
- Features custom memory management for reduced overhead
- Employs lock-free programming techniques for enhanced concurrency
- Optimizied to the max with branch prediction and PGO
  
## Showcase:
| [![CPU Usage and Stability](https://img.youtube.com/vi/gAQ-5ZlYjUQ/0.jpg)](https://www.youtube.com/watch?v=gAQ-5ZlYjUQ) | [![Features Showcase](https://img.youtube.com/vi/lyK05EK6e2A/0.jpg)](https://www.youtube.com/watch?v=lyK05EK6e2A) |
|---|---|
| Video 1 | Video 2 |

## Installation

1. Install Microsoft Visual C++ Redistributable (x64) from [here](https://aka.ms/vs/17/release/vc_redist.x64.exe)
2. Download MIDI++ from the [Release Page](https://github.com/Zephkek/MIDIPlusPlus/releases/tag/Release)
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
| ENABLED | Boolean | • Activates/deactivates Authenticity Mode<br>• When true, all other settings become active |
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
| ADJUSTMENT_INTERVAL_MS | 50 | • Minimum time between volume adjustments<br>• Prevents changes more frequent than every 50 milliseconds by defqult|

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
## Thanks

Special thanks to:
- Raven
- Anger
- Gene

Your contributions and support have been invaluable to the development of MIDI++.

## External Libraries

MIDI++ utilizes the following external libraries:

- [moodycamel's Concurrent Queue](https://github.com/cameron314/concurrentqueue): A fast multi-producer, multi-consumer lock-free concurrent queue for C++11
- [nlohmann's JSON for Modern C++](https://github.com/nlohmann/json): A JSON library for Modern C++

We are grateful for these high-quality, open-source libraries that have significantly enhanced the functionality and performance of MIDI++.

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

<div align="center">

**MIDI++: Elevating Roblox Piano Performance**

[![Download Now](https://img.shields.io/badge/Download-Now-green.svg)](https://github.com/Zephkek/MIDIPlusPlus/releases/tag/Release)

</div>
