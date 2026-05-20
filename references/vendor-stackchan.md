# Vendor StackChan

Read this when adding upstream StackChan source to this skill repo.

## Upstream Source

Use:

```text
https://github.com/m5stack/StackChan
```

This is M5Stack's official StackChan open-source repository. It includes StackChan firmware, mobile app code, server code, and remote-controller resources. For this starter, the important part is the ESP-IDF firmware project under the upstream repo's `firmware/` directory.

## Default Location

Use:

```text
vendor/StackChan
```

The starter repo contains `vendor/.gitkeep` to reserve the directory. It is not a signal to commit the full upstream checkout.

## Clone

Run from the `stack-chan-skill` skill root:

```bash
mkdir -p vendor
git clone https://github.com/m5stack/StackChan.git vendor/StackChan
```

After cloning, the ESP-IDF project root is:

```text
vendor/StackChan/firmware
```

If `vendor/StackChan` already exists, inspect it before changing it:

```bash
git -C vendor/StackChan status --short --branch
git -C vendor/StackChan remote -v
git -C vendor/StackChan rev-parse HEAD
```

## Fetch Firmware Dependencies

After ESP-IDF is active, run from the firmware project root:

```bash
cd vendor/StackChan/firmware
python3 ./fetch_repos.py
```

## Git Tracking

`vendor/StackChan` and `vendor/esp-idf` should be ignored, not committed wholesale.

The starter skill uses:

```gitignore
vendor/*
!vendor/.gitkeep
```

Use a nested Git submodule only if this starter project intentionally changes its vendoring policy.

## Do Not

- Do not copy the whole upstream StackChan repository into this skill.
- Do not modify upstream files without recording the exact changed files and why.
- Do not erase local changes in `vendor/StackChan`; the skill working tree may have local firmware patches.
