// Hatrick audio engine on miniaudio.
// Music: the current theme's stems are streamed from OGG in lockstep (one shared playhead, so
// they stay sample-locked when they loop) and each fades toward its own target gain.
// Sounds: effects and jingles are decoded into memory at startup and mixed as overlapping voices.
// The game thread talks to the audio callback through atomics, a lock-free request queue and a
// mutex that is only held for a pointer swap when the theme changes.
#define STB_VORBIS_HEADER_ONLY
#include "vendor/stb_vorbis.c"
#include "vendor/miniaudio.h"
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "audio.h"
#include "sound.h"
#include "music.h"

#define RATE 48000
#define NVOICE 32
#define QLEN 64

static const char *const SOUND_FILE[NSOUND] = {
  "sfx/coin", "sfx/jump", "sfx/jump2", "sfx/jump3", "sfx/flip", "sfx/longjump", "sfx/spin", "sfx/roll", "sfx/dive",
  "sfx/cap_throw", "sfx/cap_catch", "sfx/cap_bounce", "sfx/stomp", "sfx/brick", "sfx/gp_spin", "sfx/gp_land",
  "sfx/spring", "sfx/wall_jump", "sfx/land", "sfx/skid", "sfx/ledge", "sfx/menu_move", "sfx/menu_ok",
  "sfx/menu_back", "sfx/pause", "music/jingle_clear", "music/jingle_death",
};

typedef struct { float *pcm; ma_uint64 frames; } Clip;
typedef struct { int clip; ma_uint64 pos; float l, r; } Voice;
typedef struct { ma_decoder dec[NSTEM]; int theme; } Music;

static struct {
  int ok;
  char dir[1024];
  ma_context ctx; int has_ctx;
  ma_device dev;
  ma_mutex lock;
  Clip clip[NSOUND];
  Voice voice[NVOICE];                     // audio thread only
  _Atomic int qw, qr; int qsound[QLEN]; float qpan[QLEN];
  Music *music;                            // swapped under lock
  _Atomic long long playhead;              // frames into the loop, for snd_beat
  _Atomic float target[NSTEM]; _Atomic float rate[NSTEM];
  float gain[NSTEM];                       // audio thread only
  _Atomic float master; _Atomic int muted; float out;
  float buf[2 * 4096];
  FILE *dump; long long dumped;
  int log; double t0;                      // HATRICK_AUDIO_LOG=1: print sound and theme events (tests)
} A;

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

static int load_clip(Clip *c, const char *name) {
  char path[1200];
  snprintf(path, sizeof path, "%s/%s.ogg", A.dir, name);
  ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, RATE);
  void *pcm = NULL;
  if (ma_decode_file(path, &cfg, &c->frames, &pcm) != MA_SUCCESS) { fprintf(stderr, "hatrick: cannot load %s\n", path); return 0; }
  c->pcm = pcm;
  return 1;
}

// Reads exactly n frames of one stem, wrapping at the end of the loop.
static void read_loop(ma_decoder *d, float *dst, ma_uint32 n) {
  for (int empty = 0; n;) {
    ma_uint64 got = 0;
    ma_decoder_read_pcm_frames(d, dst, n, &got);
    if (got < n && (ma_decoder_seek_to_pcm_frame(d, 0) != MA_SUCCESS || (!got && ++empty > 1))) {
      memset(dst + got * 2, 0, (n - got) * 8); return;   // broken stem: silence instead of spinning
    }
    dst += got * 2; n -= (ma_uint32)got;
  }
}

static float soft(float x) {   // transparent below 0.9, then a smooth knee instead of clipping
  float a = fabsf(x);
  return a < 0.9f ? x : copysignf(0.9f + 0.1f * tanhf((a - 0.9f) / 0.1f), x);
}

