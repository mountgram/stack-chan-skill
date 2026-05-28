import { sampleAnimations, sampleScenes } from "./scenes";

export function renderSimulatorPage() {
  const initialScene = JSON.stringify(sampleScenes[0], null, 2);
  const initialAnimation = JSON.stringify(sampleAnimations[0], null, 2);
  const scenesJson = JSON.stringify(sampleScenes);

  return String.raw`<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Stacky Render Simulator</title>
  <style>
    :root { color-scheme: dark; font-family: ui-monospace, SFMono-Regular, Menlo, monospace; background: #080a10; color: #e8f3ff; }
    body { margin: 0; padding: 22px; }
    main { max-width: 1240px; margin: 0 auto; display: grid; grid-template-columns: 380px 1fr; gap: 18px; }
    section { border: 1px solid #223047; background: linear-gradient(180deg, #111827, #0d1220); border-radius: 16px; padding: 16px; box-shadow: 0 18px 60px #0008; }
    h1 { margin: 0 0 12px; font-size: 22px; }
    h2 { margin: 0 0 10px; font-size: 15px; color: #9fd7ff; }
    textarea { width: 100%; box-sizing: border-box; min-height: 240px; border-radius: 10px; border: 1px solid #334155; background: #05070d; color: #fff; padding: 10px; font-size: 12px; }
    button { border: 0; background: #35d0a4; color: #03100c; border-radius: 10px; padding: 10px 12px; font-weight: 800; cursor: pointer; }
    button.secondary { background: #26344f; color: #e8f3ff; }
    .row { display: flex; gap: 10px; margin: 10px 0; flex-wrap: wrap; }
    .row > * { flex: 1; }
    .stage-wrap { display: grid; place-items: center; min-height: 520px; }
    .device { width: 640px; height: 480px; image-rendering: pixelated; background: #000; border: 8px solid #1d2738; border-radius: 22px; box-shadow: inset 0 0 0 1px #61708a, 0 28px 70px #000a; }
    svg { width: 100%; height: 100%; display: block; }
    .node-bounds { fill: none; stroke: #35d0a4; stroke-dasharray: 3 3; opacity: .6; pointer-events: none; }
    pre { white-space: pre-wrap; word-break: break-word; max-height: 220px; overflow: auto; background: #05070d; padding: 10px; border-radius: 10px; border: 1px solid #1f2937; }
    @media (max-width: 980px) { main { grid-template-columns: 1fr; } .device { width: 320px; height: 240px; } }
  </style>
</head>
<body>
  <main>
    <section>
      <h1>Stacky Render Simulator</h1>
      <h2>Emotion Presets</h2>
      <div class="row"><select id="scenePreset"></select><button id="loadPreset" class="secondary">Load Preset</button></div>
      <h2>Scene JSON</h2>
      <textarea id="sceneInput">${escapeHtml(initialScene)}</textarea>
      <div class="row"><button id="renderScene">Render</button><button id="sendScene" class="secondary">Send Scene</button><button id="resetDevice" class="secondary">Reset Device Render</button></div>
      <h2>Animation JSON</h2>
      <textarea id="animationInput">${escapeHtml(initialAnimation)}</textarea>
      <div class="row"><button id="playAnimation">Play Locally</button><button id="sendAnimation" class="secondary">Send Animation</button></div>
      <div class="row"><button id="toggleBounds" class="secondary">Toggle Bounds</button></div>
      <h2>Output</h2>
      <pre id="output"></pre>
    </section>
    <section>
      <div class="stage-wrap">
        <div class="device"><svg id="stage" viewBox="0 0 320 240" role="img" aria-label="Stacky render preview"></svg></div>
      </div>
    </section>
  </main>
  <script>
    const stage = document.getElementById('stage');
    const sceneInput = document.getElementById('sceneInput');
    const animationInput = document.getElementById('animationInput');
    const output = document.getElementById('output');
    let currentScene;
    let showBounds = false;
    let animationFrame;
    const sampleScenes = ${scenesJson};

    const log = (value) => output.textContent = JSON.stringify(value, null, 2);
    const parseJson = (input) => JSON.parse(input.value);
    const svgEl = (name) => document.createElementNS('http://www.w3.org/2000/svg', name);
    const num = (value, fallback) => typeof value === 'number' ? value : fallback;
    const opacity = (node) => node.opacity === undefined ? 1 : node.opacity;
    const transformFor = (node, animated = {}) => {
      const x = num(animated.x, num(node.x, 0));
      const y = num(animated.y, num(node.y, 0));
      const scaleX = num(animated.scaleX, num(node.scaleX, 1));
      const scaleY = num(animated.scaleY, num(node.scaleY, 1));
      const rotation = num(animated.rotation, num(node.rotation, 0));
      return 'translate(' + x + ' ' + y + ') rotate(' + rotation + ') scale(' + scaleX + ' ' + scaleY + ')';
    };

    function setPaint(el, node, animated = {}) {
      el.setAttribute('fill', node.fill || 'none');
      if (node.stroke) el.setAttribute('stroke', node.stroke);
      if (node.strokeWidth !== undefined) el.setAttribute('stroke-width', String(node.strokeWidth));
      el.setAttribute('opacity', String(num(animated.opacity, opacity(node))));
      if (node.visible === false) el.setAttribute('display', 'none');
    }

    function renderNode(node, animated = {}) {
      const group = svgEl('g');
      group.dataset.nodeId = node.id;
      group.setAttribute('transform', transformFor(node, animated[node.id] || {}));
      if (node.kind === 'group') return group;
      let el;
      if (node.kind === 'circle') {
        el = svgEl('circle');
        el.setAttribute('r', String(node.r));
      } else if (node.kind === 'ellipse') {
        el = svgEl('ellipse');
        el.setAttribute('rx', String(node.rx));
        el.setAttribute('ry', String(node.ry));
      } else if (node.kind === 'rect') {
        el = svgEl('rect');
        el.setAttribute('x', String(-node.width / 2));
        el.setAttribute('y', String(-node.height / 2));
        el.setAttribute('width', String(node.width));
        el.setAttribute('height', String(node.height));
        if (node.radius !== undefined) {
          el.setAttribute('rx', String(node.radius));
          el.setAttribute('ry', String(node.radius));
        }
      }
      if (el) {
        setPaint(el, node, animated[node.id] || {});
        group.appendChild(el);
        if (showBounds) {
          const bounds = svgEl('rect');
          const w = node.kind === 'circle' ? node.r * 2 : node.kind === 'ellipse' ? node.rx * 2 : node.kind === 'rect' ? node.width : 0;
          const h = node.kind === 'circle' ? node.r * 2 : node.kind === 'ellipse' ? node.ry * 2 : node.kind === 'rect' ? node.height : 0;
          bounds.setAttribute('x', String(-w / 2));
          bounds.setAttribute('y', String(-h / 2));
          bounds.setAttribute('width', String(w));
          bounds.setAttribute('height', String(h));
          bounds.setAttribute('class', 'node-bounds');
          group.appendChild(bounds);
        }
      }
      return group;
    }

    function renderScene(scene, animated = {}) {
      currentScene = scene;
      stage.replaceChildren();
      const bg = svgEl('rect');
      bg.setAttribute('width', '320');
      bg.setAttribute('height', '240');
      bg.setAttribute('fill', scene.background || '#000000');
      stage.appendChild(bg);
      const rendered = new Map();
      for (const node of scene.nodes) rendered.set(node.id, renderNode(node, animated));
      for (const node of scene.nodes) {
        const el = rendered.get(node.id);
        const parent = node.parent ? rendered.get(node.parent) : stage;
        if (el && parent) parent.appendChild(el);
      }
    }

    function valueAt(track, elapsed) {
      const frames = [...track.keyframes].sort((a, b) => a.t - b.t);
      if (elapsed <= frames[0].t) return frames[0].value;
      for (let i = 1; i < frames.length; i++) {
        const prev = frames[i - 1];
        const next = frames[i];
        if (elapsed <= next.t) {
          const p = (elapsed - prev.t) / Math.max(1, next.t - prev.t);
          return prev.value + (next.value - prev.value) * p;
        }
      }
      return frames[frames.length - 1].value;
    }

    function playAnimation(animation) {
      if (!currentScene) renderScene(parseJson(sceneInput));
      cancelAnimationFrame(animationFrame);
      const duration = Math.max(...animation.tracks.flatMap(track => track.keyframes.map(frame => frame.t)));
      const start = performance.now();
      const tick = () => {
        const raw = performance.now() - start;
        const elapsed = animation.loop ? raw % duration : Math.min(raw, duration);
        const animated = {};
        for (const track of animation.tracks) {
          animated[track.target] ||= {};
          animated[track.target][track.property] = valueAt(track, elapsed);
        }
        renderScene(currentScene, animated);
        if (animation.loop || raw < duration) animationFrame = requestAnimationFrame(tick);
      };
      tick();
    }

    async function api(path, body) {
      const res = await fetch(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
      const json = await res.json();
      log(json);
      return json;
    }

    document.getElementById('renderScene').onclick = () => { try { renderScene(parseJson(sceneInput)); log({ rendered: currentScene.sceneId }); } catch (error) { log({ error: String(error.message || error) }); } };
    document.getElementById('playAnimation').onclick = () => { try { playAnimation(parseJson(animationInput)); } catch (error) { log({ error: String(error.message || error) }); } };
    document.getElementById('toggleBounds').onclick = () => { showBounds = !showBounds; if (currentScene) renderScene(currentScene); };
    document.getElementById('sendScene').onclick = () => api('/api/render/scene', parseJson(sceneInput));
    document.getElementById('sendAnimation').onclick = () => api('/api/render/animation', parseJson(animationInput));
    document.getElementById('resetDevice').onclick = () => api('/api/render/reset', {});
    const preset = document.getElementById('scenePreset');
    for (const scene of sampleScenes) {
      const option = document.createElement('option');
      option.value = scene.sceneId;
      option.textContent = scene.sceneId.replace('stacky.', '').replace('.v1', '');
      preset.appendChild(option);
    }
    document.getElementById('loadPreset').onclick = () => {
      const scene = sampleScenes.find(item => item.sceneId === preset.value) || sampleScenes[0];
      sceneInput.value = JSON.stringify(scene, null, 2);
      renderScene(scene);
      log({ loaded: scene.sceneId });
    };
    renderScene(parseJson(sceneInput));
  </script>
</body>
</html>`;
}

function escapeHtml(value: string) {
  return value.replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;");
}
