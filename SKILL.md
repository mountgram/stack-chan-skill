---
name: stack-chan-skill
description: Use when starting, maintaining, flashing, running, or troubleshooting a StackChan remote-agent project, including StackChan architecture, ESP-IDF under this skill's vendor/esp-idf, vendoring m5stack/StackChan under this skill's vendor/StackChan, maintaining app_remote_agent firmware, linking it into the StackChan firmware tree, building/flashing/monitoring with idf.py, running the Bun brain server, and debugging the WebSocket protocol between StackChan and the server.
---

# StackChan Skill

Use this skill to start, operate, maintain, flash, and troubleshoot a complete StackChan remote-agent system. Treat StackChan as a thin Wi-Fi body and the local server as the agent brain.

## First Decisions

| If you need to... | Read |
|---|---|
| Understand what StackChan is and what hardware is available | `references/stackchan-overview.md` |
| Choose the repo architecture and split firmware from brain code | `references/starter-architecture.md` |
| Install ESP-IDF locally for the project | `references/esp-idf-install.md` |
| Vendor the upstream StackChan source | `references/vendor-stackchan.md` |
| Add the reusable remote-agent firmware app | `references/firmware-integration.md` |
| Build, flash, or monitor the firmware | `references/firmware-build-flash.md` |
| Implement compatible server/device messages | `references/device-protocol.md` |
| Create, run, or maintain the Bun TypeScript brain server | `references/bun-brain-starter.md` |
| Diagnose build, flash, link, or runtime failures | `references/troubleshooting.md` |

## Common Workflows

| If the user asks to... | Do this |
|---|---|
| Start from a blank repo | Follow `Default Setup Path`, then verify the completion checklist. |
| Maintain existing firmware | Inspect `vendor/StackChan` status, app symlinks/files, `apps.h`, `main.cpp`, and `STACKY_WS_URL` configuration before editing. |
| Build or flash | From this skill root, source `vendor/esp-idf/export.sh`, work from `vendor/StackChan/firmware`, and use `references/firmware-build-flash.md`. |
| Run the remote agent | Start the target app's Bun brain server, verify `/health`, verify `/stacky/device` auth config, then open `REMOTE.AGENT` on StackChan. |
| Debug a disconnected robot | Check server bind/public URL/token first, then firmware URL, Wi-Fi, WebSocket logs, and device telemetry. |
| Change protocol or commands | Update firmware, server `device/protocol`, command helpers, and docs together. |

## Default Setup Path

1. Use this skill repo's `vendor/` directory for firmware dependencies.
2. Install ESP-IDF v5.5.4 into `vendor/esp-idf`; source `vendor/esp-idf/export.sh` before using `idf.py`.
3. Clone upstream StackChan from `https://github.com/m5stack/StackChan` into `vendor/StackChan`. This upstream repo contains the official StackChan open-source resources; its ESP-IDF project lives under `vendor/StackChan/firmware/`.
4. Copy or link `assets/app_remote_agent/` into this skill repo's `vendor/StackChan/firmware/main/apps/app_remote_agent/`.
5. Register `AppRemoteAgent` in the vendored firmware app list and `main.cpp`.
6. Build from `vendor/StackChan/firmware`, never from the repo root.
7. Flash only after choosing the correct serial port or confirming ESP-IDF auto-detect is safe.
8. Implement the brain server against the protocol in `references/device-protocol.md`.

## Run Path

1. Ensure the brain server has `STACKY_SERVER_HOST`, `STACKY_SERVER_PORT`, `STACKY_DEVICE_TOKEN`, and `STACKY_PUBLIC_BASE_URL` configured.
2. Start the Bun server from the target app repo, usually with `bun run dev` or `bun run start`.
3. Open `/health` and verify the server is reachable on the LAN address used by firmware.
4. Flash or run firmware built with a matching `STACKY_WS_URL`.
5. Open the `REMOTE.AGENT` app on StackChan.
6. Use the browser debug UI or `/stacky/debug` stream to verify `hello`, telemetry, commands, acks, and errors.

## Non-Negotiable Constraints

- Keep reusable firmware and firmware vendors in this skill repo, not in a downstream custom brain repo.
- Do not hard-code private LAN IPs, tokens, Wi-Fi credentials, API keys, or host-specific absolute paths into reusable files.
- Treat `vendor/.gitkeep` as a placeholder only; `vendor/esp-idf` and `vendor/StackChan` live in this skill working tree but remain ignored local checkouts.
- Do not edit vendored StackChan code blindly; prefer documented patches or small explicit registration edits.
- Do not run `idf.py` until `vendor/esp-idf/export.sh` has been sourced in the same shell.
- Do not claim firmware build/flash success without running the command and reporting the actual result.
- Do not claim the remote agent is running until both server health and device WebSocket connection are verified.
- Do not force StackChan motors by hand while powered or under control.

## Reusable Assets

| Asset | Use |
|---|---|
| `assets/app_remote_agent/app_remote_agent.cpp` | Thin StackChan terminal app implementation. |
| `assets/app_remote_agent/app_remote_agent.h` | `AppRemoteAgent` declaration. |
| `assets/app_remote_agent/link-into-stackchan.sh` | Symlink helper that links this skill's firmware asset into this skill's vendored StackChan tree. |

## Completion Checklist

- ESP-IDF is installed in this skill repo at `vendor/esp-idf` and `. vendor/esp-idf/export.sh` makes `idf.py` available.
- Upstream StackChan is present in this skill repo at `vendor/StackChan`.
- `vendor/StackChan/firmware/main/apps/app_remote_agent/` contains the remote-agent app files or symlinks.
- `apps.h` includes `app_remote_agent/app_remote_agent.h`.
- `main.cpp` installs `std::make_unique<AppRemoteAgent>()`.
- Firmware builds from `vendor/StackChan/firmware`.
- Server exposes `/stacky/device` WebSocket and handles the documented command/event protocol.
- Running system shows device `hello` and telemetry after opening `REMOTE.AGENT`.
