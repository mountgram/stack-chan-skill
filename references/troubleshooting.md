# Troubleshooting

Read this when StackChan setup, firmware, or server integration fails.

| Symptom | Check | Fix |
|---|---|---|
| `idf.py: command not found` | ESP-IDF environment is not active in this shell. | Run `. ./vendor/esp-idf/export.sh` from the `stack-chan-skill` root, then retry. |
| `vendor/esp-idf/export.sh` missing | ESP-IDF was not installed in the skill vendor path. | Follow `references/esp-idf-install.md`. |
| `idf.py` says no project | Command was run from app repo root or wrong directory. | Run `idf.py` from this skill repo's `vendor/StackChan/firmware`. |
| `fetch_repos.py` missing | `vendor/StackChan` is absent or not the upstream repo root. | Clone `https://github.com/m5stack/StackChan` into `vendor/StackChan`. |
| `app_remote_agent/app_remote_agent.h` not found | App files are not linked/copied into the vendor app directory. | Install `assets/app_remote_agent/` into `vendor/StackChan/firmware/main/apps/app_remote_agent/`. |
| `AppRemoteAgent` unknown in `main.cpp` | `apps.h` does not include the app header. | Add `#include "app_remote_agent/app_remote_agent.h"` to `apps.h`. |
| App builds but is missing from launcher | `main.cpp` does not install the app. | Add `GetMooncake().installApp(std::make_unique<AppRemoteAgent>());`. |
| Firmware connects to wrong server | `STACKY_WS_URL` was hard-coded or not passed to build. | Rebuild with `STACKY_WS_URL='ws://LAN_HOST:PORT/stacky/device?token=TOKEN' idf.py build`. |
| Firmware cannot reach audio URLs | Server uses `localhost` or unreachable host in `STACKY_PUBLIC_BASE_URL`. | Set `STACKY_PUBLIC_BASE_URL` to a LAN-reachable URL. |
| WebSocket unauthorized | Token mismatch. | Match firmware URL token and server `STACKY_DEVICE_TOKEN`. |
| Motion is jerky or unsafe | Server sends frequent/raw model values. | Clamp in server, rely on firmware clamps, and rate-limit commands. |
| Flash picks wrong serial port | Multiple USB serial devices are connected. | Inspect ports and pass `idf.py -p PORT flash`. |
| Linux flash permission denied | User lacks serial group permission. | Add user to `dialout` or distro-equivalent group, then re-login. |
| Camera images are too dark | Low-light scene or sensor exposure behavior. | Try `captureImage` with `enhance: true`; consider firmware sensor controls if supported. |

## Recovery Principles

- Do not delete `vendor/StackChan` changes to fix a build unless the user explicitly approves.
- Do not overwrite `sdkconfig` casually; `idf.py set-target` can reset configuration.
- Do not flash when the serial port is ambiguous.
- Report the exact command output for build/flash failures.
