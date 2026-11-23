#!/bin/sh
# Install the build tools and libxml2 for the mingw job on GNOME's
# Windows runners, in the MSYS2 environment that MSYSTEM names. The
# packages are the ones the README lists for MSYS2.

set -e

pacman --noconfirm -Syu

for module in gcc meson ninja pkgconf libxml2; do
    pacman --noconfirm -S --needed "${MINGW_PACKAGE_PREFIX}-$module"
done
