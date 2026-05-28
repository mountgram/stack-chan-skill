# StackChan Skill

## What Is This?

This folder is an installable AI-agent skill for building and operating a StackChan remote-agent project.

StackChan is an M5Stack-based robot body with display, servos, LEDs, microphone, speaker, and optional camera support. This skill teaches a coding agent how to treat StackChan as a thin Wi-Fi terminal while a local TypeScript/Bun server acts as the brain.

You are here because you want Claude Code, OpenCode, Codex, or another coding agent to know how to:

- vendor and use the upstream `m5stack/StackChan` firmware project
- install and source ESP-IDF in the right local place
- add the reusable `app_remote_agent` firmware app to StackChan
- build, flash, and monitor the firmware safely
- create or maintain a local Bun brain server that speaks the StackChan WebSocket protocol
- debug the path from hardware to firmware to server to AI tools

This is not the robot personality. It is the reusable technical starter kit that lets an agent build the robot connection correctly. Personality prompts, provider choices, API keys, LAN IPs, memory, and product behavior belong in the downstream project that uses this skill.

## What Is In This Repo?

This README describes only this skill folder:

```text
stack-chan-skill/
  README.md
  SKILL.md
  SPEC.md
  SOURCES.md
  .env.example
  .gitignore
  assets/
  references/
  scripts/
  vendor/
```

It does not describe the outer application repo that may contain your actual StackChan brain server.

### `SKILL.md`

The main file an agent reads first. It tells the agent when to use this skill, what reference file to open for each task, the default setup path, and the non-negotiable safety constraints.

If your agent supports skills, `SKILL.md` is the entry point.

### `references/`

Task-specific documentation for the agent. The skill is intentionally split this way so the agent does not load every firmware, server, protocol, and troubleshooting detail at once.

Current references cover:

- StackChan hardware and architecture overview
- starter repo architecture
- ESP-IDF install under this skill's `vendor/esp-idf`
- upstream StackChan checkout under this skill's `vendor/StackChan`
- firmware integration for `app_remote_agent`
- build, flash, and monitor commands
- server/device WebSocket protocol
- minimal Bun brain starter
- full-stack voice agent starter with Deepgram and AI SDK tools
- troubleshooting

### `assets/app_remote_agent/`

Reusable ESP-IDF firmware app files for the StackChan side of the remote-agent system.

These files implement the thin robot terminal behavior: connect to the brain server over WebSocket, exchange JSON commands/events, and support device capabilities exposed by the firmware.

The helper script `assets/app_remote_agent/link-into-stackchan.sh` links these source files into the vendored upstream StackChan firmware tree.

### `assets/stacky-websocket-client.ts`

A TypeScript example client for the device protocol. Agents can use this to understand or test the expected JSON and binary WebSocket messages.

### `assets/fullstack-agent/`

A bundled Bun/TypeScript starter app for a more complete server-side brain.

It includes server routes, device protocol helpers, command safety, Deepgram voice plumbing, AI SDK tool wiring, a prompt file, tests, and a debug page. It is a template source for downstream projects, not a place to store your personal robot's secrets or long-term custom behavior.

### `scripts/patch-stackchan.sh`

A small helper that patches the vendored upstream StackChan firmware CMake file so the firmware build can receive `STACKY_WS_URL` from the environment.

Run this only after `vendor/StackChan` exists.

### `.env.example`

Example firmware build variables used to construct the WebSocket URL for the robot.

Copy values from it into your downstream environment and replace placeholders. Do not commit real tokens, private LAN details, or provider keys to reusable skill files.

### `vendor/`

An intentionally ignored working area for large local firmware dependencies:

- `vendor/esp-idf` should contain ESP-IDF v5.5.4
- `vendor/StackChan` should contain a checkout of `https://github.com/m5stack/StackChan`

`vendor/.gitkeep` only keeps the directory present. The actual vendor checkouts are local and should not be committed.

### `SPEC.md` And `SOURCES.md`

Maintenance files for people improving the skill itself.

- `SPEC.md` defines the skill's scope, contract, constraints, and validation expectations.
- `SOURCES.md` records where the guidance came from, what decisions were made, and known gaps.

Most users do not need these files unless they are editing the skill.

## What Should The Agent Do With This Skill?

At a high level, the agent should use this folder as reusable StackChan operating knowledge.

The agent should not copy every file into every project. It should read `SKILL.md`, open only the references needed for the current task, and then make targeted changes in either:

- this skill repo, when installing firmware vendors or linking reusable firmware assets
- your downstream app repo, when creating or modifying the server-side brain
- the local vendored StackChan checkout, when registering the firmware app or applying the documented CMake patch

The intended architecture is:

```text
StackChan hardware
  runs vendored StackChan firmware + app_remote_agent
  connects over Wi-Fi/WebSocket

Local brain server
  runs Bun/TypeScript
  receives audio, telemetry, images, button events
  sends speech, face, LED, servo, display, and control commands
  calls AI/STT/TTS providers from the downstream app environment
```

