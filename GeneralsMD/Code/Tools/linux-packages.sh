#!/usr/bin/env bash
#	Copyright 2026 İlyas Akın
#	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#
#	This program is free software: you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
#	the Free Software Foundation, either version 3 of the License, or
#	(at your option) any later version.
#
#	This program is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU General Public License for more details.
#
#	You should have received a copy of the GNU General Public License
#	along with this program.  If not, see <http://www.gnu.org/licenses/>.
# linux-packages.sh: Zero Hour Reforged's Linux release files, from one command.  Each is the engine only: the
# player's own Zero Hour is found or asked for on the first start, and the upscaled art is read from their
# user data folder's ReforgedArt when something has put it there.  Every one carries the same
# binary: the portable folder Tools/linux-portable.sh --no-art builds in Valve's sniper SDK and checks (glibc
# 2.31, libstdc++, SDL3, FFmpeg's LGPL build and the rest static, the licences against the link line).
#
#   <out>/zero-hour-reforged-<version>-x86_64.AppImage        the folder as one AppImage
#   <out>/zero-hour-reforged_<version>_amd64.deb              Debian and Ubuntu (CPack: Tools/linux-packages/)
#   <out>/zero-hour-reforged-<version>-1.x86_64.rpm           Fedora and openSUSE (CPack)
#   <out>/zero-hour-reforged-<version>-1-x86_64.pkg.tar.zst   Arch (makepkg: Tools/linux-packages/PKGBUILD)
#   <out>/io.github.olcayseygan.ZeroHourReforged.flatpak      a single-file Flatpak bundle (flatpak-builder)
#   <out>/ZeroHourReforged-linux-x86_64.tar.zst               the folder itself
#   <out>/SHA256SUMS
#
# Usage: linux-packages.sh --build <folder> --out <folder> --cmake <cmake> --appimagetool <tool> --runtime <file>
#          [--formats "appimage deb rpm arch flatpak"] [--no-build] [--jobs <n>]
#   --build, --cmake, --jobs, --no-build   as linux-portable.sh takes them
#   --appimagetool, --runtime              linux-portable.sh's --appimage and --runtime (needed for appimage)
#   --formats                              which to make (default: all five; the tarball always)
# Every step runs in docker (the user in its group): the sniper SDK for the folder, fedora:latest for the .deb,
# the .rpm and the Flatpak (flatpak-builder in a --privileged container, as its sandbox needs), archlinux:latest
# for makepkg.  The Flatpak's runtime and SDK (org.freedesktop 25.08) are kept in <build>/flatpak-cache between
# runs.  Maintainer: ZH_PACKAGE_MAINTAINER ("Name <address>"), else this user's git identity.
# Exit status: 0 all made; 1 a step failed (it says which).

set -u
BUILD="" OUT="" CMAKE="" APPIMAGETOOL="" RUNTIME="" FORMATS="appimage deb rpm arch flatpak" NOBUILD="" JOBS=""
while [ $# -gt 0 ]; do
	case "$1" in
		--build) BUILD="$2"; shift 2;;
		--out) OUT="$2"; shift 2;;
		--cmake) CMAKE="$2"; shift 2;;
		--appimagetool) APPIMAGETOOL="$2"; shift 2;;
		--runtime) RUNTIME="$2"; shift 2;;
		--formats) FORMATS="$2"; shift 2;;
		--no-build) NOBUILD=--no-build; shift;;
		--jobs) JOBS="$2"; shift 2;;
		*) echo "linux-packages: unknown argument $1" >&2; exit 2;;
	esac
done
fail() { echo "linux-packages: $*" >&2; exit 1; }
wants() { case " $FORMATS " in *" $1 "*) return 0;; esac; return 1; }
[ -n "$BUILD" ] && [ -n "$OUT" ] || fail "--build and --out are required"
command -v docker > /dev/null || fail "no docker"
CODE="$(cd "$(dirname "$0")/.." && pwd)"
P="$CODE/Tools/linux-packages"
mkdir -p "$OUT" "$BUILD" && OUT="$(cd "$OUT" && pwd)" && BUILD="$(cd "$BUILD" && pwd)" || fail "cannot make $OUT or $BUILD"
W="$BUILD/packages"		# the folder, and each format's work
rm -rf -- "$W" && mkdir -p "$W" || fail "cannot make $W"
MAINTAINER="${ZH_PACKAGE_MAINTAINER:-$(git -C "$CODE" config user.name) <$(git -C "$CODE" config user.email)>}"
case "$MAINTAINER" in *"<>"*|" <"*) fail "set ZH_PACKAGE_MAINTAINER, or a git identity";; esac

# ---- 1. the folder, engine only (and the AppImage), checked by linux-portable.sh -------------------------
portable=( --build "$BUILD" --out "$W/ZeroHourReforged-linux-x86_64" --no-art $NOBUILD )
[ -n "$CMAKE" ] && portable+=( --cmake "$CMAKE" )
[ -n "$JOBS" ] && portable+=( --jobs "$JOBS" )
wants appimage && portable+=( --appimage "$APPIMAGETOOL" --runtime "$RUNTIME" )
bash "$CODE/Tools/linux-portable.sh" "${portable[@]}" || fail "linux-portable.sh failed"
F="$W/ZeroHourReforged-linux-x86_64"
VERSION="$(sed -n 's/^Zero Hour Reforged \([0-9.]*\) for Linux.*/\1/p' "$F/VERSION")"
[ -n "$VERSION" ] || fail "no version in $F/VERSION"
TARBALL_SHA="$(sha256sum "$F.tar.zst" | cut -d ' ' -f 1)"
cp "$F.tar.zst" "$OUT/" || fail "cannot copy the tarball"
if wants appimage; then
	cp "$F.AppImage" "$OUT/zero-hour-reforged-$VERSION-x86_64.AppImage" || fail "cannot copy the AppImage"
