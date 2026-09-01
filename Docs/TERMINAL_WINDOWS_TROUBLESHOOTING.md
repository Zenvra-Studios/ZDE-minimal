# Panduan Kompatibilitas & Pemecahan Masalah Terminal Windows (ZDE)

Dokumen ini menyediakan panduan lengkap mengenai cara kerja terminal terintegrasi ZDE di sistem operasi Windows (Windows 10, Windows 11, serta edisi Windows mod/debloated seperti Ghost Spectre, AtlasOS, ReviOS, Tiny11, dan custom ISO lainnya).

---

## 1. Variabel Lingkungan (Environment Variables) yang Didukung

ZDE menyediakan sejumlah variabel lingkungan untuk mengatur perilaku terminal secara manual jika diperlukan:

| Variabel | Nilai Contoh | Penjelasan |
|---|---|---|
| `ZDE_SHELL` | `C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe` | Memaksa ZDE menggunakan executable shell tertentu (prioritas #0). Default PowerShell-only; set ke `cmd.exe`/`bash.exe`/`wsl` hanya jika butuh manual override. |
| `ZDE_NO_CONPTY` | `1` atau `true` | Menonaktifkan Windows Pseudoconsole (ConPTY) API dan memaksa penggunaan mode Anonymous Pipe standar. Sangat berguna pada Windows mod yang komponen ConPTY-nya rusak/dipangkas. |
| `ZDE_LIVENESS_MS` | `2000` (dalam milidetik) | Mengatur durasi *liveness check* saat inisialisasi shell (default: `1500` ms untuk PowerShell/pwsh, `300` ms untuk cmd). |
| `ZDE_TERMINAL_LOG` | `0` | Mematikan logging diagnostik terminal ke `%TEMP%\zde-terminal.log` (default: aktif). |

---

## 2. Diagnostik Mandiri: `ZDE Doctor`

Untuk melihat diagnosa lengkap sistem, postur keamanan, dan subsistem terminal tanpa membuka debugger:

Jalankan perintah berikut di Command Prompt atau PowerShell:
```cmd
ZDE.exe --doctor
```

Atau buka menu **Help** -> **About ZDE** dan klik tombol **Copy** untuk menyalin status diagnosa sistem ke clipboard.

Laporan Doctor mencakup:
- Edisi & build kernel Windows (termasuk deteksi franken-mod)
- Status **Smart App Control (SAC)**
- Ketersediaan Service Windows Defender (`WinDefend`)
- Kesehatan runtime **.NET Framework 4.8+**
- Status klasifikasi: `HEALTHY`, `DEBLOATED`, atau `BROKEN`
- Jejak log terminal terbaru (`zde-terminal.log`)

---

## 3. Matriks Gejala, Penyebab, dan Solusi

| Gejala / Pesan Error | Kemungkinan Penyebab | Solusi |
|---|---|---|
| `[Process crashed with NTSTATUS 0xC0000005 — STATUS_ACCESS_VIOLATION]` | Komponen .NET Framework / CLR sistem rusak pada Windows mod/debloat (PowerShell 5.1 adalah aplikasi .NET). | 1. Pasang PowerShell 7 (`pwsh.exe`) resmi.<br>2. Jalankan `sfc /scannow` pada Command Prompt Administrator.<br>3. Atau gunakan cmd sementara via `set ZDE_SHELL=C:\Windows\System32\cmd.exe`. |
| `[Process crashed with NTSTATUS 0xC0000022 — STATUS_ACCESS_DENIED]` | Diblokir oleh **Smart App Control (SAC)** atau Windows Defender Application Control (WDAC) pada Windows 11 mod yang tidak memiliki infrastruktur cloud verdict. | Nonaktifkan SAC melalui Command Prompt Administrator:<br>`reg add "HKLM\SYSTEM\CurrentControlSet\Control\CI\Policy" /v VerifiedAndReputablePolicyState /t REG_DWORD /d 0 /f`<br>lalu restart PC. |
| `[Process crashed with NTSTATUS 0xC0000135 — STATUS_DLL_NOT_FOUND]` | Komponen DLL sistem / dependensi C++ Runtime terhapus oleh mod debloat. | Instal ulang *Microsoft Visual C++ Redistributable (2015–2022)* dan .NET Framework 4.8.1. |
| `[Process crashed with NTSTATUS 0xC0000142 — STATUS_DLL_INIT_FAILED]` | Inisialisasi console subsystem atau ConPTY gagal di level kernel/conhost. | Set `set ZDE_NO_CONPTY=1` sebelum menjalankan ZDE untuk menggunakan fallback pipe mode. |
| `[Process exited: pseudoconsole closed (0xC0000B5B)]` | Pseudoconsole ditutup secara normal saat tab terminal ditutup oleh pengguna. | Normal (bukan error). |

---

## 4. Mekanisme Pemulihan Otomatis di ZDE

ZDE dilengkapi lapisan proteksi bertingkat untuk memastikan terminal selalu dapat digunakan:

1. **Liveness Check & Candidate Blacklist (PowerShell-only)**:
   Jika `pwsh.exe`/`powershell.exe` mati instan (<1.5 detik) karena CLR crash atau SAC timeout, ZDE otomatis fallback ke kandidat PowerShell berikutnya dan mencatat yang rusak ke blacklist in-memory sesi agar tab baru berikutnya langsung coba kandidat PowerShell sehat tanpa menunggu kegagalan berulang. Tidak ada fallback diam-diam ke `cmd.exe`/`bash`.
2. **Transient Retry**:
   Untuk kandidat pertama yang gagal akibat cold-start / SAC verdict lag, ZDE mencoba satu kali retry transien setelah jeda 500 ms sebelum beralih ke fallback.
3. **Persistensi Shell Sehat (`last_good_shell`)**:
   Setelah sebuah shell berhasil hidup normal >= 3 detik, path shell tersebut disimpan secara persisten di `%LOCALAPPDATA%\ZDE\terminal_last_good_shell.txt`. Pada pembukaan ZDE sesi berikutnya, shell ini menjadi prioritas utama. Hanya shell keluarga PowerShell (`pwsh.exe` / `powershell.exe`) yang dipersist; `cmd.exe` dan `bash.exe` tidak pernah dipersist agar sesi berikutnya selalu mencoba PowerShell dulu.
4. **Crash-Safe File Logging**:
   Seluruh siklus hidup shell tercatat di `%TEMP%\zde-terminal.log` (dibatasi otomatis maksimal 1 MB).

---

## 5. Kebijakan PowerShell-Only di Windows (ConPTY)

Sejak perbaikan `Source/Terminal/TerminalSession.cpp:363`, terminal Windows bersifat **PowerShell-only** sesuai permintaan "powershell aja ga harus ke switch cmd":

> Windows native-only kini = PowerShell saja. `cmd.exe` dan `bash.exe` (WSL/Git/Scoop) **tidak lagi di-auto-discover** agar ConPTY tidak switch diam-diam saat PowerShell transient gagal.

**Urutan kandidat default (ConPTY → Pipe fallback, keduanya sama) — hanya PowerShell:**

1. `ZDE_SHELL` (jika di-set — satu-satunya cara memaksa `cmd.exe`/`bash.exe`/WSL)
2. `last_good_shell` (persist dari sesi sebelumnya, hanya PowerShell-family `pwsh.exe`/`powershell.exe`)
3. `pwsh.exe` — PowerShell 7+ (`C:\Program Files\PowerShell\7\pwsh.exe`, `%LOCALAPPDATA%\Programs\PowerShell\7\pwsh.exe`, dan `PATH`)
4. `powershell.exe` — Windows PowerShell 5.1 (`C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe` + `PATH`)

Tidak ada `cmd.exe` / `bash.exe` otomatis — baik `C:\Windows\System32\cmd.exe`, `C:\Program Files\Git\bin\bash.exe`, `Scoop`, maupun `WSL bash.exe` (`System32\bash.exe`) **sengaja dihapus** dari `resolve_host_shell_candidates()`. Jika PowerShell semua gagal, terminal menampilkan error alih-alih fallback diam-diam ke cmd.

**Mengapa bash/cmd tidak ikut lagi saat ConPTY dibuka?**
Sebelumnya kandidat non-PowerShell dimasukkan unconditional sebagai #5–6. Jika PowerShell transient gagal (CLR init lambat, SAC verdict timeout 50ms liveness), shell berikutnya yang sehat — sering `cmd`/`Git Bash` — langsung dipilih dan di-persist, sehingga tab berikutnya langsung cmd/bash padahal PowerShell sudah sehat. Sekarang ConPTY **hanya** mencoba `pwsh → powershell`. Jika Anda memang butuh cmd/bash/WSL, paksa eksplisit via `ZDE_SHELL`:

```cmd
:: PowerShell 5.1 eksplisit (default)
set ZDE_SHELL=C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe
ZDE.exe

:: Cmd (hanya jika benar-benar dibutuhkan)
set ZDE_SHELL=C:\Windows\System32\cmd.exe
ZDE.exe

:: Git Bash / WSL (hanya jika benar-benar dibutuhkan)
set ZDE_SHELL=C:\Program Files\Git\bin\bash.exe
ZDE.exe
set ZDE_SHELL=C:\Windows\System32\bash.exe
ZDE.exe
```