## Getting Started As A Human

1. Install this folder where your coding agent can discover skills.
2. Start a new or existing downstream project for your StackChan brain server.
3. Ask your agent to use `stack-chan-skill` to set up the StackChan remote-agent firmware and brain server.
4. Expect the agent to work through ESP-IDF, upstream StackChan, firmware app linking, build/flash, and server setup.
5. Keep credentials and machine-specific config in your downstream project environment, not in this skill folder.

Good first prompt:

```text
Use the stack-chan-skill to set up a StackChan remote-agent project here. Vendor ESP-IDF and upstream StackChan inside the skill's vendor directory, link app_remote_agent, create a Bun brain server, and tell me what still needs hardware validation.
```

If you already have a server repo:

```text
Use the stack-chan-skill to inspect this StackChan brain server and verify that it matches the firmware WebSocket protocol. Do not change personality or provider choices unless needed for protocol compatibility.
```

If you are flashing hardware:

```text
Use the stack-chan-skill to build the StackChan firmware. Before flashing, show me the serial port you plan to use and wait for confirmation.
```

## Installing For Claude Code

If you use Claude Code skills, place this whole folder in a skills directory Claude Code scans, for example:

```text
~/.claude/skills/stack-chan-skill/
```

or keep it in a project-local skills directory if that is how your Claude Code setup is configured.

After installing, ask Claude Code a StackChan-specific task. The skill description in `SKILL.md` should cause it to load automatically. If it does not, explicitly name it:

```text
Use the stack-chan-skill to install the StackChan firmware dependencies and create the remote-agent brain server.
```

## Installing For OpenCode

For OpenCode, keep or copy this folder under a skills directory known to your OpenCode configuration, commonly:

```text
.agents/skills/stack-chan-skill/
```

Then make sure your OpenCode configuration or workspace instructions expose that skills directory. In this layout, the agent should see `SKILL.md` and route StackChan firmware/server tasks to it.

Example prompt:

```text
Use stack-chan-skill. Set up vendor/esp-idf, vendor/StackChan, app_remote_agent, and a Bun server that can receive the robot WebSocket connection.
```

## Installing For Codex Or Other Agents

If your agent does not have a first-class skill system, you can still use this repo as structured context.

Recommended options:

- Add a short note to your project instructions telling the agent to read `stack-chan-skill/SKILL.md` for StackChan tasks.
- Put this folder somewhere stable in the workspace and reference it by path in prompts.
- For one-off work, tell the agent: `Read stack-chan-skill/SKILL.md first, then follow only the references needed for my task.`

Example instruction for a generic agent:

```text
For StackChan firmware, WebSocket protocol, ESP-IDF, build/flash, or Bun brain-server work, first read .agents/skills/stack-chan-skill/SKILL.md and follow its routing table. Do not invent paths or hard-code secrets.
```

## First Real Workflow

A typical blank-project setup looks like this:

1. The agent reads `SKILL.md`.
2. The agent installs ESP-IDF v5.5.4 into `vendor/esp-idf` under this skill folder.
3. The agent clones `https://github.com/m5stack/StackChan` into `vendor/StackChan` under this skill folder.
4. The agent runs `scripts/patch-stackchan.sh` so firmware builds can receive `STACKY_WS_URL`.
5. The agent links or copies `assets/app_remote_agent/` into `vendor/StackChan/firmware/main/apps/app_remote_agent/`.
6. The agent registers `AppRemoteAgent` in the vendored StackChan app list and `main.cpp`.
7. The agent builds from `vendor/StackChan/firmware`, after sourcing `vendor/esp-idf/export.sh` in the same shell.
8. The agent creates or updates a Bun brain server in the downstream app repo.
9. The agent verifies server health, firmware URL/token config, and device WebSocket connection.
10. The agent flashes only when hardware is connected and the serial port choice is explicit.

## Safety And Boundaries

- Do not commit `vendor/esp-idf`, `vendor/StackChan`, `node_modules`, API keys, tokens, Wi-Fi credentials, or private LAN configuration.
- Do not run StackChan firmware builds from the wrong directory. Build from `vendor/StackChan/firmware`.
- Do not run `idf.py` until `vendor/esp-idf/export.sh` has been sourced in that shell.
- Do not guess a serial port for flashing.
- Do not claim firmware build, flash, or robot connection success unless the command or hardware check actually ran.
- Do not force the StackChan motors by hand while powered or under software control.
- Do not put downstream personality, memory, provider accounts, or private deployment details into this reusable skill.

## When To Edit This Skill

Edit this skill when the reusable StackChan setup knowledge changes, such as:

- upstream `m5stack/StackChan` changes its firmware layout
- ESP-IDF version requirements change
- `app_remote_agent` changes its generic protocol or firmware behavior
- the Bun starter protocol needs a reusable correction
- agents repeatedly fail a setup step and the instructions need to be clearer

Do not edit this skill just to customize one robot's personality, prompt, voice, LED style, provider, local URL, or API key. Put that in the downstream project.
