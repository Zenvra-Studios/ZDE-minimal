# Panduan Packaging, Instalasi, dan Uninstall Linux (ZDE)

Dokumen ini menjelaskan cara mem-build, meng-install, membuat package biner (.deb, .rpm, .pkg.tar.zst), dan melakukan **uninstall bersih** untuk ZDE di berbagai distribusi Linux.

---

## 1. Instalasi Langsung via CMake (Manual Install)

Jika kamu mengompilasi ZDE langsung dari source code dan ingin memasangnya ke sistem operasi Linux:

```bash
# 1. Konfigurasi dan compile dalam mode Release
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build

# 2. Install ke direktori sistem (/usr/bin, /usr/lib/zde, /usr/share/zde, dll.)
sudo cmake --install build
```

Saat proses instalasi selesai, CMake akan mencatat setiap file yang terpasang ke dalam file manifest:
`build/install_manifest.txt`.

---

## 2. Cara Uninstall (Target `uninstall` CMake)

Jika kamu meng-install ZDE menggunakan perintah `cmake --install`, kamu bisa menghapus seluruh file ZDE dari sistem menggunakan target **`uninstall`** yang telah disediakan di [Cmake/Packaging.cmake](../Cmake/Packaging.cmake).

### Perintah Uninstall:
```bash
sudo cmake --build build --target uninstall
```
*Atau jika menggunakan generator Ninja:*
```bash
sudo ninja -C build uninstall
```

### Cara Kerja Target `uninstall`:
1. Skrip pembantu [Cmake/cmake_uninstall.cmake.in](../Cmake/cmake_uninstall.cmake.in) akan membaca file `build/install_manifest.txt`.
2. Setiap file yang terdaftar (executable di `/usr/bin/ZDE`, dynamic libraries di `/usr/lib/zde/`, icon sistem, shortcut desktop `.desktop`, fonts, dan assets syntax) akan dihapus secara otomatis dan aman dari sistem.

> [!NOTE]
> Jika folder `build/` kamu sempat terhapus dan kamu kehilangan `install_manifest.txt`, cukup jalankan kembali `sudo cmake --install build` lalu jalankan `sudo cmake --build build --target uninstall`.

---

## 3. Membuat Package Biner untuk Distro Linux (CPack & Pure CMake)

ZDE mendukung pembuatan installer resmi untuk berbagai keluarga distro Linux langsung melalui target CMake:

| Target Distro | Perintah CMake | Format Hasil | Lokasi Output |
| :--- | :--- | :--- | :--- |
| **Ubuntu / Debian / Linux Mint / Pop!_OS** | `cmake --build build --target package_deb` | `.deb` | `build/packages/` |
| **Fedora / RHEL / CentOS / openSUSE** | `cmake --build build --target package_rpm` | `.rpm` | `build/packages/` |
| **Arch Linux / Manjaro / EndeavourOS** | `cmake --build build --target package_arch` | `.pkg.tar.zst` | `Cmake/packaging/arch/` |
| **Semua Format Sekaligus** | `cmake --build build --target package` | `.deb`, `.rpm`, `.tar.xz` | `build/packages/` |

### Langkah Pembuatan Package:
```bash
# 1. Konfigurasi build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# 2. Compile
cmake --build build

# 3. Buat paket sesuai target distro kamu
cmake --build build --target package_deb    # Untuk Debian/Ubuntu
# ATAU
cmake --build build --target package_rpm    # Untuk Fedora
# ATAU
cmake --build build --target package_arch   # Untuk Arch Linux
```

---

## 4. Cara Install & Uninstall Melalui Package Manager Distro

Jika kamu mendistribusikan atau meng-install ZDE melalui file package installer biner, gunakan package manager bawaan distro kamu:

### A. Debian / Ubuntu / Linux Mint / Pop!_OS (`.deb`)
* **Install:**
  ```bash
  sudo apt install ./build/packages/zde-0.1.0-Linux.deb
  # atau
  sudo dpkg -i ./build/packages/zde-0.1.0-Linux.deb
  ```
* **Uninstall:**
  ```bash
  sudo apt remove zde
  # atau
  sudo dpkg -r zde
  ```

---

### B. Fedora / RHEL / Rocky Linux / openSUSE (`.rpm`)
* **Install:**
  ```bash
  sudo dnf install ./build/packages/zde-0.1.0-Linux.rpm
  # atau
  sudo rpm -i ./build/packages/zde-0.1.0-Linux.rpm
  ```
* **Uninstall:**
  ```bash
  sudo dnf remove zde
  # atau
  sudo rpm -e zde
  ```

---

### C. Arch Linux / Manjaro / EndeavourOS (`.pkg.tar.zst`)
* **Install:**
  ```bash
  sudo pacman -U Cmake/packaging/arch/zde-0.1.0-1-x86_64.pkg.tar.zst
  ```
* **Uninstall:**
  ```bash
  sudo pacman -R zde
  ```

---

## 5. Struktur Penempatan File di Sistem Linux (FHS Standard)

Saat terinstall, ZDE mengikuti kaidah standar Linux Filesystem Hierarchy Standard:

```
/usr/
├── bin/
│   └── ZDE                                # Executable biner utama
├── lib/
│   └── zde/                               # Shared libraries (*.so) internal ZDE
│       ├── libZDEUI.so
│       ├── libZDETerminal.so
│       ├── libZDEPlatformX11.so
│       └── ...
└── share/
    ├── applications/
    │   └── zde.desktop                    # Shortcut menu aplikasi desktop
    ├── icons/hicolor/512x512/apps/
    │   └── zde.png                        # Icon resolusi tinggi
    ├── pixmaps/
    │   └── zde.png                        # Icon fallback
    └── zde/
        ├── Assets/                        # Fonts, icons, dan TextMate syntax grammars
        ├── Resources/                     # Resource bundle
        └── manifest/
            └── runtime.json               # Runtime metadata manifest
```
