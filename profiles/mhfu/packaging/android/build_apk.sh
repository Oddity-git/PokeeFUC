#!/usr/bin/env bash
# Packs an APK of Pokee for arm64-v8a without Gradle, from a build
# configured with the NDK toolchain and -DMHFU_ANDROID_APP=ON. It carries
# no game data: the player's disc image is installed on the device.
# The build's MHFU_TITLE picks the app: Pokee (io.github.teamgdb.pokee) for
# Freedom Unite, PokeeFUC (io.github.teamgdb.pokeefuc) for FUComplete. They
# are two apps, so both install on one phone, each with its own storage.
# scripts/release_android.sh uses it for releases; on its own it makes test
# builds.
#
#   build_apk.sh <build dir> <SDL3 source dir> <libSDL3.so> <output.apk> [overlay limit]
#
# FONT_DIR may name a directory holding NotoSansCJKjp-Regular.otf and
# NotoSansCJK-LICENSE.txt, the fallback font and licence a Linux release
# fetches (packaging/linux/sources.sh pins both); they go into the APK's
# assets, and the app unpacks the font on its first start. Without it the
# game's text is blank unless the device has a font stb_truetype reads.
#
# ANDROID_HOME must name the SDK (build-tools and a platform are needed).
# DEBUGGABLE=1 marks the app debuggable, so `adb shell run-as` reaches its
# files (its log, its data) on a test device; leave it out for players.
# KEYSTORE (with KEYSTORE_PASS and KEY_ALIAS) signs with a key of your own, so
# that later builds install over earlier ones and keep the player's data;
# without it a throwaway key is made in the build directory. The password
# reaches apksigner through the environment, not its command line.
# ANDROID_BUILD_TOOLS_VERSION and ANDROID_COMPILE_SDK_VERSION select installed
# SDK versions instead of the newest installed ones.
#
# The app needs Android 10 (API 29), where every 64-bit device has Vulkan
# 1.1: build the native code for android-29 (ANDROID_PLATFORM), or it may
# import libc functions Android 10 lacks (libc++ waits with
# pthread_cond_clockwait from android-30 on). Newer Android features are
# looked up at run time.
# The overlay limit packs only the first N overlay libraries, to keep a test
# APK small; without it every one is packed.
set -euo pipefail

build_dir="$(cd "$1" && pwd)"
sdl_dir="$(cd "$2" && pwd)"
sdl_lib="$3"
output="$4"
overlay_limit="${5:-}"
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../../../.." && pwd)"

# The app the build is for (host/title.hpp). The Java code keeps its one
# package, io.github.teamgdb.pokee; aapt2 gives the APK the title's
# application id, and the activity is named in full in the manifest so it
# is still found under that id.
title="$(sed -n 's/^MHFU_TITLE:[A-Z]*=//p' "$build_dir/CMakeCache.txt" 2> /dev/null | head -1 | tr '[:lower:]' '[:upper:]')"
case "${title:-FU}" in
    FU) package=io.github.teamgdb.pokee; label=Pokee ;;
    FUC) package=io.github.teamgdb.pokeefuc; label=PokeeFUC ;;
    *) echo "error: unknown MHFU_TITLE '$title' in $build_dir" >&2; exit 1 ;;
esac

