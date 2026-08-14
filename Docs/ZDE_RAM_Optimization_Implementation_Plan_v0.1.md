# ZDE RAM Optimization Implementation Plan

## Module: Linux/X11 Rendering Memory Reduction

Version: 0.1.0\
Status: Draft\
Target Platform: Linux (X11), berlaku juga untuk Cocoa (disesuaikan)

## 1. Background & Goal

Benchmark per platform (debug build, idle, ukuran memori):

| Platform | RSS Total | Private (Anon) | Catatan |
|---|---|---|---|
| Win32 | ~3.6 MB | ~3.6 MB | Task Manager = private working set; GDI bitmap tinggal di kernel pool (tidak terhitung di proses) |
| Linux/X11 | ~24.8 MB | ~6.8 MB | `ps` RSS termasuk shared libs (libc, X11, Xft, fontconfig) = ~18 MB file-mapped |

**Kesimpulan**: selisih angka sebagian besar artefak pengukuran (RSS termasuk shared libs vs private working set), tetapi ada churn alokasi nyata di sisi X11 yang bisa dihilangkan agar private usage mendekati Win32.

**Catatan penting**: kompresi zlib **bukan** solusi RAM (malah menambah buffer + CPU). Fokus di bawah adalah pengurangan alokasi & buffering berulang.

## 2. Implementasi 1 — Reuse XftDraw (churn alokasi terbesar)

### Masalah
`Source/Utility/Fonts.h:276` — setiap pemanggilan `drawString()` melakukan `XftDrawCreate()` + `XftDrawDestroy()`. Setiap pembuatan objek memperbesar FreeType/Xft cache dan memicu alokasi kecil ribuan kali per frame saat scroll/animasi. Win32 hanya `SelectObject` HDC yang sama berulang kali.

### Perubahan
`Source/Utility/Fonts.h` (branch `#elif defined(__unix__)`):

```cpp
class AntialiasedFont {
  XftDraw *m_draw = nullptr;   // satu instance reuse per font

public:
  void drawString(Drawable drawable, const std::string &color_name, int x,
                  int y, const std::string &text,
                  const XRectangle *clip = nullptr) {
    XftColor *color = getColor(color_name);
    if (!color || !m_font) return;

    if (!m_draw)
      m_draw = XftDrawCreate(m_display, drawable, m_visual, m_colormap);
    else
      XftDrawChange(m_draw, drawable);   // retarget tanpa alokasi baru
    if (clip) XftDrawSetClipRectangles(m_draw, 0, 0, clip, 1);
    XftDrawStringUtf8(m_draw, color, m_font, x, y,
                      (const FcChar8 *)text.c_str(), text.length());
  }

  ~AntialiasedFont() {
    if (m_draw) XftDrawDestroy(m_draw);  // tambahkan di destructor
    if (m_font) XftFontClose(m_display, m_font);
    for (auto &pair : m_allocated_colors)
      XftColorFree(m_display, m_visual, m_colormap, &pair.second);
  }
  // ... sisanya tidak berubah
};
```

### Catatan
- `XftDrawChange()` aman dipanggil berulang dengan drawable berbeda (window / pixmap).
- Jangan gunakan satu XftDraw yang sama dari thread berbeda — semua drawing sudah di thread utama.

## 3. Implementasi 2 — Back buffer Pixmap persistent

### Masalah
`Source/Platform/X11/Components/X11ChromeRenderer.cpp:403` — setiap redraw membuat full-screen `XCreatePixmap` lalu `XFreePixmap`. Juga `paint_popup()` di line 1332. Buffer di-reallocasi terus-menerus padahal ukurannya jarang berubah.

### Perubahan
`X11ChromeRenderer.h` — tambah member:

```cpp
Pixmap m_back_buffer = 0;
unsigned int m_back_buffer_w = 0;
unsigned int m_back_buffer_h = 0;
```

`X11ChromeRenderer.cpp:403` — ganti pembuatan buffer:

```cpp
const unsigned int pixmap_width  = static_cast<unsigned int>(client_width);
const unsigned int pixmap_height = static_cast<unsigned int>(client_height);

if (m_back_buffer == 0 || m_back_buffer_w != pixmap_width ||
    m_back_buffer_h != pixmap_height) {
  if (m_back_buffer) XFreePixmap(m_display, m_back_buffer);
  m_back_buffer = XCreatePixmap(
      m_display, window_handle, pixmap_width, pixmap_height,
      static_cast<unsigned int>(DefaultDepth(m_display, m_screen)));
  m_back_buffer_w = pixmap_width;
  m_back_buffer_h = pixmap_height;
}
if (m_back_buffer == 0) return;
Pixmap back_buffer = m_back_buffer;
```

- Hapus `XFreePixmap` di akhir fungsi redraw.
- Bebaskan `m_back_buffer` di destructor renderer.
- Lakukan pola yang sama pada `paint_popup()` (line 1332).

### Catatan
- Dengan Pixmap persistent, memori buffer hidup di **X server** (proses terpisah) — tidak terhitung di RSS client, persis seperti GDI kernel pool di Windows. Ini keuntungan tambahan dari reuse.
- Recreate hanya saat ukuran jendela berubah (resize).

## 4. Implementasi 3 — Dirty-region clipping

### Masalah
Win32 memotong drawing ke `paint_data.rcPaint` + `IntersectClipRect` (`Win32Window.cpp:1660`). X11 saat ini mengabaikan rect yang dikirim event `Expose` (`X11Window.cpp:703`) dan selalu redraw penuh.

