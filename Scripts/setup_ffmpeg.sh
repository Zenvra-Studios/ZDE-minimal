#!/usr/bin/env bash
# ==============================================================================
# setup_ffmpeg.sh - Download & configure FFmpeg headers/libraries for Unix-like
#                   systems (Linux, macOS, FreeBSD, OpenBSD, NetBSD)
# ==============================================================================
set -euo pipefail

# ANSI color codes
CLR_RESET="\033[0m"
CLR_CYAN="\033[1;36m"
CLR_GREEN="\033[1;32m"
CLR_YELLOW="\033[1;33m"
CLR_RED="\033[1;31m"
CLR_GRAY="\033[0;90m"

echo -e "${CLR_CYAN}====================================================${CLR_RESET}"
echo -e "${CLR_CYAN}  ZDE Studio - FFmpeg Setup for Unix-like Systems   ${CLR_RESET}"
echo -e "${CLR_CYAN}  (Linux / macOS / FreeBSD / OpenBSD / NetBSD)      ${CLR_RESET}"
echo -e "${CLR_CYAN}====================================================${CLR_RESET}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
TARGET_DIR="${ROOT_DIR}/ThirdParty/ffmpeg"
MODE="auto" # auto | system | download | check

show_help() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Options:
  -t, --target-dir <DIR>  Set destination directory for bundled FFmpeg (default: ThirdParty/ffmpeg)
  -s, --system            Install via OS system package manager (apt, brew, pacman, dnf, etc.)
  -d, --download          Download prebuilt shared binary bundle directly into target directory
  -c, --check             Check if FFmpeg developer headers/libraries are available
  -h, --help              Show this help message

Examples:
  ./setup_ffmpeg.sh                 # Automatic setup (checks system, downloads bundle if needed)
  ./setup_ffmpeg.sh --system        # Install FFmpeg developer packages via system package manager
  ./setup_ffmpeg.sh --download      # Force standalone bundle download into ThirdParty/ffmpeg
  ./setup_ffmpeg.sh --check         # Inspect current FFmpeg setup
EOF
}

# Parse CLI arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        -t|--target-dir)
            TARGET_DIR="$2"
            shift 2
            ;;
        -s|--system)
            MODE="system"
            shift
            ;;
        -d|--download)
            MODE="download"
            shift
            ;;
        -c|--check)
            MODE="check"
            shift
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            echo -e "${CLR_RED}[!] Unknown option: $1${CLR_RESET}"
            show_help
            exit 1
            ;;
    esac
done

OS_TYPE="$(uname -s)"
ARCH_TYPE="$(uname -m)"

echo -e "${CLR_YELLOW}[*] Detected OS:${CLR_RESET}   ${OS_TYPE} (${ARCH_TYPE})"
echo -e "${CLR_YELLOW}[*] Target Dir:${CLR_RESET}    ${TARGET_DIR}"

# Function to check whether headers and libraries exist
has_ffmpeg_headers() {
    if [[ -f "${TARGET_DIR}/include/libavcodec/avcodec.h" ]]; then
        return 0
    fi
    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists libavcodec libavformat libavutil libswscale libswresample 2>/dev/null; then
        return 0
    fi
    for p in "/usr/include" "/usr/local/include" "/opt/homebrew/include" "/opt/local/include"; do
        if [[ -f "${p}/libavcodec/avcodec.h" ]]; then
            return 0
        fi
    done
    return 1
}

# Mode: Check only
if [[ "${MODE}" == "check" ]]; then
    echo -e "${CLR_CYAN}[*] Checking FFmpeg installation status...${CLR_RESET}"
    if has_ffmpeg_headers; then
        echo -e "${CLR_GREEN}[+] FFmpeg development libraries are available!${CLR_RESET}"
        if [[ -f "${TARGET_DIR}/include/libavcodec/avcodec.h" ]]; then
            echo -e "    - Bundled path: ${TARGET_DIR}"
        fi
        if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists libavcodec 2>/dev/null; then
            echo -e "    - pkg-config version: $(pkg-config --modversion libavcodec 2>/dev/null || echo 'N/A')"
            echo -e "    - Cflags: $(pkg-config --cflags libavcodec 2>/dev/null || true)"
            echo -e "    - Libs:   $(pkg-config --libs libavcodec 2>/dev/null || true)"
        fi
        exit 0
    else
        echo -e "${CLR_RED}[-] FFmpeg development libraries were NOT found.${CLR_RESET}"
        echo -e "    Run './setup_ffmpeg.sh' to install or bundle dependencies."
        exit 1
    fi
