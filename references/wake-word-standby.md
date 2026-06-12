# Wake-Word Standby

Read this when adding local wake-word detection to `REMOTE.AGENT`.

## Architecture

Use wake-word detection only as a standby trigger:

1. Brain connects to the device-hosted WebSocket and device sends `hello`.
2. Server enters standby and sends `standby`.
3. If the device advertised `wakeWord`, the server may include `wakeWord`.
4. Firmware arms the local detector and does not stream full PCM audio to the server.
5. On detection, firmware sends `event: "wakeWord"`.
6. Server starts the normal conversation flow and sends `startAudio`.
7. Firmware disables wake-word detection while listening, thinking, speaking, or streaming PCM.
8. After `speechDone`, the next `startAudio` is acknowledged only after microphone capture is confirmed by a queued PCM frame.

This keeps the server in charge of policy while the device owns the low-latency local detector.

## microWakeWord Notes

`OHF-Voice/micro-wake-word` is a training framework for compact TensorFlow Lite Micro wake-word models, not a drop-in ESP-IDF command handler. Its README describes a two-stage detector: 16 kHz mono audio is converted to micro-speech features, then a streaming model produces probabilities over recent windows. ESPHome's `micro_wake_word` component shows the operational contract this skill mirrors: models can be enabled/disabled, and an `on_wake_word_detected` hook receives the detected phrase.

Useful references:

- https://github.com/OHF-Voice/micro-wake-word
- https://github.com/esphome/micro-wake-word-models
- https://esphome.io/components/micro_wake_word/

## Create A Wake-Word Model Workspace

From a target project repo, run the skill script:

```bash
bash /path/to/stack-chan-skill/scripts/create-wake-word-workspace.sh "Stacky" --project-root "$PWD"
```

For this skill checkout, the usual command is:

```bash
bash .agents/skills/stack-chan-skill/scripts/create-wake-word-workspace.sh "Stacky" --project-root "$PWD"
```

This creates:

```text
.stacky-wake-words/stacky/
  wake-word.env
  training_parameters.yaml
  run.sh
  scripts/download_backgrounds.py
  scripts/generate_features.py
  scripts/generate_local_negatives.py
  scripts/write_manifest.py
```

Run order inside the generated workspace:

```bash
./run.sh setup
./run.sh preview
./run.sh generate
./run.sh backgrounds
./run.sh local-negatives
./run.sh features
./run.sh train
./run.sh manifest
```

`local-negatives` builds a compact negative dataset from downloaded AudioSet
clips. The generated workspace also keeps `negatives` for OHF's larger
pre-generated Hugging Face negative corpora, but that path requires much more
disk space.

The final model package is:

```text
dist/stacky.tflite
dist/stacky.json
```

If the preview sounds wrong, recreate the workspace with a phoneme string:

```bash
bash .agents/skills/stack-chan-skill/scripts/create-wake-word-workspace.sh "Stacky" --phoneme "stˈæki" --project-root "$PWD" --force
```

## Server Contract

Default server config:

```bash
STACKY_WAKE_WORD_ENABLED=true
STACKY_WAKE_WORD_PHRASE=Stacky
STACKY_WAKE_WORD_MODEL_ID=stacky
STACKY_WAKE_WORD_CUTOFF=0.99
STACKY_WAKE_WORD_SLIDING_WINDOW=10
# optional, only if firmware supports dynamic model loading:
STACKY_WAKE_WORD_MODEL_URL=http://LAN_HOST:6001/models/stacky.json
```

The reusable server sends:

```json
{
  "type": "standby",
  "requestId": "cmd-1",
  "text": "Standby. Say \"Stacky\".",
  "wakeWord": {
    "enabled": true,
    "phrase": "Stacky",
    "modelId": "stacky",
    "cutoff": 0.99,
    "slidingWindow": 10
  }
}
```

Only include `wakeWord` when the device advertises `wakeWord`. Otherwise use the same `standby` command without `wakeWord` and preserve tap-to-talk.

## Firmware Contract

Advertise wake-word support only after the local detector is real:

```json
{
  "type": "hello",
  "id": "stacky-abc",
  "version": 2,
  "capabilities": ["screen", "face", "look", "led", "telemetry", "tap", "audio", "camera", "volume", "standby", "wakeWord", "render"],
  "wakeWord": {
    "version": 1,
    "models": [{ "id": "stacky", "phrase": "Stacky", "sampleRate": 16000, "cutoff": 0.99, "slidingWindow": 10 }]
  }
}
```

When the detector fires:

```json
{ "type": "event", "event": "wakeWord", "wakeWord": "Stacky", "modelId": "stacky", "score": 0.98 }
```

The firmware should reject an unknown requested model with `error`, not silently arm a different one.

When wake-word standby is enabled, do not treat the `startAudio` command as healthy until capture has actually restarted. The firmware should delay the `startAudio` ack until the first PCM frame is captured or queued, and should send an `error` if capture startup times out after playback. This guards the common restart sequence `wakeWord -> startAudio -> stopAudio -> speak -> speechDone -> startAudio`.

## Firmware Build Notes

`REMOTE.AGENT` uses ESP-IDF managed components for the standalone
microWakeWord runtime. The first firmware build fetches those dependencies
under `vendor/StackChan/firmware/managed_components`.

After that first dependency fetch, run:

```bash
bash .agents/skills/stack-chan-skill/scripts/patch-micro-wake-word-component.sh
```

This applies the local compatibility fixes needed by the current managed
component with ESP-IDF 5.5: the ROM CRC include moved, and CMake must expose
the TensorFlow Lite Micro and ESPMicroSpeechFeatures component dependencies.

## Model Policy

`Stacky` is the default desired phrase. It still needs a trained microWakeWord model whose manifest phrase and model id match what the server asks for.

For customization, prefer server selection among models compiled into firmware first. Dynamic model download is a separate firmware feature: only set `dynamicModels: true` after model manifest download, storage, validation, and TFLite Micro arena allocation are implemented and tested on the target M5Stack device.

## Agent Workflow: "Update The Wake Word And Flash"

When a user asks to update the wake word:

1. Create or refresh the wake-word workspace with `scripts/create-wake-word-workspace.sh`.
2. Run `./run.sh preview` and have the user confirm the generated phrase sounds right before long training.
3. Run the remaining generated commands through `manifest`.
4. Convert `dist/<id>.tflite` to the firmware model header used by the remote-agent microWakeWord runner.
5. Update server env: `STACKY_WAKE_WORD_PHRASE`, `STACKY_WAKE_WORD_MODEL_ID`, and optional `STACKY_WAKE_WORD_MODEL_URL`.
6. Build and flash from `vendor/StackChan/firmware` using `references/firmware-build-flash.md`.
7. Verify `/health`, device `hello` advertises `standby` and `wakeWord`, then say the phrase and confirm the server receives `event: "wakeWord"` and sends `startAudio`.
