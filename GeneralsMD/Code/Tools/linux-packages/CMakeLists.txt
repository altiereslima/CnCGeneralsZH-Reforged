# The .deb and .rpm of Zero Hour Reforged (Tools/linux-packages.sh): CPack over install rules that install the
# portable folder Tools/linux-portable.sh made and checked (glibc 2.31, libstdc++ and every library static but
# glibc's and fontconfig, the licences against the link line), so every package carries the same binary.  Not a
# part of the game's own build: a distribution's compiler would give a binary none of those checks were run on.
#
#   cmake -S Tools/linux-packages -B <dir> -DZH_PORTABLE_DIR=<the folder> -DZH_PACKAGE_VERSION=<x.y.z>
#         [-DZH_PACKAGE_MAINTAINER="Name <address>"]
#   cpack --config <dir>/CPackConfig.cmake -G "DEB;RPM"
#
# The layout: the folder, unchanged, in /usr/lib/zero-hour-reforged (the game finds its overlay from its own
# executable's place); /usr/bin/zero-hour-reforged, a link to its launcher; the .desktop file and the icon
# where desktops look; the licences in /usr/share/doc (Debian's copyright file) and /usr/share/licenses.
# No game files: the player's Zero Hour is found or asked for on the first start, and the upscaled art is
# read from the user data folder's ReforgedArt when something has put it there.
cmake_minimum_required(VERSION 3.25)
project(zero-hour-reforged-packages NONE)

if(NOT IS_DIRECTORY "${ZH_PORTABLE_DIR}/bin" OR NOT EXISTS "${ZH_PORTABLE_DIR}/zero-hour-reforged.sh")
  message(FATAL_ERROR "ZH_PORTABLE_DIR must name the folder Tools/linux-portable.sh made")
endif()
if(NOT ZH_PACKAGE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "ZH_PACKAGE_VERSION must be x.y.z (the game's VERSION file has it)")
endif()
file(GLOB _zh_art "${ZH_PORTABLE_DIR}/share/zero-hour-reforged/overlay/Reforged*.big")
if(_zh_art)
  message(FATAL_ERROR "the packages carry the engine only: make the folder with linux-portable.sh --no-art")
endif()
if(NOT ZH_PACKAGE_MAINTAINER)
  execute_process(COMMAND git config user.name OUTPUT_VARIABLE _zh_name OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND git config user.email OUTPUT_VARIABLE _zh_email OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _zh_name OR NOT _zh_email)
    message(FATAL_ERROR "no ZH_PACKAGE_MAINTAINER and no git identity to take it from")
  endif()
  set(ZH_PACKAGE_MAINTAINER "${_zh_name} <${_zh_email}>")
endif()

# SPDX: the game, then what is built into it (the copyright file below says which is which); glslang brings BSD-2,
# MIT and Apache-2.0 parts beside its BSD-3 core, miniaudio is the Unlicense or MIT-0, LZH-Light has its own notice
set(ZH_PACKAGE_LICENSE "GPL-3.0-or-later AND LGPL-2.1-or-later AND GPL-2.0-or-later AND Zlib AND BSD-3-Clause AND BSD-2-Clause AND MIT AND Apache-2.0 AND (Unlicense OR MIT-0) AND CC0-1.0 AND LicenseRef-LZH-Light")
set(_zh_lib lib/zero-hour-reforged)
# the folder; its debug file stays in the tarball (a GNU debuglink that finds nothing is harmless)
install(DIRECTORY "${ZH_PORTABLE_DIR}/" DESTINATION ${_zh_lib} USE_SOURCE_PERMISSIONS
  PATTERN "generals.debug" EXCLUDE PATTERN "README.txt" EXCLUDE
  PATTERN "share/applications" EXCLUDE PATTERN "share/icons" EXCLUDE)
file(CREATE_LINK ../${_zh_lib}/zero-hour-reforged.sh "${CMAKE_CURRENT_BINARY_DIR}/zero-hour-reforged" SYMBOLIC)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/zero-hour-reforged" DESTINATION bin)
# the menu entry: the portable folder's template names its launcher by path; installed, it is on PATH
file(READ "${ZH_PORTABLE_DIR}/share/applications/zero-hour-reforged.desktop" _zh_desktop)
string(REGEX REPLACE "Exec=[^\n]*" "Exec=zero-hour-reforged" _zh_desktop "${_zh_desktop}")
string(REGEX REPLACE "# a template[^\n]*\n" "" _zh_desktop "${_zh_desktop}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/zero-hour-reforged.desktop" "${_zh_desktop}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/zero-hour-reforged.desktop" DESTINATION share/applications)
foreach(_zh_size 48x48 128x128)
  install(FILES "${ZH_PORTABLE_DIR}/share/icons/hicolor/${_zh_size}/apps/zero-hour-reforged.png"
    DESTINATION share/icons/hicolor/${_zh_size}/apps)
