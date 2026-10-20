// Replay loop: play / pause / step / seek over a Sequence, with a frame counter. The renderer is injected
// (needs reset() and executeFrame(frame)), as are the scheduler and clock, so this runs in Node unit tests.

export class Replayer {
  /**
   * @param {{reset(): void, executeFrame(frame: object): void}} renderer
   * @param {{frames: object[]}} sequence
   * @param {{fps?: number, loop?: boolean, schedule?: Function, cancel?: Function, now?: Function, onChange?: Function}} [opts]
   */
  constructor(renderer, sequence, opts = {}) {
    this.renderer = renderer;
    this.sequence = sequence;
    this.fps = opts.fps ?? 30;
    this.loop = opts.loop ?? false;
    this.schedule = opts.schedule ?? ((fn) => requestAnimationFrame(fn));
    this.cancel = opts.cancel ?? ((h) => cancelAnimationFrame(h));
    this.now = opts.now ?? (() => performance.now());
    this.onChange = opts.onChange ?? (() => {});
    this._frame = 0;
    this._playing = false;
    this._error = null;
    this._handle = null;
    this._last = 0;
  }

  get frame() { return this._frame; }
  get total() { return this.sequence.frames.length; }
  get playing() { return this._playing; }
  get error() { return this._error; }

  _emit() {
    this.onChange({ frame: this._frame, total: this.total, playing: this._playing, error: this._error });
  }

  /** Execute the next frame. Returns false at the end. A renderer error stops playback and is rethrown. */
  step() {
    if (this._frame >= this.total) return false;
    try {
      this.renderer.executeFrame(this.sequence.frames[this._frame]);
    } catch (e) {
      this._error = e;
      this._stop();
      this._emit();
      throw e;
    }
    this._frame++;
    this._emit();
    return true;
  }

  /** Show the state after `n` frames (0..total). Backwards seeks reset the renderer and replay from frame 0. */
  seek(n) {
    if (!Number.isInteger(n) || n < 0 || n > this.total) throw new RangeError(`frame ${n} out of range 0..${this.total}`);
    if (n < this._frame) { this.renderer.reset(); this._frame = 0; this._error = null; }
    while (this._frame < n) this.step();
    this._emit();
  }

  play() {
    if (this._playing) return;
    this._playing = true;
    this._last = this.now();
    this._arm();
    this._emit();
  }

  pause() {
    if (!this._playing) return;
    this._stop();
    this._emit();
  }

  toggle() { if (this._playing) this.pause(); else this.play(); }

  _stop() {
    this._playing = false;
    if (this._handle !== null) { this.cancel(this._handle); this._handle = null; }
  }

  _arm() { this._handle = this.schedule(() => this._tick()); }

  _tick() {
    this._handle = null;
    if (!this._playing) return;
    const t = this.now();
    if (t - this._last >= 1000 / this.fps) {
      this._last = t;
      if (this._frame >= this.total) {
        if (this.loop && this.total > 0) { this.renderer.reset(); this._frame = 0; } else { this._stop(); this._emit(); return; }
      }
      try { this.step(); } catch { return; }
      if (this._frame >= this.total && !this.loop) { this._stop(); this._emit(); return; }
    }
    this._arm();
  }
}
