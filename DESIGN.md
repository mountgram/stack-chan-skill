---
name: StackChan Skill
description: A friendly, task-first design system for StackChan setup, debugging, and simulator tooling.
colors:
  accent-green: "#35d0a4"
  accent-green-ink: "#03100c"
  status-cyan: "#9fd7ff"
  stacky-eye: "#eef7ff"
  stacky-eye-shadow: "#84a4c4"
  stacky-cheek: "#ff9eb5"
  console-black: "#000000"
  console-bg: "#080a10"
  control-bg: "#05070d"
  panel-top: "#111827"
  panel-bottom: "#0d1220"
  panel-border: "#223047"
  field-border: "#334155"
  secondary-control: "#26344f"
  device-frame: "#1d2738"
  device-stroke: "#61708a"
  text-primary: "#e8f3ff"
typography:
  headline:
    fontFamily: "ui-monospace, SFMono-Regular, Menlo, monospace"
    fontSize: "22px"
    fontWeight: 700
    lineHeight: 1.2
  title:
    fontFamily: "ui-monospace, SFMono-Regular, Menlo, monospace"
    fontSize: "15px"
    fontWeight: 700
    lineHeight: 1.3
  body:
    fontFamily: "ui-monospace, SFMono-Regular, Menlo, monospace"
    fontSize: "12px"
    fontWeight: 400
    lineHeight: 1.45
  label:
    fontFamily: "ui-monospace, SFMono-Regular, Menlo, monospace"
    fontSize: "12px"
    fontWeight: 800
    lineHeight: 1.2
rounded:
  sm: "10px"
  md: "16px"
  device: "22px"
spacing:
  xs: "10px"
  sm: "12px"
  md: "16px"
  lg: "18px"
  page: "22px"
components:
  button-primary:
    backgroundColor: "{colors.accent-green}"
    textColor: "{colors.accent-green-ink}"
    typography: "{typography.label}"
    rounded: "{rounded.sm}"
    padding: "10px 12px"
  button-secondary:
    backgroundColor: "{colors.secondary-control}"
    textColor: "{colors.text-primary}"
    typography: "{typography.label}"
    rounded: "{rounded.sm}"
    padding: "10px 12px"
  panel:
    backgroundColor: "{colors.panel-bottom}"
    textColor: "{colors.text-primary}"
    rounded: "{rounded.md}"
    padding: "16px"
  input-field:
    backgroundColor: "{colors.control-bg}"
    textColor: "{colors.text-primary}"
    typography: "{typography.body}"
    rounded: "{rounded.sm}"
    padding: "10px"
---

# Design System: StackChan Skill

## 1. Overview

**Creative North Star: "Workshop Console"**

The visual system is a compact dark workbench for agent-assisted robot setup. It should feel like a friendly maker console: concrete, legible, slightly playful where StackChan appears, and strict wherever firmware, tokens, serial ports, or device state are involved.

The current surface is the render simulator: a two-panel tool with JSON editors, command buttons, output logs, and a large device preview. The atmosphere comes from layered dark panels, monospace typography, green action color, cyan status labels, and a physical device frame around the 320x240 preview. It rejects the PRODUCT.md anti-references directly: no flashy demo that hides operational details, no decorative UI that distracts from setup, diagnostics, command safety, device state, or validation evidence, and no guessing of secrets, LAN IPs, serial ports, credentials, or hardware outcomes.

**Key Characteristics:**
- Dense, readable, and tool-like.
- Dark by default because the simulator and logs are used like an operator console.
- Playful only in avatar expression, preview feedback, and StackChan face colors.
- Buttons and panels are practical controls, not marketing decoration.
- Evidence, device state, and errors must be visually louder than personality.

## 2. Colors

The palette is a restrained console palette with one confident green action color, cyan section/status labels, and small expressive StackChan face accents.

### Primary
- **Workbench Action Green**: The primary action color for commands such as rendering, playing animation, and sending scenes. It should remain rare and reserved for actions that do something concrete.

### Secondary
- **Signal Cyan**: The section-heading and status accent. Use it for orientation and scanability, not for primary actions.
- **Stacky Cheek Pink**: A character accent for avatar cheeks and expressive preview details only.

### Tertiary
- **Stacky Eye Light**: The bright face primitive used for eyes and mouth shapes in the simulator and starter scenes.
- **Stacky Eye Shadow**: The quieter expression color used for brows, sleepy eyes, and lower-emphasis facial parts.

### Neutral
- **Console Black**: The device canvas and deepest visual floor.
- **Console Background**: The app background surrounding the workbench.
- **Control Well**: Textarea and log backgrounds, reserved for editable or machine-output content.
- **Layered Panel Top** and **Layered Panel Bottom**: The panel gradient pair used to separate controls from the page background.
- **Panel Border** and **Field Border**: Structural strokes for panels and fields.
- **Secondary Control Blue**: Quiet button background for non-primary actions.
- **Device Frame** and **Device Stroke**: Physical preview-frame colors that make the simulator feel like hardware rather than a web card.
- **Console Text**: Primary readable text on all dark surfaces.

