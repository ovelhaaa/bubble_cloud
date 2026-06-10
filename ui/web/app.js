const STORAGE_KEY = 'bubble_cloud_web_draft_v1';

const BASELINE = {
  noise_floor: 0.001,
  tracking_thresh: 0.01,
  sustain_thresh: 0.05,
  transient_delta: 0.05,
  duck_burst_level: 0.2,
  duck_attack_coef: 0.8,
  duck_release_coef: 0.99,
  burst_duration_ticks: 10,
  burst_immediate_count: 3,
  density_burst: 50,
  density_sustain: 15,
  density_decay: 5,
  attack_region_min_offset_samples: 441,
  attack_region_max_offset_samples: 3528,
  body_region_min_offset_samples: 3528,
  body_region_max_offset_samples: 11025,
  memory_region_min_offset_samples: 11025,
  memory_region_max_offset_samples: 39690,
  micro_duration_ms_min: 5,
  micro_duration_ms_max: 15,
  short_duration_ms_min: 20,
  short_duration_ms_max: 50,
  body_duration_ms_min: 80,
  body_duration_ms_max: 200,
  rng_seed: 1,
  mix_dry_gain: 0.5,
  mix_wet_gain: 0.5,
  stereo_width: 0.7,
  attack_pan_spread: 0.85,
  sustain_pan_spread: 0.45,
  smart_start_enable: 1,
  smart_start_range: 12,
  envelope_variation: 0.35,
  envelope_family: 0,
  wet_drive: 1.0,
  wet_clip_amount: 0.2,
  wet_output_trim: 1.0,
  final_limiter_ceiling_db: -1.0,
  final_limiter_release_ms: 50,
  sustain_diffusion_enable: 0,
  sustain_diffusion_amount: 0.35,
  sustain_diffusion_stages: 1,
  sustain_diffusion_delay: 18,
  sustain_diffusion_feedback: 0.45,
  droplet_enable: 0,
  droplet_probability: 0.12,
  droplet_gain: 0.5,
  droplet_length_scale: 0.6,
  memory_mix: 0.35,
  memory_pull: 0.25,
  memory_darkening: 0.2,
  tone_variation: 0.4,
  attack_brightness: 1.15,
  sustain_darkness: 0.25,
  attack_rate_jitter: 0,
  attack_rate_jitter_depth: 0.02,
};


const UI_MODES = Object.freeze({
  SIMPLE: 'simple',
  ADVANCED: 'advanced',
  DEVELOPER: 'developer',
});

const HISTORY_LIMIT = 80;

const MUSICAL_RANDOMIZE_RANGES = Object.freeze({
  density: [0.28, 0.78],
  bloom: [0.25, 0.82],
  texture: [0.22, 0.78],
  motion: [0.18, 0.72],
  space: [0.25, 0.82],
  gravity: [0.25, 0.72],
  memory: [0.18, 0.68],
  clarity: [0.32, 0.82],
  freeze: [0.0, 0.32],
  sparkle: [0.12, 0.68],
  warmth: [0.28, 0.82],
  mix: [0.32, 0.72],
});

const QUALITY_PROFILES = Object.freeze([
  { id: 0, key: 'MCU_SAFE', label: 'MCU Safe', voices: 8, cpu: 35, ramKb: 256 },
  { id: 1, key: 'MCU_PLUS', label: 'MCU Plus', voices: 16, cpu: 50, ramKb: 384 },
  { id: 2, key: 'WEB_STANDARD', label: 'Web Standard', voices: 24, cpu: 60, ramKb: 512 },
  { id: 3, key: 'WEB_ULTRA', label: 'Web Ultra', voices: 32, cpu: 75, ramKb: 768 },
]);

const PARAM_DEFINITIONS = {
  noise_floor: { min: 0, max: 0.1, step: 0.0005, label: 'Noise Floor' },
  tracking_thresh: { min: 0, max: 0.2, step: 0.001, label: 'Tracking Threshold' },
  sustain_thresh: { min: 0, max: 0.5, step: 0.001, label: 'Sustain Entry Threshold' },
  transient_delta: { min: 0, max: 0.5, step: 0.001, label: 'Transient Delta' },
  duck_burst_level: { min: 0, max: 1, step: 0.01, label: 'Attack Duck Target' },
  duck_attack_coef: { min: 0, max: 1, step: 0.001, label: 'Duck Engage Speed' },
  duck_release_coef: { min: 0.8, max: 0.9999, step: 0.0001, label: 'Duck Recovery Speed' },
  burst_duration_ticks: { min: 1, max: 64, step: 1, label: 'Burst Duration Ticks' },
  burst_immediate_count: { min: 1, max: 12, step: 1, label: 'Burst Immediate Count' },
  density_burst: { min: 0, max: 200, step: 0.1, label: 'Burst Density' },
  density_sustain: { min: 0, max: 100, step: 0.1, label: 'Sustain Density' },
  density_decay: { min: 0, max: 50, step: 0.1, label: 'Tail Density' },
  attack_region_min_offset_samples: { min: 0, max: 30000, step: 1, label: 'Attack Min Offset' },
  attack_region_max_offset_samples: { min: 64, max: 60000, step: 1, label: 'Attack Max Offset' },
  body_region_min_offset_samples: { min: 128, max: 90000, step: 1, label: 'Body Min Offset' },
  body_region_max_offset_samples: { min: 512, max: 120000, step: 1, label: 'Body Max Offset' },
  memory_region_min_offset_samples: { min: 512, max: 150000, step: 1, label: 'Memory Min Offset' },
  memory_region_max_offset_samples: { min: 1024, max: 220500, step: 1, label: 'Memory Max Offset' },
  micro_duration_ms_min: { min: 1, max: 100, step: 0.1, label: 'Micro Duration Min' },
  micro_duration_ms_max: { min: 1, max: 200, step: 0.1, label: 'Micro Duration Max' },
  short_duration_ms_min: { min: 1, max: 200, step: 0.1, label: 'Short Duration Min' },
  short_duration_ms_max: { min: 1, max: 300, step: 0.1, label: 'Short Duration Max' },
  body_duration_ms_min: { min: 10, max: 500, step: 0.1, label: 'Body Duration Min' },
  body_duration_ms_max: { min: 20, max: 800, step: 0.1, label: 'Body Duration Max' },
  rng_seed: { min: 1, max: 2147483520, step: 1, label: 'RNG Seed' },
  mix_dry_gain: { min: 0, max: 1, step: 0.01, label: 'Dry Gain' },
  mix_wet_gain: { min: 0, max: 1, step: 0.01, label: 'Wet Gain' },
  stereo_width: { min: 0, max: 1, step: 0.01, label: 'Stereo Width' },
  attack_pan_spread: { min: 0, max: 1, step: 0.01, label: 'Attack Pan Spread' },
  sustain_pan_spread: { min: 0, max: 1, step: 0.01, label: 'Sustain Pan Spread' },
  smart_start_enable: { min: 0, max: 1, step: 1, label: 'Smart Start Enable' },
  smart_start_range: { min: 0, max: 32, step: 1, label: 'Smart Start Range' },
  envelope_variation: { min: 0, max: 1, step: 0.01, label: 'Envelope Variation' },
  envelope_family: { min: 0, max: 1, step: 1, label: 'Envelope Family' },
  wet_drive: { min: 0.1, max: 2, step: 0.01, label: 'Wet Drive' },
  wet_clip_amount: { min: 0, max: 1, step: 0.01, label: 'Wet Clip Amount' },
  wet_output_trim: { min: 0, max: 1.5, step: 0.01, label: 'Wet Output Trim' },
  final_limiter_ceiling_db: { min: -24, max: 0, step: 0.1, label: 'Limiter Ceiling (dBFS)' },
  final_limiter_release_ms: { min: 5, max: 500, step: 1, label: 'Limiter Release (ms)' },
  sustain_diffusion_enable: { min: 0, max: 1, step: 1, label: 'Sustain Diffusion Enable' },
  sustain_diffusion_amount: { min: 0, max: 1, step: 0.01, label: 'Sustain Diffusion Amount' },
  sustain_diffusion_stages: { min: 1, max: 2, step: 1, label: 'Sustain Diffusion Stages' },
  sustain_diffusion_delay: { min: 2, max: 64, step: 1, label: 'Sustain Diffusion Delay' },
  sustain_diffusion_feedback: { min: 0, max: 0.95, step: 0.01, label: 'Sustain Diffusion Feedback' },
  droplet_enable: { min: 0, max: 1, step: 1, label: 'Droplet Enable' },
  droplet_probability: { min: 0, max: 1, step: 0.01, label: 'Droplet Probability' },
  droplet_gain: { min: 0, max: 1, step: 0.01, label: 'Droplet Gain' },
  droplet_length_scale: { min: 0.2, max: 1, step: 0.01, label: 'Droplet Length Scale' },
  memory_mix: { min: 0, max: 1, step: 0.01, label: 'Memory Mix' },
  memory_pull: { min: 0, max: 1, step: 0.01, label: 'Memory Pull' },
  memory_darkening: { min: 0, max: 1, step: 0.01, label: 'Memory Darkening' },
  tone_variation: { min: 0, max: 1, step: 0.01, label: 'Tone Variation' },
  attack_brightness: { min: 0.3, max: 2, step: 0.01, label: 'Attack Brightness' },
  sustain_darkness: { min: 0, max: 1, step: 0.01, label: 'Sustain Darkness' },
  attack_rate_jitter: { min: 0, max: 1, step: 1, label: 'Attack Rate Jitter' },
  attack_rate_jitter_depth: { min: 0, max: 0.2, step: 0.001, label: 'Attack Rate Jitter Depth' },
  motion_rate: { min: 0, max: 1, step: 0.01, label: 'Motion LFO Rate' },
  motion_depth: { min: 0, max: 1, step: 0.01, label: 'Motion LFO Depth' },
  motion_shape: { min: 0, max: 2, step: 1, label: 'Motion LFO Shape' },
};

