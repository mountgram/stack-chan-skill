# StackChan Skill

Reusable starter knowledge and firmware assets for building a StackChan remote-agent system.

Upstream StackChan source is `https://github.com/m5stack/StackChan`. That repository contains the official StackChan open-source resources, including mobile/server/remote code and the ESP-IDF firmware project in its `firmware/` directory.

This repo is intended to be installed as an agent skill. It teaches an agent how to create a blank StackChan project with:

- a vendored ESP-IDF install at `vendor/esp-idf`
- a vendored upstream StackChan checkout at `vendor/StackChan`
- a reusable `app_remote_agent` firmware app
- a local server-side brain that talks to StackChan over WebSocket

The firmware app in `assets/app_remote_agent/` is generic. Personality, model providers, voice provider choices, memory, local IPs, and private tokens belong in downstream custom projects.

`vendor/.gitkeep` is only a placeholder. ESP-IDF and the upstream StackChan checkout should be placed under this skill repo's `vendor/` directory when operating firmware, but those large local checkouts are ignored and should not be committed.
