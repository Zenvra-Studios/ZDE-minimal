# ZDE Minimal — Media Playback Implementation Plan

## Tujuan

Implementasikan media playback di ZDE Minimal agar user dapat membuka file audio/video langsung di dalam IDE sebagai editor/viewer internal, tanpa membuka aplikasi eksternal.

Target awal:

- Video: MP4, MKV, WebM, MOV, AVI, FLV, WMV, TS, dan format umum lain yang didukung build FFmpeg.
- Audio: MP3, WAV, FLAC, AAC, M4A, OGG, Opus, dan format umum lain yang didukung FFmpeg.
- Playback: open, play, pause, stop, seek, progress/duration, volume.
- Video frame ditampilkan di UI ZDE.
- Audio benar-benar dikirim ke audio output.
- Semua implementasi vendor-specific tetap berada di driver/backend; `Source/Media` hanya menjadi abstraction.

## Kondisi Repo Saat Ini

Struktur media yang sudah ada:

```text
Source/Media/
├── MediaFactory.hpp
├── MediaPlayer.hpp
├── MediaSource.hpp
└── MediaTypes.hpp

Drivers/Media/
├── FFmpegDecoder.cpp
├── FFmpegDecoder.hpp
├── FFmpegPlayer.cpp
└── FFmpegPlayer.hpp

Source/UI/Editor/
├── MediaPlayerView.cpp
└── MediaPlayerView.h

ThirdParty/ffmpeg/
├── bin/
├── include/
├── lib/
└── ...
```

Artinya FFmpeg decoder dan media abstraction bukan sesuatu yang perlu dibuat ulang dari nol. Fokus pekerjaan adalah menyelesaikan integrasi dan pipeline playback end-to-end.

## Diagnosis Utama

### 1. FFmpeg sudah tersedia di repository, tetapi runtime DLL harus dipastikan tersedia

Pada Windows, `.lib` hanya menyelesaikan link-time. Runtime ZDE juga harus dapat menemukan DLL FFmpeg.

Pastikan executable hasil build dapat menemukan setidaknya library yang memang digunakan, misalnya:

```text
avcodec-*.dll
avformat-*.dll
avutil-*.dll
swscale-*.dll
swresample-*.dll
```

Jangan bergantung pada absolute path machine developer seperti `C:/ThirdParty/ffmpeg/...`.

Gunakan path relatif terhadap repository/build output dan buat deployment rule CMake yang menyalin DLL FFmpeg ke directory runtime ZDE.

Contoh target hasil build:

```text
build/.../bin/Debug/
├── ZDE.exe
├── avcodec-*.dll
├── avformat-*.dll
├── avutil-*.dll
├── swscale-*.dll
└── swresample-*.dll
```

Alternatif yang valid adalah memastikan directory `ThirdParty/ffmpeg/bin` masuk PATH saat menjalankan ZDE, tetapi deployment DLL ke output aplikasi lebih portable.

### 2. `Drivers/Media` harus benar-benar menjadi dependency target yang digunakan UI

CMake harus menghasilkan dependency chain seperti:

```text
ZDE
  -> Application
      -> UI
          -> DriversMedia
              -> FFmpeg
```

Jangan cukup hanya membuat `DriversMedia`; pastikan target yang membuat `MediaPlayerView` juga link terhadap target driver media.

Gunakan target CMake bernama/alias yang konsisten, misalnya:

```text
Zenvra::Drivers::Media
```

dan pastikan FFmpeg imported target ditransmisikan dengan dependency yang benar.

### 3. FFmpeg backend harus diregister sebelum `MediaFactory::create_player()`

`MediaFactory` menggunakan registry backend. Pastikan `FFmpegPlayer::register_backend()` dipanggil saat startup/inisialisasi media subsystem atau gunakan auto-registration yang aman.

Urutan yang diinginkan:

```text
Application startup
    -> initialize Media subsystem
        -> FFmpegPlayer::register_backend()
    -> UI ready
```

Jangan memanggil register berkali-kali dari setiap view tanpa proteksi.

Lebih baik:

```cpp
Media::initialize();
```

yang melakukan satu kali registration.

### 4. `open()` harus benar-benar tervalidasi dan error harus ditampilkan

Saat user membuka file:

```text
MediaSource::from_file(path)
        -> MediaFactory::create_player(...)
        -> player->open(source)
```

Jangan mengabaikan return value.

Wajib lakukan:

```cpp
if (!player->open(source)) {
    show_media_error(player->last_error());
    return;
}
```

UI saat ini bisa berhenti di `Opening stream...`. State itu tidak boleh permanent.

State machine minimal:

```text
Closed
  -> Opening
      -> Ready
      -> Error

Ready
  -> Playing
  -> Paused

Playing
  -> Paused
  -> Ended
  -> Error

Paused
  -> Playing
  -> Stopped

Error
  -> Closed
```

Pastikan setiap failure mengubah UI dari `Opening` ke `Error` dan menampilkan alasan yang sebenarnya.

### 5. `play()` sekarang belum menjadi playback engine penuh

Jangan menganggap:

```cpp
play();
```

cukup untuk memutar file.

Playback membutuhkan loop/thread/tick yang secara kontinu mengambil packet/frame dan menjaga timing.

Minimal pipeline:

```text
play()
  -> start playback worker/timer
      -> decode audio/video
      -> queue frames/samples
      -> update clocks
      -> notify UI
```

Jangan melakukan decode blocking di UI thread.

### 6. Jangan hanya decode satu frame

`get_next_video_frame()` adalah mekanisme frame acquisition, tetapi player membutuhkan scheduling berulang.

Video frame harus diproses berdasarkan timestamp/duration frame, bukan sekadar satu frame setiap UI update tanpa clock.

Minimal konsep:

```text
packet PTS/DTS
    -> decoded frame
        -> frame PTS
            -> video clock
                -> render when due
```

FPS contoh hanya boleh menjadi fallback; timing utama sebaiknya berdasarkan timestamp media.

### 7. Harus ada queue/threading untuk audio dan video

Jangan melakukan semua pekerjaan berikut dalam satu UI callback:

```text
demux
decode
resample
upload texture
render
```

Minimal pisahkan:

```text
Demux/Decode worker
        |
        +---- VideoFrameQueue
        |
        +---- AudioSampleQueue
                     |
UI/render thread <----+
Audio output thread <-+
```

Queue harus bounded agar RAM tidak terus bertambah jika rendering/audio tertinggal.

### 8. VideoFrame harus benar-benar dirender ke Skia/GPU

Decoder yang menghasilkan `VideoFrame` belum berarti video sudah tampil.

Perlu adapter:

```text
VideoFrame
    -> pixel format/stride handling
    -> SkImage / backend texture
    -> SkCanvas
    -> MediaPlayerView
```

Jangan melakukan copy CPU RGBA setiap frame jika graphics backend memungkinkan texture upload yang efisien.

Namun untuk tahap pertama, CPU BGRA/RGBA upload yang benar dan stabil lebih penting. Setelah playback sudah berfungsi, optimalkan ke GPU texture/native hardware frame.

### 9. Render target harus dikelola oleh `MediaPlayerView`

`MediaPlayerView` sebaiknya memiliki object khusus, misalnya:

```text
MediaVideoSurface
```

atau:

```text
VideoRenderer
```

Tanggung jawabnya:

- menerima latest decoded frame
- mempertahankan lifetime pixel data
- membuat/refresh Skia image/texture saat frame berubah
- draw letterbox/fit/fill
- resize mengikuti view
- tidak melakukan decoding

Jangan menaruh API FFmpeg seperti `AVFrame` di kode UI jika tidak diperlukan.

### 10. Audio decode harus tersambung ke audio backend

FFmpeg decoder sudah dapat menghasilkan audio samples. Itu harus masuk ke driver audio.

Pipeline:

```text
FFmpegDecoder
  -> AudioBuffer (Float32 / format internal)
      -> AudioQueue
          -> ZDE Audio Driver
              -> OS audio device
```