fi

# If headers already exist in target dir, we are ready
if [[ -f "${TARGET_DIR}/include/libavcodec/avcodec.h" ]]; then
    echo -e "${CLR_GREEN}[+] FFmpeg headers and libraries already exist in ${TARGET_DIR}${CLR_RESET}"
    exit 0
fi

# Helper function to run commands with sudo if needed
run_elevated() {
    if [[ "$(id -u)" -eq 0 ]]; then
        "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    elif command -v doas >/dev/null 2>&1; then
        doas "$@"
    else
        echo -e "${CLR_RED}[!] Root privileges required, but neither sudo nor doas is available.${CLR_RESET}"
        return 1
    fi
}

# Install via system package manager
install_system_packages() {
    echo -e "${CLR_CYAN}[*] Attempting system package manager installation...${CLR_RESET}"
    case "${OS_TYPE}" in
        Darwin)
            if command -v brew >/dev/null 2>&1; then
                echo -e "${CLR_YELLOW}[*] Installing via Homebrew...${CLR_RESET}"
                brew install ffmpeg pkg-config
                return 0
            elif command -v port >/dev/null 2>&1; then
                echo -e "${CLR_YELLOW}[*] Installing via MacPorts...${CLR_RESET}"
                run_elevated port install ffmpeg pkgconfig
                return 0
            else
                echo -e "${CLR_RED}[!] Neither Homebrew nor MacPorts found on macOS.${CLR_RESET}"
                return 1
            fi
            ;;
        Linux)
            if command -v apt-get >/dev/null 2>&1; then
                echo -e "${CLR_YELLOW}[*] Installing via apt (Debian / Ubuntu / Mint)...${CLR_RESET}"
                run_elevated apt-get update -y
                run_elevated apt-get install -y \
                    libavcodec-dev \
                    libavformat-dev \
                    libavutil-dev \
                    libswscale-dev \
                    libswresample-dev \
                    libavfilter-dev \
                    libavdevice-dev \
                    pkg-config
                return 0
            elif command -v pacman >/dev/null 2>&1; then
                echo -e "${CLR_YELLOW}[*] Installing via pacman (Arch Linux / Manjaro)...${CLR_RESET}"
                run_elevated pacman -S --needed --noconfirm ffmpeg pkgconf
                return 0
            elif command -v dnf >/dev/null 2>&1; then
                echo -e "${CLR_YELLOW}[*] Installing via dnf (Fedora / RHEL)...${CLR_RESET}"
                run_elevated dnf install -y ffmpeg-free-devel pkgconf-pkg-config || \
                run_elevated dnf install -y ffmpeg-devel pkgconf-pkg-config
                return 0
            elif command -v zypper >/dev/null 2>&1; then
                echo -e "${CLR_YELLOW}[*] Installing via zypper (openSUSE)...${CLR_RESET}"
                run_elevated zypper --non-interactive install ffmpeg-devel pkg-config || \
                run_elevated zypper --non-interactive install libavcodec-devel libavformat-devel libavutil-devel libswscale-devel libswresample-devel
                return 0
            elif command -v apk >/dev/null 2>&1; then
                echo -e "${CLR_YELLOW}[*] Installing via apk (Alpine Linux)...${CLR_RESET}"
                run_elevated apk add --no-cache ffmpeg-dev pkgconf
                return 0
            elif command -v xbps-install >/dev/null 2>&1; then
                echo -e "${CLR_YELLOW}[*] Installing via xbps (Void Linux)...${CLR_RESET}"
                run_elevated xbps-install -Sy ffmpeg-devel pkg-config
                return 0
            fi
            ;;
        FreeBSD)
            if command -v pkg >/dev/null 2>&1; then
                echo -e "${CLR_YELLOW}[*] Installing via FreeBSD pkg...${CLR_RESET}"
                run_elevated pkg install -y ffmpeg pkgconf
                return 0
            fi
            ;;
        OpenBSD)
            if command -v pkg_add >/dev/null 2>&1; then
                echo -e "${CLR_YELLOW}[*] Installing via OpenBSD pkg_add...${CLR_RESET}"
                run_elevated pkg_add -I ffmpeg pkgconf
                return 0
            fi
            ;;
        NetBSD)
            if command -v pkgin >/dev/null 2>&1; then
                echo -e "${CLR_YELLOW}[*] Installing via NetBSD pkgin...${CLR_RESET}"
                run_elevated pkgin -y install ffmpeg6 pkgconf || run_elevated pkgin -y install ffmpeg pkgconf
                return 0
            fi
            ;;
    esac

    echo -e "${CLR_YELLOW}[!] No supported package manager found or automated installation skipped.${CLR_RESET}"
    return 1
}

