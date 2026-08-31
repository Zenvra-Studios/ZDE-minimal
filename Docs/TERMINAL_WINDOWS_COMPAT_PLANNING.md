# Planning: Kompatibilitas Terminal ZDE di Windows 10/11 (Original & Mod/Debloat)

Dokumen ini merangkum hasil investigasi insiden **"[Process exited with code 5] — ERROR_ACCESS_DENIED: shell access was blocked (check UAC/manifest/antivirus)"** pada terminal terintegrasi ZDE di Windows 11 25H2 (build 26200) versi **mod/debloat** (Ghost Spectre, custom ISO, dsb.), serta rencana implementasi perbaikannya agar ZDE robust di **semua varian Windows**: original maupun mod.

Insiden: error muncul di mesin Windows 11 mod, padahal di Windows 10 original terminal jalan normal. Investigasi dilakukan 26 Agu 2026 via sesi debugging langsung (reproduksi ConPTY, event log, registry, ACL, kebijakan keamanan).

---

## 1. Ringkasan Temuan

| # | Temuan | Dampak ke ZDE | Status |
|---|--------|---------------|--------|
| T1 | Pesan error ZDE **menyamakan exit code proses dengan Win32 error code**. Exit code 5 dari `powershell.exe` BUKAN berarti `ERROR_ACCESS_DENIED`. Jika proses crash, exit code-nya adalah NTSTATUS (contoh: `0xC0000005` = `3221225477`, bukan "5") | Pesan hint "check UAC/manifest/antivirus" **menyesatkan** dan mengarahkan debugging ke jalur yang salah | 🔴 Belum fix |
| T2 | **Smart App Control (SAC)** aktif di mesin mod (`VerifiedAndReputablePolicyState=2`, policy `VerifiedAndReputableDesktopEvaluation` ter-enforce). SAC **hanya ada di Windows 11** — tidak ada di Windows 10 → menjelaskan "di Win10 aman, di Win11 mod error" | SAC butuh infrastruktur verdict (Defender/cloud) yang sudah dicabut mod → verdict proses bisa gagal → shell mati saat inisialisasi | 🔴 Belum fix |
| T3 | Mod menghapus **Windows Defender total**: service `WinDefend` tidak terdaftar (error 1060), WMI provider Defender rusak (`Provider load failure 0x80041013`), `SecurityHealthService` disabled | Tidak bisa memblokir (sudah tidak ada), tapi **penghapusannya** yang mematahkan rantai verdict SAC/WMI | ℹ️ Kondisi sistem |
| T4 | `powershell.exe` pernah **crash `System.AccessViolationException`** (Event Log Application, 24 Agu 00:46) — crash level CLR/.NET. Aplikasi .NET lain (HandBrake) juga gagal ("You must install .NET") | PowerShell 5.1 = aplikasi .NET Framework. CLR rusak → shell mati **sebelum sempat render prompt** (terminal kosong total seperti pada screenshot insiden) | ℹ️ Kondisi sistem |
| T5 | UAC **terbukti BUKAN penyebab**: `EnableLUA=1`, `ConsentPromptBehaviorAdmin=0`, user = built-in Administrator (selalu full token High IL), manifest ZDE `asInvoker` | Hint "check UAC" pada pesan error salah sasaran | ✅ Tersingkap |
| T6 | Tidak ada blokir lain: AppLocker/SRP kosong, tidak ada IFEO `powershell.exe`, Exploit Protection `NOTSET`, tidak ada AV pihak ketiga terdaftar (SecurityCenter2 kosong), ACL `powershell.exe` (64 & 32-bit) normal | Mempersempit penyebab ke faktor mod: SAC + komponen rusak | ✅ Tersingkap |
| T7 | **Reproduksi ConPTY (parent GUI, persis kode ZDE)**: setelah reboot (26 Agu 06:41), `powershell.exe` 64-bit (3×), 32-bit (SysWOW64), dan `cmd.exe` **semua sehat** — hidup >3 detik, output mengalir via pseudoconsole | Error insiden bersifat **transient/state-dependent** (kemungkinan sebelum reboot, saat SAC policy baru di-refresh 24 Agu 22:36 & 25 Agu 20:22) | ✅ Terverifikasi |
| T8 | Kode terminal sudah punya mitigasi awal yang **terbukti arahnya benar**: liveness check 300ms (`TerminalSession.cpp:554-573, 660-673`), fallback ConPTY→pipe, env `ZDE_NO_CONPTY=1`, override `ZDE_SHELL` | Fondasi sudah ada, perlu diperkuat + diagnostik | 🟡 Perlu diperkuat |
| T9 | Log diagnostik terminal (`std::cerr`, prefix `[ZDE Terminal]`) **hilang total di GUI app** — saat insiden tidak ada jejak sama sekali | Debugging jadi spekulatif; harus diarahkan ke file log (konvensi sama dengan `zde-lsp.log`) | 🔴 Belum fix |

