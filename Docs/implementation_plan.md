# Implementation Plan: Perbaikan Koneksi & Akurasi LSP (clangd) di ZDE Studio

Dokumen ini merangkum temuan debugging koneksi **clangd** di ZDE Studio (Windows) serta rencana implementasi perbaikannya. Hasil verifikasi didapat dari sesi debugging langsung (log `zde-lsp.log`, unit test, dan uji spawn manual).

---

## 1. Ringkasan Temuan

| # | Temuan | Status |
|---|--------|--------|
| T1 | Pipeline LSP (spawn → handshake → Active) **terbukti jalan** di build baru (`windows-x64-clang-ninja-debug`, 19 Agu 12:28) | ✅ Fixed |
| T2 | Pengguna menjalankan build lama `C:\Program Files\ZDE` (instalasi Inno Setup 18 Agu) yang tidak punya perbaikan terbaru | ⚠️ Perlu reinstall |
| T3 | Pencarian `compile_commands.json` memprioritaskan build **Linux** (`build/linux-debug`) → intellisense tidak akurat di Windows | 🔴 Belum fix |
| T4 | `on_document_opened` dipanggil berulang (~100x) untuk dokumen yang sama → `did_open` dikirim ulang → clangd parse ulang boros | 🔴 Belum fix |
| T5 | `stderr` clangd dibuang ke `NUL` + `set_error_handler` tidak pernah dipasang → error server tidak terlihat | 🔴 Belum fix |
| T6 | `stop()` memanggil `CancelIoEx` pada handle pipe sinkron (bukan OVERLAPPED) → shutdown bisa menggantung | 🟡 Minor |
| T7 | Project lain (contoh: `volumetric-vulkan-graphics`) hanya punya build Linux → tidak ada compile db Windows yang valid | 🟡 Perlu aksi user |

---

## 2. Bukti Verifikasi (dari sesi debug)

```text
[zde-lsp] profile=cpp filename=...\ActivitySidebar.cpp
[zde-lsp] exe=...\plugins\lsp\clangd.exe
[zde-lsp] compile-dir=...\build\windows-x64-clang-ninja-debug
[zde-lsp] CreateProcessW OK pid=18312
[zde-lsp] client started for cpp
```

* Unit test `ServerRegistryProfileLookup` & `ThirdPartyBinaryClangdDiscovery` → **PASS**.
* Spawn manual clangd 22.1.6 (plugins & install) + handshake `initialize` → **responds OK**.
* `cmakels.log` membuktikan pipeline LSP pernah jalan (19 Agu 00:08).

---

## 3. Rencana Implementasi

### P1-1: Prioritaskan compile_commands.json build Windows

**File:** `Source/Language/LanguageServerManager.cpp` (baris ±71–82)

**Masalah:** Kandidat `direct_candidates` memeriksa `build/linux-debug` lebih awal dan lebih diprioritaskan daripada `build/windows-*`. Di Windows, path `/home/ahmadzanisy/...` tidak valid → clangd fallback heuristics → `std::` kosong / intellisense salah.

**Langkah:**
1. Deteksi platform saat ini (`#if defined(_WIN32)`).
2. Susun ulang urutan kandidat: `build/compile_commands.json` (root) → `build/windows-*` → `build/clang-*` → baru `build/linux-*`.
3. Pada platform Windows, **skip** kandidat `linux-*` dan `macos-*` (dan sebaliknya pada Linux).
4. Pertahankan fallback `directory_iterator` pada `build/` sebagai cadangan terakhir.

**Kriteria selesai:** Log menunjukkan `compile-dir=...\build\windows-x64-*` untuk semua project yang punya build Windows.

---

### P1-2: Deduplikasi `on_document_opened` / `did_open`

**File:** `Source/Language/LanguageServerManager.cpp` (`on_document_opened`) + `Source/Language/Client/LanguageClient.cpp`

**Masalah:** Setiap aktivasi tab / switch dokumen memanggil `on_document_opened` lagi, yang mengirim `textDocument/didOpen` ulang dengan full content → clangd re-parse berulang (boros CPU, completion telat).

**Langkah:**
1. Simpan set URI yang sudah di-open per client: `std::unordered_set<std::string> m_opened_uris`.
2. Di `on_document_opened`: jika URI sudah ada di set → **tidak** kirim `did_open` lagi (tapi tetap boleh refresh semantic tokens bila diperlukan).
3. Hapus URI dari set saat `on_document_closed` / `did_close`.
4. Kirim `did_open` ulang hanya jika konten file berubah dari disk (opsional: bandingkan mtime).