### Perubahan
`X11Window.cpp`, `case Expose:` — ambil rect kotor dan teruskan ke renderer:

```cpp
case Expose: {
  const XExposeEvent &e = event.xexpose;
  // redraw hanya area e.x, e.y, e.width, e.height
  // 1) XftDrawSetClipRectangles(m_draw, e.x, e.y, &rect, 1) di draw_text
  // 2) XSetClipRectangles(gc, e.x, e.y, &rect, 1) sebelum XFillRectangle
  // 3) XCopyArea back buffer -> window hanya pada rect tsb
  break;
}
```

### Catatan
- Event Expose di X11 bisa datang seri (beberapa rect untuk satu region); pertimbangkan buffer rect & intersect.
- Efek terasa saat drag selection & scroll: pekerjaan + alokasi per frame turun drastis.

## 5. Implementasi 4 — Cache SVG/PNG terbatas (eviction)

### Masalah
`Source/Platform/X11/Components/StudioWorkspaceRenderer.h:283` — `m_svg_cache` (unordered_map) menyimpan setiap `XImage` selamanya tanpa batas (icon, language badges, PNG, dll) — kebocoran memori bertahap.

### Perubahan
`StudioWorkspaceRenderer.cpp` (titik insert cache: line 1059, 1212, 1381):

```cpp
if (m_svg_cache.size() >= 64) {
  auto oldest = m_svg_cache.begin();   // atau gunakan policy lain (LRU)
  XDestroyImage(oldest->second);
  m_svg_cache.erase(oldest);
}
m_svg_cache[cache_key] = image;
```

### Catatan
- Batas 64 adalah nilai awal; sesuaikan berdasarkan jumlah icon aktual tema.
- `XDestroyImage` wajib sebelum erase agar halaman bitmap benar-benar dibebaskan.
- Destructor sudah benar (`StudioWorkspaceRenderer.cpp:723-731`).

## 6. Opsional — Allocator reclaimable (padanan kernel pool Linux)

### Masalah
GDI bitmap Windows dialokasikan di kernel paged pool sehingga tidak terhitung di working set proses. Linux tidak punya mekanisme itu untuk aplikasi user-space.

### Padanan
1. **Serahkan buffer ke X server** (sudah tercakup di Implementasi 2) — `XCreatePixmap` adalah "kernel pool"-nya X11.
2. **memfd/tmpfs + mmap** untuk buffer besar in-process — halaman jadi file-backed & reclaimable (kernel bisa drop saat tekanan RAM tanpa swap):

```cpp
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

static void *alloc_reclaimable(size_t size) {
  int fd = memfd_create("zde_backbuffer", 0);   // anonim, di tmpfs (RAM)
  if (fd < 0) return nullptr;
  if (ftruncate(fd, size) != 0) { close(fd); return nullptr; }
  void *ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);                                    // mmap tetap valid
  return ptr;
}
```

### Catatan
- Terapkan ini **setelah** Implementasi 1-4; biasanya sudah cukup tanpa memfd.
- Di `ps` halaman ini tampil sebagai file-mapped, bukan anon.

## 7. Scope Cocoa (future)

- `CocoaWindow.mm:111` memakai `NSBackingStoreBuffered` — window menyimpan full backing store terus-menerus. Opsi: `NSBackingStoreRetained`/lazy, atau pertimbangkan hapus backing store saat window obscured.
- CoreText font cache dikelola sistem (tidak bisa di-opt manual) — tidak ada tindakan.
- Terapkan pola reuse yang sama untuk CGContext / CTLine.

## 8. Kriteria Keberhasilan (Acceptance Criteria)

1. `RssAnon` ZDE pada Linux (X11) turun mendekati private working set Win32 (target: anon <= 4 MB di idle, debug build).
2. Tidak ada regresi FPS saat scroll, drag selection, atau animasi menu.
3. Ukur sebelum/sesudah dengan: `smem -P ZDE -k` atau baca `/proc/<pid>/status` (VmRSS, RssAnon).
4. Tidak ada kebocoran: jalankan dengan memindahkan file besar bolak-balik; pastikan `VmRSS` stabil.
5. Resize window tetap mulus (buffer recreate path berjalan benar).

## 9. Urutan Eksekusi & Estimasi

| # | Task | File | Estimasi |
|---|---|---|---|
| 1 | Reuse XftDraw | `Source/Utility/Fonts.h` | 0.5 hari |
| 2 | Back buffer Pixmap persistent | `X11ChromeRenderer.cpp/h` | 1 hari |
| 3 | Dirty-region clipping | `X11Window.cpp` + renderer | 1-2 hari |
| 4 | SVG cache eviction | `StudioWorkspaceRenderer.cpp/h` | 0.5 hari |
| 5 | (Opsional) memfd allocator | `Source/Utility/` baru | 1 hari |
| 6 | Benchmark & verifikasi | — | 0.5 hari |

Total: ~3-5 hari kerja.

## 10. Referensi

- `Source/Utility/Fonts.h` — font impl Win32/X11/Cocoa (XftDraw churn di line 276)
- `Source/Platform/Win32/Win32Window.cpp:1660` — pola rcPaint + IntersectClipRect
- `Source/Platform/X11/Components/X11ChromeRenderer.cpp:403,1332` — back buffer per-frame
- `Source/Platform/X11/Components/StudioWorkspaceRenderer.h:283` — SVG cache tanpa batas
- `Source/Platform/X11/X11Window.cpp:703` — handler Expose
- `Source/Platform/Cocoa/CocoaWindow.mm:111` — backing store Cocoa
