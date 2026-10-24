/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Host harness for the HAL patch's protocol sections (avatar settings
// validation, the fish_config serial parser, the avatar serial parser).
// test_hal_avatar_fish.py extracts the three "// ---- Protocol
// (host-tested)" sections into .inc files next to this harness's build.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The types avatar_settings.h declares (the .inc sections use them).
#define AVATAR_STYLE_MAX 16
#define AVATAR_NAME_MAX 16
typedef struct {
    char style[AVATAR_STYLE_MAX];
    char eye_color[AVATAR_NAME_MAX];
    int glow;
    char accessory[AVATAR_NAME_MAX];
} avatar_settings_t;
typedef struct {
    bool has_style;
    char style[AVATAR_STYLE_MAX];
    bool has_eye_color;
    char eye_color[AVATAR_NAME_MAX];
    bool has_glow;
    int glow;
    bool has_accessory;
    char accessory[AVATAR_NAME_MAX];
} avatar_patch_t;

#include "avatar_settings_protocol.inc"
#include "fish_protocol.inc"
#include "avatar_kv_protocol.inc"

static int failures;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
} while (0)

int main(void) {
    // Avatar validation.
    CHECK(avatar_style_valid("hal_faceplate") && avatar_style_valid("hal_eye")
              && avatar_style_valid("vinny") && avatar_style_valid("pixel"),
          "avatar styles");
    CHECK(!avatar_style_valid("hal") && !avatar_style_valid(""), "avatar style rejects");
    CHECK(avatar_accessory_valid("santa_hat") && avatar_accessory_valid("none"), "accessories");
    CHECK(!avatar_accessory_valid("halo"), "accessory rejects");
    uint32_t rgb = 0;
    CHECK(avatar_eye_color_rgb("red", &rgb) && rgb == 0xff2a1a, "red rgb");
    CHECK(!avatar_eye_color_rgb("pink", &rgb), "colour rejects");

    avatar_patch_t patch;
    memset(&patch, 0, sizeof(patch));
    CHECK(!avatar_patch_valid(&patch), "empty patch invalid");
    patch.has_style = true;
    snprintf(patch.style, sizeof(patch.style), "%s", "hal_eye");
    CHECK(avatar_patch_valid(&patch), "style patch valid");
    patch.has_glow = true;
    patch.glow = 101;
    CHECK(!avatar_patch_valid(&patch), "glow 101 invalid");
    patch.glow = 100;
    CHECK(avatar_patch_valid(&patch), "glow 100 valid");

    // Serial avatar line.
    avatar_cmd_t kv;
    CHECK(avatar_parse_cmd("style=hal_eye;color=green;glow=80;acc=toque", &kv)
              && kv.patch.has_style && kv.patch.has_eye_color && kv.patch.has_glow
              && kv.patch.has_accessory && kv.patch.glow == 80 && !kv.has_reaction
              && avatar_patch_valid(&kv.patch),
          "avatar kv full");
    CHECK(!avatar_parse_cmd("glow=101", &kv), "avatar kv glow range");
    CHECK(!avatar_parse_cmd("bogus=1", &kv), "avatar kv unknown field");
    CHECK(!avatar_parse_cmd("", &kv), "avatar kv empty");

    // Reactions: valid names, and a reaction-only request is a whole
    // request on its own (nothing persistent to store).
    CHECK(avatar_reaction_valid("wave") && avatar_reaction_valid("dance")
              && avatar_reaction_valid("hearts") && avatar_reaction_valid("sleep"),
          "reaction names");
    CHECK(!avatar_reaction_valid("spin") && !avatar_reaction_valid(""), "reaction rejects");
    CHECK(avatar_parse_cmd("react=dance", &kv) && kv.has_reaction
              && strcmp(kv.reaction, "dance") == 0 && !avatar_patch_any(&kv.patch),
          "avatar react only");
    CHECK(avatar_parse_cmd("style=hal_eye;react=wave", &kv) && kv.has_reaction
              && kv.patch.has_style && avatar_cmd_valid(&kv),
          "avatar style + react");
    CHECK(!avatar_parse_cmd("react=spin", &kv), "avatar react invalid");

    // Fish config line.
    fish_config_t fish;
    CHECK(fish_parse("key=abcd1234efgh", &fish) && !fish.has_voice && !fish.has_model, "fish key only");
    CHECK(fish_parse("key=abcd1234efgh;voice=voice1;model=s1", &fish)
              && fish.has_voice && strcmp(fish.voice, "voice1") == 0
              && fish.has_model && strcmp(fish.model, "s1") == 0,
          "fish full");
    CHECK(fish_parse("key=abcd%31%32%33%34efgh", &fish) && strcmp(fish.key, "abcd1234efgh") == 0,
          "fish percent-decode");
    CHECK(!fish_parse("key=short", &fish), "fish short key");
    CHECK(!fish_parse("key=abcd1234efgh;model=s2", &fish), "fish bad model");
    CHECK(!fish_parse("voice=voice1", &fish), "fish missing key");
    CHECK(!fish_parse("key=abcd1234efgh;extra=1", &fish), "fish unknown field");
    // The speech relay URL: percent-decoded, validated, clearable.
    CHECK(fish_parse("key=abcd1234efgh;url=https%3A%2F%2Fproxy.example.dev%2Fv1%2Faudio%2Fspeech", &fish)
              && fish.has_url && strcmp(fish.url, "https://proxy.example.dev/v1/audio/speech") == 0,
          "fish url decode");
    CHECK(fish_parse("key=abcd1234efgh;url=off", &fish) && fish.has_url
              && strcmp(fish.url, "off") == 0,
          "fish url off clears");
    CHECK(!fish_parse("key=abcd1234efgh;url=notaurl", &fish), "fish url invalid");
    CHECK(fish_parse("key=abcd1234efgh;url=", &fish) && !fish.has_url, "fish url empty ignored");
    char token[64] = "mgst_";
    memset(token + 5, 'A', 42);
    token[47] = 'A';
    token[48] = '\0';
    char line[96];
    snprintf(line, sizeof(line), "key=%s", token);
    CHECK(!fish_parse(line, &fish), "fish rejects an SDK token");

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("hal avatar/fish protocol checks passed\n");
    return 0;
}
