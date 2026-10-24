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

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The avatar's live settings (Tier 1 face changes): style, eye colour, glow
// and accessory. The user changes them by asking Muse (the avatar.set
// command, main/avatar_set.h) or over the serial console (">avatar=...");
// both store here, in the "homehub" NVS namespace config_store uses, and
// the renderer (components/muse/avatar/muse_pixel.c) reads them through
// avatar_settings_get() — no restart, no reflash, kept across updates
// because updates never touch NVS.
//
// The default style is the stock Muse avatar ("pixel") on every board —
// a fresh install looks stock. The HAL looks are options, not defaults:
// hal_faceplate (badge, bezel eye, grille) suits portrait boards like the
// StickS3, hal_eye (only the round glowing eye, filling the screen) suits
// round boards like the Waveshare 1.75C puck. Picking one is a setting
// like any other here: chosen in the flasher's Advanced fold-out or by
// asking, stored, and switchable back at any time.

#define AVATAR_STYLE_MAX 16
#define AVATAR_NAME_MAX 16

typedef struct {
    char style[AVATAR_STYLE_MAX];          // hal_faceplate | hal_eye | vinny | pixel
    char eye_color[AVATAR_NAME_MAX];       // red | amber | green | blue | purple | white
    int glow;                              // 0..100
    char accessory[AVATAR_NAME_MAX];       // none | toque | sunglasses | nightcap |
                                           // party_hat | santa_hat | headphones
} avatar_settings_t;

// A partial change: only the has_* fields are applied.
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

// The fresh-install default style: the stock Muse avatar ("pixel") on
// every board. HAL styles are picked, never imposed.
const char *avatar_default_style(void);

// Validation (host-tested, see tests/test_hal_avatar_fish.py).
bool avatar_style_valid(const char *style);
bool avatar_accessory_valid(const char *accessory);
// The eye colour's 0xRRGGBB, or false when name isn't a colour.
bool avatar_eye_color_rgb(const char *name, uint32_t *rgb);
bool avatar_patch_valid(const avatar_patch_t *patch);

// The current settings: the stored ones over the defaults. Cached; the
// cache reloads on the first call and after avatar_settings_invalidate().
void avatar_settings_get(avatar_settings_t *out);
void avatar_settings_invalidate(void);

// ---- One-shot reactions (transient, never stored) ---------------------------
// A reaction is a short named animation layered over the persistent look
// ("do a little dance", "wave", "show me hearts") — the muse-charm
// avatar's named reactions, behind the same single command. It lives in
// RAM only: avatar_reaction_start() arms it, the renderer reads the name
// and its start time through avatar_reaction_current() and plays it for
// AVATAR_REACTION_MS, and a reboot or the next settings change forgets
// it. Nothing here touches NVS.
#define AVATAR_REACTION_MS 3000
bool avatar_reaction_valid(const char *name);
void avatar_reaction_start(const char *name);
// The active reaction's name and milliseconds since it started, or false
// when none is playing. `now_ms` is the renderer's clock (esp_timer/1000).
bool avatar_reaction_current(int64_t now_ms, const char **name, int64_t *elapsed_ms);

// Stores a validated patch (missing keys keep their stored/default value)
// and invalidates the cache. False when a store failed.
bool avatar_settings_apply(const avatar_patch_t *patch);