---

## 2. Bukti Verifikasi (dari sesi debug 26 Agu 2026)

```text
# Sistem
Windows 11 25H2 build 26200.9168, ProductName tertulis "Windows 10 Home Single Language" (tanda franken-mod)
UAC: EnableLUA=1, ConsentPromptBehaviorAdmin=0, FilterAdministratorToken=(kosong/default)
Boot terakhir sebelum sesi debug: 26 Agu 06:41 (uptime 1 jam)

# Keamanan
sc qc WinDefend          -> [SC] OpenService FAILED 1060 (tidak terinstal)
Get-MpComputerStatus     -> Provider load failure (WMI Defender rusak)
VerifiedAndReputablePolicyState = 2 (SAC ON), policy evaluation ter-enforce (citool -lp)
AppLocker/SrpV2/IFEO/Exploit Protection -> tidak ada / NOTSET
SecurityCenter2 AntiVirusProduct -> kosong

# Event Log
24 Agu 00:46  Application Error/.NET Runtime: powershell.EXE terminated,
              System.AccessViolationException  (sejalan dengan insiden exit-mati instan)

# Reproduksi ConPTY parent-GUI (mimik persis ZDE: CreatePseudoConsole +
# PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE + EXTENDED_STARTUPINFO_PRESENT,
# cmdline: powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass)
System32\powershell.exe  x3 -> HIDUP >3000ms, output 173 byte  (SEHAT)
SysWOW64\powershell.exe     -> HIDUP >3000ms, output 173 byte  (SEHAT)
System32\cmd.exe            -> HIDUP >3000ms, output 281 byte  (SEHAT)
```

**Kesimpulan akar masalah:** kombinasi (a) SAC Win11-only yang aktif tanpa infrastruktur verdictnya, dan (b) komponen sistem yang dipatahkan debloat (Defender, .NET/CLR, WMI). Bukan UAC, bukan Defender yang memblokir, bukan ACL/kebijakan. Sifatnya transient/state-dependent sehingga perlu **deteksi + diagnostik + fallback yang tangguh**, bukan sekadar "fix satu bug".

---

## 3. Rencana Implementasi

> Checklist `- [ ]` siap dieksekusi oleh AI agent. Urutan = prioritas.

### P0-1: Perbaiki pelaporan exit code (jangan samakan dengan Win32 error)

**File:** `Source/Terminal/TerminalSession.cpp` (`poll()`, baris ±1053–1076)

**Masalah:** `exit_code` proseshint di-mapping langsung ke Win32 error (`case 5: ERROR_ACCESS_DENIED ...`). Secara semantik salah: exit code 5 bisa berarti apa pun (termasuk script yang `exit 5`), dan crash justru menghasilkan NTSTATUS besar (`0xC0000005` = 3221225477) yang jatuh ke `default:` tanpa penjelasan.

**Langkah:**
1. `-[x]` Jika `exit_code > 0x80000000` → decode sebagai **NTSTATUS**: tampilkan hex + nama bila dikenal. Mapping minimal:
   - `0xC0000005` → "crash: access violation (kemungkinan komponen .NET/console rusak pada Windows debloat)"
   - `0xC0000409` → "crash: stack buffer overrun / fail-fast"
   - `0xC0000135` → "DLL tidak ditemukan (system32 component missing)"
   - `0xC0000142` → "inisialisasi DLL/console gagal"
   - `0xC0000022` → "access denied di level code-integrity (kemungkinan diblokir Smart App Control/WDAC)"
   - `0xC0000B5B` → "pseudoconsole ditutup (normal saat user menutup tab)"
