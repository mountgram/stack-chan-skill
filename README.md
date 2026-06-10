# StackChan Skill

## What Is This?

This folder is an installable AI-agent skill for people who want to build and hack on their StackChan robot with the help of a coding agent.

If you are new here, the important idea is simple: your StackChan needs two cooperating pieces of software.

```text
1. Firmware on the robot
   This runs directly on the M5Stack hardware. It controls the screen, motors, buttons, LEDs, microphone, speaker, camera, and Wi-Fi connection.

2. A brain server on your computer
   This runs normal TypeScript code. It talks to AI, speech-to-text, text-to-speech, memory, tools, and your custom robot behavior.
```

This skill teaches an AI coding agent how to set up both sides so StackChan can become a remotely controlled AI robot instead of just a standalone firmware demo.

You are here because you want Claude Code, OpenCode, Codex, etc to help you:

- run a local AI brain server
- make the robot connect to that server over Wi-Fi
- hack on robot behaviors like faces, speech, movement, lights, audio, camera, tools, and personality
- define custom rendered avatar faces and lightweight animations from the server
- add server-selected standby behavior, including tap-to-talk and local wake-word detection

This is not the robot personality. It is the reusable technical starter kit that lets an agent build the robot connection correctly. Personality prompts, provider choices, API keys, LAN IPs, memory, and product behavior belong in your own robot brain project.

## Quick Start

Install the skill with the Skills CLI, then open Claude Code, OpenCode, Codex, or your preferred agent in the project where you want your robot brain code to live.

```text
npx skills add mountgram/stack-chan-skill
```

Copy-paste this into your agent if you are starting fresh:

```text
Use the stack-chan-skill to help me build and hack on my StackChan.

I am starting from here and want a local robot brain server that my StackChan can connect to over Wi-Fi.

First, read the skill's SKILL.md and only the references needed for this setup. Then:

1. Set up the StackChan firmware tooling inside the skill folder's vendor directory.
2. Install ESP-IDF v5.5.4 into vendor/esp-idf if it is not already present.
3. Clone the official StackChan firmware into vendor/StackChan if it is not already present.
4. Add this skill's app_remote_agent firmware app to the local StackChan firmware.
5. Patch and register the app as documented by the skill, including booting directly into `REMOTE.AGENT` while keeping the launcher available from the home button.
6. Create or update a Bun/TypeScript brain server in this project that connects out to the robot-hosted WebSocket.
7. Add server-selected standby support. The server should be able to choose tap-only standby or wake-word standby when the firmware advertises wake-word capability.
8. Tell me what environment variables I need to set, without inventing secrets or hard-coding my private LAN details.
9. Build what can be built safely.
10. Before flashing hardware, show me the serial port you plan to use and wait for my confirmation.
11. At the end, tell me exactly what was changed, what was verified, and what still needs real hardware validation.
```

### Agent Starting Checklist

When an agent uses this skill, it should start by making the local setup concrete. Do not jump straight into writing robot personality code.

1. Read `SKILL.md` to choose the right setup path.
2. Check whether `vendor/esp-idf` exists in this skill folder.
3. If ESP-IDF is missing, follow `references/esp-idf-install.md` and install ESP-IDF v5.5.4 into `vendor/esp-idf`.
4. Check whether `vendor/StackChan` exists in this skill folder.
5. If the official StackChan firmware is missing, follow `references/vendor-stackchan.md` and clone `https://github.com/m5stack/StackChan` into `vendor/StackChan`.
6. Run or apply the documented firmware patch so the robot app can host its WebSocket endpoint.
7. Link or copy `assets/app_remote_agent/` into `vendor/StackChan/firmware/main/apps/app_remote_agent/`.
8. Register `AppRemoteAgent` in the local StackChan firmware app list so it appears as a runnable app on the robot.
9. Apply `assets/app_remote_agent/boot-into-remote-agent.sh` so the launcher opens `REMOTE.AGENT` once at boot.
10. Build from `vendor/StackChan/firmware`, after sourcing `vendor/esp-idf/export.sh` in the same shell.
11. Create or update the Bun/TypeScript brain server in the user's robot brain project.
12. If wake-word standby is requested, read `references/wake-word-standby.md`, train or package the requested model, and make firmware advertise `wakeWord` only after a real detector is integrated.
13. Verify server health, `STACKY_DEVICE_WS_URL`, and device WebSocket protocol compatibility.
14. Flash only when hardware is connected and the serial port choice is explicit.