sdk="${ANDROID_HOME:?set ANDROID_HOME to the Android SDK}"
if [[ -n "${ANDROID_BUILD_TOOLS_VERSION:-}" ]]; then
    [[ "$ANDROID_BUILD_TOOLS_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] ||
        { echo 'error: invalid Android build-tools version' >&2; exit 1; }
    build_tools="$sdk/build-tools/$ANDROID_BUILD_TOOLS_VERSION"
else
    build_tools="$(ls -d "$sdk"/build-tools/* | sort -V | tail -1)"
fi
if [[ -n "${ANDROID_COMPILE_SDK_VERSION:-}" ]]; then
    [[ "$ANDROID_COMPILE_SDK_VERSION" =~ ^[0-9]+$ ]] || { echo 'error: invalid Android compile SDK version' >&2; exit 1; }
    platform="$sdk/platforms/android-$ANDROID_COMPILE_SDK_VERSION"
else
    platform="$(ls -d "$sdk"/platforms/android-* | sort -V | tail -1)"
fi
android_jar="$platform/android.jar"
[[ -d "$build_tools" && -f "$android_jar" ]] || { echo 'error: the Android SDK components are not installed' >&2; exit 1; }
work="$build_dir/apk"
rm -rf "$work"
mkdir -p "$work/classes" "$work/dex" "$work/lib/arm64-v8a"

echo "compiling SDL's Java activity and the app's own"
# Arrays, not word splitting, so a checkout path with a space still builds.
java_sources=()
while IFS= read -r -d '' file; do java_sources+=("$file"); done < <(
    find "$sdl_dir/android-project/app/src/main/java" "$here/java" -name '*.java' -print0)
javac -nowarn --release 11 -classpath "$android_jar" -d "$work/classes" \
    "${java_sources[@]}" 2> "$work/javac.log"
min_sdk=29
class_files=()
while IFS= read -r -d '' file; do class_files+=("$file"); done < <(find "$work/classes" -name '*.class' -print0)
"$build_tools/d8" --release --min-api "$min_sdk" --lib "$android_jar" --output "$work/dex" \
    "${class_files[@]}"

echo "linking resources"
# The licences of what the APK carries: Pokee's own, the third-party notices
# and the licences of the bundled libraries, SDL's and FFmpeg's (with where
# its source is), and the font's when it is packed.
mkdir -p "$work/assets/licenses"
cp "$repo/LICENSE" "$work/assets/licenses/Pokee-LICENSE.txt"
cp "$here/../THIRD_PARTY_NOTICES.md" "$work/assets/licenses/"
cp "$repo/profiles/mhfu/third_party/imgui/LICENSE.txt" "$work/assets/licenses/DearImGui-LICENSE.txt"
cp "$repo/profiles/mhfu/third_party/tiny_aes/UNLICENSE" "$work/assets/licenses/tiny-AES-c-UNLICENSE.txt"
cp "$repo/profiles/mhfu/third_party/xxhash/LICENSE" "$work/assets/licenses/xxHash-LICENSE.txt"
cp "$sdl_dir/LICENSE.txt" "$work/assets/licenses/SDL3-LICENSE.txt"
cp "$build_dir"/bin/lib/FFmpeg-COPYING.LGPLv2.1.txt "$build_dir"/bin/lib/FFmpeg-SOURCE.txt "$work/assets/licenses/"
if [[ -n "${FONT_DIR:-}" ]]; then
    mkdir -p "$work/assets/fonts"
    cp "$FONT_DIR/NotoSansCJKjp-Regular.otf" "$work/assets/fonts/"
    cp "$FONT_DIR/NotoSansCJK-LICENSE.txt" "$work/assets/licenses/NotoSansCJK-OFL.txt"
fi
assets=(-A "$work/assets")
# The resources with the title's name on the launcher.
cp -R "$here/res" "$work/res"
sed "s|<string name=\"app_name\">[^<]*</string>|<string name=\"app_name\">$label</string>|" \
    "$here/res/values/strings.xml" > "$work/res/values/strings.xml"
grep -q "<string name=\"app_name\">$label</string>" "$work/res/values/strings.xml" ||
    { echo 'error: res/values/strings.xml has no app_name to set' >&2; exit 1; }
# The icons are made already (make_icons.sh); crunching them again only
# changes their bytes.
"$build_tools/aapt2" compile --no-crunch --dir "$work/res" -o "$work/res.zip"
# The version: `git describe` of the checkout, and the number of commits as
# the code Android compares, so a later build is always an update
# (VERSION_NAME and VERSION_CODE override them).
version_name="${VERSION_NAME:-$(git -C "$repo" describe --tags --always --dirty 2>/dev/null || echo 0.0)}"
version_code="${VERSION_CODE:-$(git -C "$repo" rev-list --count HEAD 2>/dev/null || echo 1)}"
"$build_tools/aapt2" link -I "$android_jar" --manifest "$here/AndroidManifest.xml" \
    --rename-manifest-package "$package" --min-sdk-version "$min_sdk" --target-sdk-version 35 --version-name "$version_name" \
    --version-code "$version_code" ${DEBUGGABLE:+--debug-mode} "${assets[@]}" -o "$work/unsigned.apk" \
    "$work/res.zip"

echo "adding native libraries"
lib="$work/lib/arm64-v8a"
cp "$sdl_lib" "$lib/libSDL3.so"
cp "$build_dir/bin/libmain.so" "$lib/"
cp "$build_dir"/bin/lib/libavcodec.so "$build_dir"/bin/lib/libavutil.so "$lib/"
overlays=("$build_dir"/bin/overlays/libovl*.so)
if [[ -n "$overlay_limit" ]]; then overlays=("${overlays[@]:0:$overlay_limit}"); fi
cp "${overlays[@]}" "$lib/"
strip="$(ls -d "$ANDROID_NDK"/toolchains/llvm/prebuilt/*/bin/llvm-strip | head -1)"
"$strip" --strip-unneeded "$lib"/*.so
# extractNativeLibs is on, so the libraries may be compressed: the package
# manager unpacks them at install time.
# Without directory records (-D): apksigner drops them, and the signed APK
# then differs from the unsigned one in more than its signature.
(cd "$work" && cp dex/classes.dex . && zip -q unsigned.apk classes.dex && zip -q -D -r -9 unsigned.apk lib)

echo "aligning and signing"
keystore="${KEYSTORE:-$build_dir/apk-research.keystore}"
password="${KEYSTORE_PASS:-research}"
alias="${KEY_ALIAS:-research}"
if [[ ! -f "$keystore" ]]; then
    [[ -n "${KEYSTORE:-}" ]] && { echo "error: $keystore not found" >&2; exit 1; }
    keytool -genkeypair -keystore "$keystore" -storepass research -keypass research -alias research \
        -keyalg RSA -keysize 2048 -validity 3650 -dname "CN=Pokee research build" > /dev/null 2>&1
fi
"$build_tools/zipalign" -f -P 16 4 "$work/unsigned.apk" "$work/aligned.apk"
export POKEE_APK_KEY_PASSWORD="$password"
"$build_tools/apksigner" sign --ks "$keystore" --ks-pass env:POKEE_APK_KEY_PASSWORD \
    --key-pass env:POKEE_APK_KEY_PASSWORD --ks-key-alias "$alias" --out "$output" "$work/aligned.apk"
unset POKEE_APK_KEY_PASSWORD password
echo "wrote $output ($label, $package, $(du -h "$output" | cut -f1), ${#overlays[@]} overlay libraries, version $version_name ($version_code))"
