# midiRoll

A MIDI piano visualizer built with C++20 and DirectX 11. No engine, no frameworks — just raw Win32, D3D11, and XAudio2.

Inspired by [Embers](https://embers.app) and [Piano VFX](https://piano-vfx.com).

## Features

- **GPU-accelerated rendering** via DirectX 11 instanced sprites
- **Falling note visualization** with per-channel colors and velocity-reactive brightness
- **Particle system** — burst, continuous, and spark effects on note events
- **Bloom post-processing** — multi-pass gaussian blur
- **Impact flash effects** — expanding rings on key press
- **Keyboard saber** — glowing line at piano edge, color derived from active notes
- **Live keyboard input** — play notes with your QWERTY keyboard
- **MIDI file playback** — load and play .mid files
- **Audio feedback** — XAudio2 sine wave synthesis for instant feedback

## Building

### Requirements

- **Visual Studio 2022** (v143 toolset)
- **Windows 10 SDK** (10.0.19041.0 or later)
- No external dependencies — all DirectX libraries ship with Windows

### Steps

1. Open `midiRoll.sln` in Visual Studio 2022
2. Select **x64** / **Debug** or **Release**
3. Build (Ctrl+Shift+B) or press F5 to build and run

Output goes to `bin/Debug/` or `bin/Release/`.

## Controls

| Key | Action |
|-----|--------|
| `A` `S` `D` `F` `G` `H` `J` `K` `L` `;` | White keys (C4–E5) |
| `W` `E` `T` `Y` `U` `O` `P` | Black keys (C#4–D#5) |
| `Z` `X` `C` `V` `B` `N` `M` | Lower octave (C3–B3) |
| `Space` | Toggle MIDI playback |
| `Escape` | Quit |

## Architecture

```
midiRoll/
├── midiRoll.sln
├── midiRoll/
│   ├── src/
│   │   ├── main.cpp              # WinMain entry point
│   │   ├── GameLoop.h/cpp        # Fixed-timestep game loop
│   │   ├── Window.h/cpp          # Win32 window wrapper
│   │   ├── Input.h/cpp           # Keyboard → MIDI note mapping
│   │   ├── Renderer/
│   │   │   ├── D3DContext.h/cpp   # Device, swap chain, RTV
│   │   │   ├── SpriteBatch.h/cpp  # Instanced quad rendering
│   │   │   ├── ShaderManager.h/cpp
│   │   │   ├── RenderTarget.h/cpp # Off-screen RTTs
│   │   │   └── TextRenderer.h/cpp # DirectWrite + D2D
│   │   ├── Shaders/              # HLSL shader files
│   │   ├── Effects/
│   │   │   ├── ParticleSystem.h/cpp
│   │   │   └── Bloom.h/cpp       # Multi-pass gaussian bloom
│   │   ├── Piano/
│   │   │   ├── PianoRenderer.h/cpp
│   │   │   └── NoteState.h/cpp
│   │   ├── Audio/
│   │   │   ├── AudioEngine.h/cpp  # XAudio2 wrapper
│   │   │   └── MidiParser.h/cpp   # .mid file parser
│   │   └── Util/
│   │       ├── Timer.h
│   │       ├── Math.h
│   │       └── Color.h
│   └── assets/
└── .gitignore
```

## Roadmap

- [ ] GPU compute shader particles (currently CPU)
- [ ] Fluid simulation (2D Navier-Stokes on compute shader)
- [ ] Theme/preset system (neon, fire, ice, monochrome)
- [ ] 4K video export via FFmpeg pipe
- [ ] Audio visualizer mode (FFT → reactive effects)
- [ ] SoundFont playback (FluidSynth integration)
- [ ] MIDI device input (hardware keyboard support)
- [ ] Trail renderer for falling notes
- [ ] Shockwave/splash effects on note-on

## License

MIT
