(function () {
  const DEFAULT_METRICS = Object.freeze({
    activeVoices: 0,
    voiceLimit: 24,
    densityEstimate: 0,
    energy: 0,
    stereoSpread: 0,
    clipping: false,
    clipCount: 0,
    engineState: 0,
  });

  const clamp01 = (value) => Math.max(0, Math.min(1, Number(value) || 0));

  class BubbleCloudVisualizer {
    constructor(canvas, options = {}) {
      if (!canvas) throw new Error('BubbleCloudVisualizer requires a canvas');

      this.canvas = canvas;
      this.ctx = canvas.getContext('2d', { alpha: true });
      this.metrics = { ...DEFAULT_METRICS };
      this.pixelRatio = 1;
      this.animationFrame = null;
      this.running = false;
      this.lastFrameTime = 0;
      this.particles = Array.from({ length: options.particleCount || 42 }, (_, index) => ({
        phase: index * 0.618,
        radiusSeed: 0.25 + ((index * 37) % 100) / 140,
        lane: ((index * 19) % 100) / 100,
      }));
    }

    start() {
      if (this.running) return;
      this.running = true;
      this.resize();
      this.animationFrame = requestAnimationFrame((time) => this.draw(time));
    }

    stop() {
      this.running = false;
      if (this.animationFrame) {
        cancelAnimationFrame(this.animationFrame);
        this.animationFrame = null;
      }
    }

    resize() {
      const rect = this.canvas.getBoundingClientRect();
      const nextPixelRatio = Math.max(1, Math.min(window.devicePixelRatio || 1, 2));
      const nextWidth = Math.max(1, Math.floor(rect.width * nextPixelRatio));
      const nextHeight = Math.max(1, Math.floor(rect.height * nextPixelRatio));

      if (this.canvas.width !== nextWidth || this.canvas.height !== nextHeight || this.pixelRatio !== nextPixelRatio) {
        this.canvas.width = nextWidth;
        this.canvas.height = nextHeight;
        this.pixelRatio = nextPixelRatio;
      }
    }

    update(metrics = {}) {
      this.metrics = {
        activeVoices: Math.max(0, Number(metrics.activeVoices) || 0),
        voiceLimit: Math.max(1, Number(metrics.voiceLimit) || DEFAULT_METRICS.voiceLimit),
        densityEstimate: clamp01(metrics.densityEstimate),
        energy: clamp01(metrics.energy),
        stereoSpread: clamp01(metrics.stereoSpread),
        clipping: Boolean(metrics.clipping),
        clipCount: Math.max(0, Number(metrics.clipCount) || 0),
        engineState: Number(metrics.engineState) || 0,
      };
    }

    draw(time) {
      if (!this.running) return;
      this.resize();

      const ctx = this.ctx;
      const width = this.canvas.width;
      const height = this.canvas.height;
      const m = this.metrics;
      const voiceRatio = clamp01(m.activeVoices / m.voiceLimit);
      const energy = clamp01(m.energy);
      const density = clamp01(m.densityEstimate);
      const spread = clamp01(m.stereoSpread);
      const clipPulse = m.clipping ? 1 : 0;
      this.lastFrameTime = time;
      const phase = time * 0.001;

      ctx.clearRect(0, 0, width, height);
      const gradient = ctx.createLinearGradient(0, 0, width, height);
      gradient.addColorStop(0, 'rgba(13, 20, 38, 0.96)');
      gradient.addColorStop(0.55, 'rgba(23, 36, 70, 0.92)');
      gradient.addColorStop(1, clipPulse ? 'rgba(101, 28, 42, 0.92)' : 'rgba(12, 54, 70, 0.92)');
      ctx.fillStyle = gradient;
      ctx.fillRect(0, 0, width, height);

      const cx = width * 0.5;
      const cy = height * 0.52;
      const coreRadius = Math.min(width, height) * (0.12 + energy * 0.16);
      const haloRadius = coreRadius * (2.0 + density * 1.4);
      const halo = ctx.createRadialGradient(cx, cy, coreRadius * 0.25, cx, cy, haloRadius);
      halo.addColorStop(0, `rgba(112, 222, 255, ${0.24 + energy * 0.36})`);
      halo.addColorStop(0.58, `rgba(139, 112, 255, ${0.12 + density * 0.26})`);
      halo.addColorStop(1, 'rgba(112, 222, 255, 0)');
      ctx.fillStyle = halo;
      ctx.beginPath();
      ctx.arc(cx, cy, haloRadius, 0, Math.PI * 2);
      ctx.fill();

      ctx.save();
      ctx.globalCompositeOperation = 'lighter';
      for (const particle of this.particles) {
        const speed = 0.14 + density * 0.62 + energy * 0.2;
        const angle = particle.phase * Math.PI * 2 + phase * speed;
        const orbit = Math.min(width, height) * (0.14 + particle.radiusSeed * 0.45 + voiceRatio * 0.18);
        const x = cx + Math.cos(angle) * orbit * (0.35 + spread * 0.9);
        const y = cy + Math.sin(angle * (0.82 + particle.lane * 0.24)) * orbit * 0.48;
        const radius = Math.max(2 * this.pixelRatio, (3 + energy * 8 + particle.radiusSeed * 8) * this.pixelRatio);
        const alpha = 0.16 + energy * 0.28 + density * 0.2;
        ctx.fillStyle = `rgba(${120 + particle.lane * 80}, ${190 + energy * 55}, 255, ${alpha})`;
        ctx.beginPath();
        ctx.arc(x, y, radius, 0, Math.PI * 2);
        ctx.fill();
      }
      ctx.restore();

      const meterHeight = Math.max(4 * this.pixelRatio, height * 0.035);
      this.drawMeter(0.08 * width, height * 0.86, width * 0.38, meterHeight, voiceRatio, '#70e0ff', 'voices');
      this.drawMeter(0.54 * width, height * 0.86, width * 0.38, meterHeight, spread, '#b48cff', 'spread');
      this.drawMeter(0.08 * width, height * 0.92, width * 0.84, meterHeight, energy, m.clipping ? '#ff5c7a' : '#4dffb5', 'energy');

      if (m.clipping) {
        ctx.strokeStyle = `rgba(255, 92, 122, ${0.35 + 0.25 * Math.sin(phase * 16)})`;
        ctx.lineWidth = 3 * this.pixelRatio;
        ctx.strokeRect(1.5 * this.pixelRatio, 1.5 * this.pixelRatio, width - 3 * this.pixelRatio, height - 3 * this.pixelRatio);
      }

      this.animationFrame = requestAnimationFrame((nextTime) => this.draw(nextTime));
    }

    drawMeter(x, y, width, height, value, color, label) {
      const ctx = this.ctx;
      ctx.fillStyle = 'rgba(255, 255, 255, 0.10)';
      ctx.fillRect(x, y, width, height);
      ctx.fillStyle = color;
      ctx.fillRect(x, y, width * clamp01(value), height);
      ctx.fillStyle = 'rgba(236, 244, 255, 0.76)';
      ctx.font = `${Math.max(10, 10 * this.pixelRatio)}px system-ui, sans-serif`;
      ctx.fillText(label, x, y - 5 * this.pixelRatio);
    }
  }

  window.BubbleCloudVisualizer = BubbleCloudVisualizer;
}());
