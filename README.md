<div align="center">

<img src="https://github.com/user-attachments/assets/07ee6238-43bc-4080-8514-84848d499851" alt="MIDI++ Logo" width="400"/>

# MIDI++ 
[![Stars](https://img.shields.io/github/stars/Zephkek/MIDIPlusPlus?style=for-the-badge&logo=github)](https://github.com/Zephkek/MIDIPlusPlus/stargazers)
[![Issues](https://img.shields.io/github/issues/Zephkek/MIDIPlusPlus?style=for-the-badge&logo=github)](https://github.com/Zephkek/MIDIPlusPlus/issues)
[![Download Now](https://img.shields.io/badge/Download%20Now-▼-brightgreen?style=for-the-badge&logo=github&logoColor=white)](https://github.com/Zephkek/MIDIPlusPlus/releases/latest)

</div>



MIDI++ v1.0.4 is an advanced MIDI Autoplayer and MIDI to Qwerty conversion program, developed over five months of intensive optimization and real-world testing. It combines the fastest MIDI-to-QWERTY conversion engine available with an also unmatched auto-player system, setting new standards for both live play and automated performance.

### Perfect for Both Live Players and Auto-Performance
- **Live Players**: Experience true 1:1 MIDI input with zero perceptible delay, and support for velocity sensitive qwerty keyboards.
- **Auto-Performance**: Load and play complex MIDI files with perfect timing
- **Hybrid Use**: Seamlessly switch between live play and auto-performance

### What Sets MIDI++ Apart
- Built from the ground up for maximum performance
- Optimized specifically for virtual piano applications
- Uses cutting-edge hardware acceleration techniques
- Maintains perfect timing even under extreme load
- Developed and tested by professional virtual pianists and Audio engineers.

## Performance Benchmarks

- **Input Latency**: < 0.5ms from MIDI input to key press
- **Note Processing**: Up to 128 simultaneous notes with zero allocation
- **Memory Footprint**: ~50MB baseline, minimal GC impact
- **CPU Usage**: 2-5% average on modern processors
- **Timing Precision**: Nanosecond accuracy using advanced Timing algorithms that have been tested for months.
- **Throughput**: Can handle millions of notes per minute while maintaining stability and accuracy.
## Advanced Velocity System

MIDI++ uses a unique 32-key velocity mapping system that converts MIDI velocities (0-127) into keyboard inputs. This system provides precise control over note dynamics while maintaining ultra-low latency.

### 32-Key Velocity Mapping

The velocity system uses this key sequence for mapping:
```
1234567890qwertyuiopasdfghjklzxc
```
Each character represents a different velocity level, where:
- Numbers (1-0): Lower velocity range
- Top row (q-p): Medium velocity range
- Home row (a-l): Medium-high velocity range
- Bottom row (z-c): Maximum velocity range

When playing, MIDI++ will automatically send "Alt + [key]" combinations based on the incoming MIDI velocity, allowing for precise dynamic control on games that support velocity sensitivty 

### Creating Custom Velocity Curves

You can create your own velocity curves through the configuration file. Each curve needs 32 values that map to the key sequence above.

Example custom curve configuration:
```json
{
    "CUSTOM_CURVES": [
        {
            "name": "MyCustomCurve",
            "values": [
                1,4,8,12,16,20,24,28,32,36,      First 10 values (number keys)
                40,45,50,55,60,65,70,75,80,85,   Next 10 values (q-p)
                90,95,100,105,110,115,120,125,   Next 8 values (a-l)
                126,126,127,127                  Final 4 values (z-c)
            ]
        }
    ]
}
```

#### Custom Curve Guidelines:
- Values must be between 1-127
- Must provide exactly 32 values
- Values should generally increase or stay the same
- Consider your playing style when designing curves
- Test curves with different musical genres

### Practical Examples

1. **Gentle Curve** - For soft playing styles:
```json
"values": [1,2,3,4,5,6,7,8,9,10,12,14,16,18,20,25,30,35,40,45,50,60,70,80,90,100,110,115,120,123,125,127]
```

2. **Aggressive Curve** - For dramatic dynamics:
```json
"values": [1,2,4,8,16,24,32,40,48,56,64,72,80,88,96,104,110,115,120,124,126,127,127,127,127,127,127,127,127,127,127,127]
```

3. **Precision Control** - For fine control in middle velocities:
```json
"values": [1,2,4,8,16,24,32,40,48,56,60,64,68,72,76,80,84,88,92,96,100,104,108,112,116,120,122,124,125,126,127,127]
```

The system will automatically interpolate between these values to ensure smooth velocity transitions during play.

## Core Technical Features

### Unmatched MIDI-to-QWERTY Performance
- **Zero-Copy Pipeline**: Direct MIDI input to keypress without intermediate buffers
- **Kernel-Level Timing**: Uses Windows MMCSS (Multimedia Class Scheduler Service) for real-time priority
- **Hardware-Accelerated Processing**: AVX2 SIMD optimization for velocity calculations
- **Lock-Free Architecture**: Custom concurrent queues eliminate thread contention
- **Adaptive Batch Processing**: Dynamically adjusts batch sizes based on input velocity
- **Intelligent Key Mapping**: Optimized for both 61-key and 88-key layouts with zero overhead switching
- **Experience**: Developped and enhanced by people who work in the industry and have experience with real time audio processing.

### Advanced Note Processing Engine
- **TSC-Based Timing**: Uses CPU timestamp counter for sub-microsecond precision
- **Predictive Note Handling**: Smart FIFO/LIFO switching based on play patterns
- **Memory-Aligned Structures**: Custom allocators with cache line optimization
- **Zero-Allocation Hot Path**: Pre-allocated thread-local buffers for key events


### Real-Time Features

#### Precision Volume Control
- Microsecond-accurate volume adjustments
- Adaptive step size based on velocity
- Automatic volume calibration
- Zero latency response to velocity changes

#### Sustain Pedal Optimization
- Three precision modes (IG/UP/DOWN)
- Configurable cutoff with real-time adjustment
- Zero-latency pedal response
- Intelligent pedal state tracking

## System Requirements

- Windows 10/11 64-bit
- Visual C++ Redistributable 2022

## Configuration Guide

## Quick Command Reference

```
Load          - MIDI loading
Play          - playback initiation
Stop          - Immediate key release with state cleanup and shutdown
Restart       - Instant playback reset
Skip/Rewind   - Precise temporal navigation
Speed         - Real-time playback rate adjustment
```

## Interface & Demonstrations

### Classic Win32 Based User Interface
![image](https://github.com/user-attachments/assets/93a49712-96d3-41cb-ba51-150bc4294e26)

*Custom lightweight interface with intuitive controls designed purely for functionality ease of use*

# Features Showcase
### Zero Latency MIDI-to-QWERTY Conversion
https://github.com/user-attachments/assets/3b567203-da44-4a30-969d-6831ef1c6067

### Stable under stresss (MIDI Tested: Ouranos - HDSQ & The Romanticist [v1.6.6])  (LOUD WARNING!)
https://github.com/user-attachments/assets/22f449d6-dd8b-450b-b743-2adbc044109b

*Thanks to the amazing feedback of our community and especially Testers MIDI++'s stability has been enhanced through all kinds of scenarios*

### Advanced Track Management
![image](https://github.com/user-attachments/assets/673565d8-c971-4aaa-a98c-ee74c8a1e30d)

*Comprehensive track control allowing you to mute, solo any track in real time with instrument detection capabilities*

---

## 🙏 Thanks & Acknowledgements

We would like to extend our deepest gratitude to all the individuals who have contributed to the development of **MIDI++ v1.0.4**. Your support, feedback, and dedication have been invaluable in bringing this project to life.

### Special Thanks To

- **Gene**  
  *For your unwavering support, insightful feedback, and continuous encouragement throughout the development cycle.

### Heartfelt Thanks To

- **The Amazing Community**  
  *Your testing, suggestions, and enthusiasm have significantly enhanced MIDI++'s stability and performance.*
  
- **Contributors & Collaborators**  
  *Thank you for your code contributions, bug reports, and feature requests that have helped shape MIDI++.*


---