endforeach()
install(FILES "${ZH_PORTABLE_DIR}/README.txt" "${ZH_PORTABLE_DIR}/VERSION" DESTINATION share/doc/zero-hour-reforged)
# Debian's copyright file: what each part is under, and where the full texts are
file(GLOB _zh_licenses RELATIVE "${ZH_PORTABLE_DIR}/share/zero-hour-reforged/licenses"
  "${ZH_PORTABLE_DIR}/share/zero-hour-reforged/licenses/*")
list(SORT _zh_licenses)
list(JOIN _zh_licenses "\n  " _zh_license_list)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/copyright"
"Zero Hour Reforged, the Command & Conquer Generals Zero Hour engine for Linux.
Source: https://github.com/olcayseygan/CnCGeneralsZH-Reforged

The game: GPL-3.0-or-later, with additional terms under its section 7 (Zero-Hour-Reforged-LICENSE.md).
Built into it, each under its own licence: FFmpeg (LGPL-2.1-or-later, an LGPL-only build), SDL3, FreeType
(its GPL-2.0-or-later option), glslang, SPIRV-Cross, SDL_shadercross, litehtml and gumbo, miniaudio, zlib,
LZH-Light, nanosvg and the GameSpy SDK.  The full texts are in
/usr/lib/zero-hour-reforged/share/zero-hour-reforged/licenses/ and /usr/share/licenses/zero-hour-reforged/:
  ${_zh_license_list}

No game data is included: the game needs the player's own Command & Conquer Generals Zero Hour.
")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/copyright" DESTINATION share/doc/zero-hour-reforged)
install(DIRECTORY "${ZH_PORTABLE_DIR}/share/zero-hour-reforged/licenses/" DESTINATION share/licenses/zero-hour-reforged)

set(CPACK_PACKAGE_NAME zero-hour-reforged)
set(CPACK_PACKAGE_VERSION ${ZH_PACKAGE_VERSION})
set(CPACK_PACKAGE_VENDOR "Zero Hour Reforged")
set(CPACK_PACKAGE_CONTACT "${ZH_PACKAGE_MAINTAINER}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Command & Conquer Generals Zero Hour engine (needs your own copy of the game)")
set(CPACK_PACKAGE_DESCRIPTION "Zero Hour Reforged runs Command & Conquer Generals Zero Hour natively, from the
player's own copy of the game (Steam, EA App, CD or The First Decade), which it finds or asks for on its first
start.  No game files are included, and no upscaled art.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/olcayseygan/CnCGeneralsZH-Reforged")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_STRIP_FILES OFF)				# stripped already, with its debuglink
set(CPACK_PACKAGE_FILE_NAME "zero-hour-reforged-${ZH_PACKAGE_VERSION}-x86_64")

# Debian and Ubuntu: glibc 2.31 and fontconfig are needed; the rest SDL and miniaudio load when they are there
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE amd64)
set(CPACK_DEBIAN_PACKAGE_SECTION games)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.31), libfontconfig1")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "libvulkan1, mesa-vulkan-drivers | vulkan-icd, libasound2t64 | libasound2, libpulse0, libx11-6, libxext6, libxcursor1, libxi6, libxrandr2, libxkbcommon0, libwayland-client0, libwayland-cursor0, libdecor-0-0, libdbus-1-3, libudev1, curl")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "xdg-desktop-portal, zenity")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)

# Fedora and openSUSE: their names differ, so each weak dependency lists both
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_ARCHITECTURE x86_64)
set(CPACK_RPM_PACKAGE_LICENSE "${ZH_PACKAGE_LICENSE}")
set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")
set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_RPM_PACKAGE_AUTOREQ OFF)			# the dynamic section asks glibc and fontconfig, named below
set(CPACK_RPM_PACKAGE_AUTOPROV OFF)
set(CPACK_RPM_PACKAGE_REQUIRES "glibc >= 2.31, fontconfig")
set(CPACK_RPM_SPEC_MORE_DEFINE "%global _build_id_links none
Recommends: (vulkan-loader or libvulkan1)
Recommends: (mesa-vulkan-drivers or libvulkan_lvp or libvulkan_radeon or libvulkan_intel)
Recommends: (alsa-lib or libasound2)
Recommends: (libX11 or libX11-6)
Recommends: (libxkbcommon or libxkbcommon0)
Recommends: (libwayland-client or libwayland-client0)
Recommends: (dbus-libs or libdbus-1-3)
Recommends: curl")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION /usr/share/applications /usr/share/icons
  /usr/share/icons/hicolor /usr/share/icons/hicolor/48x48 /usr/share/icons/hicolor/48x48/apps
  /usr/share/icons/hicolor/128x128 /usr/share/icons/hicolor/128x128/apps
  /usr/share/doc /usr/share/licenses /usr/lib)
include(CPack)
