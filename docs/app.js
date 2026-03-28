const STORAGE_KEY = "sound_bubbles_preset_draft_v1";

const BASELINE_PRESET = {
  noise_floor: 0.001,
  tracking_thresh: 0.01,
  sustain_thresh: 0.1,
  transient_delta: 0.05,
  duck_burst_level: 0.2,
  duck_attack_coef: 0.99,
  duck_release_coef: 0.999,
  burst_duration_ticks: 10,
  burst_immediate_count: 3,
  density_burst: 50,
  density_sustain: 15,
  density_decay: 5,
  sustain_read_center_offset_samples: 22050,
  micro_duration_ms_min: 5,
  micro_duration_ms_max: 15,
  micro_offset_samples: 441,
  micro_jitter_samples: 100,
  short_duration_ms_min: 20,
  short_duration_ms_max: 50,
  short_offset_samples: 4410,
  short_jitter_samples: 500,
  body_duration_ms_min: 80,
  body_duration_ms_max: 200,
  body_offset_samples: 22050,
  body_jitter_samples: 4410,
  master_dry_gain: 0.5,
  master_wet_gain: 0.5,
};

const GROUPS = [
  {
    title: "Analysis / Detection",
    description: "Signal floor and threshold tuning.",
    fields: ["noise_floor", "tracking_thresh", "sustain_thresh", "transient_delta"],
  },
  {
    title: "Ducking",
    description: "Burst attenuation and envelope timing.",
    fields: ["duck_burst_level", "duck_attack_coef", "duck_release_coef"],
  },
  {
    title: "Burst",
    description: "Burst trigger length and instant spawn count.",
    fields: ["burst_duration_ticks", "burst_immediate_count"],
  },
  {
    title: "Density",
    description: "Spawn rates across burst/sustain/decay phases.",
    fields: ["density_burst", "density_sustain", "density_decay"],
  },
  {
    title: "Sustain Read Position",
    description: "Read center offset in samples.",
    fields: ["sustain_read_center_offset_samples"],
  },
  {
    title: "Micro Class",
    description: "Micro event duration and timeline jitter.",
    fields: [
      "micro_duration_ms_min",
      "micro_duration_ms_max",
      "micro_offset_samples",
      "micro_jitter_samples",
    ],
  },
  {
    title: "Short Class",
    description: "Short event duration and timeline jitter.",
    fields: [
      "short_duration_ms_min",
      "short_duration_ms_max",
      "short_offset_samples",
      "short_jitter_samples",
    ],
  },
  {
    title: "Body Class",
    description: "Body event duration and timeline jitter.",
    fields: ["body_duration_ms_min", "body_duration_ms_max", "body_offset_samples", "body_jitter_samples"],
  },
  {
    title: "Output Mix",
    description: "Final dry/wet blend.",
    fields: ["master_dry_gain", "master_wet_gain"],
  },
];

const FIELD_CONFIG = {
  noise_floor: { min: 0, max: 0.1, step: 0.001 },
  tracking_thresh: { min: 0, max: 0.5, step: 0.005 },
  sustain_thresh: { min: 0, max: 0.5, step: 0.005 },
  transient_delta: { min: 0, max: 0.5, step: 0.005 },
  duck_burst_level: { min: 0, max: 1, step: 0.01 },
  duck_attack_coef: { min: 0.5, max: 0.999, step: 0.001 },
  duck_release_coef: { min: 0.5, max: 0.999, step: 0.001 },
  burst_duration_ticks: { min: 0, max: 100, step: 1 },
  burst_immediate_count: { min: 0, max: 20, step: 1 },
  density_burst: { min: 0, max: 200, step: 1 },
  density_sustain: { min: 0, max: 100, step: 1 },
  density_decay: { min: 0, max: 50, step: 1 },
  sustain_read_center_offset_samples: { min: 0, max: 88200, step: 10 },
  micro_duration_ms_min: { min: 1, max: 50, step: 0.5 },
  micro_duration_ms_max: { min: 1, max: 100, step: 0.5 },
  micro_offset_samples: { min: 0, max: 88200, step: 10 },
  micro_jitter_samples: { min: 0, max: 44100, step: 10 },
  short_duration_ms_min: { min: 10, max: 100, step: 1 },
  short_duration_ms_max: { min: 10, max: 200, step: 1 },
  short_offset_samples: { min: 0, max: 88200, step: 10 },
  short_jitter_samples: { min: 0, max: 44100, step: 10 },
  body_duration_ms_min: { min: 50, max: 500, step: 1 },
  body_duration_ms_max: { min: 50, max: 1000, step: 1 },
  body_offset_samples: { min: 0, max: 88200, step: 10 },
  body_jitter_samples: { min: 0, max: 88200, step: 10 },
  master_dry_gain: { min: 0, max: 2, step: 0.01 },
  master_wet_gain: { min: 0, max: 2, step: 0.01 },
};

const state = {
  preset: { ...BASELINE_PRESET },
};