static void callback(ma_device *dev, void *output, const void *input, ma_uint32 n) {
  (void)dev; (void)input;
  float *o = output;
  memset(o, 0, (size_t)n * 8);
  // new sounds
  int r = atomic_load(&A.qr), w = atomic_load_explicit(&A.qw, memory_order_acquire);
  for (; r != w; r = (r + 1) % QLEN) {
    Voice *v = A.voice, *best = A.voice;
    for (; v < A.voice + NVOICE; v++) { if (v->clip < 0) break; if (v->pos > best->pos) best = v; }
    if (v == A.voice + NVOICE) v = best;   // all busy: replace the one that has played longest
    float p = A.qpan[r] * 0.5f;
    v->clip = A.qsound[r]; v->pos = 0; v->l = p > 0 ? 1 - p : 1; v->r = p < 0 ? 1 + p : 1;
  }
  atomic_store(&A.qr, r);
  // music: every stem advances by the same n frames
  ma_mutex_lock(&A.lock);
  if (A.music) {
    for (ma_uint32 done = 0; done < n;) {
      ma_uint32 m = n - done > 4096 ? 4096 : n - done;
      for (int s = 0; s < NSTEM; s++) {
        float t = atomic_load(&A.target[s]), step = atomic_load(&A.rate[s]), g = A.gain[s];
        read_loop(&A.music->dec[s], A.buf, m);
        if (g == 0 && t == 0) continue;
        for (ma_uint32 i = 0; i < m; i++) {
          g = g < t ? fminf(t, g + step) : fmaxf(t, g - step);
          o[2 * (done + i)] += A.buf[2 * i] * g; o[2 * (done + i) + 1] += A.buf[2 * i + 1] * g;
        }
        A.gain[s] = g;
      }
      done += m;
    }
    long long frames = THEME[A.music->theme].frames;
    atomic_store(&A.playhead, (atomic_load(&A.playhead) + n) % frames);
  }
  ma_mutex_unlock(&A.lock);
  // sounds
  for (Voice *v = A.voice; v < A.voice + NVOICE; v++) {
    if (v->clip < 0) continue;
    Clip *c = &A.clip[v->clip];
    ma_uint64 k = c->frames - v->pos < n ? c->frames - v->pos : n;
    const float *s = c->pcm + 2 * v->pos;
    for (ma_uint64 i = 0; i < k; i++) { o[2 * i] += s[2 * i] * v->l; o[2 * i + 1] += s[2 * i + 1] * v->r; }
    v->pos += k;
    if (v->pos >= c->frames) v->clip = -1;
  }
  // master volume (ramped, so mute and volume changes never click) and a soft limiter
  float want = atomic_load(&A.muted) ? 0 : atomic_load(&A.master), g = A.out;
  for (ma_uint32 i = 0; i < n; i++) {
    g += (want - g) * 0.002f;
    o[2 * i] = soft(o[2 * i] * g); o[2 * i + 1] = soft(o[2 * i + 1] * g);
  }
  A.out = g;
  if (A.dump) { fwrite(o, 8, n, A.dump); A.dumped += n; }
}

static void wav_header(FILE *f, long long frames) {   // 32-bit float stereo WAV
  unsigned data = (unsigned)(frames * 8), v;
  fseek(f, 0, SEEK_SET);
  fwrite("RIFF", 1, 4, f); v = 36 + data; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  unsigned short fmt[] = { 3, 2 }; v = 16; fwrite(&v, 4, 1, f); fwrite(fmt, 2, 2, f);
  v = RATE; fwrite(&v, 4, 1, f); v = RATE * 8; fwrite(&v, 4, 1, f);
  unsigned short align[] = { 8, 32 }; fwrite(align, 2, 2, f);
  fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
  fseek(f, 0, SEEK_END);
}

