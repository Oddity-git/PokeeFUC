"""build_apk.sh against stand-in SDK tools: which app a build packs, and how
it hands the signing password over. No SDK, NDK, game data or device."""

import os
import shutil
import stat
import subprocess
import tempfile
import sys
import unittest
from pathlib import Path

PROFILE = Path(__file__).resolve().parents[1]
BUILD_APK = PROFILE / "packaging" / "android" / "build_apk.sh"

# Each stand-in records its arguments, one per line, then does the least its
# real counterpart does that the script depends on: aapt2 compile and link,
# zipalign and apksigner write their output file; apksigner also records the
# password it was given through the environment.
TOOL = """#!/usr/bin/env bash
{{ printf '%s\\n' "$(basename "$0")"; printf '%s\\n' "$@"; printf -- '--\\n'; }} >> "{log}"
out=""
prev=""
for arg in "$@"; do
    [[ "$prev" == -o || "$prev" == --out || "$prev" == --output ]] && out="$arg"
    prev="$arg"
done
case "$(basename "$0")" in
    aapt2)
        if [[ "$1" == compile ]]; then
            dir=""; prev=""
            for arg in "$@"; do [[ "$prev" == --dir ]] && dir="$arg"; prev="$arg"; done
            cp "$dir/values/strings.xml" "{root}/strings.xml"
        fi
        python3 -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1], "w").close()' "$out" ;;
    zipalign) cp "${{@: -2:1}}" "${{@: -1}}" ;;
    apksigner) printf '%s\\n' "$POKEE_APK_KEY_PASSWORD" > "{root}/password"; cp "${{@: -1}}" "$out" ;;
    d8) mkdir -p "$out"; touch "$out/classes.dex" ;;
    javac)
        dir=""; prev=""
        for arg in "$@"; do [[ "$prev" == -d ]] && dir="$arg"; prev="$arg"; done
        touch "$dir/Stub.class" ;;
esac
"""