const editorSectionsEl = document.getElementById("editorSections");
const loadStatusEl = document.getElementById("loadStatus");
const toastEl = document.getElementById("toast");

init();

function init() {
  const loadedDraft = loadDraft();
  state.preset = loadedDraft || { ...BASELINE_PRESET };
  setLoadStatus(loadedDraft ? "Draft loaded" : "Baseline loaded", Boolean(loadedDraft));
  renderEditor();
  bindActions();
}

function bindActions() {
  document.getElementById("copyJsonBtn").addEventListener("click", copyJson);
  document.getElementById("downloadJsonBtn").addEventListener("click", downloadJson);
  document.getElementById("resetDraftBtn").addEventListener("click", resetDraft);
}

function loadDraft() {
  const raw = localStorage.getItem(STORAGE_KEY);
  if (!raw) return null;

  try {
    const parsed = JSON.parse(raw);
    const merged = { ...BASELINE_PRESET };
    for (const key of Object.keys(BASELINE_PRESET)) {
      if (Object.hasOwn(parsed, key)) {
        merged[key] = Number(parsed[key]);
      }
    }
    return merged;
  } catch {
    return null;
  }
}

function saveDraft() {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(state.preset));
  setLoadStatus("Draft loaded", true);
}

function resetDraft() {
  localStorage.removeItem(STORAGE_KEY);
  state.preset = { ...BASELINE_PRESET };
  renderEditor();
  setLoadStatus("Baseline loaded", false);
  showToast("Draft reset");
}

function renderEditor() {
  editorSectionsEl.innerHTML = "";

  GROUPS.forEach((group, index) => {
    const section = document.createElement("section");
    section.className = "card";

    const details = document.createElement("details");
    details.className = "param-group";
    details.open = index === 0;

    const summary = document.createElement("summary");
    summary.innerHTML = `<span>${group.title}</span>`;
    details.appendChild(summary);

    const helper = document.createElement("p");
    helper.className = "group-helper";
    helper.textContent = group.description;
    details.appendChild(helper);

    group.fields.forEach((fieldName) => {
      details.appendChild(createFieldRow(fieldName));
    });

    section.appendChild(details);
    editorSectionsEl.appendChild(section);
  });
}

function createFieldRow(fieldName) {
  const cfg = FIELD_CONFIG[fieldName] || { min: 0, max: 100, step: 1 };

  const row = document.createElement("div");
  row.className = "field-row";

  const topRow = document.createElement("div");
  topRow.className = "field-top";

  const label = document.createElement("label");
  label.setAttribute("for", `${fieldName}-range`);
  label.textContent = humanize(fieldName);

  const numberInput = document.createElement("input");
  numberInput.type = "number";
  numberInput.className = "number-input";
  numberInput.id = `${fieldName}-number`;
  numberInput.min = String(cfg.min);
  numberInput.max = String(cfg.max);
  numberInput.step = String(cfg.step);
  numberInput.value = String(state.preset[fieldName]);

  const slider = document.createElement("input");
  slider.type = "range";
  slider.className = "slider-input";
  slider.id = `${fieldName}-range`;
  slider.min = String(cfg.min);
  slider.max = String(cfg.max);
  slider.step = String(cfg.step);
  slider.value = String(state.preset[fieldName]);

  const applyValue = (rawValue) => {
    const value = clamp(Number(rawValue), cfg.min, cfg.max);
    state.preset[fieldName] = value;
    numberInput.value = String(value);
    slider.value = String(value);
    saveDraft();
  };

  slider.addEventListener("input", (event) => applyValue(event.target.value));
  numberInput.addEventListener("input", (event) => applyValue(event.target.value));

  topRow.appendChild(label);
  topRow.appendChild(numberInput);
  row.appendChild(topRow);
  row.appendChild(slider);

  return row;
}

function copyJson() {
  const text = JSON.stringify(state.preset, null, 2);
  navigator.clipboard
    .writeText(text)
    .then(() => {
      showToast("Copied!");
    })
    .catch(() => {
      showToast("Copy failed");
    });
}

function downloadJson() {
  const text = JSON.stringify(state.preset, null, 2);
  const blob = new Blob([text], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = "current.json";
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  URL.revokeObjectURL(url);
  showToast("JSON downloaded");
}

function setLoadStatus(text, isDraft) {
  loadStatusEl.textContent = text;
  loadStatusEl.classList.toggle("draft", isDraft);
  loadStatusEl.classList.toggle("baseline", !isDraft);
}

function showToast(text) {
  toastEl.textContent = text;
  toastEl.classList.add("show");
  window.clearTimeout(showToast.timeoutId);
  showToast.timeoutId = window.setTimeout(() => {
    toastEl.classList.remove("show");
  }, 1200);
}

function clamp(value, min, max) {
  if (!Number.isFinite(value)) return min;
  return Math.min(max, Math.max(min, value));
}

function humanize(key) {
  return key
    .split("_")
    .map((token) => token[0].toUpperCase() + token.slice(1))
    .join(" ");
}