**Kriteria selesai:** Log `on_document_opened` hanya muncul sekali per dokumen per sesi (tidak 100x).

---

### P1-3: Wire `set_error_handler` + log stderr clangd

**File:** `Source/Language/Transport/StdioProcessTransport.cpp`, `Source/Language/Client/LanguageClient.cpp`, `Source/Language/LanguageServerManager.cpp`

**Masalah:** `si.hStdError = NUL` (StdioProcessTransport.cpp:82–90) membuang semua error clangd; `set_error_handler` tidak pernah dipasang.

**Langkah:**
1. Buat pipe stderr terpisah (jangan NUL).
2. Di `LanguageServerManager::get_or_start_client_for_file`, pasang `transport->set_error_handler(...)` yang menulis ke `zde-lsp.log`.
3. Tampilkan error fatal di UI status bar (opsional, fase lanjut).
4. Pertahankan logging debug yang sudah ada (kecil, append-only, di `%TEMP%`).

**Kriteria selesai:** Jika clangd gagal start, alasannya muncul di `%TEMP%\zde-lsp.log` dan tidak lagi "mati senyap".

---

### P1-4: Reinstall / sinkronkan build Program Files

**Masalah:** `C:\Program Files\ZDE` berisi build 18 Agu (ZDE.exe 770 KB) — lebih lama dari build dev (`clang-ninja`, 744 KB).

**Langkah:**
1. Setelah P1-1..P1-3 selesai dan lolos uji, build ulang + jalankan installer Inno Setup.
2. Verifikasi versi: `ZDE.exe` timestamp >= build dev.
3. Pastikan `plugins/lsp/clangd.exe` ikut ter-deploy (sudah otomatis via target build).

---

### P2-5: Perbaikan `stop()` transport (CancelIoEx pada pipe sinkron)

**File:** `Source/Language/Transport/StdioProcessTransport.cpp` (baris ±232)

**Masalah:** `CancelIoEx` hanya bekerja pada handle dengan OVERLAPPED I/O; pipe dibaca sinkron (`ReadFile` blocking di reader thread) → cancel tidak efektif, shutdown bisa hang.

**Langkah (pilih salah satu):**
1. Gunakan `PeekNamedPipe` sebelum `ReadFile` dengan timeout kecil, atau
2. Ganti ke OVERLAPPED I/O untuk `ReadFile` + `CancelIoEx`, atau
3. Pada `stop()`: tutup pipe stdout terlebih dahulu (`CloseHandle`) agar `ReadFile` keluar dengan error, lalu `join()` reader thread.

**Kriteria selesai:** `shutdown_all()` tidak menggantung saat ZDE ditutup.

---

### P3-6: Project tanpa build Windows (opsional, aksi user)

**Masalah:** `volumetric-vulkan-graphics` hanya punya `build/linux-debug` → compile db Linux di Windows.

**Langkah:**
```powershell
# Di root project
cmake --preset windows-x64-ninja-debug        # atau preset ninja lain
# pastikan CMakePresets.json punya preset windows dengan
# CMAKE_EXPORT_COMPILE_COMMANDS=ON
```
Setelah P1-1 diterapkan, ZDE akan otomatis memilih `build/windows-*` yang valid.

---

## 4. Urutan Eksekusi

| Urutan | Item | Estimasi |
|--------|------|----------|
| 1 | P1-1 Prioritas compile db Windows | 0.5 hari |
| 2 | P1-2 Dedup did_open | 0.5 hari |
| 3 | P1-3 stderr + error handler | 0.5 hari |
| 4 | P1-4 Reinstall build | 0.25 hari |
| 5 | P2-5 Fix stop() transport | 0.5 hari |
| 6 | P3-6 Aksi user per project | — |

---

## 5. Verifikasi Akhir

1. Buka project dengan build Windows (`ZDE-minimal`) → ketik `std::` → completion muncul dari clangd.
2. `%TEMP%\zde-lsp.log` menampilkan `compile-dir=...\windows-*` dan tidak ada `CreateProcessW FAILED`.
3. Buka project tanpa build Windows → log menampilkan `no compile_commands.json found` (bukan `linux-debug`).
4. Switch tab bolak-balik → `did_open` tidak terkirim ulang (cek lewat log tambahan bila perlu).
5. Tutup ZDE → tidak hang; semua proses clangd ikut mati.