def executable(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


@unittest.skipIf(sys.platform == "win32", "build_apk.sh runs on Linux and macOS")
class BuildApk(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix="pokee-apk-"))
        self.addCleanup(shutil.rmtree, self.root)
        self.log = self.root / "tools.log"
        tool = TOOL.format(log=self.log, root=self.root)
        self.sdk = self.root / "sdk"
        for name in ("aapt2", "d8", "zipalign", "apksigner"):
            executable(self.sdk / "build-tools" / "35.0.0" / name, tool)
        (self.sdk / "platforms" / "android-35").mkdir(parents=True)
        (self.sdk / "platforms" / "android-35" / "android.jar").write_bytes(b"")
        self.bin = self.root / "bin"
        executable(self.bin / "javac", tool)
        executable(self.bin / "keytool", "#!/bin/sh\nexit 1\n")
        self.ndk = self.root / "ndk"
        executable(self.ndk / "toolchains" / "llvm" / "prebuilt" / "linux-x86_64" / "bin" / "llvm-strip",
                   "#!/bin/sh\nexit 0\n")
        self.sdl = self.root / "sdl"
        java = self.sdl / "android-project" / "app" / "src" / "main" / "java"
        java.mkdir(parents=True)
        (java / "SDLActivity.java").write_text("class SDLActivity {}\n")
        (self.sdl / "LICENSE.txt").write_text("zlib\n")
        self.build = self.root / "build dir"
        for name in ("bin/libmain.so", "bin/lib/libavcodec.so", "bin/lib/libavutil.so",
                     "bin/lib/FFmpeg-COPYING.LGPLv2.1.txt", "bin/lib/FFmpeg-SOURCE.txt",
                     "bin/overlays/libovl_a.so", "bin/overlays/libovl_b.so"):
            (self.build / name).parent.mkdir(parents=True, exist_ok=True)
            (self.build / name).write_bytes(b"\x7fELF")
        (self.root / "libSDL3.so").write_bytes(b"\x7fELF")
        (self.root / "key.keystore").write_bytes(b"key")

    def cache(self, title):
        lines = ["MHFU_ANDROID_APP:BOOL=ON"]
        if title is not None:
            lines.append(f"MHFU_TITLE:STRING={title}")
        (self.build / "CMakeCache.txt").write_text("\n".join(lines) + "\n")

    def pack(self, title, **extra):
        self.cache(title)
        env = {k: v for k, v in os.environ.items() if not k.startswith(("KEYSTORE", "KEY_", "ANDROID_"))}
        env.update(ANDROID_HOME=str(self.sdk), ANDROID_NDK=str(self.ndk), PATH=f"{self.bin}:{env['PATH']}",
                   KEYSTORE=str(self.root / "key.keystore"), KEYSTORE_PASS="s3cret pass", KEY_ALIAS="release",
                   VERSION_NAME="1.0", VERSION_CODE="7")
        env.update(extra)
        return subprocess.run(["bash", str(BUILD_APK), str(self.build), str(self.sdl),
                               str(self.root / "libSDL3.so"), str(self.root / "out.apk")],
                              env=env, capture_output=True, text=True)

    def calls(self, tool):
        entries = self.log.read_text().split("--\n") if self.log.exists() else []
        return [e.splitlines()[1:] for e in entries if e.splitlines() and e.splitlines()[0] == tool]

    def link_args(self):
        link = [c for c in self.calls("aapt2") if c and c[0] == "link"]
        self.assertEqual(len(link), 1)
        return link[0]

    def label(self):
        return (self.root / "strings.xml").read_text()

    def test_freedom_unite_is_pokee(self):
        result = self.pack("FU")
        self.assertEqual(result.returncode, 0, result.stderr)
        args = self.link_args()
        self.assertEqual(args[args.index("--rename-manifest-package") + 1], "io.github.teamgdb.pokee")
        self.assertIn('<string name="app_name">Pokee</string>', self.label())
        self.assertTrue((self.root / "out.apk").is_file())

    def test_a_build_without_a_title_is_pokee(self):
        result = self.pack(None)
        self.assertEqual(result.returncode, 0, result.stderr)
        args = self.link_args()
        self.assertEqual(args[args.index("--rename-manifest-package") + 1], "io.github.teamgdb.pokee")

    def test_fucomplete_is_its_own_app(self):
        result = self.pack("fuc")
        self.assertEqual(result.returncode, 0, result.stderr)
        args = self.link_args()
        self.assertEqual(args[args.index("--rename-manifest-package") + 1], "io.github.teamgdb.pokeefuc")
        self.assertIn('<string name="app_name">PokeeFUC</string>', self.label())
        # The source resources keep Pokee's name; only the packed copy changes.
        source = (PROFILE / "packaging" / "android" / "res" / "values" / "strings.xml").read_text()
        self.assertIn('<string name="app_name">Pokee</string>', source)
        self.assertIn("PokeeFUC, io.github.teamgdb.pokeefuc", result.stdout)

    def test_unknown_title_is_refused(self):
        result = self.pack("MHP2G")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unknown MHFU_TITLE", result.stderr)
        self.assertFalse((self.root / "out.apk").exists())

    def test_password_goes_through_the_environment(self):
        result = self.pack("FUC")
        self.assertEqual(result.returncode, 0, result.stderr)
        signing = self.calls("apksigner")[0]
        self.assertNotIn("s3cret pass", " ".join(signing))
        self.assertIn("env:POKEE_APK_KEY_PASSWORD", signing)
        self.assertEqual((self.root / "password").read_text(), "s3cret pass\n")

    def test_bundled_licences_are_packed(self):
        result = self.pack("FUC")
        self.assertEqual(result.returncode, 0, result.stderr)
        licenses = self.build / "apk" / "assets" / "licenses"
        for name in ("Pokee-LICENSE.txt", "THIRD_PARTY_NOTICES.md", "DearImGui-LICENSE.txt",
                     "tiny-AES-c-UNLICENSE.txt", "xxHash-LICENSE.txt", "SDL3-LICENSE.txt"):
            self.assertTrue((licenses / name).is_file(), name)

    def test_a_failed_java_build_is_refused(self):
        executable(self.bin / "javac", "#!/bin/sh\nexit 0\n")
        result = self.pack("FUC")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("javac made no classes", result.stderr)
        self.assertFalse((self.root / "out.apk").exists())

    def test_chosen_sdk_versions(self):
        result = self.pack("FU", ANDROID_BUILD_TOOLS_VERSION="35.0.0", ANDROID_COMPILE_SDK_VERSION="35")
        self.assertEqual(result.returncode, 0, result.stderr)
        missing = self.pack("FU", ANDROID_COMPILE_SDK_VERSION="34")
        self.assertNotEqual(missing.returncode, 0)
        self.assertIn("not installed", missing.stderr)
        invalid = self.pack("FU", ANDROID_BUILD_TOOLS_VERSION="../35")
        self.assertNotEqual(invalid.returncode, 0)
        self.assertIn("invalid Android build-tools version", invalid.stderr)


if __name__ == "__main__":
    unittest.main()
