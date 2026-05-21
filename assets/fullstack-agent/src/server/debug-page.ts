export function debugPage() {
  return String.raw`<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Stacky Chan Terminal</title>
  <style>
    :root { color-scheme: dark; font-family: ui-monospace, SFMono-Regular, Menlo, monospace; background: #090b12; color: #e8f3ff; }
    body { margin: 0; padding: 24px; }
    main { max-width: 1100px; margin: 0 auto; display: grid; grid-template-columns: 1.1fr .9fr; gap: 18px; }
    section { border: 1px solid #223047; background: linear-gradient(180deg, #111827, #0d1220); border-radius: 16px; padding: 18px; box-shadow: 0 18px 60px #0008; }
    h1 { margin: 0 0 16px; font-size: 24px; }
    h2 { margin: 0 0 12px; font-size: 16px; color: #9fd7ff; }
    textarea, input, select { width: 100%; box-sizing: border-box; border-radius: 10px; border: 1px solid #334155; background: #070a12; color: #fff; padding: 10px; }
    button { border: 0; background: #35d0a4; color: #03100c; border-radius: 10px; padding: 10px 12px; font-weight: 800; cursor: pointer; }
    button.secondary { background: #26344f; color: #e8f3ff; }
    button.danger { background: #ff5c7a; color: #1a0208; }
    .row { display: flex; gap: 10px; margin: 10px 0; flex-wrap: wrap; }
    .row > * { flex: 1; }
    .camera-frame { min-height: 180px; display: grid; place-items: center; border: 1px solid #1f2937; border-radius: 10px; background: #05070d; overflow: hidden; color: #64748b; }
    .camera-frame img { display: block; max-width: 100%; width: 100%; height: auto; image-rendering: auto; }
    #cameraMeta { margin: 8px 0 0; color: #9ca3af; font-size: 12px; }
    #status { display: inline-block; padding: 5px 9px; border-radius: 999px; background: #3a1b22; color: #ffadbd; }
    #status.connected { background: #123528; color: #80ffd0; }
    .control-label { align-items: center; color: #9ca3af; display: flex; flex: 0 0 auto; font-size: 13px; font-weight: 800; gap: 8px; }
    pre { white-space: pre-wrap; word-break: break-word; max-height: 520px; overflow: auto; background: #05070d; padding: 12px; border-radius: 10px; border: 1px solid #1f2937; }
    @media (max-width: 850px) { body { padding: 12px; } main { grid-template-columns: 1fr; } }
  </style>
</head>
<body>
  <main>
    <section>
      <h1>Stacky Chan Terminal <span id="status">offline</span></h1>
      <h2>Talk To Agent</h2>
      <textarea id="prompt" rows="4" placeholder="look left and tell me a joke"></textarea>
      <div class="row"><button id="sendPrompt">Send Prompt</button><button id="startVoice" class="secondary">Wake Voice</button><button id="stopVoice" class="secondary">Standby</button></div>
      <pre id="transcript"></pre>
      <h2>Manual Body Controls</h2>
      <div class="row"><button data-look="left">Left</button><button data-look="center">Center</button><button data-look="right">Right</button><button data-look="up">Up</button><button data-look="down">Down</button></div>
      <div class="row"><select id="face"><option>none</option><option>neutral</option><option>happy</option><option>angry</option><option>sad</option><option>doubt</option><option>sleepy</option></select><button id="setFace">Set Face</button></div>
      <div class="row"><input id="screenText" placeholder="Screen text" /><button id="setScreen">Set Screen</button></div>
      <div class="row"><input id="ledColor" type="color" value="#33cc99" /><button id="setLed">Set LED</button></div>
      <div class="row"><label class="control-label" for="volume">Volume <span id="volumeValue">90%</span></label><input id="volume" type="range" min="0" max="100" value="90" /></div>
      <div class="row"><button id="home" class="secondary">Home</button><button id="stop" class="danger">Stop</button></div>
      <h2>Faces</h2>
      <div class="row"><button data-face="none">None</button><button data-face="neutral">Neutral</button><button data-face="happy">Happy</button><button data-face="angry">Angry</button><button data-face="sad">Sad</button><button data-face="doubt">Doubt</button><button data-face="sleepy">Sleepy</button></div>
      <h2>Avatar Features</h2>
      <div class="row"><button id="resetAvatar">Reset Features</button><button id="avatarSurprised">Surprised</button><button id="avatarSleepy">Sleepy</button><button id="avatarAngry">Angry</button></div>
      <div class="row"><textarea id="avatarJson" rows="4" placeholder='{"leftEye":{"rotation":1550,"weight":72},"mouth":{"weight":80}}'></textarea><button id="sendAvatarJson">Send Avatar JSON</button></div>
      <h2>Decorators</h2>
      <div class="row"><button data-decorator="heart">Heart</button><button data-decorator="angry">Angry</button><button data-decorator="sweat">Sweat</button><button data-decorator="shy">Shy</button><button data-decorator="dizzy">Dizzy</button></div>
      <div class="row"><button id="clearDecorators">Clear Decorators</button></div>
      <audio id="audio" controls></audio>
    </section>
    <section>
      <h2>State</h2>
      <pre id="state">loading...</pre>
      <h2>Last Camera Image</h2>
      <div class="row"><button id="captureImage" class="secondary">Capture Image</button><button id="captureEnhancedImage" class="secondary">Capture Enhanced</button></div>
      <div class="camera-frame" id="cameraFrame">no image captured yet</div>
      <div id="cameraMeta"></div>
      <h2>Event Log</h2>
      <pre id="log"></pre>
    </section>
  </main>
  <script>
    const $ = (id) => document.getElementById(id);
    const log = (value) => { $('log').textContent = JSON.stringify(value, null, 2) + '\n\n' + $('log').textContent.slice(0, 12000); };
    const updateCamera = (image) => {
      if (!image?.dataUrl) return;
      $('cameraFrame').innerHTML = '<img alt="Last Stacky camera capture" />';
      $('cameraFrame').querySelector('img').src = image.dataUrl;
      const size = [image.width, image.height].filter(Boolean).join('x');
      $('cameraMeta').textContent = (size || 'unknown size') + ' - ' + image.bytes + ' bytes - ' + new Date(image.capturedAt).toLocaleTimeString();
    };
    const updateVolume = (volume) => {
      if (typeof volume !== 'number') return;
      $('volume').value = String(volume);
      $('volumeValue').textContent = volume + '%';
    };
    const api = async (path, body) => {
      const res = await fetch(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
      const json = await res.json();
      log(json);
      if (json.audioUrl) log({ type: 'audio-ready', url: json.audioUrl });
      return json;
    };
    const ws = new WebSocket(location.origin.replace(/^http/, 'ws') + '/stacky/debug');
    ws.onmessage = (event) => {
      const data = JSON.parse(event.data);
      log(data);
      if (data.type === 'snapshot') { $('state').textContent = JSON.stringify(data.state, null, 2); updateCamera(data.lastCameraImage); updateVolume(data.state?.volume); }
      if (data.type === 'camera-image') updateCamera(data.image);
      if (data.type === 'command' && data.command?.type === 'volume') updateVolume(data.command.volume);
      if (data.type === 'device-connected' || data.type === 'device-disconnected' || data.type === 'device-message') fetch('/health').then(r => r.json()).then(j => { $('state').textContent = JSON.stringify(j, null, 2); updateVolume(j.device?.volume); });
      const connected = JSON.stringify(data).includes('connected') && !JSON.stringify(data).includes('disconnected');
      if (connected) { $('status').textContent = 'connected'; $('status').className = 'connected'; }
    };
    $('sendPrompt').onclick = () => api('/api/prompt', { prompt: $('prompt').value });
    document.querySelectorAll('[data-look]').forEach(btn => btn.onclick = () => api('/api/command', { type: 'lookAt', direction: btn.dataset.look }));
    document.querySelectorAll('[data-face]').forEach(btn => btn.onclick = () => api('/api/command', { type: 'face', emotion: btn.dataset.face }));
    $('setFace').onclick = () => api('/api/command', { type: 'face', emotion: $('face').value });
    $('setScreen').onclick = () => api('/api/command', { type: 'screen', text: $('screenText').value, mode: 'connected' });
    $('setLed').onclick = () => api('/api/command', { type: 'led', color: $('ledColor').value });
    $('volume').oninput = () => updateVolume(Number($('volume').value));
    $('volume').onchange = () => api('/api/command', { type: 'volume', volume: Number($('volume').value) });
    $('home').onclick = () => api('/api/command', { type: 'home' });
    $('stop').onclick = () => api('/api/command', { type: 'stop' });
    $('captureImage').onclick = () => api('/api/command', { type: 'captureImage' });
    $('captureEnhancedImage').onclick = () => api('/api/command', { type: 'captureImage', enhance: true });
    document.querySelectorAll('[data-decorator]').forEach(btn => btn.onclick = () => api('/api/command', { type: 'decorator', action: 'add', name: btn.dataset.decorator, durationMs: 3000 }));
    $('clearDecorators').onclick = () => api('/api/command', { type: 'decorator', action: 'clear' });
    $('sendAvatarJson').onclick = () => {
      try {
        const json = JSON.parse($('avatarJson').value);
        if (typeof json !== 'object' || Array.isArray(json)) throw new Error('expected object');
        api('/api/command', { type: 'avatarJson', ...json });
      } catch (e) {
        $('log').textContent = 'Invalid JSON\n' + $('log').textContent;
      }
    };
    $('resetAvatar').onclick = () => api('/api/command', { type: 'avatarJson', leftEye: { rotation: 0, weight: 100, size: 0 }, rightEye: { rotation: 0, weight: 100, size: 0 }, mouth: { weight: 0, rotation: 0 } });
    $('avatarSurprised').onclick = () => api('/api/command', { type: 'avatarJson', leftEye: { rotation: 0, weight: 75 }, rightEye: { rotation: 0, weight: 75 }, mouth: { weight: 60 } });
    $('avatarSleepy').onclick = () => api('/api/command', { type: 'avatarJson', leftEye: { rotation: -50, weight: 35 }, rightEye: { rotation: 50, weight: 35 }, mouth: { weight: 20 } });
    $('avatarAngry').onclick = () => api('/api/command', { type: 'avatarJson', leftEye: { rotation: 450, weight: 70 }, rightEye: { rotation: -450, weight: 70 }, mouth: { weight: 30, rotation: 0 } });
    $('startVoice').onclick = async () => {
      $('transcript').textContent = 'listening...';
      await api('/api/voice/start', {});
    };
    $('stopVoice').onclick = async () => {
      $('transcript').textContent = 'stopping device mic...';
      const result = await api('/api/voice/stop', {});
      if (result.transcript) $('transcript').textContent = result.transcript;
    };
  </script>
</body>
</html>`;
}
