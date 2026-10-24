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

#include "fish_config_console.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "sdkconfig.h"

#include "config_store.h"

static const char *TAG = "link.fish";

#define COMMAND "fish_config="
#define KEY_API_KEY "fish_key"
#define KEY_VOICE "fish_voice"
#define KEY_MODEL "fish_model"
#define KEY_URL "fish_url"

// ---- Protocol (host-tested) ------------------------------------------------
// Pure parsing and validation: no NVS, no logging, never prints the key.
// tests/test_hal_avatar_fish.py compiles this section against a harness.

typedef struct {
    char key[161];        // the Fish API key (required)
    char voice[65];       // reference_id override, "" when absent
    char model[24];       // model override, "" when absent
    char url[161];        // speech relay URL override, "" when absent
    bool has_voice;
    bool has_model;
    bool has_url;
} fish_config_t;

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Percent-decodes src into out. False on a bad escape or overflow.
static bool fish_decode(const char *src, char *out, size_t cap) {
    size_t n = 0;
    for (const char *p = src; *p; p++) {
        char c = *p;
        if (c == '%') {
            int hi = hex_val(p[1]), lo = p[1] ? hex_val(p[2]) : -1;
            if (hi < 0 || lo < 0) return false;
            c = (char)(hi * 16 + lo);
            p += 2;
            if (c == '\0' || c == ';' || c == '\n') return false;
        }
        if (n + 1 >= cap) return false;
        out[n++] = c;
    }
    out[n] = '\0';
    return true;
}

static bool fish_model_valid(const char *model) {
    return model && (strcmp(model, "s2.1-pro-free") == 0 || strcmp(model, "s1") == 0);
}

// A relay URL is plausible when it's an http(s) URL with a host: the
// speech code POSTs to it verbatim, so anything else is a paste error.
static bool fish_url_valid(const char *url) {
    const char *host = NULL;
    if (strncmp(url, "https://", 8) == 0) host = url + 8;
    if (strncmp(url, "http://", 7) == 0) host = url + 7;
    if (!host || !host[0] || host[0] == '/') return false;
    for (const char *c = url; *c; c++) {
        if (*c == ' ' || *c == '\t') return false;
    }
    return true;
}

// Parses the part after "fish_config=":
// key=<v>[;voice=<v>][;model=<v>][;url=<v>]. A url of "off" or "-"
// clears a stored relay URL (back to calling Fish directly); an empty
// url= is ignored, like an empty voice= or model=.
// False when the key is missing/implausible or a value doesn't validate.
static bool fish_parse(const char *args, fish_config_t *out) {
    memset(out, 0, sizeof(*out));
    bool has_key = false;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", args ? args : "");
    for (char *tok = strtok(buf, ";"); tok; tok = strtok(NULL, ";")) {
        char *eq = strchr(tok, '=');
        if (!eq) return false;
        *eq = '\0';
        const char *value = eq + 1;
        if (strcmp(tok, "key") == 0) {
            if (!fish_decode(value, out->key, sizeof(out->key))) return false;
            has_key = true;
        } else if (strcmp(tok, "voice") == 0) {
            if (!fish_decode(value, out->voice, sizeof(out->voice))) return false;
            out->has_voice = out->voice[0] != '\0';
        } else if (strcmp(tok, "model") == 0) {
            if (!fish_decode(value, out->model, sizeof(out->model))) return false;
            out->has_model = out->model[0] != '\0';
        } else if (strcmp(tok, "url") == 0) {
            if (!fish_decode(value, out->url, sizeof(out->url))) return false;
            out->has_url = out->url[0] != '\0';
        } else {
            return false;   // unknown field
        }
    }
    size_t klen = strlen(out->key);
    if (!has_key || klen < 8 || klen > 160) return false;
    for (const char *c = out->key; *c; c++) {
        if (*c == ' ' || *c == '\t') return false;
    }
    if (strncmp(out->key, "mgst_", 5) == 0) return false;   // an SDK token in the wrong box
    if (out->has_model && !fish_model_valid(out->model)) return false;
    if (out->has_url && strcmp(out->url, "off") != 0 && strcmp(out->url, "-") != 0
        && !fish_url_valid(out->url)) return false;
    return true;
}

// ---- End of host-tested section ---------------------------------------------

// Fallbacks when the Fish Kconfig options are not in this build's
// sdkconfig (they depend on MUSE_HATCH): empty key/voice/model.
#ifndef CONFIG_MUSE_TTS_FISH_API_KEY
#define CONFIG_MUSE_TTS_FISH_API_KEY ""
#endif
#ifndef CONFIG_MUSE_TTS_FISH_REFERENCE_ID
#define CONFIG_MUSE_TTS_FISH_REFERENCE_ID ""
#endif
#ifndef CONFIG_MUSE_TTS_FISH_MODEL
#define CONFIG_MUSE_TTS_FISH_MODEL ""
#endif

bool fish_config_console_command(const char *line) {
    if (strncmp(line, COMMAND, strlen(COMMAND)) != 0) return false;
    fish_config_t cfg;
    if (!fish_parse(line + strlen(COMMAND), &cfg)) {
        ESP_LOGW(TAG, "not a Fish config");
        printf("@fish_config error invalid\n");
        fflush(stdout);
        return true;
    }
    bool ok = config_set_str(KEY_API_KEY, cfg.key);
    if (ok && cfg.has_voice) ok = config_set_str(KEY_VOICE, cfg.voice);
    if (ok && cfg.has_model) ok = config_set_str(KEY_MODEL, cfg.model);
    if (ok && cfg.has_url) {
        // "off"/"-" clears the relay: back to calling Fish directly.
        if (strcmp(cfg.url, "off") == 0 || strcmp(cfg.url, "-") == 0) {
            ok = config_erase_key(KEY_URL);
        } else {
            ok = config_set_str(KEY_URL, cfg.url);
        }
    }
    memset(cfg.key, 0, sizeof(cfg.key));
    if (!ok) {
        ESP_LOGE(TAG, "couldn't store the Fish config");
        printf("@fish_config error storage\n");
        fflush(stdout);
        return true;
    }
    ESP_LOGI(TAG, "Fish Audio config stored");
    printf("@fish_config ok\n");
    fflush(stdout);
    return true;
}

static bool fish_get(const char *key, const char *fallback, char *out, size_t cap) {
    if (config_get_str(key, out, cap) && out[0]) return true;
    if (fallback && fallback[0]) {
        snprintf(out, cap, "%s", fallback);
        return true;
    }
    out[0] = '\0';
    return false;
}

bool fish_config_api_key(char *out, size_t cap) {
    return fish_get(KEY_API_KEY, CONFIG_MUSE_TTS_FISH_API_KEY, out, cap);
}

bool fish_config_reference_id(char *out, size_t cap) {
    return fish_get(KEY_VOICE, CONFIG_MUSE_TTS_FISH_REFERENCE_ID, out, cap);
}

bool fish_config_model(char *out, size_t cap) {
    return fish_get(KEY_MODEL, CONFIG_MUSE_TTS_FISH_MODEL, out, cap);
}

bool fish_config_speech_url(char *out, size_t cap) {
    return fish_get(KEY_URL, NULL, out, cap);
}