# Download prebuilt standalone shared bundle into TargetDir
download_standalone_bundle() {
    echo -e "${CLR_CYAN}[*] Downloading prebuilt FFmpeg shared bundle for ${OS_TYPE} (${ARCH_TYPE})...${CLR_RESET}"

    local download_url=""
    if [[ "${OS_TYPE}" == "Linux" ]]; then
        case "${ARCH_TYPE}" in
            x86_64|amd64)
                download_url="https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-linux64-gpl-shared.tar.xz"
                ;;
            aarch64|arm64)
                download_url="https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-linuxarm64-gpl-shared.tar.xz"
                ;;
            *)
                echo -e "${CLR_RED}[!] Unsupported architecture for prebuilt Linux bundle: ${ARCH_TYPE}${CLR_RESET}"
                return 1
                ;;
        esac
    elif [[ "${OS_TYPE}" == "Darwin" ]]; then
        # On macOS, if brew is installed, bundle from brew prefix directly into TargetDir
        if command -v brew >/dev/null 2>&1; then
            if ! brew list ffmpeg >/dev/null 2>&1; then
                echo -e "${CLR_YELLOW}[*] Installing FFmpeg via Homebrew first...${CLR_RESET}"
                brew install ffmpeg
            fi
            local brew_prefix
            brew_prefix="$(brew --prefix ffmpeg)"
            echo -e "${CLR_GREEN}[*] Linking Homebrew FFmpeg into ${TARGET_DIR}...${CLR_RESET}"
            mkdir -p "${TARGET_DIR}/include" "${TARGET_DIR}/lib"
            cp -RL "${brew_prefix}/include/"* "${TARGET_DIR}/include/"
            cp -RL "${brew_prefix}/lib/"* "${TARGET_DIR}/lib/"
            return 0
        fi
        echo -e "${CLR_YELLOW}[!] On macOS, please install Homebrew (https://brew.sh) and run: brew install ffmpeg${CLR_RESET}"
        return 1
    elif [[ "${OS_TYPE}" =~ BSD ]]; then
        # On BSD, copy/symlink from /usr/local
        if [[ -d "/usr/local/include/libavcodec" ]]; then
            echo -e "${CLR_GREEN}[*] Linking BSD /usr/local FFmpeg into ${TARGET_DIR}...${CLR_RESET}"
            mkdir -p "${TARGET_DIR}/include" "${TARGET_DIR}/lib"
            cp -RL /usr/local/include/libav* "${TARGET_DIR}/include/" 2>/dev/null || true
            cp -RL /usr/local/include/libsw* "${TARGET_DIR}/include/" 2>/dev/null || true
            cp -RL /usr/local/lib/libav* "${TARGET_DIR}/lib/" 2>/dev/null || true
            cp -RL /usr/local/lib/libsw* "${TARGET_DIR}/lib/" 2>/dev/null || true
            return 0
        fi
        echo -e "${CLR_YELLOW}[!] On BSD, please install via: pkg install ffmpeg${CLR_RESET}"
        return 1
    fi

    if [[ -z "${download_url}" ]]; then
        return 1
    fi

    local temp_archive="/tmp/ffmpeg-bundle-$$.tar.xz"
    local temp_extract="/tmp/ffmpeg-extract-$$"

    echo -e "    ${CLR_GRAY}URL: ${download_url}${CLR_RESET}"

    if command -v curl >/dev/null 2>&1; then
        curl -fSL "${download_url}" -o "${temp_archive}"
    elif command -v wget >/dev/null 2>&1; then
        wget -q --show-progress "${download_url}" -O "${temp_archive}"
    else
        echo -e "${CLR_RED}[!] Neither curl nor wget is installed.${CLR_RESET}"
        return 1
    fi

    echo -e "${CLR_CYAN}[*] Extracting bundle...${CLR_RESET}"
    rm -rf "${temp_extract}"
    mkdir -p "${temp_extract}" "${TARGET_DIR}"
    tar -xf "${temp_archive}" -C "${temp_extract}"

    # Move extracted files into TARGET_DIR
    local extracted_root
    extracted_root="$(find "${temp_extract}" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
    if [[ -n "${extracted_root}" && -d "${extracted_root}" ]]; then
        cp -RL "${extracted_root}/"* "${TARGET_DIR}/"
    else
        cp -RL "${temp_extract}/"* "${TARGET_DIR}/"
    fi

    # Cleanup temporary archive and extracted directory
    rm -rf "${temp_archive}" "${temp_extract}"
    return 0
}

