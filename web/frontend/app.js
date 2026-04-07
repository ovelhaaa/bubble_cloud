let audioContext;
let workletNode;
let audioInitialized = false;
let sourceNode = null;

const statusEl = document.getElementById('status');
const meterEl = document.getElementById('metrics');

function setStatus(msg) {
  statusEl.textContent = msg;
}

async function initAudio() {
  if (audioInitialized) return;
  audioContext = new AudioContext();
  await audioContext.audioWorklet.addModule('worklet.js');

  workletNode = new AudioWorkletNode(audioContext, 'bubble-cloud-processor', {
    numberOfInputs: 1,
    numberOfOutputs: 1,
    outputChannelCount: [2],
  });

  workletNode.port.onmessage = (e) => {
    const d = e.data || {};
    if (d.type === 'ready') {
      audioInitialized = true;
      setStatus('ready');
    } else if (d.type === 'wasm-error') {
      setStatus(`wasm-error: ${d.error}`);
    } else if (d.type === 'metrics') {
      meterEl.textContent = `env=${d.envelope.toFixed(4)} state=${d.state} voices=${d.voices}`;
    }
  };

  workletNode.connect(audioContext.destination);
  setStatus('initializing');
}

document.getElementById('start').onclick = async () => {
  await initAudio();
  await audioContext.resume();
};

document.querySelectorAll('[data-param-id]').forEach((el) => {
  el.addEventListener('input', () => {
    if (!workletNode) return;
    workletNode.port.postMessage({
      type: 'param',
      paramId: Number(el.dataset.paramId),
      value: Number(el.value),
    });
  });
});

document.getElementById('bypass').addEventListener('change', (e) => {
  if (!workletNode) return;
  workletNode.port.postMessage({ type: 'bypass', enabled: e.target.checked });
});

document.getElementById('file').addEventListener('change', async (e) => {
  const file = e.target.files?.[0];
  if (!file || !audioContext || !workletNode) return;
  const buf = await file.arrayBuffer();
  const audioBuf = await audioContext.decodeAudioData(buf);
  if (sourceNode) sourceNode.disconnect();
  sourceNode = audioContext.createBufferSource();
  sourceNode.buffer = audioBuf;
  sourceNode.connect(workletNode);
  sourceNode.start();
});

// Simple synth path for validation
const osc = { node: null, gain: null };
document.getElementById('synth').addEventListener('change', async (e) => {
  if (!audioContext || !workletNode) return;
  if (e.target.checked) {
    osc.node = audioContext.createOscillator();
    osc.gain = audioContext.createGain();
    osc.gain.gain.value = 0.2;
    osc.node.frequency.value = 220;
    osc.node.connect(osc.gain).connect(workletNode);
    osc.node.start();
  } else if (osc.node) {
    osc.node.stop();
    osc.node.disconnect();
    osc.gain.disconnect();
    osc.node = null;
  }
});