const PARAM_HELP = {
  noise_floor: { musical: 'Define o piso de ruído ignorado pelo detector.', technical: 'Energia mínima para considerar sinal válido.', increase: 'o efeito atua já com sinais mais fracos.', decrease: 'o efeito só atua em sinais mais fortes.' },
  tracking_thresh: { musical: 'Ajusta a sensibilidade principal do tracking.', technical: 'Threshold de entrada do detector de envelope.', increase: 'exige sinal mais forte para reagir.', decrease: 'reage mais cedo a pequenas variações.' },
  sustain_thresh: { musical: 'Controla a entrada no estado de sustain.', technical: 'Limite superior para transição tracking/decay → sustain.', increase: 'valor maior = entra em sustain mais tarde (exige envelope maior).', decrease: 'valor menor = entra em sustain mais cedo.' },
  transient_delta: { musical: 'Regula agressividade na captura de ataque.', technical: 'Delta mínimo entre amostras para transiente.', increase: 'detecta menos ataques (mais seletivo).', decrease: 'detecta mais ataques (mais sensível).' },
  duck_burst_level: { musical: 'Define o alvo de ganho wet durante burst/attack.', technical: 'Target aplicado ao ducking interno em estado transiente.', increase: 'valor maior = menos duck (mais presença de wet no ataque).', decrease: 'valor menor = mais duck (ataque mais limpo no dry).' },
  duck_attack_coef: { musical: 'Velocidade com que o duck engata no ataque.', technical: 'Coeficiente do smoothing 1-pole durante transient burst.', increase: 'valor maior = duck engata mais rápido.', decrease: 'valor menor = duck engata mais lento.' },
  duck_release_coef: { musical: 'Velocidade com que o duck se recupera após ataque.', technical: 'Coeficiente do smoothing 1-pole no retorno para ganho 1.0.', increase: 'valor maior = recuperação mais rápida.', decrease: 'valor menor = recuperação mais lenta.' },
  burst_duration_ticks: { musical: 'Duração do modo burst após ataque.', technical: 'Número de ticks mantidos em burst.', increase: 'burst dura mais tempo.', decrease: 'burst termina mais cedo.' },
  burst_immediate_count: { musical: 'Quantidade de grãos imediatos no ataque.', technical: 'Número de vozes disparadas instantaneamente.', increase: 'ataque mais cheio e denso.', decrease: 'ataque mais leve.' },
  density_burst: { musical: 'Densidade durante a fase de burst.', technical: 'Taxa de spawn no estado burst.', increase: 'mais grãos por segundo no ataque.', decrease: 'menos grãos por segundo no ataque.' },
  density_sustain: { musical: 'Densidade no corpo/sustain.', technical: 'Taxa de spawn no estado sustentado.', increase: 'textura contínua mais cheia.', decrease: 'textura de sustain mais espaçada.' },
  density_decay: { musical: 'Define quanta densidade existe na cauda (sparse decay).', technical: 'Teto de target_density no estado ENGINE_STATE_SPARSE_DECAY.', increase: 'valor maior = cauda mais densa (mais spawns no decay).', decrease: 'valor menor = cauda mais rarefeita.' },
  attack_region_min_offset_samples: { musical: 'Início da janela de leitura de ataque.', technical: 'Offset mínimo em samples da região attack.', increase: 'lê material um pouco mais antigo.', decrease: 'lê material mais recente.' },
  attack_region_max_offset_samples: { musical: 'Fim da janela de leitura de ataque.', technical: 'Offset máximo em samples da região attack.', increase: 'amplia alcance temporal da região.', decrease: 'encurta alcance temporal da região.' },
  body_region_min_offset_samples: { musical: 'Início da janela de corpo.', technical: 'Offset mínimo em samples da região body.', increase: 'empurra leitura para trás no tempo.', decrease: 'aproxima leitura do presente.' },
  body_region_max_offset_samples: { musical: 'Fim da janela de corpo.', technical: 'Offset máximo em samples da região body.', increase: 'inclui material mais antigo.', decrease: 'foca em material menos antigo.' },
  memory_region_min_offset_samples: { musical: 'Começo da janela de memória longa.', technical: 'Offset mínimo em samples da região memory.', increase: 'remove parte da memória mais recente.', decrease: 'traz memória mais recente para a região.' },
  memory_region_max_offset_samples: { musical: 'Limite máximo da memória longa.', technical: 'Offset máximo em samples da região memory.', increase: 'acessa memória mais distante.', decrease: 'restringe memória mais distante.' },
  micro_duration_ms_min: { musical: 'Menor duração das microbolhas.', technical: 'Duração mínima (ms) da classe micro.', increase: 'microgrãos ficam menos curtos.', decrease: 'microgrãos ficam mais curtinhos.' },
  micro_duration_ms_max: { musical: 'Maior duração das microbolhas.', technical: 'Duração máxima (ms) da classe micro.', increase: 'permite microgrãos mais longos.', decrease: 'limita microgrãos mais curtos.' },
  short_duration_ms_min: { musical: 'Menor duração dos grãos curtos.', technical: 'Duração mínima (ms) da classe short.', increase: 'grãos short começam mais longos.', decrease: 'grãos short podem ser mais curtos.' },
  short_duration_ms_max: { musical: 'Maior duração dos grãos curtos.', technical: 'Duração máxima (ms) da classe short.', increase: 'grãos short podem alongar mais.', decrease: 'grãos short ficam mais contidos.' },
  body_duration_ms_min: { musical: 'Menor duração dos grãos de corpo.', technical: 'Duração mínima (ms) da classe body.', increase: 'camada body fica menos curta.', decrease: 'camada body fica mais curta.' },
  body_duration_ms_max: { musical: 'Maior duração dos grãos de corpo.', technical: 'Duração máxima (ms) da classe body.', increase: 'camada body pode sustentar mais.', decrease: 'camada body sustenta menos.' },
  rng_seed: { musical: 'Troca o caráter randômico do preset.', technical: 'Seed do gerador pseudoaleatório.', increase: 'gera outra distribuição de escolhas.', decrease: 'gera uma distribuição diferente também.' },
  mix_dry_gain: { musical: 'Volume do sinal original.', technical: 'Ganho linear no caminho dry.', increase: 'mais som limpo na saída.', decrease: 'menos som limpo na saída.' },
  mix_wet_gain: { musical: 'Volume do sinal processado.', technical: 'Ganho linear no caminho wet.', increase: 'mais efeito na mistura final.', decrease: 'menos efeito na mistura final.' },
  stereo_width: { musical: 'Abre o campo estéreo das bolhas.', technical: 'Abre o campo estéreo das bolhas.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  attack_pan_spread: { musical: 'Espalha ataques pelo panorama.', technical: 'Espalha ataques pelo panorama.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  sustain_pan_spread: { musical: 'Espalha sustain no estéreo.', technical: 'Espalha sustain no estéreo.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  smart_start_enable: { musical: 'Ativa busca de início suave no spawn.', technical: 'Ativa busca de início suave no spawn.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  smart_start_range: { musical: 'Faixa de busca para zero-cross.', technical: 'Faixa de busca para zero-cross.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  envelope_variation: { musical: 'Mistura variantes discretas de envelope.', technical: 'Mistura variantes discretas de envelope.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  envelope_family: { musical: 'Seleciona família de curvas de envelope.', technical: 'Seleciona família de curvas de envelope.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  wet_drive: { musical: 'Empurra o bus wet na saturação leve.', technical: 'Empurra o bus wet na saturação leve.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  wet_clip_amount: { musical: 'Define quanto soft-clip aplicar.', technical: 'Define quanto soft-clip aplicar.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  wet_output_trim: { musical: 'Compensa nível após clip.', technical: 'Compensa nível após clip.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  final_limiter_ceiling_db: { musical: 'Define o teto final de saída para evitar clipping digital.', technical: 'Ceiling dBFS do limiter peak sem lookahead após dry + wet.', increase: 'saída mais alta, com menos margem.', decrease: 'mais headroom e proteção.' },
  final_limiter_release_ms: { musical: 'Controla quão rápido o limiter volta ao ganho normal.', technical: 'Tempo de release do envelope/gain do limiter final.', increase: 'release mais suave.', decrease: 'recuperação mais rápida.' },
  sustain_diffusion_enable: { musical: 'Liga difusão all-pass no sustain.', technical: 'Liga difusão all-pass no sustain.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  sustain_diffusion_amount: { musical: 'Quanto sustain difuso entra no mix.', technical: 'Quanto sustain difuso entra no mix.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  sustain_diffusion_stages: { musical: 'Número de estágios all-pass.', technical: 'Número de estágios all-pass.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  sustain_diffusion_delay: { musical: 'Delay curto da difusão.', technical: 'Delay curto da difusão.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  sustain_diffusion_feedback: { musical: 'Feedback dos all-pass.', technical: 'Feedback dos all-pass.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  droplet_enable: { musical: 'Permite 2ª geração limitada de grãos.', technical: 'Permite 2ª geração limitada de grãos.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  droplet_probability: { musical: 'Chance de gerar um filho no ataque.', technical: 'Chance de gerar um filho no ataque.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  droplet_gain: { musical: 'Ganho do filho.', technical: 'Ganho do filho.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  droplet_length_scale: { musical: 'Escala de duração do filho.', technical: 'Escala de duração do filho.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  memory_mix: { musical: 'Peso base de memory na seleção.', technical: 'Peso base de memory na seleção.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  memory_pull: { musical: 'Puxa sustain/decay para memory.', technical: 'Puxa sustain/decay para memory.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  memory_darkening: { musical: 'Escurece material vindo de memory.', technical: 'Escurece material vindo de memory.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  tone_variation: { musical: 'Ativa perfis tímbricos discretos.', technical: 'Ativa perfis tímbricos discretos.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  attack_brightness: { musical: 'Brilho dos ataques.', technical: 'Brilho dos ataques.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  sustain_darkness: { musical: 'Escurecimento de sustain.', technical: 'Escurecimento de sustain.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  attack_rate_jitter: { musical: 'Ativa micro-jitter no ataque.', technical: 'Ativa micro-jitter no ataque.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  attack_rate_jitter_depth: { musical: 'Profundidade do micro-jitter.', technical: 'Profundidade do micro-jitter.', increase: 'intensifica o efeito.', decrease: 'suaviza o efeito.' },
  motion_rate: { musical: 'Velocidade dos LFOs lentos do macro Motion.', technical: 'Taxa normalizada dos osciladores de fase acumulada em control-rate.', increase: 'movimento mais rápido.', decrease: 'movimento mais lento.' },
  motion_depth: { musical: 'Profundidade global da modulação Motion.', technical: 'Escala bipolar aplicada aos LFOs de densidade, panorama, memory, sparkle, reverse e diffusion.', increase: 'mais variação automática.', decrease: 'menos variação automática.' },
  motion_shape: { musical: 'Formato dos LFOs de Motion.', technical: '0=triangular, 1=suave, 2=sample-and-hold determinístico.', increase: 'altera o contorno para movimentos mais discretos.', decrease: 'retorna a contornos mais contínuos.' },
};

const PARAMETER_GROUPS = [
  { name: 'Stereo', params: ['stereo_width', 'attack_pan_spread', 'sustain_pan_spread'] },
  { name: 'Texture', params: ['smart_start_enable', 'smart_start_range', 'envelope_variation', 'envelope_family', 'droplet_enable', 'droplet_probability', 'droplet_gain', 'droplet_length_scale', 'attack_rate_jitter', 'attack_rate_jitter_depth'] },
  { name: 'Motion', params: ['motion_rate', 'motion_depth', 'motion_shape'] },
  { name: 'Diffusion', params: ['wet_drive', 'wet_clip_amount', 'wet_output_trim', 'final_limiter_ceiling_db', 'final_limiter_release_ms', 'sustain_diffusion_enable', 'sustain_diffusion_amount', 'sustain_diffusion_stages', 'sustain_diffusion_delay', 'sustain_diffusion_feedback'] },
  { name: 'Tone', params: ['tone_variation', 'attack_brightness', 'sustain_darkness'] },
  { name: 'Memory', params: ['memory_mix', 'memory_pull', 'memory_darkening', 'attack_region_min_offset_samples', 'attack_region_max_offset_samples', 'body_region_min_offset_samples', 'body_region_max_offset_samples', 'memory_region_min_offset_samples', 'memory_region_max_offset_samples'] },
  { name: 'Dynamics', params: ['noise_floor', 'tracking_thresh', 'sustain_thresh', 'transient_delta', 'duck_burst_level', 'duck_attack_coef', 'duck_release_coef', 'burst_duration_ticks', 'burst_immediate_count', 'density_burst', 'density_sustain', 'density_decay'] },
  { name: 'Advanced', params: ['micro_duration_ms_min', 'micro_duration_ms_max', 'short_duration_ms_min', 'short_duration_ms_max', 'body_duration_ms_min', 'body_duration_ms_max', 'rng_seed', 'mix_dry_gain', 'mix_wet_gain'] },
];

const WASM_PARAM_ID_MAP = Object.freeze({
  noise_floor: 0,
  tracking_thresh: 1,
  sustain_thresh: 2,
  transient_delta: 3,
  duck_burst_level: 4,
  duck_attack_coef: 5,
  duck_release_coef: 6,
  burst_duration_ticks: 7,
  burst_immediate_count: 8,
  density_burst: 9,
  density_sustain: 10,
  density_decay: 11,
  attack_region_min_offset_samples: 12,
  attack_region_max_offset_samples: 13,
  body_region_min_offset_samples: 14,
  body_region_max_offset_samples: 15,
  memory_region_min_offset_samples: 16,
  memory_region_max_offset_samples: 17,
  micro_duration_ms_min: 18,
  micro_duration_ms_max: 19,
  short_duration_ms_min: 20,
  short_duration_ms_max: 21,
  body_duration_ms_min: 22,
  body_duration_ms_max: 23,
  rng_seed: 24,
  mix_dry_gain: 25,
  mix_wet_gain: 26,
  stereo_width: 27,
  attack_pan_spread: 28,
  sustain_pan_spread: 29,
  smart_start_enable: 30,
  smart_start_range: 31,
  envelope_variation: 32,
  envelope_family: 33,
  wet_drive: 34,
  wet_clip_amount: 35,
  wet_output_trim: 36,
  sustain_diffusion_enable: 37,
  sustain_diffusion_amount: 38,
  sustain_diffusion_stages: 39,
  sustain_diffusion_delay: 40,
  sustain_diffusion_feedback: 41,
  droplet_enable: 42,
  droplet_probability: 43,
  droplet_gain: 44,
  droplet_length_scale: 45,
  memory_mix: 46,
  memory_pull: 47,
  memory_darkening: 48,
  tone_variation: 49,
  attack_brightness: 50,
  sustain_darkness: 51,
  attack_rate_jitter: 52,
  attack_rate_jitter_depth: 53,
  freeze_amount: 56,
  freeze_enabled: 57,
  reverse_probability: 58,
  pitch_mode: 59,
  shimmer_amount: 60,
  final_limiter_ceiling_db: 61,
  final_limiter_release_ms: 62,
  motion_rate: 63,
  motion_depth: 64,
  motion_shape: 65,
});

const WASM_MACRO_PARAM_ID_MAP = Object.freeze({
  density: 2000,
  bloom: 2001,
  motion: 2002,
  texture: 2003,
  space: 2004,
  gravity: 2005,
  memory: 2006,
  clarity: 2007,
  freeze: 2008,
  sparkle: 2009,
  warmth: 2010,
  mix: 2011,
});

const WASM_DEVELOPER_MODE_PARAM_ID = 2999;

function clamp(value, min, max) {
  if (Number.isNaN(value)) return min;
  return Math.min(max, Math.max(min, value));
}

function pickUnknownParams(params, definitions) {
  const unknown = {};
  Object.keys(params || {}).forEach((key) => {
    if (!definitions[key]) unknown[key] = params[key];
  });
  return unknown;
}

document.addEventListener('alpine:init', () => {
  Alpine.data('editorApp', () => ({
    baseParams: { ...BASELINE },
    params: { ...BASELINE },
    resolvedParams: { ...BASELINE },
    macroValues: window.BubbleCloudMacroLayer.createNeutralMacroValues(),
    macroPipeline: window.BubbleCloudMacroLayer.PIPELINE_ORDER,
    macroDestinations: window.BubbleCloudMacroLayer.MACRO_DESTINATIONS,
    macroPerfHint: 1.0,
    macroLabels: {
      density: 'Density',
      bloom: 'Bloom',
      motion: 'Motion',
      texture: 'Texture',
      space: 'Space',
      gravity: 'Gravity',
      memory: 'Memory',
      clarity: 'Clarity',
      freeze: 'Freeze',
      sparkle: 'Sparkle',
      warmth: 'Warmth',
      mix: 'Mix',
    },
    macroPrimaryPage: ['density', 'bloom', 'texture', 'motion', 'space', 'mix'],
    macroSecondaryPage: ['gravity', 'memory', 'clarity', 'freeze', 'sparkle', 'warmth'],
    uiModes: UI_MODES,
    uiMode: UI_MODES.SIMPLE,
    activeMacroPage: 0,
    shiftMode: false,
    compareMode: false,
    abSlots: {
      A: { label: 'A', baseParams: { ...BASELINE }, macroValues: window.BubbleCloudMacroLayer.createNeutralMacroValues() },
      B: { label: 'B', baseParams: { ...BASELINE }, macroValues: window.BubbleCloudMacroLayer.createNeutralMacroValues() },
    },
    activeABSlot: 'A',
    undoStack: [],
    redoStack: [],
    historyTransactionBefore: null,
    showAdvancedRawRange: false,
    developerMode: false,
    baseline: { ...BASELINE },
    factoryPresets: [],
    selectedPresetCategory: 'All',
    loadedPresetReference: { preset_name: 'Baseline', preset_slug: 'baseline', params: { ...BASELINE } },
    currentPresetSource: 'factory',
    currentPresetInfo: {},
    presetImportInfo: null,
    parameterGroups: PARAMETER_GROUPS,
    paramDefinitions: PARAM_DEFINITIONS,
    paramHelp: PARAM_HELP,

    openAccordion: 'Advanced',
    openHelp: {},

    toasts: [],
    showResetModal: false,
    isDraft: false,
    hasUnexportedChanges: false,
    parityNote: 'Paridade sonora: mesma engine DSP para modo synth e modo file.',
    qualityProfiles: QUALITY_PROFILES,
    selectedQualityProfile: 2,

    audioInitialized: false,
    audioContext: null,
    workletNode: null,
    sourceBus: null,
    synthOscillator: null,
    synthGain: null,
    workletModuleLoaded: false,
    workletReady: false,

    audioStatusMsg: 'Não iniciado',
    audioError: false,

    previewMode: 'file',
    repeatContinuous: false,
    isPlaying: false,
    dragOver: false,

    audioFile: null,
    audioBuffer: null,
    isDecoding: false,
    isExportingMp3: false,
    mp3Worker: null,

    fileSourceNode: null,
    startedAt: 0,
    pausedAt: 0,
    currentTime: 0,
    seekTime: 0,
    duration: 0,

    metrics: { envelope: 0, state: 0, voices: 0, voiceLimit: 24, peakL: 0, peakR: 0, clipCount: 0, limiterGain: 1, cpuLoad: 0 },
    telemetry: {
      targetDensity: { value: 0, ratio: 0, available: false },
      wetDuckAmount: { value: 0, ratio: 0, available: false },
      schedulerPressure: { value: 0, ratio: 0, available: false },
      clipping: { value: 0, ratio: 0, available: false },
    },
    recentStateTransition: 'N/A → N/A',
    previousEngineState: null,
    telemetryUiUpdateQueued: false,
    lastMacroAffectedText: '',
    pollInterval: null,
    transportTimer: null,

    saveQueued: false,
    draftPersistTimer: null,
    draftPersistDelayMs: 180,

    init() {
      this.params = this.baseParams;
      this.factoryPresets = (window.BubbleCloudFactoryPresets?.createFactoryPresets?.(this.baseline) || []).map((preset) =>
        window.BubbleCloudPresetSchema.createCanonicalPreset(preset)
      );
      if (this.factoryPresets.length > 0) {
        this.loadedPresetReference = window.BubbleCloudPresetSchema.createCanonicalPreset(this.factoryPresets[0]);
        this.currentPresetInfo = { ...this.loadedPresetReference };
      }
      this.restoreDraft();
      this.validateParamRanges();
      this.pushAllParamsToAudio();
    },

    get presetCategories() {
      const categories = new Set(this.factoryPresets.map((preset) => preset.ui_category));
      return ['All', ...categories];
    },

    get filteredFactoryPresets() {
      if (this.selectedPresetCategory === 'All') return this.factoryPresets;
      return this.factoryPresets.filter((preset) => preset.ui_category === this.selectedPresetCategory);
    },

    setAudioStatus(message, isError = false) {
      this.audioStatusMsg = message;
      this.audioError = !!isError;
      if (isError) console.error('[audio]', message);
      else console.log('[audio]', message);
    },

    toast(message, type = 'info') {
      const id = `${Date.now()}-${Math.random().toString(16).slice(2)}`;
      this.toasts.push({ id, message, type });
      setTimeout(() => {
        this.toasts = this.toasts.filter((t) => t.id !== id);
      }, 2800);
    },

    toggleAccordion(name) {
      this.openAccordion = this.openAccordion === name ? '' : name;
    },

    get allMacroKeys() {
      return this.macroPipeline.filter((key) => this.macroLabels[key]);
    },

    get visibleParameterGroups() {
      return this.uiMode === UI_MODES.DEVELOPER ? this.parameterGroups : [];
    },

    get currentUiModeLabel() {
      if (this.uiMode === UI_MODES.DEVELOPER) return 'Developer';
      if (this.uiMode === UI_MODES.ADVANCED) return 'Advanced';
      return 'Simple';
    },

    setUiMode(mode) {
      if (!Object.values(UI_MODES).includes(mode) || this.uiMode === mode) return;
      this.recordHistory('UI mode');
      this.uiMode = mode;
      this.developerMode = mode === UI_MODES.DEVELOPER;
      if (this.developerMode && !this.openAccordion) this.openAccordion = this.parameterGroups[0]?.name || 'Advanced';
      this.saveDraft();
      this.toast(`Modo ${this.currentUiModeLabel} ativo.`, 'info');
    },

    get displayedMacroKeys() {
      if (this.uiMode === UI_MODES.SIMPLE) return this.macroPrimaryPage;
      return this.allMacroKeys;
    },

    get macroPageTitle() {
      if (this.uiMode === UI_MODES.SIMPLE) return 'Simple Musical Macros';
      if (this.uiMode === UI_MODES.ADVANCED) return 'Advanced Musical Macros';
      return 'Developer: Musical Macros + Internal Groups';
    },

    get macroSliderStep() {
      return this.shiftMode ? 0.005 : 0.01;
    },

    cycleMacroPage() {
      if (this.uiMode === UI_MODES.SIMPLE) {
        this.setUiMode(UI_MODES.ADVANCED);
        return;
      }
      this.setUiMode(UI_MODES.SIMPLE);
    },

    toggleShiftMode() {
      this.shiftMode = !this.shiftMode;
    },

    toggleCompareMode() {
      this.compareMode = !this.compareMode;
      this.queueParamFlush();
      this.toast(this.compareMode ? 'Compare ativo: ouvindo Base Params.' : 'Compare desligado: ouvindo Resolved Params.', 'info');
    },

    toggleHelp(paramKey) {
      this.openHelp[paramKey] = !this.openHelp[paramKey];
    },

    getParamConfig(paramKey) {
      const cfg = this.paramDefinitions[paramKey] || { min: 0, max: 1, step: 0.01 };
      const shouldUseRaw = this.showAdvancedRawRange && this.isAdvancedParam(paramKey);
      if (shouldUseRaw) return cfg;
      const perf = window.BubbleCloudMacroLayer.resolvePerformanceRange(paramKey, cfg);
      if (!perf || perf.isRawFallback) return cfg;
      return { ...cfg, min: perf.min, max: perf.max };
    },

    isAdvancedParam(paramKey) {
      const advancedGroup = this.parameterGroups.find((group) => group.name === 'Advanced');
      return Array.isArray(advancedGroup?.params) && advancedGroup.params.includes(paramKey);
    },

    formatLabel(paramKey) {
      return this.paramDefinitions[paramKey]?.label || paramKey.replaceAll('_', ' ');
    },

    getDirectionalTooltip(paramKey) {
      const help = this.paramHelp[paramKey];
      if (!help) return '';
      return `Valor maior: ${help.increase} | Valor menor: ${help.decrease}`;
    },

    validateMacroRanges() {
      Object.keys(this.macroValues).forEach((key) => {
        this.macroValues[key] = clamp(Number(this.macroValues[key]), 0, 1);
      });
    },

    updateResolvedParams() {
      this.validateMacroRanges();
      this.resolvedParams = window.BubbleCloudMacroLayer.applyMacroPipeline(
        this.baseParams,
        this.macroValues,
        this.paramDefinitions,
        this.macroPerfHint
      );
    },

    validateParamRanges() {
      Object.keys(this.paramDefinitions).forEach((key) => {
        if (key in this.baseParams) {
          const cfg = this.paramDefinitions[key];
          this.baseParams[key] = clamp(Number(this.baseParams[key]), cfg.min, cfg.max);
        }
      });
      this.params = this.baseParams;
      this.updateResolvedParams();
    },

    persistDraftNow() {
      this.validateParamRanges();
      localStorage.setItem(
        STORAGE_KEY,
        JSON.stringify({
          params: this.resolvedParams,
          base_params: this.baseParams,
          macro_values: this.macroValues,
          developer_mode: this.developerMode,
          ui_mode: this.uiMode,
          ab_slots: this.abSlots,
          savedAt: Date.now(),
        })
      );
    },

    scheduleDraftPersist() {
      if (this.draftPersistTimer) {
        clearTimeout(this.draftPersistTimer);
      }
      this.draftPersistTimer = setTimeout(() => {
        this.draftPersistTimer = null;
        this.persistDraftNow();
      }, this.draftPersistDelayMs);
    },

    saveDraft() {
      this.validateParamRanges();
      this.scheduleDraftPersist();
      this.isDraft = true;
      this.hasUnexportedChanges = true;
      this.queueParamFlush();
    },

    createHistorySnapshot() {
      return {
        baseParams: { ...this.baseParams },
        macroValues: { ...this.macroValues },
        uiMode: this.uiMode,
        developerMode: this.developerMode,
        abSlots: JSON.parse(JSON.stringify(this.abSlots)),
      };
    },

    restoreHistorySnapshot(snapshot) {
      if (!snapshot) return;
      this.baseParams = { ...this.baseParams, ...(snapshot.baseParams || {}) };
      this.params = this.baseParams;
      this.macroValues = window.BubbleCloudMacroLayer.normalizeMacroValues(snapshot.macroValues);
      this.uiMode = Object.values(UI_MODES).includes(snapshot.uiMode) ? snapshot.uiMode : UI_MODES.SIMPLE;
      this.developerMode = this.uiMode === UI_MODES.DEVELOPER;
      if (snapshot.abSlots) this.abSlots = JSON.parse(JSON.stringify(snapshot.abSlots));
      this.validateParamRanges();
      this.scheduleDraftPersist();
      this.queueParamFlush();
      this.isDraft = true;
      this.hasUnexportedChanges = true;
    },

    recordHistory(label = 'Parameter change') {
      this.undoStack.push({ label, snapshot: this.createHistorySnapshot() });
      if (this.undoStack.length > HISTORY_LIMIT) this.undoStack.shift();
      this.redoStack = [];
    },

    beginHistoryTransaction(label = 'Parameter change') {
      if (this.historyTransactionBefore) return;
      this.historyTransactionBefore = { label, snapshot: this.createHistorySnapshot() };
    },

    commitHistoryTransaction() {
      if (!this.historyTransactionBefore) return;
      const before = this.historyTransactionBefore;
      this.historyTransactionBefore = null;
      const after = JSON.stringify(this.createHistorySnapshot());
      if (JSON.stringify(before.snapshot) === after) return;
      this.undoStack.push(before);
      if (this.undoStack.length > HISTORY_LIMIT) this.undoStack.shift();
      this.redoStack = [];
    },

    undoParameterChange() {
      const entry = this.undoStack.pop();
      if (!entry) return;
      this.redoStack.push({ label: 'Redo', snapshot: this.createHistorySnapshot() });
      this.restoreHistorySnapshot(entry.snapshot);
      this.toast(`Undo: ${entry.label}`, 'info');
    },

    redoParameterChange() {
      const entry = this.redoStack.pop();
      if (!entry) return;
      this.undoStack.push({ label: 'Undo', snapshot: this.createHistorySnapshot() });
      this.restoreHistorySnapshot(entry.snapshot);
      this.toast('Redo aplicado.', 'info');
    },

    onMacroInput(macroKey) {
      this.beginHistoryTransaction(`Macro ${this.macroLabels[macroKey] || macroKey}`);
      const affected = (this.macroDestinations?.[macroKey] || [])
        .map((target) => this.formatLabel(target.param))
        .filter(Boolean);
      this.lastMacroAffectedText = affected.length > 0 ? `Affected: ${affected.join(', ')}` : 'Affected: N/A';
      this.saveDraft();
    },

    onParamInput(paramKey) {
      this.beginHistoryTransaction(`Parameter ${this.formatLabel(paramKey)}`);
      this.saveDraft();
    },

    randomizeMusical() {
      this.recordHistory('Randomize musical');
      this.allMacroKeys.forEach((key) => {
        const [min, max] = MUSICAL_RANDOMIZE_RANGES[key] || [0.25, 0.75];
        const value = min + Math.random() * (max - min);
        this.macroValues[key] = Number(value.toFixed(3));
      });
      this.macroValues.mix = Math.min(this.macroValues.mix, 0.72);
      this.validateMacroRanges();
      this.lastMacroAffectedText = 'Randomize musical: ranges seguros aplicados aos 12 macros.';
      this.saveDraft();
      this.toast('Randomize musical aplicado com ranges seguros.', 'success');
    },

    captureABSlot(slot) {
      const normalizedSlot = slot === 'B' ? 'B' : 'A';
      this.abSlots[normalizedSlot] = {
        label: normalizedSlot,
        baseParams: { ...this.baseParams },
        macroValues: { ...this.macroValues },
      };
      this.activeABSlot = normalizedSlot;
      this.scheduleDraftPersist();
      this.toast(`Slot ${normalizedSlot} capturado.`, 'success');
    },

    recallABSlot(slot) {
      const normalizedSlot = slot === 'B' ? 'B' : 'A';
      const stored = this.abSlots[normalizedSlot];
      if (!stored) return;
      this.recordHistory(`Recall ${normalizedSlot}`);
      this.activeABSlot = normalizedSlot;
      this.baseParams = { ...this.baseParams, ...(stored.baseParams || {}) };
      this.params = this.baseParams;
      this.macroValues = window.BubbleCloudMacroLayer.normalizeMacroValues(stored.macroValues);
      this.validateParamRanges();
      this.saveDraft();
      this.toast(`Comparação A/B: ouvindo slot ${normalizedSlot}.`, 'info');
    },

    swapABSlots() {
      this.recordHistory('Swap A/B');
      const previousA = this.abSlots.A;
      this.abSlots.A = this.abSlots.B;
      this.abSlots.B = previousA;
      this.scheduleDraftPersist();
      this.toast('Slots A/B trocados.', 'info');
    },

    resetMacros() {
      this.recordHistory('Reset macros');
      this.macroValues = window.BubbleCloudMacroLayer.createNeutralMacroValues();
      this.saveDraft();
      this.toast('Macros resetados para neutro.', 'success');
    },

    commitResolvedToBase() {
      this.recordHistory('Commit resolved to base');
      this.validateParamRanges();
      this.baseParams = { ...this.resolvedParams };
      this.params = this.baseParams;
      this.macroValues = window.BubbleCloudMacroLayer.createNeutralMacroValues();
      this.validateParamRanges();
      this.scheduleDraftPersist();
      this.queueParamFlush();
      this.hasUnexportedChanges = true;
      this.isDraft = true;
      this.toast('Resolved Params fundidos em Base Params.', 'success');
    },

    restoreDraft() {
      try {
        const raw = localStorage.getItem(STORAGE_KEY);
        if (!raw) return;
        const parsed = JSON.parse(raw);
        if (parsed && typeof parsed === 'object') {
          this.baseParams = { ...this.baseParams, ...(parsed.base_params || parsed.params || {}) };
          this.params = this.baseParams;
          this.macroValues = window.BubbleCloudMacroLayer.normalizeMacroValues(parsed.macro_values);
          this.uiMode = Object.values(UI_MODES).includes(parsed.ui_mode) ? parsed.ui_mode : (parsed.developer_mode ? UI_MODES.DEVELOPER : UI_MODES.SIMPLE);
          this.developerMode = this.uiMode === UI_MODES.DEVELOPER;
          if (parsed.ab_slots && typeof parsed.ab_slots === 'object') {
            this.abSlots = {
              ...this.abSlots,
              ...parsed.ab_slots,
            };
          }
          this.isDraft = true;
          this.hasUnexportedChanges = true;
          this.validateParamRanges();
        }
      } catch (err) {
        this.toast(`Falha ao restaurar rascunho: ${err.message || err}`, 'error');
      }
    },

    clearDraft() {
      if (this.draftPersistTimer) {
        clearTimeout(this.draftPersistTimer);
        this.draftPersistTimer = null;
      }
      localStorage.removeItem(STORAGE_KEY);
      this.isDraft = false;
      this.hasUnexportedChanges = false;
    },

    confirmReset() {
      this.recordHistory('Reset baseline');
      this.baseParams = { ...this.baseline };
      this.params = this.baseParams;
      this.macroValues = window.BubbleCloudMacroLayer.createNeutralMacroValues();
      this.validateParamRanges();
      this.clearDraft();
      this.showResetModal = false;
      this.queueParamFlush();
      if (this.workletNode) {
        this.workletNode.port.postMessage({ type: 'reset-engine' });
      }
      this.toast('Preset restaurado para o baseline.', 'success');
    },

    loadFactoryPreset(slug) {
      this.recordHistory('Load factory preset');
      const preset = this.factoryPresets.find((item) => item.preset_slug === slug);
      if (!preset) return;
      this.baseParams = { ...(preset.base_params || preset.params) };
      this.params = this.baseParams;
      this.macroValues = window.BubbleCloudMacroLayer.normalizeMacroValues(preset.macro_values);
      this.validateParamRanges();
      this.currentPresetSource = 'factory';
      this.currentPresetInfo = { ...preset };
      this.loadedPresetReference = window.BubbleCloudPresetSchema.createCanonicalPreset(preset);
      this.queueParamFlush();
      this.scheduleDraftPersist();
      this.isDraft = true;
      this.hasUnexportedChanges = true;
      this.toast('Preset importado com sucesso', 'success');
      if (!preset.esp32_safe) this.toast('Preset com custo maior: verifique uso em ESP32.', 'info');
    },

    resetToLoadedPresetDefaults() {
      this.recordHistory('Reset loaded preset defaults');
      this.baseParams = { ...(this.loadedPresetReference.base_params || this.loadedPresetReference.params) };
      this.params = this.baseParams;
      this.macroValues = window.BubbleCloudMacroLayer.normalizeMacroValues(this.loadedPresetReference.macro_values);
      this.validateParamRanges();
      this.queueParamFlush();
      this.scheduleDraftPersist();
      this.hasUnexportedChanges = true;
      this.toast('Preset restaurado para os defaults carregados.', 'success');
    },

    serializeCurrentPreset() {
      const info = this.currentPresetInfo || {};
      const payload = window.BubbleCloudPresetSchema.createCanonicalPreset({
        ...info,
        preset_name: info.preset_name || 'Current Preset',
        preset_slug: info.preset_slug || window.BubbleCloudPresetSchema.slugify(info.preset_name || 'current-preset'),
        params: { ...this.resolvedParams },
        base_params: { ...this.baseParams },
        macro_values: { ...this.macroValues },
        metadata: {
          ...(info.metadata || {}),
          source: this.currentPresetSource,
          export_context: 'preset',
          unknown_params: this.presetImportInfo?.unknownParams || {},
        },
      });
      return this.normalizeAndValidatePreset(payload).normalized;
    },

    serializeCurrentStateSnapshot() {
      return {
        app: 'bubble_cloud_web',
        exported_at: new Date().toISOString(),
        mode: this.previewMode,
        transport: {
          is_playing: this.isPlaying,
          current_time: this.currentTime,
          duration: this.duration,
          repeat_continuous: this.repeatContinuous,
        },
        preset: this.serializeCurrentPreset(),
      };
    },

    normalizeAndValidatePreset(canonicalPreset) {
      const { normalized, warnings: normalizeWarnings } = window.BubbleCloudPresetValidation.normalizePreset(
        canonicalPreset,
        this.paramDefinitions,
        this.baseline
      );
      const { errors, warnings: validateWarnings } = window.BubbleCloudPresetValidation.validatePreset(
        normalized,
        this.paramDefinitions
      );
      return { normalized, errors, warnings: [...normalizeWarnings, ...validateWarnings] };
    },

    async copyJson() {
      const payload = JSON.stringify(this.serializeCurrentPreset(), null, 2);
      try {
        await navigator.clipboard.writeText(payload);
        this.hasUnexportedChanges = false;
        this.toast('Preset copiado para a área de transferência.', 'success');
      } catch (err) {
        this.toast(`Falha ao copiar JSON: ${err.message || err}`, 'error');
      }
    },

    downloadPresetJson() {
      try {
        const payload = this.serializeCurrentPreset();
        window.BubbleCloudPresetIO.downloadObjectAsJson(
          payload,
          `bubble-cloud-preset-${new Date().toISOString().replace(/[\W:]/g, '-')}.json`
        );
        this.hasUnexportedChanges = false;
        this.toast('Preset exportado com sucesso.', 'success');
      } catch (err) {
        this.toast(`Falha ao baixar JSON: ${err.message || err}`, 'error');
      }
    },

    downloadSnapshotJson() {
      try {
        const snapshot = this.serializeCurrentStateSnapshot();
        window.BubbleCloudPresetIO.downloadObjectAsJson(
          snapshot,
          `bubble-cloud-session-${new Date().toISOString().replace(/[\W:]/g, '-')}.json`
        );
        this.toast('Snapshot de sessão exportado com sucesso.', 'success');
      } catch (err) {
        this.toast(`Falha ao exportar snapshot: ${err.message || err}`, 'error');
      }
    },

    async handlePresetImport(event) {
      const file = event?.target?.files?.[0];
      if (!file) return;
      try {
        const raw = await window.BubbleCloudPresetIO.readJsonFile(file);
        const topLevelKnown = new Set([
          'schema_version',
          'preset_name',
          'preset_slug',
          'engine_version',
          'created_at',
          'ui_category',
          'tags',
          'esp32_safe',
          'quality_tier',
          'description',
          'params',
          'base_params',
          'macro_values',
          'metadata',
          'preset',
        ]);
        const schemaVersion = Number(raw?.schema_version) || 0;
        let candidate = raw?.preset ? raw.preset : raw;
        let legacyMigrated = false;
        if (!schemaVersion || !candidate.schema_version) {
          candidate = window.BubbleCloudPresetMigration.migrateLegacyToCanonical(
            candidate,
            window.BubbleCloudPresetSchema.SCHEMA_VERSION,
            this.baseline,
            window.BubbleCloudPresetSchema.slugify
          );
          legacyMigrated = true;
        } else {
          candidate = window.BubbleCloudPresetSchema.createCanonicalPreset(candidate);
        }

        const unknownParams = pickUnknownParams(candidate.params, this.paramDefinitions);
        const unknownTopLevel = Object.keys(raw || {}).reduce((acc, key) => {
          if (!topLevelKnown.has(key)) acc[key] = raw[key];
          return acc;
        }, {});
        if (Object.keys(unknownParams).length > 0) {
          candidate.metadata = {
            ...(candidate.metadata || {}),
            extra: {
              ...((candidate.metadata || {}).extra || {}),
              unknown_params: unknownParams,
            },
          };
        }
        if (Object.keys(unknownTopLevel).length > 0) {
          candidate.metadata = {
            ...(candidate.metadata || {}),
            extra: {
              ...((candidate.metadata || {}).extra || {}),
              unknown_top_level_fields: unknownTopLevel,
            },
          };
        }

        const { normalized, errors, warnings } = this.normalizeAndValidatePreset(candidate);
        if (errors.length > 0) {
          this.toast('Arquivo inválido', 'error');
          this.toast(errors[0], 'error');
          return;
        }

        this.baseParams = { ...(normalized.base_params || normalized.params) };
        this.params = this.baseParams;
        this.macroValues = window.BubbleCloudMacroLayer.normalizeMacroValues(normalized.macro_values);
        this.validateParamRanges();
        this.loadedPresetReference = window.BubbleCloudPresetSchema.createCanonicalPreset(normalized);
        this.currentPresetInfo = { ...normalized };
        this.currentPresetSource = 'imported';
        this.presetImportInfo = { unknownParams };
        this.queueParamFlush();
        this.scheduleDraftPersist();
        this.hasUnexportedChanges = true;
        this.isDraft = true;
        this.toast('Preset importado com sucesso', 'success');
        if (legacyMigrated) this.toast('Preset legado convertido automaticamente', 'info');
        if (Object.keys(unknownParams).length > 0 || Object.keys(unknownTopLevel).length > 0) this.toast('Alguns campos desconhecidos foram preservados', 'info');
        if (warnings.length > 0) this.toast('Valores fora da faixa foram ajustados', 'info');
      } catch (err) {
        this.toast('Arquivo inválido', 'error');
        this.toast(`Falha na importação: ${err.message || err}`, 'error');
      } finally {
        event.target.value = '';
      }
    },

    async initAudio() {
      try {
        this.setAudioStatus('Inicializando contexto de áudio...');
        if (!this.audioContext) {
          this.audioContext = new AudioContext();
        }

        if (this.audioContext.state === 'suspended') {
          await this.audioContext.resume();
        }

        if (!this.workletModuleLoaded) {
          this.setAudioStatus('Carregando AudioWorklet...');
          await this.audioContext.audioWorklet.addModule(new URL('worklet.js', window.location.href).toString());
          this.workletModuleLoaded = true;
        }

        if (this.workletNode) {
          this.workletNode.disconnect();
        }

        this.workletNode = new AudioWorkletNode(this.audioContext, 'sound-bubbles-worklet', { outputChannelCount: [2] });
        this.sourceBus = new GainNode(this.audioContext, { gain: 1 });
        this.sourceBus.connect(this.workletNode);
        this.workletNode.connect(this.audioContext.destination);

        this.workletNode.onprocessorerror = (event) => {
          const message = event?.message || 'processor error';
          this.setAudioStatus(`Erro no AudioWorklet: ${message}`, true);
          this.toast(this.audioStatusMsg, 'error');
        };

        this.workletNode.port.onmessage = (event) => {
          const data = event.data || {};
          if (data.type === 'loading') {
            this.setAudioStatus('Inicializando WASM...');
          } else if (data.type === 'ready') {
            this.audioInitialized = true;
            this.workletReady = true;
            this.setAudioStatus('Áudio pronto');
            this.pushQualityProfileToAudio();
            this.pushAllParamsToAudio();
            this.startMetricsPolling();
            this.toast('Engine inicializado com sucesso.', 'success');
          } else if (data.type === 'state') {
            this.metrics.envelope = Number(data.envelope) || 0;
            this.metrics.state = Number(data.state) || 0;
            this.metrics.voices = Number(data.voices) || 0;
            this.metrics.voiceLimit = Number(data.voiceLimit) || this.activeVoiceLimit;
            this.metrics.peakL = Number(data.peakL) || 0;
            this.metrics.peakR = Number(data.peakR) || 0;
            this.metrics.clipCount = Number(data.clipCount) || 0;
            this.metrics.limiterGain = Number(data.limiterGain) || 1;
            this.metrics.cpuLoad = Number(data.cpuLoad) || 0;
            this.scheduleTelemetryUiUpdate();
          } else if (data.type === 'init-failed' || data.type === 'wasm-error' || data.type === 'processor-error') {
            this.workletReady = false;
            this.setAudioStatus(data.message || 'Falha na inicialização do WASM.', true);
            this.toast(this.audioStatusMsg, 'error');
          }
        };
      } catch (err) {
        this.setAudioStatus(`Falha ao iniciar áudio: ${err.message || err}`, true);
        this.toast(this.audioStatusMsg, 'error');
      }
    },

    get activeQualityProfile() {
      return QUALITY_PROFILES.find((profile) => profile.id === Number(this.selectedQualityProfile)) || QUALITY_PROFILES[2];
    },

    get activeVoiceLimit() {
      return Number(this.metrics.voiceLimit) || this.activeQualityProfile.voices;
    },

    setQualityProfile(profileId) {
      this.selectedQualityProfile = Number(profileId);
      this.metrics.voiceLimit = this.activeQualityProfile.voices;
      this.pushQualityProfileToAudio();
      this.scheduleTelemetryUiUpdate();
      this.toast(`Perfil de qualidade: ${this.activeQualityProfile.label}`, 'info');
    },

    pushQualityProfileToAudio() {
      if (!this.workletReady || !this.workletNode) return;
      this.workletNode.port.postMessage({ type: 'quality-profile', profile: Number(this.selectedQualityProfile) });
    },

    pushParamToAudio(key) {
      if (!this.workletReady || !this.workletNode || !this.developerMode) return;
      const id = WASM_PARAM_ID_MAP[key];
      if (id === undefined) return;
      const activeParams = this.compareMode ? this.baseParams : this.resolvedParams;
      this.workletNode.port.postMessage({ type: 'param', id, value: Number(activeParams[key]) });
    },

    pushMacroToAudio(key) {
      if (!this.workletReady || !this.workletNode) return;
      const id = WASM_MACRO_PARAM_ID_MAP[key];
      if (id === undefined) return;
      const macroValues = this.compareMode ? window.BubbleCloudMacroLayer.createNeutralMacroValues() : this.macroValues;
      this.workletNode.port.postMessage({ type: 'param', id, value: Number(macroValues[key]) });
    },

    pushAllParamsToAudio() {
      if (!this.workletReady || !this.workletNode) return;
      this.workletNode.port.postMessage({ type: 'param', id: WASM_DEVELOPER_MODE_PARAM_ID, value: this.developerMode ? 1 : 0 });
      Object.keys(this.macroValues).forEach((key) => this.pushMacroToAudio(key));
      if (this.developerMode) {
        Object.keys(this.resolvedParams).forEach((key) => this.pushParamToAudio(key));
      }
    },

    queueParamFlush() {
      if (this.saveQueued) return;
      this.saveQueued = true;
      requestAnimationFrame(() => {
        this.saveQueued = false;
        this.pushAllParamsToAudio();
      });
    },

    startMetricsPolling() {
      clearInterval(this.pollInterval);
      this.pollInterval = setInterval(() => {
        if (this.workletNode && this.workletReady) {
          this.workletNode.port.postMessage({ type: 'poll' });
        }
      }, 100);
    },

    scheduleTelemetryUiUpdate() {
      if (this.telemetryUiUpdateQueued) return;
      this.telemetryUiUpdateQueued = true;
      requestAnimationFrame(() => {
        this.telemetryUiUpdateQueued = false;
        this.updateTelemetryIndicators();
      });
    },

    updateTelemetryIndicators() {
      const currentState = Number(this.metrics.state);
      if (Number.isFinite(currentState) && currentState !== this.previousEngineState) {
        const previousLabel = this.previousEngineState === null ? 'N/A' : this.getEngineStateName(this.previousEngineState);
        this.recentStateTransition = `${previousLabel} → ${this.getEngineStateName(currentState)}`;
        this.previousEngineState = currentState;
      }

      const targetDensityEstimate = this.estimateTargetDensity(currentState, Number(this.metrics.envelope));
      const wetDuckEstimate = this.estimateWetDuckAmount(currentState, Number(this.metrics.envelope));
      const schedulerPressure = this.estimateSchedulerPressure(Number(this.metrics.voices), targetDensityEstimate);
      const clipEvents = Number(this.metrics.clipCount) || 0;

      this.telemetry.targetDensity = this.createSmoothedIndicator(this.telemetry.targetDensity, targetDensityEstimate, 160);
      this.telemetry.wetDuckAmount = this.createSmoothedIndicator(this.telemetry.wetDuckAmount, wetDuckEstimate, 1);
      this.telemetry.schedulerPressure = this.createSmoothedIndicator(this.telemetry.schedulerPressure, schedulerPressure, 1);
      this.telemetry.clipping = this.createSmoothedIndicator(this.telemetry.clipping, clipEvents, 8);
    },

    estimateTargetDensity(state, envelope) {
      if (!Number.isFinite(state) || !Number.isFinite(envelope)) return null;
      const env = clamp(envelope, 0, 1);
      if (state === 3) {
        const min = Number(this.resolvedParams.density_sustain) || 0;
        const max = Number(this.resolvedParams.density_burst) || 0;
        return min + (max - min) * env;
      }
      if (state === 2) return Number(this.resolvedParams.density_sustain);
      if (state === 1) return Number(this.resolvedParams.density_decay);
      return 0;
    },

    estimateWetDuckAmount(state, envelope) {
      if (!Number.isFinite(state) || !Number.isFinite(envelope)) return null;
      if (state === 0) return 0;
      const env = clamp(envelope, 0, 1);
      const maxDuck = 1 - clamp(Number(this.resolvedParams.duck_burst_level), 0, 1);
      if (state === 3) return maxDuck;
      if (state === 2 || state === 1) return maxDuck * env * 0.5;
      return 0;
    },

    estimateSchedulerPressure(voices, targetDensityEstimate) {
      if (!Number.isFinite(voices) || !Number.isFinite(targetDensityEstimate)) return null;
      const voiceRatio = clamp(voices / this.activeVoiceLimit, 0, 1);
      const densityCap = Math.max(
        Number(this.resolvedParams.density_burst) || 0,
        Number(this.resolvedParams.density_sustain) || 0,
        1
      );
      const densityRatio = clamp(targetDensityEstimate / densityCap, 0, 1);
      return clamp(voiceRatio * 0.7 + densityRatio * 0.3, 0, 1);
    },

    createSmoothedIndicator(previous, nextValue, maxReference) {
      const available = Number.isFinite(nextValue);
      if (!available) return { value: 0, ratio: 0, available: false };
      const oldValue = previous?.available ? Number(previous.value) : Number(nextValue);
      const smoothedValue = oldValue + (Number(nextValue) - oldValue) * 0.35;
      return {
        value: smoothedValue,
        ratio: clamp(smoothedValue / Math.max(1e-6, maxReference), 0, 1),
        available: true,
      };
    },

    formatIndicator(value, decimals = 0, suffix = '', available = false) {
      if (!available || !Number.isFinite(value)) return 'N/A';
      return `${Number(value).toFixed(decimals)}${suffix}`;
    },

    indicatorClass(available) {
      return available ? 'indicator-live' : 'indicator-na';
    },

    clippingIndicatorClass() {
      if (!this.telemetry.clipping.available) return 'indicator-na';
      return (Number(this.metrics.clipCount) || 0) > 0 ? 'indicator-clip' : 'indicator-live';
    },

    handleModeSwitch() {
      this.stopPlay();
      if (this.previewMode === 'synth') {
        this.duration = 0;
        this.currentTime = 0;
        this.seekTime = 0;
      } else if (this.audioBuffer) {
        this.duration = this.audioBuffer.duration;
      }
      this.setAudioStatus(this.previewMode === 'synth' ? 'Modo synth selecionado.' : 'Modo arquivo selecionado.');
    },

    canPlay() {
      if (!this.audioInitialized || !this.workletReady) return false;
      return this.previewMode === 'synth' ? true : !!this.audioBuffer;
    },

    togglePlay() {
      if (!this.canPlay()) return;
      if (this.isPlaying) {
        this.pausePlay();
      } else if (this.previewMode === 'synth') {
        this.startSynth();
      } else {
        this.startFilePlayback(this.pausedAt || 0);
      }
    },

    stopPlay() {
      if (this.previewMode === 'synth') {
        this.stopSynth();
      } else {
        this.stopFilePlayback(true);
      }
      this.isPlaying = false;
      this.stopTransportTimer();
      this.currentTime = 0;
      this.seekTime = 0;
      this.pausedAt = 0;
      if (this.previewMode === 'file' && this.audioBuffer) {
        this.duration = this.audioBuffer.duration;
      }
      this.setAudioStatus('Parado.');
    },

    pausePlay() {
      if (this.previewMode === 'synth') {
        this.stopSynth();
        this.isPlaying = false;
        this.stopTransportTimer();
        this.setAudioStatus('Synth pausado.');
        return;
      }
      this.stopFilePlayback(false);
      this.isPlaying = false;
      this.stopTransportTimer();
      this.setAudioStatus('Pausado.');
    },

    startSynth() {
      if (!this.audioContext || !this.sourceBus) return;
      this.stopSynth();
      this.synthOscillator = new OscillatorNode(this.audioContext, { type: 'sawtooth', frequency: 220 });
      this.synthGain = new GainNode(this.audioContext, { gain: 0 });
      this.synthOscillator.connect(this.synthGain).connect(this.sourceBus);
      const now = this.audioContext.currentTime;
      this.synthGain.gain.cancelScheduledValues(now);
      this.synthGain.gain.setValueAtTime(0, now);
      this.synthGain.gain.linearRampToValueAtTime(0.25, now + 0.02);
      this.synthOscillator.start();
      this.isPlaying = true;
      this.startedAt = now;
      this.setAudioStatus('Synth tocando.');
      this.startTransportTimer();
    },

    stopSynth() {
      const now = this.audioContext?.currentTime || 0;
      if (this.synthGain) {
        this.synthGain.gain.cancelScheduledValues(now);
        this.synthGain.gain.setTargetAtTime(0, now, 0.015);
      }
      if (this.synthOscillator) {
        try {
          this.synthOscillator.stop(now + 0.05);
        } catch (_) {}
        this.synthOscillator.disconnect();
        this.synthOscillator = null;
      }
      if (this.synthGain) {
        this.synthGain.disconnect();
        this.synthGain = null;
      }
    },

    startFilePlayback(offsetSeconds) {
      if (!this.audioBuffer || !this.sourceBus || !this.audioContext) return;
      this.stopFilePlayback(false);

      const source = new AudioBufferSourceNode(this.audioContext, { buffer: this.audioBuffer });
      source.connect(this.sourceBus);
      source.onended = () => {
        if (!this.fileSourceNode) return;
        this.fileSourceNode = null;
        if (this.repeatContinuous && this.previewMode === 'file' && this.audioBuffer) {
          this.startFilePlayback(0);
          return;
        }
        this.isPlaying = false;
        this.stopTransportTimer();
        this.pausedAt = 0;
        this.currentTime = this.duration;
        this.seekTime = this.duration;
      };

      const safeOffset = clamp(offsetSeconds || 0, 0, this.audioBuffer.duration);
      source.start(0, safeOffset);
      this.fileSourceNode = source;
      this.startedAt = this.audioContext.currentTime - safeOffset;
      this.pausedAt = safeOffset;
      this.isPlaying = true;
      this.duration = this.audioBuffer.duration;
      this.setAudioStatus('Arquivo tocando.');
      this.startTransportTimer();
    },

    stopFilePlayback(resetOffset) {
      if (this.fileSourceNode) {
        try {
          this.fileSourceNode.onended = null;
          this.fileSourceNode.stop();
        } catch (_) {}
        this.fileSourceNode.disconnect();
        this.fileSourceNode = null;
      }
      if (!this.audioContext) return;
      if (resetOffset) {
        this.pausedAt = 0;
      } else {
        this.pausedAt = clamp(this.audioContext.currentTime - this.startedAt, 0, this.duration || Infinity);
      }
    },

    startTransportTimer() {
      this.stopTransportTimer();
      const tick = () => {
        if (!this.isPlaying || !this.audioContext) return;
        if (this.previewMode === 'file') {
          this.currentTime = clamp(this.audioContext.currentTime - this.startedAt, 0, this.duration || 0);
          this.seekTime = this.currentTime;
        }
        this.transportTimer = requestAnimationFrame(tick);
      };
      this.transportTimer = requestAnimationFrame(tick);
    },

    stopTransportTimer() {
      if (this.transportTimer) {
        cancelAnimationFrame(this.transportTimer);
        this.transportTimer = null;
      }
    },

    handleSeekInput() {
      if (this.previewMode !== 'file') return;
      this.currentTime = this.seekTime;
    },

    handleSeekChange() {
      if (this.previewMode !== 'file' || !this.audioBuffer) return;
      const target = clamp(Number(this.seekTime), 0, this.audioBuffer.duration);
      this.currentTime = target;
      this.pausedAt = target;
      if (this.isPlaying) {
        this.startFilePlayback(target);
      }
    },

    async handleFileSelect(event) {
      const file = event?.target?.files?.[0];
      if (!file) return;
      await this.loadFile(file);
    },

    async handleFileDrop(event) {
      this.dragOver = false;
      const file = event?.dataTransfer?.files?.[0];
      if (!file) return;
      await this.loadFile(file);
    },

    async loadFile(file) {
      if (!this.audioInitialized || !this.audioContext) {
        this.toast('Ative o áudio antes de carregar um arquivo.', 'error');
        return;
      }

      try {
        this.isDecoding = true;
        this.stopPlay();
        this.audioBuffer = null;
        this.duration = 0;
        this.currentTime = 0;
        this.seekTime = 0;
        this.pausedAt = 0;
        this.audioFile = file;
        const arrayBuffer = await file.arrayBuffer();
        this.audioBuffer = await this.audioContext.decodeAudioData(arrayBuffer);
        this.duration = this.audioBuffer.duration;
        this.currentTime = 0;
        this.seekTime = 0;
        this.pausedAt = 0;
        this.previewMode = 'file';
        this.setAudioStatus(`Arquivo carregado: ${file.name}`);
        this.toast(`Arquivo pronto (${this.formatTime(this.duration)}).`, 'success');
      } catch (err) {
        this.audioBuffer = null;
        this.duration = 0;
        this.currentTime = 0;
        this.seekTime = 0;
        this.setAudioStatus(`Falha ao decodificar arquivo: ${err.message || err}`, true);
        this.toast(this.audioStatusMsg, 'error');
      } finally {
        this.isDecoding = false;
      }
    },

    async createOfflineRenderedBuffer() {
      if (!this.audioBuffer) {
        throw new Error('Nenhum arquivo carregado para exportação.');
      }

      const sampleRate = Math.min(this.audioBuffer.sampleRate || 44100, 48000);
      const tailSeconds = 0.8;
      const frameCount = Math.ceil((this.audioBuffer.duration + tailSeconds) * sampleRate);
      const offlineContext = new OfflineAudioContext(2, frameCount, sampleRate);
      await offlineContext.audioWorklet.addModule(new URL('worklet.js', window.location.href).toString());

      const workletNode = new AudioWorkletNode(offlineContext, 'sound-bubbles-worklet', { outputChannelCount: [2] });
      const source = new AudioBufferSourceNode(offlineContext, { buffer: this.audioBuffer });
      source.connect(workletNode).connect(offlineContext.destination);

      let errorHandler = null;
      await new Promise((resolve, reject) => {
        const timeoutId = setTimeout(() => reject(new Error('Tempo limite ao iniciar o processador de áudio.')), 5000);
        errorHandler = (event) => {
          const data = event.data || {};
          if (data.type === 'init-failed' || data.type === 'wasm-error' || data.type === 'processor-error') {
            reject(new Error(data.message || 'Falha no processador de áudio.'));
          }
        };
        workletNode.port.onmessage = (event) => {
          const data = event.data || {};
          if (data.type === 'ready') {
            clearTimeout(timeoutId);
            this.pushAllParamsToPort(workletNode.port);
            workletNode.port.onmessage = errorHandler;
            resolve();
          } else if (data.type === 'init-failed' || data.type === 'wasm-error' || data.type === 'processor-error') {
            clearTimeout(timeoutId);
            reject(new Error(data.message || 'Falha no processador de áudio.'));
          }
        };
      });

      source.start(0);
      try {
        return await offlineContext.startRendering();
      } finally {
        workletNode.port.onmessage = null;
      }
    },

    pushAllParamsToPort(port) {
      port.postMessage({ type: 'quality-profile', profile: Number(this.selectedQualityProfile) });
      port.postMessage({ type: 'param', id: WASM_DEVELOPER_MODE_PARAM_ID, value: this.developerMode ? 1 : 0 });
      Object.keys(this.macroValues).forEach((key) => {
        const id = WASM_MACRO_PARAM_ID_MAP[key];
        if (id === undefined) return;
        port.postMessage({ type: 'param', id, value: Number(this.macroValues[key]) });
      });
      if (this.developerMode) {
        Object.keys(this.resolvedParams).forEach((key) => {
          const id = WASM_PARAM_ID_MAP[key];
          if (id === undefined) return;
          port.postMessage({ type: 'param', id, value: Number(this.resolvedParams[key]) });
        });
      }
    },

    isMp3ExportAvailable() {
      return typeof Worker !== 'undefined';
    },

    getOrCreateMp3Worker() {
      if (this.mp3Worker) return this.mp3Worker;
      this.mp3Worker = new Worker(new URL('mp3EncoderWorker.js', window.location.href));
      return this.mp3Worker;
    },

    async encodeAudioBufferToMp3InWorker(audioBuffer, kbps = 192) {
      const left = audioBuffer.getChannelData(0).slice();
      const right = (audioBuffer.numberOfChannels > 1 ? audioBuffer.getChannelData(1) : left).slice();
      const worker = this.getOrCreateMp3Worker();
      const requestId = `${Date.now()}-${Math.random().toString(16).slice(2)}`;

      const mp3Bytes = await new Promise((resolve, reject) => {
        const handleMessage = (event) => {
          const data = event.data || {};
          if (data.requestId !== requestId) return;
          worker.removeEventListener('message', handleMessage);
          worker.removeEventListener('error', handleError);
          if (data.type === 'success' && data.mp3Buffer) resolve(data.mp3Buffer);
          else reject(new Error(data.message || 'Falha na codificação MP3.'));
        };
        const handleError = (event) => {
          worker.removeEventListener('message', handleMessage);
          worker.removeEventListener('error', handleError);
          const detail = event?.message ? ` ${event.message}` : '';
          reject(new Error(`Falha no worker de codificação MP3.${detail}`));
        };

        worker.addEventListener('message', handleMessage);
        worker.addEventListener('error', handleError);
        worker.postMessage(
          {
            type: 'encode-mp3',
            requestId,
            sampleRate: audioBuffer.sampleRate,
            kbps,
            leftBuffer: left.buffer,
            rightBuffer: right.buffer,
          },
          [left.buffer, right.buffer]
        );
      });

      return new Blob([mp3Bytes], { type: 'audio/mpeg' });
    },

    async exportProcessedTrackMp3() {
      if (!this.audioBuffer) {
        this.toast('Carregue um arquivo para exportar.', 'error');
        return;
      }
      if (!this.isMp3ExportAvailable()) {
        this.toast('Seu navegador não suporta exportação MP3 em background.', 'error');
        return;
      }

      this.isExportingMp3 = true;
      try {
        this.setAudioStatus('Renderizando faixa processada para exportação...');
        const renderedBuffer = await this.createOfflineRenderedBuffer();
        this.setAudioStatus('Codificando MP3 em background...');
        const mp3Blob = await this.encodeAudioBufferToMp3InWorker(renderedBuffer);
        const filename = `bubble-cloud-processed-${new Date().toISOString().replace(/[\W:]/g, '-')}.mp3`;
        const url = URL.createObjectURL(mp3Blob);
        const anchor = document.createElement('a');
        anchor.href = url;
        anchor.download = filename;
        document.body.appendChild(anchor);
        anchor.click();
        anchor.remove();
        setTimeout(() => URL.revokeObjectURL(url), 100);
        this.setAudioStatus('Exportação MP3 concluída.');
        this.toast('Faixa processada exportada em MP3.', 'success');
      } catch (err) {
        const baseMessage = err?.message || String(err);
        const hint = /carregar o encoder mp3|importscripts|network|cdn/i.test(baseMessage)
          ? ' Verifique conexão de rede/CSP e a disponibilidade de vendor/lame.min.js.'
          : '';
        this.setAudioStatus(`Falha na exportação MP3: ${baseMessage}${hint}`, true);
        this.toast(this.audioStatusMsg, 'error');
      } finally {
        this.isExportingMp3 = false;
      }
    },

    clearAudioFile() {
      this.stopPlay();
      this.audioFile = null;
      this.audioBuffer = null;
      this.duration = 0;
      this.currentTime = 0;
      this.seekTime = 0;
      const input = document.getElementById('audio-upload');
      if (input) input.value = '';
      this.setAudioStatus('Arquivo removido.');
    },

    getEngineStateName(state) {
      const names = {
        0: 'Idle',
        1: 'Tracking',
        2: 'Sustain',
        3: 'Transient',
      };
      return names[state] || `State ${state}`;
    },

    formatTime(seconds) {
      const safe = Math.max(0, Number(seconds) || 0);
      const min = Math.floor(safe / 60);
      const sec = Math.floor(safe % 60);
      return `${min}:${String(sec).padStart(2, '0')}`;
    },
  }));
});
