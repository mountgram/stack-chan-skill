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
- `render.animate`: acknowledged for protocol compatibility; firmware keyframe playback is a follow-up

The firmware advertises `render` in `hello` with primitive and limit metadata.

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

## Follow-Up Work

- Add firmware-side keyframe runtime for blink and mouth animations.
- Add render scene fallback behavior on server disconnect.
- Decide whether scenes should persist across reconnect or be resent after every `hello`.
