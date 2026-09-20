#!/usr/bin/env bash
#
# Builds the FFmpeg the game links against, with the codecs it opens and nothing else.
#
# Run it through ffmpeg-build.bat, which supplies the MSVC environment. The result lands in
# Libraries/Source/FFmpeg/dist, laid out the way CMakeLists.txt expects: include/, lib/*.lib,
# bin/*.dll. Those DLLs are post-build copied next to generals.exe and are what the payload ships.
#
# What the game opens, and therefore all that is built:
#   .bik movies  -> bink demuxer, the bink video decoder and both binkaudio decoders
#   .mp3 music   -> mp3 demuxer and decoder, mpegaudio parser
#   .wav speech  -> wav demuxer, the PCM decoders and the two ADPCM flavours its files use
# avfilter, avdevice, the programs, the network layer and every other codec are off. A stock
# general-purpose build of the same version is 123MB of DLLs; this is 3.3MB.
#
# The version matters: the library majors decide the DLL names (avcodec-62 and friends), which
# CMakeLists.txt and launcher/build-payload.js both name.
set -e

FFMPEG_VERSION=8.1.2
SOURCE_ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
DIST="$SOURCE_ROOT/GeneralsMD/Code/Libraries/Source/FFmpeg/dist"
WORK="${TMPDIR:-/tmp}/zhr-ffmpeg-$FFMPEG_VERSION"
TARBALL="ffmpeg-$FFMPEG_VERSION.tar.xz"

mkdir -p "$WORK"
cd "$WORK"

if [ ! -d "ffmpeg-$FFMPEG_VERSION" ]; then
  [ -f "$TARBALL" ] || curl -fsSL -o "$TARBALL" "https://ffmpeg.org/releases/$TARBALL"
  tar xf "$TARBALL"
fi
cd "ffmpeg-$FFMPEG_VERSION"

# MSYS2's coreutils ships a link.exe that shadows the MSVC linker, and configure cannot find cl's
# one while it is first on PATH.
if [ -f /usr/bin/link.exe ]; then
  mv /usr/bin/link.exe /usr/bin/link-coreutils.exe
fi
restore_link() {
  if [ -f /usr/bin/link-coreutils.exe ]; then
    mv /usr/bin/link-coreutils.exe /usr/bin/link.exe
  fi
}
trap restore_link EXIT

./configure \
  --prefix="$WORK/out" \
  --toolchain=msvc \
  --arch=x86_64 \
  --target-os=win64 \
  --enable-shared \
  --disable-static \
  --disable-everything \
  --disable-autodetect \
  --disable-programs \
  --disable-doc \
  --disable-avdevice \
  --disable-avfilter \
  --disable-network \
  --disable-debug \
  --enable-swscale \
  --enable-swresample \
  --enable-demuxer=bink,mp3,wav \
  --enable-decoder=bink,binkaudio_dct,binkaudio_rdft,mp3,mp3float,pcm_s16le,pcm_s16be,pcm_u8,pcm_s24le,pcm_s32le,pcm_f32le,adpcm_ima_wav,adpcm_ms \
  --enable-parser=mpegaudio \
  --enable-protocol=file

# configure takes an unknown component name without a word. The first build of this asked for a
# decoder called "binkvideo", which is that decoder's display name rather than the name configure
# knows it by ("bink"), and produced libraries that could not open a single movie.
for component in BINK_DECODER BINKAUDIO_DCT_DECODER BINKAUDIO_RDFT_DECODER MP3_DECODER \
                 ADPCM_IMA_WAV_DECODER PCM_S16LE_DECODER BINK_DEMUXER MP3_DEMUXER WAV_DEMUXER \
                 MPEGAUDIO_PARSER FILE_PROTOCOL; do
  if ! grep -q "^#define CONFIG_${component} 1$" config_components.h; then
    echo "configure did not enable ${component}" >&2
    exit 1
  fi
done

make -j"$(nproc)"
make install

# The install puts the import libraries in bin/ next to the DLLs; the build expects them in lib/.
rm -rf "$DIST"
mkdir -p "$DIST/bin" "$DIST/lib"
cp -r "$WORK/out/include" "$DIST/"
cp "$WORK/out/bin"/*.dll "$DIST/bin/"
cp "$WORK/out/bin"/*.lib "$DIST/lib/"
cp COPYING.LGPLv2.1 "$DIST/LICENSE.txt"

echo
echo "FFmpeg $FFMPEG_VERSION installed into $DIST"
du -ch "$DIST/bin"/*.dll | tail -1