Untuk tahap awal boleh pakai format internal sederhana dan resample dengan FFmpeg `libswresample`.

Wajib ada:

- sample rate
- channels
- sample count
- interleaved/planar handling
- underflow handling
- flush saat stop/seek

### 11. Video + audio perlu synchronization

Jangan membuat dua loop yang berjalan bebas.

Gunakan satu master clock.

Pendekatan awal yang disarankan:

```text
Audio clock = master clock
Video clock = compare to audio clock
```

Aturan sederhana:

```text
video too early -> tunggu
video too late  -> drop frame jika perlu
```

Untuk audio-only file seperti MP3, video pipeline cukup tidak digunakan.

### 12. Seek harus mengosongkan queue

Saat seek:

```text
pause/deactivate playback
 -> flush demux/decode
 -> av_seek_frame / avformat_seek_file
 -> avcodec_flush_buffers
 -> clear VideoFrameQueue
 -> clear AudioSampleQueue
 -> set new clock
 -> decode first frames after target
 -> resume
```

Jika queue tidak di-flush, frame lama akan muncul setelah seek.

### 13. File type detection jangan hanya berdasarkan extension

Extension boleh dipakai untuk memilih editor awal, tetapi FFmpeg harus tetap menjadi sumber kebenaran media.

Contoh:

```text
foo.mp4
```

boleh dispatch ke media editor, lalu `avformat_open_input`/stream probing menentukan stream sebenarnya.

Support:

- video-only
- audio-only
- video + audio
- file tanpa extension jika FFmpeg masih dapat mendeteksi format

### 14. Media editor harus menangani audio-only vs video

UI jangan selalu menampilkan video canvas.

```text
Video media
    -> Video canvas + controls

Audio-only
    -> Album/artwork placeholder + controls
```

Minimal audio view boleh sederhana:

```text
┌──────────────────────────────┐
│                              │
│          AUDIO               │
│                              │
│      title / filename        │
│                              │
│  ▶ ━━━━━━━━━━━━━━━ 🔊       │
└──────────────────────────────┘
```

## Target Arsitektur

Pertahankan abstraction yang sekarang.

```text
Source/
├── Media/
│   ├── MediaFactory.hpp
│   ├── MediaPlayer.hpp
│   ├── MediaSource.hpp
│   └── MediaTypes.hpp
│
├── UI/
│   └── Editor/
│       ├── MediaPlayerView.*
│       ├── VideoRenderer.*
│       └── AudioPlayerView.*
│
Drivers/
└── Media/
    ├── FFmpegPlayer.*
    ├── FFmpegDecoder.*
    └── ...

ThirdParty/
└── ffmpeg/
    ├── include/
    ├── lib/
    └── bin/
```

Dependency rule:

```text
UI
 -> Media abstraction
 -> Driver implementation
 -> FFmpeg
```

Bukan:

```text
UI
 -> AVFormatContext / AVCodecContext langsung
```

## Suggested API Refinement

Abstraction jangan mengekspos tipe FFmpeg.

Contoh minimal:

```cpp
class MediaPlayer {
public:
    virtual ~MediaPlayer() = default;

    virtual bool open(const MediaSource& source) = 0;
    virtual void close() = 0;

    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;

    virtual bool seek(double seconds) = 0;

    virtual PlaybackState state() const = 0;
    virtual double position() const = 0;
    virtual double duration() const = 0;

    virtual std::optional<VideoFrame> latest_video_frame() = 0;
    virtual std::optional<AudioBuffer> pull_audio_samples() = 0;

    virtual std::string last_error() const = 0;
};
```

API aktual repo boleh dipertahankan bila sudah kompatibel; jangan melakukan refactor besar hanya demi nama.

## Implementasi Bertahap

### Phase 1 — Make FFmpeg actually load

Checklist:

- [ ] ThirdParty/ffmpeg headers ditemukan CMake.
- [ ] `.lib` ditemukan CMake.
- [ ] FFmpeg DLL ditemukan saat menjalankan ZDE.
- [ ] Hapus absolute Windows path.
- [ ] Pastikan configuration Debug/Release sama-sama benar.
- [ ] Tambahkan CMake post-build copy atau install rule untuk FFmpeg DLL.
- [ ] Tambahkan diagnostic log:
  - FFmpeg enabled/disabled
  - FFmpeg library path
  - FFmpeg runtime path
  - avformat/avcodec version