## The Robot App This Skill Adds

`app_remote_agent` is the small custom firmware app included with this skill.

Its job is to make StackChan act like a Wi-Fi robot terminal:

- it starts on the StackChan hardware
- it hosts a WebSocket server that your computer's brain connects to
- it sends robot events like button presses, audio, images, telemetry, and connection status
- it receives commands like speak, show text, change face, move servos, set LEDs, and capture audio/image data
- it can enter standby where the server decides whether tap-to-talk is enough or whether a local wake-word detector should also be armed

The point is to keep complicated AI behavior off the microcontroller. The robot runs a thin app; your computer runs the smarter brain.

The firmware app also includes a constrained server-driven renderer. The brain server can define a black-background `320x240` face scene from simple primitives, then run small animation layers for things like blinking and mouth movement. Firmware-owned connection/status text remains visible above rendered faces.

Render animations are intentionally ESP32-friendly:

- up to four active animation layers at a time
- up to 32 total active tracks
- transform and opacity tracks only: `x`, `y`, `scaleX`, `scaleY`, `rotation`, `opacity`
- optional `audioLevel` tracks so firmware can locally drive values from mic or playback loudness

This lets the server say "scale the mouth with playback level" once, without sending per-frame mouth updates over WebSocket.

Wake-word detection follows the same thin-terminal boundary. The server owns policy: it sends `standby` with no wake-word config for tap-only standby, or with a `wakeWord` request when it wants the robot to arm a compiled local microWakeWord model. The firmware only advertises `wakeWord` after the detector exists and can run on the target hardware.

Camera capture is designed around ESP32 memory pressure. Debug UI captures should send `preview: true`; plain preview is a small grayscale BMP, while enhanced preview uses a low-memory color BMP when the camera source supports color. Full JPEG capture remains possible for higher-quality agent vision paths, but it can fail under audio/wake-word/speech load and should not be the default debug button behavior.

You do not need to understand all the firmware tooling before getting started. The agent uses this skill to handle those details and should explain hardware or setup blockers in plain language.

### `SKILL.md`

The main file an agent reads first. It tells the agent when to use this skill, what reference file to open for each task, the default setup path, and the non-negotiable safety constraints.

If your agent supports skills, `SKILL.md` is the entry point.

### `references/`

Task-specific documentation for the agent. The skill is intentionally split this way so the agent does not load every firmware, server, protocol, and troubleshooting detail at once.

Current references cover:

- StackChan hardware and architecture overview
- starter project architecture
- ESP-IDF install, which is the toolchain used to build firmware for StackChan's ESP32 hardware
- upstream StackChan checkout, which is the official firmware source this skill builds on
- firmware integration for `app_remote_agent`, which is the included robot-side Wi-Fi terminal app
- build, flash, and monitor commands
- server/device WebSocket protocol
- server-driven rendering, blink behavior, multi-layer animation, and audio-reactive mouth controls
- server-selected wake-word standby with OHF microWakeWord training and firmware packaging
- minimal Bun brain starter
- full-stack voice agent starter with Deepgram and AI SDK tools
- Tailscale/LAN proxying for remote brain servers
- troubleshooting

### `assets/app_remote_agent/`

Reusable robot-side firmware app files for the StackChan side of the remote-agent system.

These files implement the thin robot terminal behavior: host the device WebSocket, exchange JSON commands/events with the brain, and support device capabilities exposed by the firmware.

The remote-agent assets also include the local wake-word runner and generated model packaging files when wake-word support is enabled. Those files keep detection local to the robot while preserving the normal server-side voice pipeline after wake.