# Execution workflow according to selected mode
if [[ "${MODE}" == "system" ]]; then
    install_system_packages
elif [[ "${MODE}" == "download" ]]; then
    download_standalone_bundle
else
    # Auto mode:
    # 1. First check if system already has FFmpeg
    if has_ffmpeg_headers; then
        echo -e "${CLR_GREEN}[+] FFmpeg development headers already detected on system.${CLR_RESET}"
    else
        # 2. Try download prebuilt bundle into ThirdParty/ffmpeg
        echo -e "${CLR_YELLOW}[*] FFmpeg headers not found. Trying standalone bundle download...${CLR_RESET}"
        if ! download_standalone_bundle; then
            echo -e "${CLR_YELLOW}[*] Standalone download not available or failed. Falling back to system package manager...${CLR_RESET}"
            install_system_packages || true
        fi
    fi
fi

# Final validation
echo -e "${CLR_CYAN}[*] Verifying FFmpeg installation...${CLR_RESET}"
if has_ffmpeg_headers; then
    echo -e "${CLR_GREEN}====================================================${CLR_RESET}"
    echo -e "${CLR_GREEN}[+] FFmpeg setup successful!                        ${CLR_RESET}"
    echo -e "${CLR_GREEN}====================================================${CLR_RESET}"
    if [[ -f "${TARGET_DIR}/include/libavcodec/avcodec.h" ]]; then
        echo -e "Installed bundle location: ${TARGET_DIR}"
    else
        echo -e "System packages detected via pkg-config / default library search paths."
    fi
    echo -e "CMake will automatically discover FFmpeg when configuring the build."
    exit 0
else
    echo -e "${CLR_RED}====================================================${CLR_RESET}"
    echo -e "${CLR_RED}[-] FFmpeg setup was not completed automatically.   ${CLR_RESET}"
    echo -e "${CLR_RED}====================================================${CLR_RESET}"
    echo -e "Please install FFmpeg development packages manually:"
    echo -e "  - Ubuntu/Debian: sudo apt-get install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev"
    echo -e "  - Arch Linux:    sudo pacman -S ffmpeg"
    echo -e "  - Fedora:        sudo dnf install ffmpeg-free-devel"
    echo -e "  - macOS:         brew install ffmpeg"
    echo -e "  - FreeBSD:       sudo pkg install ffmpeg"
    exit 1
fi