### Phase 2 — Register backend dan make open deterministic

- [ ] Tambahkan `Media::initialize()`.
- [ ] Register FFmpeg backend satu kali.
- [ ] `MediaFactory::create_player()` harus menghasilkan player non-null.
- [ ] `open()` harus dicek return value-nya.
- [ ] Error `avformat_open_input` harus sampai ke UI.
- [ ] Error `avformat_find_stream_info` harus sampai ke UI.
- [ ] Error decoder not found harus sampai ke UI.
- [ ] Hilangkan state `Opening` yang tidak pernah selesai.

### Phase 3 — Playback engine

- [ ] Playback worker thread atau scheduler.
- [ ] Video frame queue.
- [ ] Audio sample queue.
- [ ] Stop/pause lifecycle.
- [ ] EOS/end-of-stream.
- [ ] Clock.
- [ ] Flush decoder.
- [ ] Seek.

### Phase 4 — Video rendering

- [ ] `VideoRenderer` di sisi UI.
- [ ] BGRA/RGBA CPU path terlebih dahulu.
- [ ] Konversi stride dengan benar.
- [ ] Skia image lifetime aman.
- [ ] Draw aspect-ratio correct.
- [ ] Letterbox/pillarbox.
- [ ] Resize window tanpa crash.
- [ ] Frame update invalidates/repaints view.

### Phase 5 — Audio output

- [ ] Audio driver interface.
- [ ] Audio queue consumer.
- [ ] Float32 output.
- [ ] Sample rate/channel conversion.
- [ ] Start/stop device.
- [ ] Underrun/overrun handling.
- [ ] Mute/volume.

### Phase 6 — A/V synchronization

- [ ] Select master clock.
- [ ] Timestamp handling.
- [ ] Drop late video frame.
- [ ] Delay early frame.
- [ ] Correct seek clock reset.

### Phase 7 — UX

- [ ] Double click media file.
- [ ] Media editor tab.
- [ ] Play/pause.
- [ ] Seek bar.
- [ ] Duration and current time.
- [ ] Volume.
- [ ] Close media tab.
- [ ] Keyboard shortcuts.
- [ ] Audio-only layout.
- [ ] Error screen with useful FFmpeg error.

## CMake Requirements

Jangan mengandalkan:

```cmake
C:/ThirdParty/ffmpeg/...
```

Gunakan repository-local path, misalnya konsep:

```cmake
set(ZDE_FFMPEG_ROOT "${PROJECT_SOURCE_DIR}/ThirdParty/ffmpeg")
```

Lalu buat imported targets untuk library yang benar-benar dipakai.

Pastikan dependency `FFmpeg -> DriversMedia -> UI/Application -> ZDE` tersedia pada link stage dan runtime stage.

Tambahkan deployment rule khusus Windows.

## Runtime Debug Checklist

Saat video menunjukkan `Opening stream...`, debug berurutan:

```text
1. Apakah FFmpeg backend ter-register?
2. Apakah MediaFactory mengembalikan player?
3. Apakah player->open() return true?
4. Kalau false, apa last_error()?
5. Apakah avformat_open_input() berhasil?
6. Apakah stream info berhasil?
7. Apakah video decoder ditemukan?
8. Apakah audio/video stream index valid?
9. Apakah get_next_video_frame() menghasilkan frame?
10. Apakah MediaPlayerView::update() dipanggil terus?
11. Apakah VideoFrame benar-benar di-upload ke Skia/GPU?
12. Apakah view di-invalidate/repaint setiap ada frame?
13. Apakah audio samples masuk ke audio device?
```

Tambahkan logging sementara di setiap boundary, bukan menebak.

Contoh:

```text
[Media] backend=FFmpeg registered
[Media] create_player=ok
[Media] open path=...
[FFmpeg] avformat_open_input=ok
[FFmpeg] video stream index=0 codec=h264
[FFmpeg] audio stream index=1 codec=aac
[FFmpeg] duration=123.456
[Media] state=Ready
[Media] state=Playing
[Video] decoded frame pts=0.000
[Video] rendered frame pts=0.000
[Audio] submitted samples=1024
```

## Acceptance Test

### Video

Test minimal:

```text
MP4/H.264 + AAC
MKV/H.264 + AAC
WebM/VP9 + Opus
MOV/H.264 + AAC
```

Expected:

- file opens
- metadata tampil
- duration benar
- video tampil
- audio terdengar
- play/pause bekerja
- seek bekerja
- close bekerja

### Audio

Test minimal:

```text
MP3
WAV
FLAC
M4A/AAC
OGG/Opus
```

Expected:

- file opens
- duration tampil
- play/pause bekerja
- progress bergerak
- audio terdengar
- seek bekerja

### Failure tests

- file nonexistent
- file corrupt
- unsupported codec
- unsupported container
- FFmpeg DLL hilang
- zero-length file
- video tanpa audio
- audio tanpa video
- very large video

Setiap failure harus menghasilkan UI error yang jelas, bukan berhenti di `Opening stream...`.

## Performance Rules

- Jangan decode di UI thread.
- Jangan copy seluruh frame berulang-ulang jika tidak perlu.
- Gunakan bounded queue.
- Jangan biarkan decoded frames menumpuk tanpa batas.
- Gunakan timestamps untuk scheduling.
- Untuk tahap awal prioritaskan correctness; setelah stabil, optimalkan GPU upload/hardware decode.

## Scope Control

Jangan mengimplementasikan berikut sebelum basic local playback selesai:

- network streaming
- DRM
- YouTube integration
- subtitle editing
- video editing
- hardware encode
- filter graph kompleks
- playlist engine kompleks

Fokus terlebih dahulu pada:

```text
LOCAL FILE
  -> OPEN
  -> DECODE
  -> PLAY
  -> RENDER
  -> AUDIO
  -> SEEK
  -> STOP
```

## Instruksi untuk Agent yang Mengimplementasikan

1. Baca repository aktual terlebih dahulu sebelum mengubah file.
2. Jangan mengasumsikan komponen yang sudah ada belum dibuat.
3. Jangan membuat decoder FFmpeg baru jika `FFmpegDecoder` yang ada masih dapat dipakai.
4. Perbaiki integrasi CMake/runtime deployment jika diperlukan.
5. Jangan mengekspos FFmpeg types ke `Source/UI` kecuali benar-benar diperlukan.
6. Jangan memindahkan media subsystem ke `Utility`; pertahankan `Source/Media` sebagai abstraction dan `Drivers/Media` sebagai backend.
7. Implementasikan perubahan kecil per phase dan build setelah setiap phase.
8. Tambahkan logging sebelum menyatakan masalah selesai.
9. Jangan menyembunyikan error dengan fallback/stub tanpa memberi tahu UI.
10. Pastikan Windows Debug dan Release sama-sama dapat menjalankan media playback.

## End State yang Diinginkan

```text
User double-clicks `movie.mp4`
            |
            v
      Editor dispatch
            |
            v
       MediaPlayerView
            |
            v
      MediaFactory
            |
            v
       FFmpegPlayer
            |
            v
      FFmpegDecoder
        /          \
       v            v
 VideoFrame      AudioBuffer
       |              |
       v              v
 VideoRenderer    AudioDriver
       |              |
       v              v
      Skia          Speaker

Playback clock / seek / queue / state management
               berada di MediaPlayer/backend layer
```

## Referensi Repository

- Repository: https://github.com/Zenvra-Studios/ZDE-minimal
- FFmpeg documentation: https://ffmpeg.org/documentation.html

Catatan: gunakan source aktual di branch yang sedang dikerjakan sebagai sumber kebenaran terakhir. Dokumen ini adalah implementation plan, bukan alasan untuk menganggap nama file/API di branch masa depan tetap identik.
