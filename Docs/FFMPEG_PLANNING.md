# Dokumen Arsitektur & Perencanaan Integrasi FFmpeg & Audio Driver untuk ZDE Studio

Dokumen ini memetakan arsitektur multimedia, pemisahan antarmuka (interface) dan backend driver, integrasi **Audio Driver (`Drivers/Audio/`)**, serta matriks dukungan format video & audio lengkap dari **FFmpeg (libavcodec, libavformat, libavutil, libswscale, libswresample)** di dalam ZDE Studio.

---

## 1. Arsitektur Modular: Media & Audio Driver Integration

ZDE Studio mengadopsi pola arsitektur **Driver Pattern** yang memisahkan antara layer domain/antarmuka publik (`Source/Media/`), layer implementasi pemutar video/media (`Drivers/Media/`), dan subsistem audio driver (`Drivers/Audio/`):

```mermaid
graph TD
    subgraph App_Layer [Source/ - Aplikasi & UI]
        UI[UI Panels / Media Player Widget]
        AudioService[Audio Engine / Sound FX Services]
        Explorer[File Explorer Asset Inspector]
    end

    subgraph Interface_Layer [Source/Media/ - Core Abstractions]
        MF[MediaFactory - Backend Registry & Resolver]
        MP[IMediaPlayer - Abstract Player Interface]
        MS[MediaSource - File, Stream URL, Memory Buffer]
        MT[MediaTypes - Enums, Frames, AudioBuffers, Metadata]
    end

    subgraph Driver_Media [Drivers/Media/ - FFmpeg Media Driver]
        FF[FFmpeg Driver<br/>FFmpegPlayer + FFmpegDecoder]
    end

    subgraph Driver_Audio [Drivers/Audio/ - Audio Engine & Mixer]
        AE[AudioEngine - Singleton Manager]
        AD[AudioDevice - Software Audio Mixer & Voices]
        AC[AudioClip - Decoded In-Memory Audio PCM]
    end

    subgraph External_Libraries [ThirdParty / System]
        FFmpegLib[FFmpeg C API<br/>libavcodec, libavformat, libavutil, libswscale, libswresample]
    end

    App_Layer --> MF
    App_Layer --> MP
    App_Layer --> AE
    MF --> MP
    MP <|.. FF
    FF --> FFmpegLib
    AC --> FF
    AE --> AD
    AD --> AC
```

---

## 2. Struktur Folder & Modul

```text
ZDE-minimal/
├── Source/
│   ├── Core/
│   ├── UI/
│   └── Media/                               # Layer Antarmuka Publik (Header-only / Interface)
│       ├── CMakeLists.txt
│       ├── MediaTypes.hpp                   # Enums, PlaybackState, VideoFrame, AudioBuffer, Metadata
│       ├── MediaSource.hpp                  # Abstraksi input (File, Stream URL, Memory)
│       ├── MediaPlayer.hpp                  # Interface murni IMediaPlayer & Callbacks
│       └── MediaFactory.hpp                 # Factory registry untuk pembuatan player
│
├── Drivers/
│   ├── Graphics/
│   ├── Media/                               # Driver Media Video/Audio FFmpeg
│   │   ├── CMakeLists.txt                   # Target DriversMedia (Zenvra::Drivers::Media)
│   │   ├── FFmpegDecoder.hpp                # Low-level FFmpeg demuxer & codec decoder C++20
│   │   ├── FFmpegDecoder.cpp
│   │   ├── FFmpegPlayer.hpp                 # Konkretisasi IMediaPlayer via FFmpeg
│   │   └── FFmpegPlayer.cpp
│   └── Audio/                               # Driver Audio & Mixer Subsystem
│       ├── CMakeLists.txt                   # Target DriversAudio (Zenvra::Drivers::Audio)
│       ├── AudioClip.h                      # Wrapper buffer audio PCM in-memory (decoded via FFmpeg)
│       ├── AudioClip.cpp
│       ├── AudioDevice.h                    # Software audio mixer, voice management, pan & master volume
│       ├── AudioDevice.cpp
│       ├── AudioEngine.h                    # Singleton Audio Engine untuk sound effects & playback
│       └── AudioEngine.cpp
│
├── ThirdParty/
│   └── ffmpeg/                              # Prebuilt SDK FFmpeg (headers di include/, lib & dll)
│
├── Cmake/
│   ├── FindFFmpeg.cmake                     # CMake finder module resmi ZDE untuk FFmpeg
│   └── Depedencies.cmake                    # Konfigurasi find_package(FFmpeg)
│
├── Scripts/
│   └── setup_ffmpeg.ps1                     # Script setup otomatis Windows x64
│
└── Tests/
    ├── FFmpegTests.cpp                      # Unit test untuk MediaSource, MediaFactory, FFmpegPlayer, AudioClip, AudioDevice, AudioEngine
    └── CMakeLists.txt
```

