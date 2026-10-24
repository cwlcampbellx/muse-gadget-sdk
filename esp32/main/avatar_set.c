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

#include "avatar_set.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "sdkconfig.h"

#include "avatar_settings.h"

static const char *TAG = "link.avatar";

#define COMMAND "avatar="

// ---- Protocol (host-tested) ------------------------------------------------
// Pure parsing of the serial form into an avatar_patch_t. Validation lives
// in avatar_settings (avatar_patch_valid). tests/test_hal_avatar_fish.py
// compiles this section against a harness.

static bool copy_field(char *out, size_t cap, const char *value) {
    size_t n = strlen(value);
    if (!n || n >= cap) return false;
    memcpy(out, value, n + 1);
    return true;
}

// One avatar.set / ">avatar=" request: the persistent patch plus an
// optional one-shot reaction (transient — avatar_settings' reaction
// state, never stored).
typedef struct {
    avatar_patch_t patch;
    char reaction[AVATAR_NAME_MAX];   // "" when absent
    bool has_reaction;
} avatar_cmd_t;

static bool avatar_patch_any(const avatar_patch_t *patch) {
    return patch->has_style || patch->has_eye_color || patch->has_glow || patch->has_accessory;
}

// The whole request validates when the patch part (if any) validates,
// the reaction (if any) is a known one, and at least one of them exists.
static bool avatar_cmd_valid(const avatar_cmd_t *cmd) {
    if (!cmd) return false;
    bool has_patch = avatar_patch_any(&cmd->patch);
    if (!has_patch && !cmd->has_reaction) return false;
    if (has_patch && !avatar_patch_valid(&cmd->patch)) return false;
    if (cmd->has_reaction && !avatar_reaction_valid(cmd->reaction)) return false;
    return true;
}

// Parses the part after "avatar=": style=<s>;color=<c>;glow=<0..100>;
// acc=<a>;react=<r> in any order, at least one field. False on an
// unknown field or bad value shape (range/enum checks are the
// validators').
static bool avatar_parse_cmd(const char *args, avatar_cmd_t *out) {
    memset(out, 0, sizeof(*out));
    avatar_patch_t *patch = &out->patch;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", args ? args : "");
    for (char *tok = strtok(buf, ";"); tok; tok = strtok(NULL, ";")) {
        char *eq = strchr(tok, '=');
        if (!eq) return false;
        *eq = '\0';
        const char *value = eq + 1;
        if (strcmp(tok, "style") == 0) {
            if (!copy_field(patch->style, sizeof(patch->style), value)) return false;
            patch->has_style = true;
        } else if (strcmp(tok, "color") == 0) {
            if (!copy_field(patch->eye_color, sizeof(patch->eye_color), value)) return false;
            patch->has_eye_color = true;
        } else if (strcmp(tok, "glow") == 0) {
            char *end = NULL;
            long v = strtol(value, &end, 10);
            if (!end || *end || v < 0 || v > 100) return false;
            patch->glow = (int)v;
            patch->has_glow = true;
        } else if (strcmp(tok, "acc") == 0) {
            if (!copy_field(patch->accessory, sizeof(patch->accessory), value)) return false;
            patch->has_accessory = true;
        } else if (strcmp(tok, "react") == 0) {
            if (!copy_field(out->reaction, sizeof(out->reaction), value)) return false;
            out->has_reaction = true;
        } else {
            return false;   // unknown field
        }
    }
    return avatar_cmd_valid(out);
}

// ---- End of host-tested section ---------------------------------------------

static cJSON *result_error(const char *code, const char *message) {
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", false);
    cJSON *error = cJSON_CreateObject();
    cJSON_AddStringToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);
    cJSON_AddItemToObject(result, "error", error);
    return result;
}

// Everything below the host-tested section drives avatar_settings, which
// the muse component only compiles when CONFIG_MUSE_ENABLED — so the
// device side is guarded the same way app.c guards its avatar.set call
// (there by CONFIG_MUSE_AVATAR_SET, which itself depends on
// MUSE_ENABLED). avatar_set.c is in every build and the console hooks in
// sdk_token_console.c / muse_glue.c call avatar_console_command()
// unconditionally, so non-Muse builds get the stubs in the #else below
// instead of references to symbols that were never compiled.
#if CONFIG_MUSE_ENABLED

