# Product

## Register

product

## Users

This project serves builders and coding agents working on StackChan remote-agent projects. The human user is typically setting up firmware tooling, connecting hardware to a local brain server, testing voice or render behavior, or asking an agent to make safe changes across firmware, server, and docs. The agent user needs reliable routing, concrete paths, safety constraints, and reusable assets that prevent guesswork.

## Product Purpose

`stack-chan-skill` is an installable agent skill for turning StackChan into a thin Wi-Fi robot terminal controlled by a local Bun/TypeScript brain server. It exists to make the firmware, WebSocket protocol, server starter, render simulator, and setup workflow repeatable without leaking downstream personality, secrets, private LAN details, or hardware assumptions into reusable files. Success means an agent can bootstrap, run, inspect, and maintain a StackChan remote-agent system while clearly reporting what was changed, what was verified, and what still needs real hardware validation.

## Brand Personality

Friendly maker, playful companion, and careful technical guide. The voice should be approachable enough for hardware hackers starting fresh, but precise when handling firmware, flashing, WebSocket protocol, env vars, serial ports, and safety constraints. StackChan can feel expressive and charming; setup and debugging should still feel grounded, legible, and trustworthy.

## Anti-references

Do not make the project feel like a flashy demo that hides operational details. Avoid decorative UI that distracts from setup, diagnostics, command safety, device state, or validation evidence. Do not guess or hard-code secrets, LAN IPs, serial ports, Wi-Fi credentials, provider keys, or hardware validation outcomes. Avoid implying the robot is connected, flashed, or safe until commands and device evidence prove it.

## Design Principles

1. Make the robot boundary visible: separate firmware responsibilities, brain-server behavior, and downstream personality choices.
2. Prefer concrete evidence over confidence: show commands, paths, health checks, device events, and unresolved blockers plainly.
3. Keep setup friendly without hiding risk: explain hardware and firmware steps in accessible language while preserving safety constraints.
4. Let StackChan feel alive in controlled places: use playful expression for avatar rendering and simulator feedback, not for critical setup or flashing decisions.
5. Design for agent handoff: documents, debug screens, and starter code should tell the next agent what to read, what not to touch, and how to verify work.

## Accessibility & Inclusion

No formal WCAG target is specified yet. Interfaces and docs should default to readable contrast, plain labels, keyboard-accessible controls where browser UI exists, and reduced-motion-safe behavior for simulator or debug interactions. Avoid visual effects, motion, or playful language that makes setup status, warnings, errors, or safety-sensitive actions harder to understand.
