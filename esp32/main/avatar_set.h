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

#include "cJSON.h"

// The avatar.set command and its serial twin: the Tier 1 face settings
// (components/muse/avatar_settings.h). Two front doors, one storage path:
//
// - Muse calls avatar.set over the Link session when the user just asks
//   ("make your eye green", "put your toque on"). Advertised in
//   build_register_json() (main/noise_control.cpp), handled in
//   on_ws_command() (main/app.c), both under CONFIG_MUSE_AVATAR_SET.
// - The web flasher / a bench tool sends the same settings over the serial
//   console: ">avatar=style=hal_eye;color=green;glow=80;acc=toque", and
//   the board answers "@avatar ok" or "@avatar error <why>". Serial-only,
//   like the token and Fish commands.
//
// Everything is optional; at least one field is required. Values are
// validated here and in avatar_settings: style hal_faceplate | hal_eye |
// vinny | pixel, eye colour red | amber | green | blue | purple | white,
// glow 0..100, accessory none | toque | sunglasses | nightcap | party_hat
// | santa_hat | headphones. Changes are live and survive reboots. One
// extra param, `reaction` (serial: react=<name>), is different: a named
// one-shot animation — wave | dance | hearts | stars | hop | peek |
// stretch | sleep — that plays over the stored look for a few seconds
// and is gone (never stored), the muse-charm avatar's named reactions
// behind the same command.

// Handles the WS command's params (may be NULL) and returns the result
// object in on_ws_command()'s shape: {"ok": true, "payload": {...current
// settings...}} or a command_error()-shaped failure.
cJSON *avatar_set_ws_command(cJSON *params);

// Handles one console line, without its leading '>'. False if the line
// isn't this command. Wired beside sdk_token_console_command.
bool avatar_console_command(const char *line);
