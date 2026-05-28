# Server-Driven Rendering

Read this when exposing custom StackChan avatar rendering from the server.

## Goal

After one firmware update, the server can define custom avatar scenes and send them to the device without reflashing for every face or expression change.

The v1 renderer is intentionally small for the M5Stack ESP32-S3 target:

- black-background 320x240 scene
- `group`, `circle`, `ellipse`, and `rect` primitives only
- no arbitrary SVG
- no paths
- no gradients, masks, filters, fonts, or remote images
- custom scenes replace the existing StackChan avatar view
- firmware-owned status chrome remains above custom scenes

## Bundled Server Assets

The fullstack starter includes:

```text
assets/fullstack-agent/src/render/
  protocol.ts
  scenes.ts
  commands.ts
  simulator-page.ts
  protocol.test.ts
```

The simulator is available at:

```text
GET /render/simulator
```

Use it before touching hardware. It lets an agent load emotion presets, edit scene JSON, preview keyframe animations locally, and send the resulting scene JSON to the device when connected.

## Bundled Firmware Support

`assets/app_remote_agent/` handles:

- `render.defineScene`: stores and immediately draws a v1 render scene
- `render.setScene`: redraws the previously defined scene by `sceneId`
- `render.reset`: removes the custom render scene
- `render.animate`: plays keyframes for `x`, `y`, `scaleX`, `scaleY`, `rotation`, and `opacity` at about 30 fps
- multiple `render.animate` calls can run together as separate layers, capped at 4 active animations and 32 total tracks
- animation tracks can optionally add local audio level influence from playback, mic, or the louder of either source

The firmware advertises `render` in `hello` with primitive and limit metadata.

Server-driven render owns only the avatar/content layer. Firmware status chrome is always preserved above it:

- status dot remains visible
- `Standby`, `Listening`, `Thinking`, `Speaking`, and error text remain visible
- status updates move the firmware UI root foreground after render updates

## Scene Shape

Server scene example:

```json
{
  "type": "render.defineScene",
  "requestId": "render-1",
  "sceneId": "stacky.grumpy.v1",
  "size": { "width": 320, "height": 240 },
  "background": "#000000",
  "nodes": [
    { "id": "face", "kind": "group", "x": 160, "y": 120 },
    { "id": "leftEye", "parent": "face", "kind": "ellipse", "x": -48, "y": -20, "rx": 28, "ry": 20, "fill": "#eef7ff" },
    { "id": "rightEye", "parent": "face", "kind": "ellipse", "x": 48, "y": -20, "rx": 28, "ry": 20, "fill": "#eef7ff" },
    { "id": "mouth", "parent": "face", "kind": "rect", "x": 0, "y": 52, "width": 58, "height": 8, "radius": 4, "fill": "#eef7ff" }
  ]
}
```

Coordinate rules:

- Device canvas is `320x240`.
- Node `x`/`y` are center-based.
- Child coordinates are relative to parent `group` or shape center.
- Firmware v1 applies parent position but not full inherited scale/rotation.

## Animation Shape

Animation commands target node IDs from the active scene:

```json
{
  "type": "render.animate",
  "requestId": "blink-1",
  "animationId": "blink",
  "tracks": [
    { "target": "leftEye", "property": "scaleY", "keyframes": [{ "t": 0, "value": 1 }, { "t": 55, "value": 0.01 }, { "t": 125, "value": 1 }] },
    { "target": "rightEye", "property": "scaleY", "keyframes": [{ "t": 0, "value": 1 }, { "t": 55, "value": 0.01 }, { "t": 125, "value": 1 }] }
  ]
}
```

Layering rules:

- Each `animationId` is one active layer.
- Sending the same `animationId` replaces that layer.
- Sending a different `animationId` adds another layer, up to the firmware cap.
- This allows blink and mouth animation to run together.
- Avoid two simultaneous layers that write the same node/property unless replacing is intended.

## Audio-Reactive Tracks

Use `audioLevel` when the firmware should locally add mic or playback loudness to an animated value:

```json
{
  "type": "render.animate",
  "requestId": "mouth-1",
  "animationId": "talking-mouth",
  "loop": true,
  "tracks": [
    {
      "target": "mouth",
      "property": "scaleY",
      "keyframes": [{ "t": 0, "value": 1 }, { "t": 480, "value": 1 }],
      "audioLevel": { "source": "playback", "scale": 2.4, "min": 0.7, "max": 3.2 }
    }
  ]
}
```

`audioLevel` fields:

- `source`: `playback`, `mic`, or `any`; defaults to playback in firmware
- `scale`: how strongly the normalized level affects the property
- `offset`: optional extra value added after scaling
- `min` and `max`: optional clamps for the final value

The firmware computes audio levels from local PCM, so the server does not send per-frame audio envelopes. This is the preferred path for mouth movement during speech playback.

## Blink Behavior

Blinking should be modeled as a small runtime state machine, not as "close eyes every N seconds."

Track at least:

- whether the character is speaking
- how long they have been silent
- whether a blink is currently in progress
- how long since the last blink
- a randomized next dry-eye threshold
- whether a speech-pause blink already happened
- whether to occasionally do a double blink

Basic behavior:

```ts
if (isSpeaking) {
  silenceTimer = 0;
  blinkRate = 0.75; // Blink less often while speaking.
  didSilenceBlink = false;
} else {
  silenceTimer += delta;
  blinkRate = 1.5; // Blink more readily during silence.
  if (silenceTimer >= 250 && !didSilenceBlink) {
    didSilenceBlink = true;
    tryTriggerBlink(true); // Blink shortly after speech ends or pauses.
  }
}

if (!isBlinking) {
  blinkCounter += delta * blinkRate;
  if (blinkCounter >= nextRandomBlinkMs) {
    blinkCounter = 0;
    nextRandomBlinkMs = randomBetween(5000, 8000);
    if (tryTriggerBlink(false) && Math.random() < 0.05) {
      setTimeout(() => tryTriggerBlink(true), 200); // Occasional double blink.
    }
  }
}
```

Blinking has two causes:

- physiological/dry-eye cadence: randomized 5-8 second intervals
- conversational timing: a blink shortly after speech stops or pauses

The blink itself should be a one-shot layered action with a soft weight envelope:

```ts
function pulseBlink() {
  isBlinking = true;
  const start = now() - clipDuration * 0.1;
  while (elapsed < clipDuration) {
    const progress = elapsed / clipDuration;
    const weight = progress < 0.5 ? progress * 2 : (1 - progress) * 2;
    applyBlinkWeight(weight);
  }
  applyBlinkWeight(0);
  isBlinking = false;
}
```

Architectural rules:

- Blinking should be an additive animation layer on top of the main pose.
- Do not bake blink state into emotion or speech animation.
- A blink clip should only affect eyelids or blink-related controls.
- Blink animation must not fight gaze direction, pupil tracking, facial emotion, head pose, or lip sync.
- Application code should provide high-level state like `isSpeaking`, select a blink layer such as `blink-neutral`, and optionally expose debug readouts like `isBlinking`, `nextBlinkMs`, and `timeSinceLastBlink`.

Why this is not just a timer:

- Fixed intervals look robotic.
- People blink differently during speech versus silence.
- Speech pauses often cause visible blinks.
- Blinks need cooldowns so they do not stack unnaturally.
- Occasional double blinks add realism.
- The motion needs a soft weight envelope, not an instant toggle.
- Blinking must compose with other animation layers without overriding gaze, expression, or lip sync.

## Follow-Up Work

- Add render scene fallback behavior on server disconnect.
- Decide whether scenes should persist across reconnect or be resent after every `hello`.
