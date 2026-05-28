# Sources

This file tracks source material synthesized into `stack-chan-skill`.

## Source Inventory

| Source | Type | Trust tier | Retrieved | Confidence | Contribution | Usage constraints | Notes |
|---|---|---|---|---|---|---|---|
| `m5stack/StackChan` GitHub repository | upstream source/docs | canonical | 2026-05-20 | high | Confirms StackChan hardware, repo layout, firmware/app/server/mobile resources, and safety note. | Do not copy full upstream repo; vendor by Git. | Adopted for overview and vendor instructions. |
| `vendor/StackChan/firmware/README.md` | local upstream checkout | canonical | 2026-05-20 | high | States ESP-IDF v5.5.4, `fetch_repos.py`, `idf.py build`, and `idf.py flash`. | Upstream sparse docs; supplement with ESP-IDF docs. | Adopted for build root and version. |
| ESP-IDF v5.5.4 ESP32-S3 Linux/macOS setup docs | official docs | canonical | 2026-05-20 | high | Provides clone, install, export, build, flash, monitor, serial-port, and troubleshooting commands. | Adapt path from `~/esp/esp-idf` to this skill repo's `vendor/esp-idf`. | Adopted for skill-local ESP-IDF convention. |
| Parent repo `plan.md` | local planning docs | local evidence | 2026-05-20 | medium | Captures desired architecture, protocol, firmware responsibilities, screen behavior, and milestones. | Converted to concise references; downstream personality omitted. | Adopted where generic. |
| Parent repo `README.md` | local workflow docs | local evidence | 2026-05-20 | medium | Shows current run/build flow and firmware linkage. | Host-specific paths/IPs omitted from reusable docs. | Adopted as migration source. |
| Parent repo `.agents/skills/stackchan-firmware-flash/*` | local skill | local evidence | 2026-05-20 | high | Existing firmware link/build/flash workflow and troubleshooting. | Folded into this starter to avoid duplicate generic skill. | Adopted and generalized. |
| Parent repo `firmware/app_remote_agent/*` | local firmware implementation | local evidence | 2026-05-20 | high | Reusable remote-agent firmware app, protocol handlers, audio, camera, UI, link script. | Private LAN URL must be parameterized; MIT SPDX preserved. | Adopted as `assets/app_remote_agent/`. |
| Parent repo `src/device/*` and `src/server/routes.ts` | local server implementation | local evidence | 2026-05-20 | high | TypeScript command/event protocol, safety limits, WebSocket route behavior, binary audio/image framing. | Do not copy downstream app personality or private config. | Adopted for protocol and Bun starter docs. |
| Parent repo `src/voice/*`, `src/agent/*`, and `src/server/routes.ts` | local server implementation | local evidence | 2026-05-21 | high | Full voice loop with Deepgram live STT, Deepgram streaming TTS, AI SDK tools, device command helpers, and turn coordination. | Generalize package names, routes, and architecture; omit downstream personality and private config. | Adopted for full-stack voice agent reference. |

## Decisions

1. Skill class: `integration-documentation`, because agents need correct use of upstream StackChan, ESP-IDF, firmware APIs, server protocol, and runtime configuration.
2. Primary execution shape: `reference-backed-expert`, because the skill needs optional depth by task branch and should not load every hardware, firmware, server, and troubleshooting detail on every invocation.
3. Secondary shape: `asset-template`, because `assets/app_remote_agent/` is a reusable starter artifact that agents link into this skill's vendored StackChan checkout.
4. ESP-IDF path: use this skill repo's `vendor/esp-idf`, not a user-global `~/esp` path, so firmware operations are self-contained and reproducible.
5. StackChan vendor path: use this skill repo's ignored `vendor/StackChan` checkout.
6. Firmware ownership: generic `app_remote_agent` lives in this skill repo; downstream custom brain repos should not own a separate generic firmware copy.
7. Downstream customization boundary: personality, provider choices, tokens, local IPs, memory, and application UX stay in the downstream repo.

## Coverage Matrix

| Dimension | Status | Evidence |
|---|---|---|
| API surface and behavior contracts | complete | Protocol, firmware integration, and Bun starter references. |
| Config/runtime options | complete | ESP-IDF vendor path, StackChan vendor path, `STACKY_WS_URL`, token, public base URL. |
| Downstream use cases | complete | Blank repo setup, firmware integration, build/flash, server implementation, full voice agent, debug UI, audio/camera extensions. |
| Known issues/workarounds | partial | Troubleshooting covers IDF, vendor paths, app registration, serial ports, malformed protocol, and audio/camera limits. |
| Version variance | partial | ESP-IDF v5.5.4 and upstream StackChan main documented; future upstream changes require maintenance. |
| Asset template quality | partial | Firmware assets included; patch files may be added later if registration edits become automatable. |

## Open Gaps

1. No automated installer script yet; instructions are manual to avoid hiding vendor edits.
2. Firmware assets should be periodically tested against upstream `m5stack/StackChan` main.
3. Flash validation requires hardware and a known serial port.
4. The app registration patch is documented in references; a reusable patch asset can be added once the upstream target is stable enough.

## Trigger Optimization

Should trigger:
- "install a StackChan starter in this repo"
- "set up vendor/esp-idf for StackChan"
- "vendor m5stack/StackChan and add app_remote_agent"
- "build the StackChan firmware with idf.py"
- "create a Bun brain server for StackChan"
- "create a full StackChan voice agent with Deepgram"
- "what protocol does the StackChan firmware app speak?"

Should not trigger:
- "change Stacky's personality"
- "fix a generic Bun route"
- "install ESP-IDF for an unrelated ESP32 project"
- "tune Deepgram TTS latency"
- "review TypeScript code that does not involve StackChan"

## Changelog

- 2026-05-20: Initial `stack-chan-skill` skill with reference-backed docs, skill-local ESP-IDF convention, and reusable firmware asset plan.
- 2026-05-21: Added full-stack voice agent reference covering Deepgram STT/TTS, AI SDK tools, and server turn coordination.
- 2026-05-28: Added server-driven render protocol starter, local simulator, sample emotion scenes, and firmware scene rendering support for group/circle/ellipse/rect primitives.