int snd_init(const char *dir, int silent, const char *dump) {
  snprintf(A.dir, sizeof A.dir, "%s", dir);
  A.log = getenv("HATRICK_AUDIO_LOG") != NULL;
  A.t0 = now();
  for (int i = 0; i < NVOICE; i++) A.voice[i].clip = -1;
  for (int s = 0; s < NSTEM; s++) atomic_store(&A.rate[s], 1.0f / RATE);
  atomic_store(&A.master, 0.64f);
  for (int i = 0; i < NSOUND; i++) if (!load_clip(&A.clip[i], SOUND_FILE[i])) return 0;
  if (ma_mutex_init(&A.lock) != MA_SUCCESS) return 0;
  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32; cfg.playback.channels = 2; cfg.sampleRate = RATE;
  cfg.dataCallback = callback;
  cfg.periodSizeInMilliseconds = 10; cfg.performanceProfile = ma_performance_profile_low_latency;
  if (silent) {
    ma_backend null = ma_backend_null;
    if (ma_context_init(&null, 1, NULL, &A.ctx) != MA_SUCCESS) return 0;
    A.has_ctx = 1;
  }
  if (dump && !(A.dump = fopen(dump, "wb"))) return 0;
  if (A.dump) wav_header(A.dump, 0);
  if (ma_device_init(A.has_ctx ? &A.ctx : NULL, &cfg, &A.dev) != MA_SUCCESS) { fprintf(stderr, "hatrick: no audio device\n"); return 0; }
  if (ma_device_start(&A.dev) != MA_SUCCESS) { ma_device_uninit(&A.dev); return 0; }
  return A.ok = 1;
}

void snd_quit(void) {
  if (!A.ok) return;
  ma_device_uninit(&A.dev);
  if (A.has_ctx) ma_context_uninit(&A.ctx);
  if (A.dump) { wav_header(A.dump, A.dumped); fclose(A.dump); }
  A.ok = 0;
}

void snd_play(int sound, float pan) {
  if (!A.ok || sound < 0 || sound >= NSOUND) return;
  int w = atomic_load(&A.qw), next = (w + 1) % QLEN;
  if (next == atomic_load(&A.qr)) return;   // queue full: drop
  A.qsound[w] = sound; A.qpan[w] = pan;
  if (A.log) fprintf(stderr, "%.3f play %s\n", now() - A.t0, strrchr(SOUND_FILE[sound], '/') + 1);
  atomic_store_explicit(&A.qw, next, memory_order_release);
}

void snd_theme(int theme, int restart) {
  if (!A.ok) return;
  if (A.music && A.music->theme == theme && !restart) return;
  if (A.log) fprintf(stderr, "%.3f theme %s\n", now() - A.t0, THEME[theme].name);
  Music *m = calloc(1, sizeof *m);
  m->theme = theme;
  for (int s = 0; s < NSTEM; s++) {
    char path[1200];
    snprintf(path, sizeof path, "%s/music/%s/%s.ogg", A.dir, THEME[theme].name, STEM_NAME[s]);
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, RATE);
    if (ma_decoder_init_file(path, &cfg, &m->dec[s]) != MA_SUCCESS) {
      fprintf(stderr, "hatrick: cannot open %s\n", path);
      for (int j = 0; j < s; j++) ma_decoder_uninit(&m->dec[j]);
      free(m); return;
    }
  }
  ma_mutex_lock(&A.lock);
  Music *old = A.music;
  A.music = m; atomic_store(&A.playhead, 0);
  for (int s = 0; s < NSTEM; s++) A.gain[s] = 0;   // every (re)start fades in from silence
  ma_mutex_unlock(&A.lock);
  if (old) { for (int s = 0; s < NSTEM; s++) ma_decoder_uninit(&old->dec[s]); free(old); }
}

void snd_stem(int stem, float gain, int ms) {
  if (!A.ok) return;
  atomic_store(&A.rate[stem], 1.0f / (RATE * (ms > 1 ? ms : 1) / 1000.0f));
  atomic_store(&A.target[stem], gain);
}

void snd_volume(float master, int muted) {
  atomic_store(&A.master, master); atomic_store(&A.muted, muted);
}

double snd_beat(void) {
  if (!A.ok || !A.music) return -1;
  int t = A.music->theme;
  return (double)atomic_load(&A.playhead) / THEME[t].frames * THEME[t].beats;
}
