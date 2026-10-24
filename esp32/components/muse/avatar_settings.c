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

#include "avatar_settings.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "muse.avatar";

// Same namespace and key style as main/config_store.c. The keys are short:
// NVS key names cap at 15 characters.
#define AVATAR_NS "homehub"
#define KEY_STYLE "av_style"
#define KEY_COLOR "av_color"
#define KEY_GLOW "av_glow"
#define KEY_ACC "av_acc"

// ---- Protocol (host-tested) ------------------------------------------------
// Pure validation: no NVS, no logging. tests/test_hal_avatar_fish.py
// compiles this section against a harness.

static const char *const AVATAR_STYLES[] = { "hal_faceplate", "hal_eye", "vinny", "pixel" };
static const char *const AVATAR_ACCESSORIES[] = {
    "none", "toque", "sunglasses", "nightcap", "party_hat", "santa_hat", "headphones",
};

static bool in_list(const char *value, const char *const *list, size_t count) {
    if (!value) return false;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(value, list[i]) == 0) return true;
    }
    return false;
}

bool avatar_style_valid(const char *style) {
    return in_list(style, AVATAR_STYLES, sizeof(AVATAR_STYLES) / sizeof(AVATAR_STYLES[0]));
}

bool avatar_accessory_valid(const char *accessory) {
    return in_list(accessory, AVATAR_ACCESSORIES,
                   sizeof(AVATAR_ACCESSORIES) / sizeof(AVATAR_ACCESSORIES[0]));
}

bool avatar_eye_color_rgb(const char *name, uint32_t *rgb) {
    static const struct {
        const char *name;
        uint32_t rgb;
    } colors[] = {
        { "red", 0xff2a1a }, { "amber", 0xffa018 }, { "green", 0x2eff5a },
        { "blue", 0x3a8cff }, { "purple", 0xb45cff }, { "white", 0xf4f6ff },
    };
    if (!name) return false;
    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
        if (strcmp(name, colors[i].name) == 0) {
            if (rgb) *rgb = colors[i].rgb;
            return true;
        }
    }
    return false;
}

bool avatar_patch_valid(const avatar_patch_t *patch) {
    if (!patch) return false;
    if (!patch->has_style && !patch->has_eye_color && !patch->has_glow && !patch->has_accessory) {
        return false;   // nothing to change
    }
    if (patch->has_style && !avatar_style_valid(patch->style)) return false;
    if (patch->has_eye_color && !avatar_eye_color_rgb(patch->eye_color, NULL)) return false;
    if (patch->has_glow && (patch->glow < 0 || patch->glow > 100)) return false;
    if (patch->has_accessory && !avatar_accessory_valid(patch->accessory)) return false;
    return true;
}

// The one-shot reaction names (avatar.set's `reaction` param and the
// serial line's react= field). Pure validation, here with the rest.
static const char *const AVATAR_REACTIONS[] = {
    "wave", "dance", "hearts", "stars", "hop", "peek", "stretch", "sleep",
};

bool avatar_reaction_valid(const char *name) {
    return in_list(name, AVATAR_REACTIONS, sizeof(AVATAR_REACTIONS) / sizeof(AVATAR_REACTIONS[0]));
}

// ---- End of host-tested section ---------------------------------------------

const char *avatar_default_style(void) {
    // The stock Muse avatar on every board, Colin's included: a fresh
    // install looks stock. The HAL styles (hal_faceplate, hal_eye) are
    // options the user picks — in the flasher's Advanced fold-out or by
    // asking Muse (avatar.set) — never the baked-in default.
    return "pixel";
}

static bool nvs_get(const char *key, char *out, size_t cap) {
    nvs_handle_t h;
    if (nvs_open(AVATAR_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = cap;
    esp_err_t err = nvs_get_str(h, key, out, &len);
    nvs_close(h);
    return err == ESP_OK;
}

static bool nvs_set(const char *key, const char *value) {
    nvs_handle_t h;
    if (nvs_open(AVATAR_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_str(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGW(TAG, "set %s failed: %s", key, esp_err_to_name(err));
    return err == ESP_OK;
}

static avatar_settings_t s_cache;
static bool s_cached;

void avatar_settings_invalidate(void) {
    s_cached = false;
}

void avatar_settings_get(avatar_settings_t *out) {
    if (!s_cached) {
        avatar_settings_t s;
        snprintf(s.style, sizeof(s.style), "%s", avatar_default_style());
        snprintf(s.eye_color, sizeof(s.eye_color), "%s", "red");
        s.glow = 85;
        snprintf(s.accessory, sizeof(s.accessory), "%s", "none");
        char buf[AVATAR_STYLE_MAX];
        if (nvs_get(KEY_STYLE, buf, sizeof(buf)) && avatar_style_valid(buf)) {
            snprintf(s.style, sizeof(s.style), "%s", buf);
        }
        if (nvs_get(KEY_COLOR, buf, sizeof(buf)) && avatar_eye_color_rgb(buf, NULL)) {
            snprintf(s.eye_color, sizeof(s.eye_color), "%s", buf);
        }
        char glow[8];
        if (nvs_get(KEY_GLOW, glow, sizeof(glow))) {
            int v = atoi(glow);
            if (v >= 0 && v <= 100) s.glow = v;
        }
        if (nvs_get(KEY_ACC, buf, sizeof(buf)) && avatar_accessory_valid(buf)) {
            snprintf(s.accessory, sizeof(s.accessory), "%s", buf);
        }
        s_cache = s;
        s_cached = true;
    }
    *out = s_cache;
}

// The playing reaction, RAM only (see avatar_settings.h). The renderer
// is the only reader; avatar.set / the serial line are the only writers,
// both on other tasks — word-sized fields, and a torn read at worst
// skips a frame of the animation. The name is copied: the caller's
// string (a cJSON value, a stack parse buffer) doesn't outlive the call.
static char s_reaction_name[AVATAR_NAME_MAX];
static bool s_reaction_on;
static int64_t s_reaction_start_ms;

void avatar_reaction_start(const char *name) {
    if (!avatar_reaction_valid(name)) return;
    snprintf(s_reaction_name, sizeof(s_reaction_name), "%s", name);
    s_reaction_start_ms = esp_timer_get_time() / 1000;
    s_reaction_on = true;
}

bool avatar_reaction_current(int64_t now_ms, const char **name, int64_t *elapsed_ms) {
    if (!s_reaction_on) return false;
    int64_t elapsed = now_ms - s_reaction_start_ms;
    if (elapsed < 0 || elapsed >= AVATAR_REACTION_MS) {
        s_reaction_on = false;
        return false;
    }
    if (name) *name = s_reaction_name;
    if (elapsed_ms) *elapsed_ms = elapsed;
    return true;
}

bool avatar_settings_apply(const avatar_patch_t *patch) {
    if (!avatar_patch_valid(patch)) return false;
    bool ok = true;
    if (patch->has_style) ok = nvs_set(KEY_STYLE, patch->style) && ok;
    if (patch->has_eye_color) ok = nvs_set(KEY_COLOR, patch->eye_color) && ok;
    if (patch->has_glow) {
        char glow[8];
        snprintf(glow, sizeof(glow), "%d", patch->glow);
        ok = nvs_set(KEY_GLOW, glow) && ok;
    }
    if (patch->has_accessory) ok = nvs_set(KEY_ACC, patch->accessory) && ok;
    if (ok) {
        avatar_settings_invalidate();
        ESP_LOGI(TAG, "avatar settings stored");
    }
    return ok;
}