---

## 3. Matriks Total Format Video & Audio yang Didukung

### A. Format Container / Demuxer
- **Video Kontainer**: MP4, MKV (Matroska), WebM, MOV (QuickTime), AVI, FLV, WMV, MPEG-TS (.ts), OGV, 3GP, VOB, RMVB, ASF, M4V.
- **Animasi & Gambar Bergerak**: Animated GIF, Animated WebP, APNG, Motion JPEG (.mjpg).
- **Audio Kontainer**: MP3, WAV, FLAC, AAC, OGG (Vorbis/Opus), M4A, AIFF, WMA, AC3, DTS.

### B. Video Codecs
- **Modern & Next-Gen**: AV1, HEVC / H.265, AVC / H.264, VP9, VP8.
- **Produksi & Editing**: Apple ProRes (422, 4444 dengan Alpha), Avid DNxHD / DNxHR, FFV1 (Lossless), Motion JPEG (MJPEG), DV.
- **Legacy & Broadcast**: MPEG-1, MPEG-2, MPEG-4 Part 2, VC-1, Theora, Sorenson Spark (FLV1), Cinepak, Indeo.

### C. Konversi Pixel Format & Color Space (`libswscale`)
- **8-bit YUV**: `YUV420P`, `YUVJ420P`, `NV12`, `NV21`, `YUYV422`, `UYVY422`, `YUV422P`, `YUV444P`.
- **10-bit / 12-bit HDR & Deep Color**: `YUV420P10LE`, `YUV422P10LE`, `YUV444P10LE`, `P010LE`.
- **Alpha Transparency**: `YUVA420P`, `YUVA422P`, `YUVA444P`, `ARGB`, `RGBA`, `BGRA`, `ABGR`.
- **Grayscale & RGB**: `GRAY8`, `GRAY16LE`, `RGB24`, `BGR24`, `RGB48LE`.
- **Target Output Runtime**: `RGBA32` (default untuk Skia/UI rendering), `BGRA32`, `RGB24`, `BGR24`, `Gray8`, atau `RGBA64_LE` (HDR).

---

## 4. Pola Penggunaan Audio Driver C++20

### A. Memainkan Sound Effect Langsung dari File (MP3, WAV, OGG, FLAC)
```cpp
#include "Drivers/Audio/AudioEngine.h"

using namespace Zenvra::Drivers::Audio;

// 1. Inisialisasi AudioEngine
auto& audio = AudioEngine::instance();
audio.init();

// 2. Mainkan efek suara (mendukung WAV, MP3, OGG, FLAC, dll. otomatis di-decode FFmpeg)
VoiceId sound_id = audio.play_sound("Assets/sounds/click.wav", 0.8f /* volume */, 0.0f /* center pan */);

// 3. Kontrol volume master
audio.set_master_volume(1.0f);
```

### B. Pre-load Audio Clip ke RAM untuk Latensi Instan
```cpp
#include "Drivers/Audio/AudioClip.h"
#include "Drivers/Audio/AudioEngine.h"

// Load audio clip sekali di awal (misal untuk keyboard type sound, UI clicks, dialog audio)
auto click_clip = Zenvra::Drivers::Audio::AudioClip::load_from_file("Assets/sounds/typewriter.ogg");

// Mainkan berkali-kali secara concurrent tanpa I/O disk ulang
auto& audio = Zenvra::Drivers::Audio::AudioEngine::instance();
audio.play_clip(click_clip, 0.5f, -0.2f /* slight left */);
```