fi
in_image() {	# in_image <image> <extra docker args> <command>: as root in a throwaway container, $W mounted
	docker run --rm $2 -v "$W:$W" -v "$P:$P:ro" -w "$W" "$1" bash -c "$3"
}
owner="$(id -u):$(id -g)"

# ---- 2. .deb and .rpm: CPack in Fedora (rpmbuild; CPack writes the .deb itself) --------------------------
if wants deb || wants rpm; then
	gens=""; wants deb && gens="DEB"; wants rpm && gens="${gens:+$gens;}RPM"
	in_image fedora:latest "" "dnf -q -y install cmake rpm-build > '$W/cpack-deps.log' 2>&1 \
		&& cmake -S '$P' -B '$W/cpack' -DZH_PORTABLE_DIR='$F' -DZH_PACKAGE_VERSION='$VERSION' \
			'-DZH_PACKAGE_MAINTAINER=$MAINTAINER' > '$W/cpack.log' 2>&1 \
		&& cd '$W/cpack' && cpack -G '$gens' >> '$W/cpack.log' 2>&1; status=\$?; chown -R $owner '$W'; exit \$status" \
		|| { tail -n 20 "$W/cpack.log" >&2; fail "CPack failed (the log: $W/cpack.log)"; }
	if wants deb; then cp "$W/cpack/zero-hour-reforged_${VERSION}_amd64.deb" "$OUT/" || fail "no .deb"; fi
	if wants rpm; then cp "$W/cpack/zero-hour-reforged-$VERSION-1.x86_64.rpm" "$OUT/" || fail "no .rpm"; fi
fi

# ---- 3. Arch: makepkg over the PKGBUILD, as an unprivileged user (makepkg refuses root) ------------------
if wants arch; then
	mkdir -p "$W/arch" && cp "$P/PKGBUILD" "$F.tar.zst" "$W/arch/" || fail "cannot stage the PKGBUILD"
	in_image archlinux:latest "" "pacman -Sy --noconfirm --needed base-devel > '$W/arch-deps.log' 2>&1 \
		&& useradd -m builder && chown -R builder '$W/arch' \
		&& su builder -c 'cd $W/arch && ZH_PACKAGE_VERSION=$VERSION ZH_TARBALL_SHA256=$TARBALL_SHA PKGDEST=$W/arch makepkg --nodeps --noconfirm' \
			> '$W/makepkg.log' 2>&1; status=\$?; chown -R $owner '$W'; exit \$status" \
		|| { tail -n 20 "$W/makepkg.log" >&2; fail "makepkg failed (the log: $W/makepkg.log)"; }
	cp "$W/arch/zero-hour-reforged-$VERSION-1-x86_64.pkg.tar.zst" "$OUT/" || fail "no Arch package"
fi

# ---- 4. Flatpak: flatpak-builder in a privileged container, its runtime cached in the build folder -------
if wants flatpak; then
	id=io.github.olcayseygan.ZeroHourReforged
	mkdir -p "$W/flatpak" "$BUILD/flatpak-cache" || fail "cannot make the Flatpak's folders"
	cp -a "$F" "$P/$id.yml" "$P/$id.metainfo.xml" "$W/flatpak/" || fail "cannot stage the Flatpak's sources"
	# the container's system installation (flatpak refuses a user one as root), kept in the build folder
	docker run --rm --privileged -v "$W:$W" -v "$BUILD/flatpak-cache:/var/lib/flatpak" -w "$W/flatpak" \
		fedora:latest bash -c "dnf -q -y install flatpak flatpak-builder > '$W/flatpak-deps.log' 2>&1 \
		&& flatpak remote-add --system --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo \
		&& flatpak install --system -y --noninteractive flathub org.freedesktop.Platform//25.08 org.freedesktop.Sdk//25.08 \
			>> '$W/flatpak-deps.log' 2>&1 \
		&& flatpak-builder --disable-rofiles-fuse --force-clean --repo=repo build $id.yml > '$W/flatpak.log' 2>&1 \
		&& flatpak build-bundle --runtime-repo=https://dl.flathub.org/repo/flathub.flatpakrepo repo $id.flatpak $id \
			>> '$W/flatpak.log' 2>&1; status=\$?; chown -R $owner '$W' /var/lib/flatpak; exit \$status" \
		|| { tail -n 20 "$W/flatpak-deps.log" "$W/flatpak.log" >&2; fail "the Flatpak failed (the log: $W/flatpak.log)"; }
	cp "$W/flatpak/$id.flatpak" "$OUT/" || fail "no Flatpak bundle"
fi

( cd "$OUT" && rm -f SHA256SUMS && sha256sum -- *.tar.zst $(ls *.AppImage *.deb *.rpm *.flatpak 2>/dev/null) > SHA256SUMS ) \
	|| fail "cannot write SHA256SUMS"
echo "linux-packages: Zero Hour Reforged $VERSION in $OUT:"
( cd "$OUT" && ls -l -- $(awk '{print $2}' SHA256SUMS) | awk '{printf "  %s  %s\n", $5, $9}' )
