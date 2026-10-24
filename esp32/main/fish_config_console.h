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

// Setting the Fish Audio config over serial, which the web flasher does
// after installing (web-flasher/fish-config.js in the muse-flasher site).
//
// The command is one line:
//
//   >fish_config=key=<api-key>[;voice=<reference_id>][;model=<model>][;url=<speech-url>]
//
// Values are percent-encoded, so a key may carry any printable character
// except a newline. Only the key is required: the model defaults to the
// firmware's CONFIG_MUSE_TTS_FISH_MODEL (s2.1-pro-free) and the voice to
// CONFIG_MUSE_TTS_FISH_REFERENCE_ID (the HAL clone), applied by the speech
// code in muse_chat_session.cpp when no stored value exists.
//
// The optional url is a speech relay (the AIPI "Make it talk" Step 4
// shape: Speech URL / API key / Model / Voice): when one is stored, the
// speech code POSTs an OpenAI-format speech request to it instead of
// calling Fish directly — Colin's aipi-fish-proxy Worker is such a relay,
// so switching between direct and relayed speech is this config line, not
// a rebuild. url=off (or url=-) clears a stored relay.
//
// The board answers "@fish_config ok" or "@fish_config error <why>" and
// does NOT restart: the speech code reads these values from NVS for each
// reply. It's serial-only, like the SDK token command, so no one nearby
// can change a gadget's Fish key over BLE. The key is never logged.
//
// Storage keys in the "homehub" NVS namespace (config_store): fish_key,
// fish_voice, fish_model, fish_url. muse_chat_session.cpp reads the same
// keys — they are the shared contract between this file and the speech
// code.

// Handles one console line, without its leading '>'. False if the line
// isn't this command. Wired beside sdk_token_console_command (muse_glue.c
// for Muse boards, sdk_token_console.c's readers for the rest).
bool fish_config_console_command(const char *line);

// The stored values, falling back to the build's CONFIG_MUSE_TTS_FISH_*
// (empty string when neither exists). For settings/diagnostics code; the
// speech path reads NVS itself. The key getter's result must not be logged.
bool fish_config_api_key(char *out, size_t cap);
bool fish_config_reference_id(char *out, size_t cap);
bool fish_config_model(char *out, size_t cap);
// The stored speech relay URL, or false when none is stored (direct Fish).
bool fish_config_speech_url(char *out, size_t cap);