static cJSON *result_ok(const char *reaction) {
    avatar_settings_t s;
    avatar_settings_get(&s);
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "style", s.style);
    cJSON_AddStringToObject(payload, "eye_color", s.eye_color);
    cJSON_AddNumberToObject(payload, "glow", s.glow);
    cJSON_AddStringToObject(payload, "accessory", s.accessory);
    // Only when one was just fired: it's transient, so it isn't part of
    // the stored settings the rest of this payload reports.
    if (reaction) cJSON_AddStringToObject(payload, "reaction", reaction);
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", true);
    cJSON_AddItemToObject(result, "payload", payload);
    return result;
}

static bool str_param(cJSON *params, const char *key, char *out, size_t cap, bool *has) {
    cJSON *item = params ? cJSON_GetObjectItem(params, key) : NULL;
    if (!item) return true;   // absent is fine
    if (!cJSON_IsString(item) || !item->valuestring || strlen(item->valuestring) >= cap) return false;
    snprintf(out, cap, "%s", item->valuestring);
    *has = true;
    return true;
}

cJSON *avatar_set_ws_command(cJSON *params) {
    avatar_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    avatar_patch_t *patch = &cmd.patch;
    if (!str_param(params, "style", patch->style, sizeof(patch->style), &patch->has_style)
        || !str_param(params, "eye_color", patch->eye_color, sizeof(patch->eye_color), &patch->has_eye_color)
        || !str_param(params, "accessory", patch->accessory, sizeof(patch->accessory), &patch->has_accessory)
        || !str_param(params, "reaction", cmd.reaction, sizeof(cmd.reaction), &cmd.has_reaction)) {
        return result_error("invalid_param", "style, eye_color, accessory and reaction must be short strings");
    }
    cJSON *glow = params ? cJSON_GetObjectItem(params, "glow") : NULL;
    if (glow) {
        if (!cJSON_IsNumber(glow) || glow->valueint < 0 || glow->valueint > 100) {
            return result_error("invalid_param", "glow must be a number from 0 to 100");
        }
        patch->glow = glow->valueint;
        patch->has_glow = true;
    }
    if (!avatar_cmd_valid(&cmd)) {
        return result_error("invalid_param",
                            "give at least one of style (hal_faceplate, hal_eye, vinny, pixel), "
                            "eye_color (red, amber, green, blue, purple, white), glow (0-100), "
                            "accessory (none, toque, sunglasses, nightcap, party_hat, santa_hat, headphones), "
                            "reaction (wave, dance, hearts, stars, hop, peek, stretch, sleep)");
    }
    if (avatar_patch_any(patch) && !avatar_settings_apply(patch)) {
        return result_error("storage_error", "couldn't store the avatar settings");
    }
    if (cmd.has_reaction) avatar_reaction_start(cmd.reaction);
    ESP_LOGI(TAG, "avatar changed by Muse%s", cmd.has_reaction ? " (with a reaction)" : "");
    return result_ok(cmd.has_reaction ? cmd.reaction : NULL);
}

bool avatar_console_command(const char *line) {
    if (strncmp(line, COMMAND, strlen(COMMAND)) != 0) return false;
    avatar_cmd_t cmd;
    if (!avatar_parse_cmd(line + strlen(COMMAND), &cmd)) {
        ESP_LOGW(TAG, "not an avatar setting line");
        printf("@avatar error invalid\n");
        fflush(stdout);
        return true;
    }
    if (avatar_patch_any(&cmd.patch) && !avatar_settings_apply(&cmd.patch)) {
        ESP_LOGE(TAG, "couldn't store the avatar settings");
        printf("@avatar error storage\n");
        fflush(stdout);
        return true;
    }
    if (cmd.has_reaction) avatar_reaction_start(cmd.reaction);
    printf("@avatar ok\n");
    fflush(stdout);
    return true;
}

#else  // !CONFIG_MUSE_ENABLED

// No Muse UI in this build, so avatar_settings was never compiled and
// there is nowhere to store a look. The WS entry point is unreachable
// (app.c only dispatches avatar.set under CONFIG_MUSE_AVATAR_SET, which
// depends on MUSE_ENABLED); it still answers with an error instead of
// referencing the missing storage. The serial entry point consumes an
// ">avatar=" line and answers unsupported, mirroring the real path's
// error reply, so the unconditional console hooks behave the same.
cJSON *avatar_set_ws_command(cJSON *params) {
    (void)params;
    return result_error("unsupported",
                          "avatar settings need the Muse display UI, which this build leaves out");
}

bool avatar_console_command(const char *line) {
    if (strncmp(line, COMMAND, strlen(COMMAND)) != 0) return false;
    printf("@avatar error unsupported\n");
    fflush(stdout);
    return true;
}

#endif  // CONFIG_MUSE_ENABLED
