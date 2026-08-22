#!/usr/bin/env bash
# ==============================================================================
# build-packages.sh - Helper script to build DEB, RPM, and Archive packages
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

echo "==> Building ZDE in Release mode..."
cmake -B "${BUILD_DIR}" -S "${ROOT_DIR}" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --config Release

PACKAGE_TYPE="${1:-all}"

case "${PACKAGE_TYPE}" in
    deb|DEB)
        echo "==> Generating Debian/Ubuntu package (.deb)..."
        (cd "${BUILD_DIR}" && cpack -G DEB)
        ;;
    rpm|RPM)
        echo "==> Generating Fedora/RHEL package (.rpm)..."
        (cd "${BUILD_DIR}" && cpack -G RPM)
        ;;
    tar|txz|archive)
        echo "==> Generating Portable Archive (.tar.xz)..."
        (cd "${BUILD_DIR}" && cpack -G TXZ)
        ;;
    arch|pacman)
        echo "==> Building Arch Linux package (makepkg)..."
        ARCH_DIR="${ROOT_DIR}/Cmake/packaging/arch"
        (cd "${ARCH_DIR}" && makepkg -f)
        ;;
    all)
        echo "==> Generating all CPack supported packages..."
        (cd "${BUILD_DIR}" && cpack -G "DEB;RPM;TXZ")
        ;;
    *)
        echo "Usage: $0 [deb|rpm|tar|arch|all]"
        exit 1
        ;;
esac

echo "==> Packaging complete! Packages generated in ${BUILD_DIR}/"