The helper script `assets/app_remote_agent/link-into-stackchan.sh` links these source files into your local copy of the official StackChan firmware.

The helper script `assets/app_remote_agent/boot-into-remote-agent.sh` patches the local launcher so StackChan boots directly into `REMOTE.AGENT`. The launcher is still installed first, and the remote app's home button still closes back to the launcher.

### `assets/stacky-websocket-client.ts`

A TypeScript brain-side client for the device protocol. Agents can use this to understand or test the expected JSON and binary WebSocket messages.

### `assets/fullstack-agent/`

A bundled Bun/TypeScript starter app for a more complete server-side brain.

It includes server routes, device protocol helpers, command safety, Deepgram voice plumbing, AI SDK tool wiring, a prompt file, tests, and a debug page. It is a template source for your own robot brain project, not a place to store your personal robot's secrets or long-term custom behavior.

It also includes `scripts/stacky-ws-proxy.ts`, a small Tailscale/LAN WebSocket proxy. Run it on a LAN machine that can reach StackChan and is also on your Tailscale tailnet; a remote brain server can then connect to the proxy's MagicDNS name or `100.x.y.z` address as if it were connecting directly to StackChan.

### `scripts/patch-stackchan.sh`

A small helper that patches the local copy of the official StackChan firmware for HTTPD WebSocket server support.

Run this only after `vendor/StackChan` exists.

### `scripts/create-wake-word-workspace.sh`

Creates a project-local OHF microWakeWord training workspace for a requested phrase, including Piper sample generation controls, feature generation, training config, and model manifest output.

For example:

```text
bash .agents/skills/stack-chan-skill/scripts/create-wake-word-workspace.sh "Stacky" --project-root "$PWD"
```

Use this when a user asks for a custom wake word such as "update the wake word to Banana and flash the device." The agent should train or refresh the model, package the resulting `.tflite` for the firmware wake-word runner, build, flash, and verify the device hears the phrase.

### `scripts/patch-micro-wake-word-component.sh`

Applies local ESP-IDF compatibility fixes to the fetched standalone microWakeWord managed component after the first firmware build downloads it.

Run this from the project using the skill checkout after `vendor/StackChan/firmware/managed_components/micro_wake_word` exists:

```text
bash .agents/skills/stack-chan-skill/scripts/patch-micro-wake-word-component.sh
```

### `.env.example`

Example firmware build variables used to construct the WebSocket URL for the robot.

Copy values from it into your own robot brain project and replace placeholders. Do not commit real tokens, private LAN details, or provider keys to reusable skill files.

### `vendor/`

An intentionally ignored working area for large local firmware dependencies. These are installed locally because they are too large and too machine-specific to commit into this skill.

You usually do not fill this folder by hand. Ask your agent to set up the StackChan firmware tools, and it should use this skill's references to create:

- `vendor/esp-idf`, by installing ESP-IDF v5.5.4
- `vendor/StackChan`, by cloning `https://github.com/m5stack/StackChan`

If you are doing it manually, read `references/esp-idf-install.md` first, then `references/vendor-stackchan.md`.

`vendor/.gitkeep` only keeps the empty directory present before those tools are installed. The actual local checkouts are ignored and should not be committed.

### `SPEC.md` And `SOURCES.md`

Maintenance files for people improving the skill itself.

- `SPEC.md` defines the skill's scope, contract, constraints, and validation expectations.
- `SOURCES.md` records where the guidance came from, what decisions were made, and known gaps.

Most users do not need these files unless they are editing the skill.

## What Should The Agent Do With This Skill?

At a high level, the agent should use this folder as reusable StackChan operating knowledge. The human should be able to say what they want in normal terms, such as "make my StackChan talk to a local AI server," and the agent should translate that into the correct firmware and server steps.

The agent should not copy every file into every project. It should read `SKILL.md`, open only the references needed for the current task, and then make targeted changes in either:

- this skill folder, when installing firmware tools or linking reusable firmware assets
- your own robot brain project, when creating or modifying the server-side brain
- the local copy of the official StackChan firmware, when registering the robot app or applying the documented CMake patch

The intended architecture is:

```text
StackChan hardware
  runs the official StackChan firmware plus this skill's remote-agent app
  connects over Wi-Fi/WebSocket

Local brain server
  runs Bun/TypeScript
  receives audio, telemetry, images, button events
  sends speech, face, LED, servo, display, and control commands
  calls AI, speech-to-text, text-to-speech, tools, and memory from your robot brain project
```

## Installing The Skill

The easiest path is the Skills CLI from [skills.sh](https://www.skills.sh/):

```text
npx skills add mountgram/stack-chan-skill
```

That installs this skill from its GitHub source and makes it available to supported agents such as Claude Code, OpenCode, Codex, Cursor, and others.

After installing, open your agent in the project where your robot brain code should live and use the Quick Start prompt above. The skill description in `SKILL.md` should cause the agent to load it automatically. If it does not, explicitly name it:

```text
Use the stack-chan-skill to help me build and hack on my StackChan.
```

If your agent does not support the Skills CLI, install or copy this folder into a skills directory your agent scans, then point the agent at `SKILL.md`:

```text
For StackChan firmware, WebSocket protocol, ESP-IDF, build/flash, or Bun brain-server work, first read stack-chan-skill/SKILL.md and follow its routing table. Do not invent paths or hard-code secrets.
```

## Technical Details

You can start without knowing these terms. They matter when the agent is installing tools, building firmware, or explaining a failure.

### ESP-IDF

ESP-IDF is Espressif's development toolkit for ESP32 chips. StackChan's M5Stack hardware is built around an ESP32-family chip, so firmware work uses ESP-IDF instead of normal Node, Bun, Python, or browser tooling.

In practice, ESP-IDF gives the agent commands like `idf.py build`, `idf.py flash`, and `idf.py monitor`.

You usually do not write ESP-IDF setup code yourself. The agent uses this skill to install ESP-IDF into:

```text
vendor/esp-idf
```

### Official StackChan Firmware

The official StackChan project lives at:

```text
https://github.com/m5stack/StackChan
```

That official project contains M5Stack's open-source StackChan software. The important part for this skill is its `firmware/` directory, which is the ESP-IDF project that builds the software running on the robot.

This skill does not replace that official project. Instead, the agent clones a local copy into:

```text
vendor/StackChan
```

Then the agent adds `app_remote_agent` to that local firmware checkout.

## Safety And Boundaries

- Do not commit `vendor/esp-idf`, `vendor/StackChan`, `node_modules`, API keys, tokens, Wi-Fi credentials, or private LAN configuration.
- Do not run StackChan firmware builds from the wrong directory. Build from `vendor/StackChan/firmware`.
- Do not run `idf.py` until `vendor/esp-idf/export.sh` has been sourced in that shell.
- Do not guess a serial port for flashing.
- Do not claim firmware build, flash, or robot connection success unless the command or hardware check actually ran.
- Do not claim wake-word support is present until firmware advertises `wakeWord`, the local detector initializes, and a spoken phrase triggers the normal listen flow on hardware.
- Do not commit private wake-word training workspaces, raw downloaded datasets, or one-off generated artifacts unless they are intentionally part of the reusable skill.
- Do not force the StackChan motors by hand while powered or under software control.
- Do not put your robot's personality, memory, provider accounts, or private deployment details into this reusable skill.

## When To Edit This Skill

Edit this skill when the reusable StackChan setup knowledge changes, such as:

- upstream `m5stack/StackChan` changes its firmware layout
- ESP-IDF version requirements change
- `app_remote_agent` changes its generic protocol or firmware behavior
- wake-word training, packaging, firmware integration, or verification workflow changes
- the Bun starter protocol needs a reusable correction
- agents repeatedly fail a setup step and the instructions need to be clearer

Do not edit this skill just to customize one robot's personality, prompt, voice, LED style, provider, local URL, or API key. Put that in your own robot brain project.