2. `-[x]` Untuk exit code kecil (0–1024), ubah hint menjadi netral: "process exited with code N (bukan Win32 error; lihat log terminal untuk detail)" — **hapus klaim pasti** seperti "shell access was blocked".
3. `-[x]` Tambahkan baris kedua berisi **nama shell + path** yang di-spawn (`m_shell_path`) dan apakah mode ConPTY atau pipe.

**Kriteria selesai:** Crash `0xC0000005` tampil sebagai "crash: access violation …", bukan "check UAC". Unit test untuk decoder NTSTATUS (fungsi murni, mudah dites).

---

### P0-2: Logging diagnostik terminal ke file

**File:** `Source/Terminal/TerminalSession.cpp` (helper log baru), konvensi mengikuti `%TEMP%\zde-lsp.log`

**Masalah:** Semua log `[ZDE Terminal] ...` ditulis ke `std::cerr` yang **buang siang-siang** di GUI app. Saat insiden, nol jejak.

**Langkah:**
1. `-[x]` Buat helper `terminal_debug_log(const std::string&)` yang append ke `%TEMP%\zde-terminal.log` (atau `%LOCALAPPDATA%\ZDE\logs\` bila sudah ada konvensi folder log). Guard dengan mutex, buka-tutup per tulisan (crash-safe).
2. `-[x]` Ganti semua `std::cerr << "[ZDE Terminal] ..."` di `start()`/liveness check/pipe fallback dengan helper ini. Event yang wajib tercatat:
   - daftar kandidat shell hasil `resolve_host_shell_candidates()` + kandidat mana yang lolos `is_valid_executable_file`
   - `CreatePseudoConsole` OK/FAILED (hr), `UpdateProcThreadAttribute` OK/FAILED
   - `CreateProcessW` OK (pid) / FAILED (Win32 error)
   - liveness check: "died within Nms, exit code X" per kandidat
   - mode final yang dipakai (ConPTY/pipe) + working directory
3. `-[x]` Batasi ukuran file (mis. truncate jika >1 MB) agar tidak menumpuk.
4. `-[x]` Env override `ZDE_TERMINAL_LOG=0` untuk mematikan.

**Kriteria selesai:** Saat membuka terminal, `zde-terminal.log` berisi jejak lengkap spawn tanpa perlu debugger.

---

### P1-1: Deteksi Smart App Control + panduan pengguna

**File:** baru `Source/Terminal/WindowsSecurityProbe.h/.cpp` (atau `Source/Utility/`), dipanggil dari `start()` saat gagal

**Masalah:** SAC hanya ada di Win11 dan di Windows debloat statusnya bisa ON tanpa infrastruktur verdict → spawn shell bisa gagal/transient. ZDE saat ini buta terhadap kondisi ini.

**Langkah:**
1. `-[x]` Baca registry `HKLM\SYSTEM\CurrentControlSet\Control\CI\Policy\VerifiedAndReputablePolicyState` (DWORD): `0`=off, `1`=evaluation, `2`=on.
2. `-[x]` Simpan hasil probe (struct `WindowsSecurityInfo { sac_state; defender_service_present; ... }`) — probe **sekali per sesi ZDE** (cache), bukan per spawn.
3. `-[x]` Jika spawn shell gagal berulang DAN `sac_state != 0` → tampilkan status hint di terminal:
   ```text
   [ZDE] Smart App Control terdeteksi AKTIF (state=2). Pada Windows debloat tanpa Defender,
   SAC dapat membuat shell gagal dimulai. Matikan via registry:
     reg add "HKLM\SYSTEM\CurrentControlSet\Control\CI\Policy" /v VerifiedAndReputablePolicyState /t REG_DWORD /d 0 /f
   lalu restart PC.
   ```
4. `-[x]` Jangan tampilkan hint ini kalau spawn sukses (hindari noise).

**Kriteria selesai:** Pada mesin dengan SAC ON + spawn gagal, hint muncul di terminal; pada mesin sehat tidak ada output tambahan.

---

### P1-2: Deteksi "Windows debloat/bermasalah" + health check ringan

**File:** `WindowsSecurityProbe` yang sama (P1-1)

**Masalah:** Windows mod mematahkan komponen yang dipakai rantai spawn shell (Defender, .NET/CLR, WMI, conhost). ZDE perlu tahu dia berjalan di lingkungan seperti ini untuk memberi diagnosa yang tepat.

**Langkah:**
1. `-[x]` Probe ringan (tanpa WMI yang bisa rusak — pakai API/registry langsung):
   - Defender ada? → `OpenServiceW(SC_MANAGER_CONNECT, L"WinDefend")` gagal `ERROR_SERVICE_DOES_NOT_EXIST` = dihapus.
   - Tanda franken-mod → bandingkan `ProductName` (`HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion`) vs `CurrentBuild` (contoh insiden: "Windows 10" tapi build 26200 = Win11 25H2).
   - .NET Framework sehat? → cek `HKLM\SOFTWARE\Microsoft\NET Framework Setup\NDP\v4\Full\Release` ada & >= 528040; opsional: spawn sekali `powershell -NoProfile -Command $PSVersionTable.PSVersion` dengan timeout 5 dtk sebagai canary (hanya saat probe, tidak per spawn terminal).
2. `-[x]` Klasifikasi: `HEALTHY` / `DEBLOATED` (Defender hilang ≥1 tanda) / `BROKEN` (canary .NET gagal atau conhost bermasalah).
3. `-[x]` Saat `DEBLOATED/BROKEN` dan spawn gagal → hint spesifik per gejala (crash NTSTATUS `0xC0000005`/`0xC0000135` → sarankan repair .NET (`sfc /scannow`, installer .NET Framework 4.8.1) dan/atau pakai `cmd.exe` sementara).
4. `-[x]` Tambahkan halaman/ringkasan di About/Diagnostics (P2-1) menampilkan hasil probe.

**Kriteria selesai:** Di mesin mod insiden, ZDE melaporkan `DEBLOATED` dengan alasan konkret; di Win10/11 original melaporkan `HEALTHY`.

---

### P1-3: Pergurangan kandidat shell yang mati + persistensi shell sehat

**File:** `Source/Terminal/TerminalSession.cpp` (`start()`, `resolve_host_shell_candidates()`), `Config/`

**Masalah:** Liveness check 300ms sudah ada, tapi (a) hasilnya tidak diingat — setiap tab baru mengulang percobaan ke kandidat yang sama yang rusak; (b) 300ms terlalu pendek untuk CLR lambat di mesin debloat (powershell butuh init .NET); (c) tidak ada retry untuk kegagalan transient (kasus insiden: SAC verdict timeout).

**Langkah:**
1. `-[x]` Naikkan liveness check 300ms → **1500ms** untuk powershell/pwsh (init CLR), tetap 300ms untuk cmd. Konstanta bernama + env override `ZDE_LIVENESS_MS`.
2. `-[x]` Catat kandidat yang "died immediately" ke blacklist **in-memory per sesi** (urutan coba berikutnya melewatkannya).
3. `-[x]` Persistensi lintas sesi: simpan shell terakhir yang **lolos 3 detik hidup** ke config (`Config/`, key `terminal.last_good_shell`). Saat `resolve_host_shell_candidates()`, taruh `last_good_shell` sebagai kandidat #0 (di atas pwsh), kecuali file-nya sudah tidak ada.
4. `-[x]` Retry sekali untuk kandidat pertama: jika ConPTY spawn sukses tapi proses mati <2 dtk **tanpa output sama sekali** (pola gagal-init/SAC), tunggu 500ms lalu spawn ulang sekali sebelum fallback ke kandidat berikutnya. Catat di log (P0-2).
5. `-[x]` Pastikan urutan kandidat Windows tetap: `ZDE_SHELL` → `last_good_shell` → pwsh7 → powershell 5.1 (System32, SysWOW64, PATH) → cmd → git bash.

**Kriteria selesai:** Pada mesin yang powershell-nya rusak, tab pertama gagal → fallback cmd, tab berikutnya langsung buka cmd tanpa menunggu gagal lagi; config berisi `last_good_shell`.

---

### P2-1: Panel/CLI diagnostics (`ZDE Doctor`)

**File:** baru `Source/Utility/Doctor.*` + entry menu About/Help

**Masalah:** Saat ini diagnosa butuh sesi debugging manual (seperti insiden ini).

**Langkah:**
1. `-[x]` Kumpulkan: versi Windows (build + UBR), hasil `WindowsSecurityProbe` (SAC, Defender, .NET), mode terminal aktif, `last_good_shell`, isi ringkas `zde-terminal.log`.
2. `-[x]` Tampilkan di modal About bagian "Diagnostics" + tombol "Copy report" (teks siap paste untuk bug report).
3. `-[x]` Opsional CLI: `ZDE.exe --doctor` mencetak report ke stdout lalu keluar (mudah di-automate AI agent saat debugging).

**Kriteria selesai:** `ZDE.exe --doctor` menghasilkan report satu layar yang menjawab: OS apa, SAC?, Defender?, .NET?, shell mana yang dipakai, error terakhir apa.

---

### P2-2: Dokumentasi pengguna (Windows mod)

**File:** baru `Docs/TERMINAL_WINDOWS_TROUBLESHOOTING.md` + README

**Langkah:**
1. `-[x]` Jelaskan env yang didukung: `ZDE_SHELL`, `ZDE_NO_CONPTY=1`, `ZDE_LIVENESS_MS`, `ZDE_TERMINAL_LOG`.
2. `-[x]` Tabel gejala → penyebab → solusi (exit code NTSTATUS umum, SAC, Defender dihapus, .NET rusak) — sumber: dokumen ini bagian 2.
3. `-[x]` Catatan eksplisit: ZDE tetap berusaha jalan di Windows debloat (Ghost Spectre dll.) tapi komponen yang dihapus oleh mod di luar kontrol ZDE; sertakan langkah repair umum.

**Kriteria selesai:** Pengguna Windows mod bisa self-solve dari dokumen tanpa debugging source.

---

### P3 (opsional): Hardening lanjutan

- [ ] Canary spawn ringan saat startup ZDE (spawn `cmd /c exit` via ConPTY sekali; jika gagal → set flag "console broken", semua terminal langsung pipe-mode tanpa mencoba ConPTY).
- [ ] Deteksi App Execution Alias stub `pwsh.exe` (0-byte reparse point di `WindowsApps`) sudah ada (`is_valid_executable_file`) — tambahkan unit test khusus stub.
- [ ] Pertimbangkan bundling fallback kecil: jika SEMUA shell sistem gagal, tawarkan spawn `plugins/lsp/clangd.exe`? (tidak — jangan; cukup pesan error yang jelas + dokumen).

---

## 4. Matriks Pengujian

| Skenario | Win10 original | Win11 original | Win11 debloat (Ghost Spectre dsb.) |
|---|---|---|---|
| Tab terminal pertama (powershell) | spawn OK via ConPTY | spawn OK via ConPTY | spawn OK **atau** fallback cepat + log jelas |
| Powershell di-break (simulasi: rename/ACL deny) | fallback cmd <2 dtk | sama | sama + `last_good_shell` tersimpan |
| SAC ON + verdict gagal (simulasi: registry state=2) | n/a (SAC tak ada) | hint SAC tampil | hint SAC tampil, retry 1× jalan |
| .NET rusak (uninstall/registry Release dihapus) | hint repair .NET | hint repair .NET | hint repair .NET + fallback cmd |
| Tutup/buka tab cepat 10× | tidak ada handle leak (cek Task Manager conhost) | sama | sama |
| `ZDE.exe --doctor` | report HEALTHY | report HEALTHY + SAC state akurat | report DEBLOATED + alasan |

**Definisi selesai global:** di ketiga kolom, membuka terminal **tidak pernah** menampilkan pesan error tanpa diikuti (1) fallback otomatis yang sukses, atau (2) hint yang actionable + jejak lengkap di `zde-terminal.log`.

---

## 5. Urutan Eksekusi yang Disarankan (untuk AI agent)

1. P0-1 (pesan exit code) + P0-2 (file log) → **dulu**, karena semua langkah berikutnya butuh observabilitas.
2. P1-3 (liveness 1500ms + blacklist + last_good_shell + retry) → menghilangkan mayoritas gejala user.
3. P1-1 + P1-2 (probe SAC/debloat + hint) → diagnosis mandiri.
4. P2-1 (doctor) → alat bantu bug report.
5. P2-2 (docs) → penutup.
6. Setiap item: build `windows-clang-ninja-release`, uji manual di mesin mod insiden + unit test untuk fungsi murni (decoder NTSTATUS, urutan kandidat, klasifikasi probe).
