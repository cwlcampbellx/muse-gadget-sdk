# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Host checks for the HAL flasher patch: the avatar.set / fish_config /
avatar serial protocols, plus the AGENTS.md wiring rules (advertised and
dispatched under the same Kconfig option, console hooks chained)."""

from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

SECTIONS = [
    ("components/muse/avatar_settings.c", "avatar_settings_protocol.inc"),
    ("main/fish_config_console.c", "fish_protocol.inc"),
    ("main/avatar_set.c", "avatar_kv_protocol.inc"),
]


def section(path):
    source = (ROOT / path).read_text()
    start = source.index("// ---- Protocol (host-tested)")
    return source[start:source.index("// ---- End of host-tested section", start)]


class HalAvatarFishTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        out = Path(cls.temp.name)
        for src, inc in SECTIONS:
            (out / inc).write_text(section(src))
        cc = shlex.split(os.environ.get("CC", "cc"))
        cmd = [*cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(out),
               str(ROOT / "tests/hal_avatar_fish_harness.c"), "-o", str(out / "hal")]
        compiled = subprocess.run(cmd, capture_output=True, text=True)
        if compiled.returncode:
            raise AssertionError(compiled.stdout + compiled.stderr)
        cls.out = out

    def test_protocols(self):
        result = subprocess.run([str(self.out / "hal")], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_avatar_set_advertised_and_dispatched_with_the_feature(self):
        noise = (ROOT / "main/noise_control.cpp").read_text()
        start = noise.index("#if CONFIG_MUSE_AVATAR_SET")
        self.assertIn('add_command(commands, "avatar.set"', noise[start:noise.index("#endif", start)])
        app = (ROOT / "main/app.c").read_text()
        start = app.index("static cJSON *on_ws_command(")
        dispatch = app[start:app.index("unsupported command", start)]
        block = dispatch.index("#if CONFIG_MUSE_AVATAR_SET")
        self.assertIn('strcmp(command, "avatar.set")', dispatch[block:dispatch.index("#endif", block)])

    def test_serial_commands_share_the_console_hooks(self):
        glue = (ROOT / "main/muse_glue.c").read_text()
        for fn in ("sdk_token_console_command", "fish_config_console_command", "avatar_console_command"):
            self.assertIn(fn, glue)
        readers = (ROOT / "main/sdk_token_console.c").read_text()
        self.assertIn("fish_config_console_command", readers)
        self.assertIn("avatar_console_command", readers)

    def test_hal_renderer_styles(self):
        renderer = (ROOT / "components/muse/avatar/muse_pixel.c").read_text()
        for style in ("hal_faceplate", "hal_eye", "vinny", "pixel"):
            self.assertIn(style, renderer)
        self.assertIn("avatar_settings_get", renderer)

    def test_steal_list_wiring(self):
        # The speech adapter: Markdown stripped before speech, the relay
        # override read from NVS, and the TLS pre-warm in the fetch path.
        session = (ROOT / "components/muse/muse_chat_session.cpp").read_text()
        self.assertIn("strip_markdown(", session)
        self.assertIn('"fish_url"', session)
        self.assertIn("tts_prewarm(", session)
        self.assertIn('"input"', session)            # the relay's OpenAI-format body
        self.assertIn('"response_format"', session)
        # The avatar reaction is advertised on the same command, and the
        # renderer plays it from the transient reaction state.
        noise = (ROOT / "main/noise_control.cpp").read_text()
        self.assertIn('"reaction"', noise)
        renderer = (ROOT / "components/muse/avatar/muse_pixel.c").read_text()
        self.assertIn("avatar_reaction_current", renderer)
        # HAL builds encrypt NVS (the Fish key lives there) and accept
        # cross-signed TLS roots, from the shared Muse overlay.
        overlay = (ROOT / "devices/sdkconfig.muse").read_text()
        self.assertIn("CONFIG_HOMEHUB_NVS_ENCRYPTION=y", overlay)
        self.assertIn("CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y", overlay)


if __name__ == "__main__":
    unittest.main()