### Named Rules
**The Evidence Color Rule.** Green means a concrete command. Cyan means orientation or status. Pink belongs to StackChan expression. Never use these accents as generic decoration.

## 3. Typography

**Display Font:** ui-monospace, SFMono-Regular, Menlo, monospace
**Body Font:** ui-monospace, SFMono-Regular, Menlo, monospace
**Label/Mono Font:** ui-monospace, SFMono-Regular, Menlo, monospace

**Character:** The system is mono-forward because users read JSON, logs, protocol names, command output, file paths, and device events. The mono stack makes UI labels, data, and debug output feel like one coherent tool.

### Hierarchy
- **Headline** (700, 22px, 1.2): Page or tool title, used sparingly.
- **Title** (700, 15px, 1.3): Section labels such as Emotion Presets, Scene JSON, Animation JSON, and Output.
- **Body** (400, 12px, 1.45): JSON textareas, logs, helper text, and dense tool content.
- **Label** (800, 12px, 1.2): Button labels and high-confidence controls.

### Named Rules
**The Same Instrument Rule.** Do not introduce a display font for tooling UI. Firmware setup, JSON editing, route names, and status output should feel like they belong to the same instrument panel.

## 4. Elevation

This system uses layered utility: borders, tonal panels, and a small number of deep shadows separate the editor, output, and device preview. Depth is functional. It explains what is editable, what is previewed, and what is hardware-shaped.

### Shadow Vocabulary
- **Panel Depth** (`box-shadow: 0 18px 60px #0008`): Used on main control panels to lift them from the console background.
- **Device Stage** (`box-shadow: inset 0 0 0 1px #61708a, 0 28px 70px #000a`): Used only on the simulated device frame to create a physical preview object.

### Named Rules
**The Layered Utility Rule.** Shadows must clarify workspace structure. If a shadow makes a button, card, or panel feel decorative rather than easier to parse, remove it.

## 5. Components

### Buttons
- **Shape:** Gently squared controls (10px radius), not pills.
- **Primary:** Workbench Action Green background with dark action ink, bold mono label, and compact padding.
- **Hover / Focus:** Keep interactions direct and fast. Add a visible focus outline when implementing new browser UI; do not rely on color alone.
- **Secondary:** Secondary Control Blue with Console Text for safe, lower-emphasis actions such as loading presets, toggling bounds, or resetting device render.

### Cards / Containers
- **Corner Style:** Practical panel corners (16px radius).
- **Background:** Dark layered panel gradient from Layered Panel Top to Layered Panel Bottom.
- **Shadow Strategy:** Use Panel Depth only on substantial work areas.
- **Border:** Panel Border defines the work area against the console background.
- **Internal Padding:** 16px for panels, 10px to 12px for compact controls.

### Inputs / Fields
- **Style:** Control Well background, Field Border stroke, Console Text, mono body text, 10px radius, and 10px internal padding.
- **Focus:** Use a visible accent or outline that does not obscure JSON content.
- **Error / Disabled:** Errors should be plain, high-contrast, and explicit in text. Disabled controls should remain readable and should not imply a device command was sent.

### Navigation
- **Style, typography, default/hover/active states, mobile treatment.** The current simulator has no navigation. Future navigation should stay compact, mono-forward, and task-labeled around setup, simulator, device, logs, and docs. On narrow screens, the existing pattern stacks control and preview panels into one column.

### Stacky Device Preview

The preview is the signature component. It uses a black 320x240 stage, a dark physical frame, an inset stroke, and pixelated rendering. It should look like a simulator for constrained hardware, not a generic browser canvas.

## 6. Do's and Don'ts

### Do:
- **Do** preserve the dark mono console atmosphere for setup, simulator, and debug surfaces.
- **Do** reserve Workbench Action Green for concrete actions that run commands, send scene data, or change preview/device state.
- **Do** make device state, output logs, and errors readable before adding personality.
- **Do** use playful color and expression inside the StackChan avatar preview, where charm has a clear boundary.
- **Do** keep responsive behavior structural: stack panels on small screens and preserve the 320x240 device ratio.

### Don't:
- **Don't** create a flashy demo that hides operational details.
- **Don't** add decorative UI that distracts from setup, diagnostics, command safety, device state, or validation evidence.
- **Don't** guess or hard-code secrets, LAN IPs, serial ports, Wi-Fi credentials, provider keys, or hardware validation outcomes.
- **Don't** use accents as generic decoration. Green is action, cyan is orientation or status, pink is StackChan expression.
- **Don't** replace the mono tool vocabulary with display type, marketing cards, gradient text, or decorative glass panels.
