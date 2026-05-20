# StackChan Skill Specification

## Intent

`stack-chan-skill` gives coding agents enough reusable StackChan context to bootstrap, run, flash, and maintain a complete remote-agent project.

It separates generic StackChan hardware, firmware integration, ESP-IDF, vendor, protocol, and server architecture guidance from downstream custom personality and application behavior.

## Scope

In scope:
- StackChan hardware and safety overview.
- Starter architecture for a thin StackChan terminal plus server-side brain.
- Installing ESP-IDF v5.5.4 under `vendor/esp-idf`.
- Vendoring `m5stack/StackChan` under `vendor/StackChan`.
- Providing a reusable `app_remote_agent` firmware app asset.
- Linking/registering the app into the vendored StackChan firmware tree.
- Building, flashing, and troubleshooting the ESP-IDF firmware flow.
- Defining the JSON/binary protocol expected by the starter firmware and server.
- Describing a minimal Bun TypeScript brain server shape.

Out of scope:
- Downstream robot personality prompts.
- Cloud provider account setup beyond naming required environment variables.
- Private LAN IPs, API keys, Wi-Fi credentials, or device tokens.
- Owning or redistributing the full upstream StackChan repository.
- Replacing Espressif or M5Stack upstream documentation.

## Users And Trigger Context

- Primary users: coding agents creating or maintaining StackChan remote-agent starter projects.
- Common user requests: "install StackChan starter", "set up StackChan firmware", "vendor ESP-IDF", "link app_remote_agent", "build StackChan with idf.py", "create a StackChan Bun brain".
- Should not trigger for: unrelated ESP-IDF projects, downstream personality tuning, generic Bun server work with no StackChan hardware, or private deployment credentials.

## Runtime Contract

- Required first actions:
  - Identify whether this skill repo already has `vendor/esp-idf`, `vendor/StackChan`, and linked firmware app files; inspect the target app repo for server code.
  - Read only the reference files needed for the current branch.
  - Preserve downstream custom code unless the user explicitly requests migration or cleanup.
- Required outputs:
  - Concrete files changed or commands run.
  - Any unresolved vendor, ESP-IDF, serial port, or hardware blockers.
  - Validation results for skill structure, server tests, firmware build, or flash when run.
- Non-negotiable constraints:
  - Do not store secrets in reusable assets or references.
  - Do not guess serial ports when flashing.
  - Do not run `idf.py` outside `vendor/StackChan/firmware` for StackChan firmware builds.
  - Do not claim firmware validation without actual command output.
- Expected bundled files loaded at runtime:
  - `SKILL.md` always.
  - Focused `references/*.md` only when routed.
  - `assets/app_remote_agent/*` only when installing or inspecting firmware assets.

## Source And Evidence Model

Authoritative sources:
- `SOURCES.md` for provenance and decisions.
- `assets/app_remote_agent/*` for the starter firmware implementation.
- Official M5Stack `m5stack/StackChan` repository for upstream firmware layout.
- ESP-IDF v5.5.4 documentation for install, export, build, flash, and monitor commands.

Useful improvement sources:
- Build or flash failures observed in downstream projects.
- Upstream StackChan firmware layout changes.
- ESP-IDF version changes required by upstream StackChan.
- Positive and negative examples from agent attempts to bootstrap a blank repo.

Data that must not be stored:
- API keys, Wi-Fi credentials, tokens, private serial logs, private IPs tied to a user's network, or device identifiers not needed for reproduction.

## Reference Architecture

- `SKILL.md` contains runtime routing and the default build path.
- `references/` contains focused runtime depth by decision or task.
- `assets/` contains reusable starter firmware files.
- `SOURCES.md` contains source inventory, decisions, coverage, gaps, and changelog.
- `scripts/` is unused until a repeated validation or install operation becomes fragile enough to automate.

## Validation

- Lightweight validation:
  - Skill structural validator passes.
  - Every routed reference exists.
  - Every listed asset exists.
- Deeper validation:
  - This skill repo can source `vendor/esp-idf/export.sh` and find `idf.py`.
  - StackChan firmware builds from `vendor/StackChan/firmware`.
  - Flash/monitor succeeds when hardware and a correct serial port are available.
- Acceptance gates:
  - Blank-repo instructions cover ESP-IDF install, StackChan vendor checkout, firmware app integration, build/flash, and server protocol.
  - Reusable files do not contain private host-specific values.

## Known Limitations

- The starter firmware app depends on upstream StackChan firmware APIs and may require small patches if upstream layout changes.
- Camera, audio, and servo behavior depend on StackChan firmware internals and hardware state.
- Flashing cannot be fully validated without hardware and the correct serial port.

## Maintenance Notes

- Update `SKILL.md` when trigger behavior, routing, or the default build path changes.
- Update `SOURCES.md` when upstream docs, upstream commits, or evidence change.
- Update `assets/app_remote_agent/` when the generic firmware app changes.
- Add references only when a new task branch has a clear "open when..." reason.
