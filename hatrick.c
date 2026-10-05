// Hatrick: a tiny cap-throwing platformer for Linux/X11 (i386, no libc).
// Arrows move, Z/Y or Space jumps, X throws the cap, Down crouches / ground pounds.
// Gamepads use the Super Mario Odyssey layout (see padkeys).
// R restarts the level (or the whole run once finished), M mutes, Esc quits.
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "gfx.h"
#include "levels.h"

typedef unsigned char u8;
typedef unsigned u32;

#define W 256           // internal resolution
#define H 144
#ifndef SC
#define SC 4            // window scale
#endif
#define MW 256          // map size in 8 px tiles
#define MH 32
#define SOLID 0x3E      // tile types 1..5 block movement
// Tiles: 1 ground, 2 brick, 3 spikes, 4 stone, 5 spring, 6 coin.
// Physics is fixed point, 1/256 px, 60 steps per second.
#define GRAV 48
#define MAXV 400
#define ACC 18
#define AACC 14
#define FRIC 24
enum { NORM, LONGJ, GPWIND, GPSLAM, GPLAND, DIVE, SLIDE, DEAD, WIN };

static u8 map[MH][MW];
static u32 big[H*SC][W*SC];   // the window image; the world is drawn straight into it at sub-pixel positions
static int lw, gx, gy, gb;   // level width, flag column / top row, pixel row where the pole meets the ground
static int hx, hy, hvx, hvy, face, st, stt, gnd, jn, landt, capok, diveok, stall, wall, coy, jbuf, lock, spin, cut, skid;
static int cst, cxp, cyp, cvx, ct;
static int lvl, deaths, coins, lcoins, tim, shake, done, prevk, fr, capless, capoff;
// Presentation only (never read by the game logic): camera, flips, squash and stretch.
static int cxf, cyf, look, camgy;          // camera position and look-ahead in 1/256 px, last standing height
static int spinlen, spind, sqv, turnt, lface, runph, vang, pgnd, pvy, dustt;
static volatile u8 *shm;  // audio process mailbox: [0] sfx counter, [1] sfx id, [2] mute
typedef struct { int x, y, vx, vy, t, a, h; } E;
static E en[48];
static int ne;
typedef struct { int x, y, vx, vy, l, g, ml; u32 c; } P;
#define NP 256
static P pt[NP];
static u32 seed = 1;
static int htx, hty;

static int rnd(int n) { seed = seed*1103515245 + 12345; return (seed >> 16) % n; }
static int iabs(int v) { return v < 0 ? -v : v; }
static void sfx(int i) { if (shm) { shm[1] = i; shm[0]++; } }
// Controller rumble: 1 cap bounce / wall jump, 2 hard landing, 3 brick, 4 stomp, 5 spring,
// 6 ground-pound landing, 7 death, 8 goal. The strongest request in a frame is played.
static int rumq;
static void rumble(int k) { if (k > rumq) rumq = k; }

static int tile(int x, int y) { return x < 0 ? 4 : x >= lw || y < 0 || y >= MH ? 0 : map[y][x]; }
// First tile of a type in mask m under the pixel rect; its position goes to htx/hty.
static int scan(int x, int y, int w, int h, int m) {
  for (int ty = y >> 3; ty <= (y+h-1) >> 3; ty++)
    for (int tx = x >> 3; tx <= (x+w-1) >> 3; tx++) {
      int t = tile(tx, ty);
      if (m >> t & 1) { htx = tx; hty = ty; return t; }
    }
  return 0;
}
static int ov(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
  return ax < bx+bw && bx < ax+aw && ay < by+bh && by < ay+ah;
}
static P *part(int x, int y, int vx, int vy, int l, int g, u32 c) {   // x, y in 1/256 px
  for (P *p = pt; p < pt+NP; p++)
    if (!p->l) { p->x = x; p->y = y; p->vx = vx; p->vy = vy; p->l = p->ml = l; p->g = g; p->c = c; return p; }
  return 0;
}
static void burst(int x, int y, u32 c, int n) {
  while (n--) part(x << 8, y << 8, rnd(640)-320, -rnd(700)-150, 30+rnd(20), 40, c);
}
static void dust(int x, int y, int dir, int n) {   // soft puffs at the feet, x, y in 1/256 px
  while (n--) part(x + (rnd(5)-2)*256, y - 256, dir*(60+rnd(160)) + rnd(60)-30, -40-rnd(110), 16+rnd(14), 3, 0xf4f1e6);
}
#define SPIN(n, d) (spin = spinlen = (n), spind = (d))   // flip animation: length, direction
static void brk(int tx, int ty) { map[ty][tx] = 0; burst(tx*8+4, ty*8+4, 0xd0602a, 6); sfx(4); rumble(3); }
static void die(void) { if (st < DEAD) { st = DEAD; stt = 0; hvy = -900; hvx = 0; deaths++; sfx(5); rumble(7); } }

static void load(void) {
  const u8 *p = LV[lvl];
  for (u8 *m = map[0]; m < map[0]+MH*MW; m++) *m = 0;
  ne = 0;
  hx = p[0] << 11; hy = p[1] << 11;
  for (p += 2;; p += 4) {
    int x = p[0], y = p[1] & 31, t = p[1] >> 5, w = p[2], h = p[3];
    if (!t) { gx = x; gy = y; lw = w; break; }
    if (t == 7) { E *e = en + ne++; e->x = x << 11; e->y = e->h = y << 11; e->vx = -100; e->vy = 0; e->t = w; e->a = 1; continue; }
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) map[y+j][x+i] = t;
  }
  for (gb = gy*8; gb < MH*8 && !scan(gx*8+3, gb, 1, 1, SOLID); gb++);
  hvx = hvy = st = stt = jn = cst = lock = spin = skid = fr = gnd = jbuf = coy = wall = cut = capok = diveok = stall = 0;
  face = lface = 1; landt = 99; coins = lcoins;
  cxf = hx - (W/2 - 3 << 8); cyf = hy - (H*5/8 << 8); look = 0; camgy = hy; sqv = turnt = vang = pgnd = pvy = 0;
}

static void hero(int k, int pr) {
  int dir = (k >> 1 & 1) - (k & 1), D = k >> 3 & 1, g = GRAV, X, Y, t;
  if (st == DEAD) { hvy += GRAV; hy += hvy; if (++stt > 60) load(); return; }
  if (st == WIN) {
    hvx = 0; if (!scan(hx >> 8, (hy >> 8)+1, 6, 11, SOLID)) hy += 256;
    if (++stt == 90) { if (lvl < NLV-1) { lvl++; lcoins = coins; load(); } else done = 1; }
    return;
  }
  if (lock) lock--, dir = 0;
  jbuf = pr & 16 ? 6 : jbuf ? jbuf-1 : 0;
  if (gnd) coy = 0, capok = diveok = stall = 1; else coy++;
  if (landt < 99) landt++;
  if (spin) spin--;
  if (skid) skid--;

  if (st == GPWIND) { hvx = hvy = g = 0; if (++stt > 14) st = GPSLAM; }
  else if (st == GPSLAM) { hvx = g = 0; hvy = 1400; }
  else if (st == GPLAND) {
    hvx = 0;
    if (jbuf) { jbuf = 0; hvy = -1290; st = NORM; SPIN(36, face); cut = 0; gnd = 0; sfx(1); }
    else if (++stt > 12) st = NORM;
  } else if (st == SLIDE) {
    hvx -= hvx > 0 ? 14 : -14;
    if (iabs(hvx) < 100) st = NORM, hvx = 0;
    if (jbuf) { jbuf = 0; st = NORM; hvy = -760; cut = 0; gnd = 0; sfx(1); }
  } else if (st != DIVE) {
    if (gnd) {
      st = NORM;
      if (dir && !D) {
        if (dir*hvx < 0) { if (dir*hvx < -150) skid = 8; hvx += dir*36; }
        else if (dir*hvx < MAXV) hvx += dir*ACC;
        else if (dir*hvx > MAXV+12) hvx -= dir*12;   // extra speed from long jumps and dives eases off
        face = dir;
      } else hvx = hvx > FRIC ? hvx-FRIC : hvx < -FRIC ? hvx+FRIC : 0;
    } else if (st == NORM && dir) { if (dir*hvx < MAXV) hvx += dir*AACC; face = dir; }
    if (jbuf && coy < 6) {
      jbuf = 0; coy = 99; cut = 1; spin = 0; gnd = 0;
      if (D && iabs(hvx) > 150) { st = LONGJ; hvx = face*700; hvy = -560; cut = 0; }
      else if (D) { hvy = -1300; hvx = -face*200; SPIN(40, -face); cut = 0; }                // backflip
      else if (skid && dir) { face = dir; hvx = dir*260; hvy = -1250; SPIN(40, dir); cut = 0; } // side flip
      else {
        jn = landt < 8 && jn < 2 && iabs(hvx) > 200 ? jn+1 : 0;  // double / triple jump chain
        hvy = jn == 2 ? -1240 : jn ? -1030 : -870;
        if (jn == 2) SPIN(40, face);
      }
      sfx(1);
    } else if (jbuf && wall && !gnd) {
      jbuf = 0; hvx = -wall*440; hvy = -900; face = -wall; lock = 7; cut = 1; jn = 0; spin = 0;
      st = NORM; capok = diveok = stall = 1; sfx(1); rumble(1);
    } else if (pr & 8 && !gnd) { st = GPWIND; stt = 0; SPIN(14, face); }
  }
  if (pr & 32) {
    if (D && diveok && st != DIVE && st != SLIDE && st < GPSLAM && (!gnd || iabs(hvx) > 150)) {
      st = DIVE; hvx = face*760; hvy = gnd ? -560 : -420; diveok = 0; gnd = 0; spin = 0; sfx(2);
    } else if (!D && !cst && st <= LONGJ) {
      cst = 1; cxp = hx + face*512 - 256; cyp = hy + 768; cvx = face*1100; ct = 0;
      if (!gnd && stall) { stall = 0; if (hvy > -280) hvy = -280; }
      sfx(2);
    }
  }
  if (st == LONGJ) g = 34;
  if (hvy < 0 && cut && !(k & 16)) g *= 2;                                  // short hop when jump is released
  else if (st == NORM && k & 16 && hvy > -200 && hvy < 200) g = g*5/8;     // a little hang at the top while held
  hvy += g;
  if (st != GPSLAM) {
    int ws = !gnd && wall && wall == dir && hvy > 0 && st == NORM;          // wall slide: ease into it
    if (ws && hvy > 200) hvy = hvy-100 > 200 ? hvy-100 : 200;
    if (hvy > 1100) hvy = 1100;
  }

  // Move on each axis separately and back out of solid tiles pixel by pixel.
  hx += hvx; X = hx >> 8; Y = hy >> 8;
  if (scan(X, Y, 6, 11, SOLID)) {
    int s = hvx > 0 ? -1 : 1;
    do X += s; while (scan(X, Y, 6, 11, SOLID));
    hx = X << 8; hvx = 0;
  }
  int was = gnd, vy0 = hvy; gnd = 0;
  hy += hvy; Y = hy >> 8;
  if (st == GPSLAM) while (scan(X, Y, 6, 12, 4) == 2) brk(htx, hty);        // pound through bricks, also the ones just touched
  if (hvy < 0) while (scan(X, Y, 6, 11, 4) == 2) brk(htx, hty), hvy = 0;   // head-bump bricks
  if (scan(X, Y, 6, 11, SOLID)) {
    int s = hvy > 0 ? -1 : 1;
    do Y += s; while (scan(X, Y, 6, 11, SOLID));
    hy = Y << 8;
    if (hvy > 0) gnd = 1;
    hvy = 0;
  } else if (hvy >= 0 && scan(X, Y+11, 6, 1, SOLID)) gnd = 1, hy = Y << 8, hvy = 0;   // resting on ground
  if (gnd) {
    if (!was) {
      landt = 0;
      if (st == DIVE) st = SLIDE;
      if (st == GPSLAM) { st = GPLAND; stt = 0; shake = 8; sfx(4); rumble(6); }
      else if (vy0 > 900) rumble(2);
      if (st == LONGJ) st = NORM;
    }
    if (scan(X, Y+11, 6, 1, 32)) { hvy = -1500; gnd = 0; st = NORM; SPIN(30, face); cut = 0; sfx(8); rumble(5); }  // spring
  }
  wall = 0;
  if (!gnd) wall = scan(X+6, Y+1, 1, 9, SOLID) ? 1 : scan(X-1, Y+1, 1, 9, SOLID) ? -1 : 0;
  while (scan(X, Y, 6, 11, 64)) { map[hty][htx] = 0; coins++; sfx(7); }
  // Spikes: touching one face counts, but the corners are forgiven (sides along the middle of the
  // body, top and bottom across the middle 4 px), so grazes and one-pixel toe overlaps survive.
  if (scan(X-1, Y+2, 8, 8, 8) || scan(X+1, Y-1, 4, 13, 8) || Y > MH*8+8) die();
  // The pole counts at any height above its base, so jumping over the flag still wins.
  if (X+6 > gx*8+2 && X < gx*8+6 && Y < gb && st < DEAD) { st = WIN; stt = 0; hvx = hvy = 0; hx = gx*8-3 << 8; sfx(6); rumble(8); }   // grab the pole
}

static void capupd(int k) {
  if (!cst) return;
  if (cst == 1) {
    cxp += cvx; cvx -= cvx > 0 ? 56 : -56;
    if (iabs(cvx) < 100 || scan(cxp >> 8, cyp >> 8, 8, 4, SOLID)) cst = 2, ct = 0;
  } else if (cst == 2) {
    if (++ct > (k & 32 ? 80 : 24)) cst = 3;   // holding the button keeps the cap hovering
  } else {
    int dx = hx + 256 - cxp, dy = hy + 768 - cyp;
    cxp += dx > 1000 ? 1000 : dx < -1000 ? -1000 : dx;
    cyp += dy > 1000 ? 1000 : dy < -1000 ? -1000 : dy;
    if (iabs(dx) < 1024 && iabs(dy) < 1024) cst = 0;
  }
  if (cst && cst < 3 && capok && !gnd && st < DEAD && ov(hx >> 8, hy >> 8, 6, 11, cxp >> 8, cyp >> 8, 8, 5)) {
    capok = 0; diveok = 1; hvy = -940; cut = 0; st = NORM; jn = 0; spin = 0; cst = 3;   // cap jump
    burst((cxp >> 8)+4, cyp >> 8, 0xffffff, 5); sfx(3); rumble(1);
  }
}

static void kill(E *e) { e->a = 0; burst((e->x >> 8)+4, (e->y >> 8)+4, 0x9a48d0, 8); sfx(4); rumble(4); }

static void enemies(int k) {
  int X = hx >> 8, Y = hy >> 8;
  for (E *e = en; e < en+ne; e++) {
    if (!e->a) continue;
    int ex, ey;
    if (e->t == 1) {   // walker: patrols its platform
      e->vy += GRAV; e->y += e->vy; ex = e->x >> 8; ey = e->y >> 8;
      if (scan(ex, ey, 8, 8, SOLID)) { do ey--; while (scan(ex, ey, 8, 8, SOLID)); e->y = ey << 8; e->vy = 0; }
      e->x += e->vx; ex = e->x >> 8;
      if (scan(ex, ey, 8, 8, SOLID) || (!e->vy && !scan(e->vx > 0 ? ex+8 : ex-1, ey+8, 1, 1, SOLID)))
        e->x -= e->vx, e->vx = -e->vx;
      if (ey > MH*8) e->a = 0;
    } else {           // flyers: 2 bob vertically, 3 sweep horizontally
      int ph = (fr + (e->h >> 9)) & 127, o = (ph < 64 ? ph : 128-ph) - 32;
      if (e->t == 2) e->y = e->h + o*192; else e->x += (ph < 64 ? 1 : -1)*96;
    }
    ex = e->x >> 8; ey = e->y >> 8;
    if (st < DEAD && ov(X, Y, 6, 11, ex+1, ey+1, 6, 7)) {
      if ((hvy > 0 || st == GPSLAM) && Y+11 < ey+6) {
        kill(e); hvy = k & 16 ? -1000 : -650; st = NORM; cut = 0; capok = diveok = 1; spin = 0;
      } else die();
    }
    if (e->a && cst && cst < 3 && ov(cxp >> 8, cyp >> 8, 8, 5, ex, ey, 8, 8)) kill(e);
  }
}

static void tick(int k) {
  int pr = k & ~prevk;
  prevk = k;
  if (pr & 128 && shm) shm[2] ^= 1;
  if (pr & 64) { if (done) lvl = deaths = lcoins = tim = done = 0; load(); return; }
  fr++;
  if (!done) tim++;
  if (shake) shake--;
  hero(k, pr);
  capupd(k);
  enemies(k);
  for (P *p = pt; p < pt+NP; p++) if (p->l) p->l--, p->x += p->vx, p->y += p->vy, p->vy += p->g;
  if (done && !(fr & 15)) {   // fireworks: a ring of sparks in one colour
    static const u32 FW[5] = { 0xff5050, 0xffd84a, 0x5af07a, 0x60d8ff, 0xff80e0 };
    int x = rnd(W-40)+20 + (cxf >> 8) << 8, y = rnd(50)+12 + (cyf >> 8) << 8;
    u32 c = FW[rnd(5)];
    for (int i = 0; i < 24; i++) part(x, y, SIN[(i*32/3+64) & 255] * (3+rnd(2)), SIN[i*32/3 & 255] * (3+rnd(2)), 40+rnd(20), 6, c);
  }
}

// ---------- drawing ----------
// The world is drawn straight into the window image. Every world pixel is an SC x SC block
// placed with sub-pixel precision, so scrolling and movement are smooth while the art stays
// pixelated. Sprites are sampled per screen pixel, which lets them rotate and squash.
#define SW (W*SC)
#define SH (H*SC)
static int ox, oy;   // camera in screen pixels for this frame
static void blk(int x, int y, int w, int h, u32 c) {   // filled rectangle in screen pixels
  if (x < 0) w += x, x = 0;
  if (y < 0) h += y, y = 0;
  if (x+w > SW) w = SW-x;
  if (y+h > SH) h = SH-y;
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) big[y+j][x+i] = c;
}
static void px(int x, int y, u32 c) { blk(x*SC, y*SC, SC, SC, c); }          // screen-fixed (HUD)
static void wpx(int x, int y, u32 c) { blk(x*SC-ox, y*SC-oy, SC, SC, c); }   // world pixel
static int fdiv(int a, int b) { return a >= 0 ? a/b : -((b-1-a)/b); }        // division rounding down
static void bm(u32 b, int w, int n, int x, int y, int s, u32 c) {   // 1-bit bitmap, n pixels, w wide
  for (int i = 0; i < n; i++)
    if (b >> (n-1-i) & 1)
      for (int j = 0; j < s*s; j++) px(x + i%w*s + j%s, y + i/w*s + j/s, c);
}
static void txt(u32 b, int w, int n, int x, int y, int s, u32 c) { bm(b, w, n, x+1, y+1, s, 0x182030); bm(b, w, n, x, y, s, c); }
static int num(int v, int dig, int x, int y, int s, u32 c) {
  int d = 1, n = 1;
  while (n < dig || v / d >= 10) d *= 10, n++;
  for (; d; d /= 10) txt(FONT[v / d % 10], 3, 15, x, y, s, c), x += 4*s;
  return x;
}
// An 8-wide sprite with its feet at (fx, fy) in 1/256 world px, rotated by ang (256 = one
// turn, clockwise, about its centre) and scaled by sx, sy (256 = 1, about its feet).
static void sprx(const u16 *s, int h, int fx, int fy, int fl, int ang, int sx, int sy, const u32 *pal) {
  int co = SIN[(ang+64) & 255], si = SIN[ang & 255], ux = SC*sx, uy = SC*sy;
  int cx = fx*SC - (ox << 8), cy = fy*SC - (oy << 8) - h*uy/2;   // centre, 1/256 screen px
  int r = (h > 8 ? h : 8) * (ux > uy ? ux : uy) / 256 * 3 / 4 + 2;
  for (int ys = (cy >> 8) - r; ys <= (cy >> 8) + r; ys++) {
    if ((unsigned)ys >= SH) continue;
    for (int xs = (cx >> 8) - r; xs <= (cx >> 8) + r; xs++) {
      if ((unsigned)xs >= SW) continue;
      int dx = (xs << 8) + 128 - cx, dy = (ys << 8) + 128 - cy;
      int u = ((dx*co + dy*si) / ux + 4*256) >> 8, v = ((dy*co - dx*si) / uy + h*128) >> 8;
      if ((unsigned)u > 7 || (unsigned)v >= h) continue;
      int c = s[v] >> (14 - 2*(fl ? 7-u : u)) & 3;
      if (c) big[ys][xs] = capless && v >= capoff && v < capoff+3 && c == 1 ? 0x5a3018 : pal[c-1];
    }
  }
}
static const u32 HPAL[3] = { 0xff7a1c, 0xffcc99, 0x1f4f5f };
static const u32 GPAL[3] = { 0x9a48d0, 0xffffff, 0x301040 };
static const u32 BPAL[3] = { 0xffd23c, 0xe8f4ff, 0x2a2a2a };

static u32 tilepx(int t, int tx, int ty, int u, int v) {
  int up = SOLID >> tile(tx, ty-1) & 1;
  if (t == 1) {
    if (!up && v < 3 && !(v == 2 && (u*5+tx) % 3 == 0)) return v ? 0x4cb83c : 0x8be05a;
    if (!up && v == 3) return 0x6a3a1a;
    return (u*7+v*3+tx*5+ty*11) % 11 ? 0xa8642c : 0x7a4420;
  }
  if (t == 2) {
    if (v == 3 || v == 7 || (v < 3 ? u == 7 : u == 3)) return 0x5a2410;
    return v == 0 || v == 4 ? 0xf09060 : 0xd0602a;
  }
  if (t == 3) {
    int vv = up && !(SOLID >> tile(tx, ty+1) & 1) ? 7-v : v, q = u & 3;
    if (vv == 7) return 0x606870;
    if (vv < (q == 0 || q == 3 ? 4 : 1)) return 0;
    return q < 2 ? 0xe0e6ee : 0x8a94a0;
  }
  if (t == 4) return v == 0 || u == 0 ? 0xc8d0e0 : v == 7 || u == 7 ? 0x586070 : 0x9098a8;
  if (t == 5) {
    if (v < 3) return v ? 0xe8403a : 0xff8a7a;
    return v & 1 ? 0x9aa4b0 : u > 1 && u < 6 ? 0x606a78 : 0;
  }
  int ph = fr >> 3 & 3, hw = ph == 0 ? 3 : ph == 2 ? 0 : 2, rw = v == 1 || v == 6 ? hw-1 : hw, d = iabs(2*u-7);
  if (v < 1 || v > 6 || d > 2*rw+1) return 0;
  return d > 2*rw-2 ? 0xc88a18 : 0xffd84a;
}

// Camera: looks ahead in the direction of travel (eased, so turning does not swing it), and
// vertically keeps the height Hatrick last stood on, only following jumps that leave a band
// around it, like a 2D Mario camera. Everything is in 1/256 px, so it never stalls or steps.
static void camera(void) {
  if (st != DEAD) {
    int want = st == WIN ? 0 : hvx * 24;
    if (want > 48 << 8) want = 48 << 8;
    if (want < -(48 << 8)) want = -(48 << 8);
    look += (want - look) / 40;
    cxf += (hx + (3 << 8) + look - (W/2 << 8) - cxf) / 7;
    if (gnd || st == WIN) camgy = hy;
    int ty = camgy - (H*5/8 << 8);
    if (hy - ty < 36 << 8) ty = hy - (36 << 8);       // keep a high jump in view
    if (hy - ty > 100 << 8) ty = hy - (100 << 8);     // and a fall
    cyf += (ty - cyf) / 9;
  }
  if (cxf > (lw*8-W) << 8) cxf = (lw*8-W) << 8;
  if (cxf < 0) cxf = 0;
  if (cyf > (MH*8-H) << 8) cyf = (MH*8-H) << 8;
  if (cyf < 0) cyf = 0;
}

static void render(void) {
  static int cm[SW], fh[SW], nh[SW];
  camera();
  ox = cxf * SC >> 8;
  oy = (cyf * SC >> 8) + (shake ? (shake & 2 ? 2*SC : -2*SC) : 0);
  // sky, clouds and two layers of hills, each scrolling at its own rate
  int lo = (((MH*8-H) << 8) - cyf) * SC >> 8;     // camera height above the bottom, screen px
  for (int x = 0; x < SW; x++) {
    int m = (x + ox/8) / SC % 200 - 100; cm[x] = m;
    m = (x + ox/4) / SC % 160 - 80; fh[x] = H - 46 + m*m/150;
    m = (x + ox/2) / SC % 112 - 56; nh[x] = H - 26 + m*m/110;
  }
  for (int y = 0; y < SH; y++) {
    u32 sky = (0x4a + 0x75*y/SH) << 16 | (0xa0 + 0x46*y/SH) << 8 | 0xff, *row = big[y];
    int dy = fdiv(y + lo/8, SC) - 26, yf = fdiv(y - lo/3, SC), yn = fdiv(y - lo/2, SC);
    for (int x = 0; x < SW; x++) {
      u32 c = sky;
      int m = cm[x];
      if (m*m/12 + dy*dy*4 < 300 || (m+14)*(m+14)/8 + (dy+5)*(dy+5)*3 < 120) c = 0xffffff;     // clouds
      if (yf > fh[x]) c = 0x9ad6a0;                                                           // far hills
      if (yn > nh[x]) c = yn == nh[x]+1 ? 0x3e8f48 : 0x5cb860;                                 // near hills
      row[x] = c;
    }
  }
  for (int ty = fdiv(oy, 8*SC); ty <= (oy+SH) / (8*SC); ty++)
    for (int tx = fdiv(ox, 8*SC); tx <= (ox+SW) / (8*SC); tx++) {
      int t = tile(tx, ty);
      if (t) for (int v = 0; v < 8; v++) for (int u = 0; u < 8; u++) {
        u32 c = tilepx(t, tx, ty, u, v);
        if (c) wpx(tx*8+u, ty*8+v, c);
      }
    }
  // goal: pole down to the ground and a waving pennant
  for (int y = 0; !scan(gx*8+3, gy*8+y, 1, 1, SOLID) && y < 256; y++) wpx(gx*8+3, gy*8+y, 0xd8dde4), wpx(gx*8+4, gy*8+y, 0xa0a8b4);
  for (int i = 0; i < 9; i++) wpx(gx*8+2+i%3, gy*8-3+i/3, 0xffd84a);
  for (int j = 0; j < 8; j++) for (int i = 0; i < 8-j; i++) wpx(gx*8+2-i, gy*8+1+j/2+(j > 3 ? j-3 : 0)/2+((fr >> 3)+i/3 & 1), 0x2ec85a);
  for (E *e = en; e < en+ne; e++)
    if (e->a) {
      if (e->t == 1) sprx(fr & 8 ? GRUM2 : GRUM1, 8, e->x + (4 << 8), e->y + (8 << 8), e->vx > 0, 0, 256, 256, GPAL);
      else sprx(fr & 4 ? BUZZ2 : BUZZ1, 8, e->x + (4 << 8), e->y + (8 << 8), 0, 0, 256, 256, BPAL);
    }
  capless = 0;
  if (cst) {   // the thrown cap spins: its width follows a cosine
    int w = SIN[(fr*24 + 64) & 255];
    sprx(CAP, 4, cxp + (4 << 8), cyp + (4 << 8), w < 0, 0, iabs(w) < 48 ? 48 : iabs(w), 256, HPAL);
  }
  capless = cst != 0;
  {
    const u16 *f = HJUMP; int fl = face < 0, ang = 0, tang = 0, sx = 256, sy, bob = 0, fx = hx + (3 << 8), fy = hy + (11 << 8);
    if (face != lface) turnt = 4, lface = face;
    if (st < DEAD) {
      if (gnd && !pgnd) {                                  // landing: squash, dust on hard landings
        sqv = -(pvy > 1400 ? 1400 : pvy) / 10;
        if (pvy > 500) dust(fx, fy, -1, 3 + pvy/400), dust(fx, fy, 1, 3 + pvy/400);
      }
      if (!gnd && pgnd && hvy < -300) sqv = 56, dust(fx, fy, 0, 2);   // takeoff: stretch
    }
    pgnd = gnd; pvy = hvy;
    capoff = 0;
    if (st == DEAD) ang = 128;
    else if (st == DIVE || st == SLIDE) {
      tang = face*64;
      if (st == SLIDE && !(fr & 3)) dust(fx - face*(3 << 8), fy, -face, 1);
    } else if (st == LONGJ) tang = face*36;
    else if (st == GPSLAM || st == GPLAND || (gnd && prevk & 8 && st == NORM)) f = HCROUCH, capoff = 3;
    else if (gnd) {
      if (iabs(hvx) < 50) f = HSTAND, runph = 0;
      else {   // the run cycle advances with distance, so it never looks like skating
        int ph = (runph += iabs(hvx)) / 2600 & 1;
        f = ph ? HRUN2 : HRUN1; bob = ph ? -128 : 0;
      }
      if (skid) { tang = -face*24; if (fr & 1) dust(fx, fy, hvx > 0 ? 1 : -1, 1); }
    } else if (wall && hvy > 0) {
      fl = wall > 0;
      if (hvy <= 200 && !(fr & 3)) dust(fx + wall*(3 << 8), fy - (4 << 8), -wall, 1);
    }
    vang += (tang - vang) / 3;
    if (st != DEAD) {
      ang = vang;
      if (spin) {   // flips: one smooth turn, eased in and out
        int t = (spinlen - spin) * 256 / spinlen;
        ang += spind * (t*t*(768 - 2*t) >> 16);
      }
    }
    if (turnt) { if (!spin && st < DIVE) sx = sx * (256 - turnt*44) >> 8; turnt--; }   // quick turn, not mid-flip
    sy = 256 + sqv; sx = sx * (256 - sqv/2) >> 8; sqv = sqv * 13 / 16;
    sprx(f, 12, fx, fy + bob + iabs(vang) * 512 / 64, fl, ang, sx, sy, HPAL);
  }
  {   // iris wipe: opens on Hatrick after a (re)start, closes before a restart or the next level
    int t = fr < 24 ? fr*256/24 : st == DEAD && stt > 36 ? (60-stt)*256/24 : st == WIN && stt > 66 && !(lvl == NLV-1) ? (90-stt)*256/24 : 256;
    if (t < 256) {
      int cx = (hx*SC >> 8) - ox + 3*SC, cy = (hy*SC >> 8) - oy + 6*SC, r = t*t/256 * (SW*5/4) / 256;
      for (int y = 0; y < SH; y++) for (int x = 0; x < SW; x++)
        if ((x-cx)*(x-cx) + (y-cy)*(y-cy) > r*r) big[y][x] = 0x0c0e18;
    }
  }
  for (P *p = pt; p < pt+NP; p++)
    if (p->l) {
      int z = p->l*3 > p->ml ? 2*SC : SC;   // particles shrink as they fade
      blk((p->x*SC >> 8) - ox - z/2, (p->y*SC >> 8) - oy - z/2, z, z, p->c);
    }
  // HUD: level, deaths, coins, run time
  txt(I_FLAG, 5, 25, 4, 4, 1, 0xffffff); num(lvl+1, 1, 11, 4, 1, 0xffffff);
  txt(I_SKULL, 5, 25, 24, 4, 1, 0xffffff); num(deaths, 1, 31, 4, 1, 0xffffff);
  txt(I_COIN, 5, 25, 60, 4, 1, 0xffd84a); num(coins, 1, 67, 4, 1, 0xffffff);
  {
    int s = done ? 2 : 1, x = done ? 84 : W-48, yy = done ? 60 : 4;
    u32 c = done ? 0xffd84a : 0xffffff;
    x = num(tim/3600, 1, x, yy, s, c);
    txt(FONT[10], 3, 15, x, yy, s, c);
    x = num(tim/60 % 60, 2, x+4*s, yy, s, c);
    txt(FONT[11], 3, 15, x, yy, s, c);
    num(tim % 60 * 100 / 60, 2, x+4*s, yy, s, c);
  }
}

#ifndef SIM
static int sc(int n, int a, int b, int c) {
  int r;
  asm volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
  return r;
}

// ---------- audio: a forked synth process piped into aplay ----------
#define HZ(f) ((int)((f)*97391.548))
static const int SFX[9][3] = {   // start phase step, step change per sample, length (negative: noise)
  {0}, {HZ(330), (HZ(700)-HZ(330))/4400, 4400}, {HZ(1100), (HZ(500)-HZ(1100))/3500, 3500},
  {HZ(600), (HZ(1500)-HZ(600))/5000, 5000}, {HZ(300), 0, -4000}, {HZ(700), (HZ(80)-HZ(700))/26000, 26000},
  {HZ(500), (HZ(1600)-HZ(500))/22000, 22000}, {HZ(1500), (HZ(2100)-HZ(1500))/3000, 3000},
  {HZ(250), (HZ(1300)-HZ(250))/9000, 9000},
};
// Original tune: 16 bars of sixteenth notes. 0 holds, 1 rests, otherwise a MIDI note.
static const u8 MEL[256] = {
  67,0,0,72,0,0,76,0, 79,0,76,0,72,0,74,0,   76,0,0,72,0,0,69,0, 72,0,0,0,1,0,71,72,
  77,0,0,76,0,0,74,0, 72,0,69,0,72,0,77,0,   79,0,0,0,77,0,76,0, 74,0,0,0,1,0,67,0,
  72,0,0,76,0,0,79,0, 84,0,83,0,79,0,76,0,   81,0,0,79,0,0,76,0, 72,0,0,0,74,0,76,0,
  77,0,74,0,77,0,81,0, 79,0,77,0,74,0,71,0,  72,0,0,67,0,0,72,0, 72,0,1,0,0,0,0,0,
  81,0,79,0,77,0,0,76, 0,0,77,0,81,0,0,0,    83,0,81,0,79,0,0,77, 0,0,79,0,83,0,0,0,
  84,0,0,83,0,0,79,0, 76,0,0,0,79,0,83,0,    84,0,0,0,81,0,0,0, 76,0,0,0,1,0,0,0,
  77,0,81,0,86,0,84,0, 81,0,77,0,74,0,77,0,  79,0,83,0,86,0,84,0, 83,0,79,0,74,0,71,0,
  72,0,76,0,79,0,84,0, 88,0,0,0,84,0,0,0,    86,0,0,0,83,0,0,0, 79,0,74,0,71,0,74,0,
};
static const u8 BASS[32] = { 48,48,45,45,41,41,43,43,48,48,45,45,50,43,48,48,
                             41,41,43,43,40,40,45,45,50,50,43,43,48,48,43,43 };
static const signed char BPAT[16] = { 0,-1,-1,12,-1,-1,7,-1, 0,-1,-1,12,-1,-1,7,-1 };   // 3+3+2 bounce
static u32 note(int m) { return NOTE[m % 12] >> (8 - m/12); }

static void synth(int fd) {
  static u8 buf[512];
  u32 mp = 0, minc = 0, bp = 0, binc = 0, kp = 0, sp = 0, nz = 1;
  int menv = 0, ss = 0, step = 0, kick = 0, snare = 0, hat = 0, sinc = 0, sd = 0, sl = 0, sn = 0;
  u8 last = 0;
  for (;;) {
    if (shm[0] != last) {
      const int *f = SFX[shm[1]];
      last = shm[0]; sinc = f[0]; sd = f[1]; sl = iabs(f[2]); sn = f[2] < 0;
    }
    for (int i = 0; i < 512; i++) {
      if (--ss <= 0) {
        int m = MEL[step & 255], q = step & 15;
        ss = 4594;   // 144 bpm sixteenths
        if (m > 1) minc = note(m), menv = 4000; else if (m) menv = 0;
        if (BPAT[q] >= 0) binc = note(BASS[step >> 3 & 31] + BPAT[q]);
        if (!(q & 7)) kick = 3000;
        if ((q & 7) == 4) snare = 2400; else if (!(q & 1)) hat = 500;
        step++;
      }
      int s = 0, t;
      nz = nz*1103515245 + 12345;
      if (menv) { mp += minc; s += mp >> 30 ? -(menv >> 7) : menv >> 7; if (menv > 1400) menv--; }
      bp += binc; t = bp >> 24; s += ((t < 128 ? t : 255-t) - 64) >> 2;
      if (kick) { kick--; kp += kick*5000; s += kp >> 31 ? -(kick >> 7) : kick >> 7; }
      if (snare) { snare--; s += ((int)(nz >> 28) - 8) * (snare >> 8) / 4; }
      if (hat) { hat--; s += nz >> 31 ? 3 : -3; }
      if (sl) { sl--; sp += sinc; sinc += sd; s += (sn ? nz : sp) >> 31 ? -22 : 22; }
      buf[i] = shm[2] ? 128 : 128 + s;
    }
    if (sc(4, fd, (int)buf, 512) <= 0) sc(1, 0, 0, 0);
  }
}

static void audio(char **envp) {
  static const int mm[6] = { 0, 4096, 3, 0x21, -1, 0 };   // shared anonymous page
  static char *av[] = { "aplay", "-q", "-traw", "-fU8", "-r44100", "-B40000", 0 };
  int fd[2];
  shm = (u8 *)sc(90, (int)mm, 0, 0);
  if (sc(2, 0, 0, 0)) return;          // the game continues in the parent
  sc(172, 1, 9, 0);                    // die with the game (PR_SET_PDEATHSIG, SIGKILL)
  sc(42, (int)fd, 0, 0);
  if (!sc(2, 0, 0, 0)) {               // grandchild: aplay reads the pipe
    sc(63, fd[0], 0, 0);
    sc(11, (int)"/usr/bin/aplay", (int)av, (int)envp);
    sc(1, 0, 0, 0);
  }
  sc(55, fd[1], 1031, 4096);           // F_SETPIPE_SZ: keep latency low
  synth(fd[1]);
}

// ---------- gamepads: every evdev gamepad is read directly, Super Mario Odyssey layout ----------
// A/B jump, X/Y cap, LT/RT (or LB/RB) crouch / ground pound, left stick or D-pad move,
// Menu restarts, View mutes. Pads are rescanned every 2 s, so hotplugging works, and pads
// with force feedback get rumble effects uploaded (played from rumble()).
static const short PADB[] = { 304, 305, 307, 308, 310, 311, 312, 313, 314, 315, 546, 547 };
static const u8 PADK[]    = { 16,  16,  32,  32,  8,   8,   8,   8,   128, 64,  1,   2   };
static struct { int fd, num, held, x, hx, t1, t2, mid, dz, tq, c1, c2, fx[8]; } pad[4];   // fd is stored +1, 0 = free slot
// Rumble effects 1..8 (see rumble()): strong motor, weak motor, length in ms.
static const unsigned short RUM[8][3] = {
  { 0x0000, 0x4800,  50 }, { 0x3000, 0x3800,  70 }, { 0x5000, 0x3000,  80 }, { 0x6800, 0x5000, 100 },
  { 0x6000, 0x7000, 150 }, { 0xb800, 0x9000, 180 }, { 0xffff, 0xc000, 380 }, { 0x5000, 0x9000, 450 },
};

static void padscan(void) {
  static char path[] = "/dev/input/event\0\0";
  static u32 nopad;   // devices known not to be gamepads: opening some (audio jacks) takes ~10 ms
  for (int i = 0; i < 32; i++) {
    int free = -1, fd, ai[6];
    u8 b[64];
    for (int j = 0; j < 4; j++) { if (pad[j].fd && pad[j].num == i) free = -2; if (!pad[j].fd && free == -1) free = j; }
    if (free < 0) continue;
    path[16] = i < 10 ? '0'+i : '0'+i/10; path[17] = i < 10 ? 0 : '0'+i%10;
    if (nopad >> i & 1) { if (sc(33, (int)path, 0, 0) < 0) nopad &= ~(1u << i); continue; }   // gone: recheck if reused
    if ((fd = sc(5, (int)path, 0x802, 0)) < 0 && (fd = sc(5, (int)path, 0x800, 0)) < 0) continue;   // O_RDWR (rumble), else O_RDONLY; O_NONBLOCK
    if (sc(54, fd, 0x80404521, (int)b) > 38 && b[38] & 1 && sc(54, fd, 0x80184540, (int)ai) >= 0) {   // EVIOCGBIT(EV_KEY): BTN_SOUTH; EVIOCGABS(ABS_X)
      typeof(pad[0]) *p = pad + free;
      p->fd = fd+1; p->num = i; p->held = p->hx = p->t1 = p->t2 = 0; p->x = ai[0];
      p->mid = (ai[1]+ai[2]) / 2; p->dz = (ai[2]-ai[1]) / 6;
      sc(54, fd, 0x80084523, (int)b);                     // EVIOCGBIT(EV_ABS): Bluetooth pads put the triggers on ABS_BRAKE / ABS_GAS
      p->c1 = b[1] & 2 ? 10 : 2; p->c2 = b[1] & 2 ? 9 : 5;
      p->tq = sc(54, fd, 0x80184540 + p->c1, (int)ai) < 0 ? 64 : ai[2] / 4;
      for (int j = 0; j < 8; j++) p->fx[j] = -1;
      // Rumble only on physical pads. Effect uploads to a virtual (uinput) pad, e.g. Steam's,
      // wait up to 30 s each for the program behind it, and lock the device for everyone else
      // (logind included) meanwhile, so a stalled program could freeze the desktop session.
      static char sys[] = "/sys/class/input/event\0\0";
      char lk[128] = { 0 };
      sys[22] = path[16]; sys[23] = path[17];
      int n = sc(85, (int)sys, (int)lk, 127), virt = 0;   // readlink: ../../devices/virtual/input/... for uinput
      for (int c = 0; c + 7 < n; c++) if (lk[c] == 'v' && lk[c+1] == 'i' && lk[c+2] == 'r' && lk[c+3] == 't') virt = 1;
      if (n > 0 && !virt && sc(54, fd, 0x80104535, (int)b) > 10 && b[10] & 1)   // EVIOCGBIT(EV_FF): FF_RUMBLE
        for (int j = 0; j < 8; j++) {
          unsigned short e[22] = { 0x50, 0xffff, 0, 0, 0, RUM[j][2], 0, 0, RUM[j][0], RUM[j][1] };   // struct ff_effect, id -1 = new
          if (sc(54, fd, 0x402c4580, (int)e) >= 0) p->fx[j] = (short)e[1];                           // EVIOCSFF
        }
    } else sc(6, fd, 0, 0), nopad |= 1u << i;
  }
}

static void padrumble(int k) {   // play effect k (1..8) on every pad that has rumble
  for (int j = 0; j < 4; j++)
    if (pad[j].fd && pad[j].fx[k-1] >= 0) {
      int ev[4] = { 0, 0, 0x15 | pad[j].fx[k-1] << 16, 1 };   // struct input_event: EV_FF, effect id, play once
      sc(4, pad[j].fd-1, (int)ev, 16);
    }
}

static int padkeys(void) {
  static int wait;
  int k = 0, n, ev[64];
  if (--wait < 0) wait = 120, padscan();
  for (int j = 0; j < 4; j++) {
    typeof(pad[0]) *p = pad + j;
    if (!p->fd) continue;
    while ((n = sc(3, p->fd-1, (int)ev, sizeof ev)) > 0)
      for (int *e = ev; e < ev + n/4; e += 4) {           // struct input_event: time, u16 type, u16 code, s32 value
        int type = e[2] & 0xffff, code = e[2] >> 16, v = e[3];
        if (type == 1) for (int i = 0; i < 12; i++) { if (PADB[i] == code) p->held = v ? p->held | 1 << i : p->held & ~(1 << i); }
        if (type == 3) {
          if (code == 0) p->x = v;
          if (code == 16) p->hx = v;
          if (code == p->c1) p->t1 = v;
          if (code == p->c2) p->t2 = v;
        }
      }
    if (n != -11) { sc(6, p->fd-1, 0, 0); p->fd = 0; continue; }  // anything but EAGAIN: unplugged
    for (int i = 0; i < 12; i++) if (p->held >> i & 1) k |= PADK[i];
    if (p->x < p->mid - p->dz || p->hx < 0) k |= 1;
    if (p->x > p->mid + p->dz || p->hx > 0) k |= 2;
    if (p->t1 > p->tq || p->t2 > p->tq) k |= 8;
  }
  return k;
}

// The stack gets envp for aplay and stays 16-byte aligned as the ABI expects.
asm(".globl _start\n_start: mov (%esp),%eax\n lea 8(%esp,%eax,4),%eax\n sub $12,%esp\n push %eax\n call run");

__attribute__((noreturn)) void run(char **envp) {
  audio(envp);
  Display *d = XOpenDisplay(0);
  Window w = XCreateSimpleWindow(d, RootWindow(d, 0), 0, 0, W*SC, H*SC, 0, 0, 0);
  XStoreName(d, w, "Hatrick");
  XMapWindow(d, w);
  XImage *im = XCreateImage(d, DefaultVisual(d, 0), 24, ZPixmap, 0, (char *)big, W*SC, H*SC, 32, 0);
  struct { int s, n; } t;
  char km[32];
  load();
  sc(265, 1, (int)&t, 0);              // clock_gettime(CLOCK_MONOTONIC)
  for (;;) {
    XQueryKeymap(d, km);
#define K(c) (km[c >> 3] >> (c & 7) & 1)
    if (K(9)) sc(1, 0, 0, 0);
    tick(padkeys() | K(113) | K(114) << 1 | K(111) << 2 | K(116) << 3 | (K(52) | K(65)) << 4 | K(53) << 5 | K(27) << 6 | K(58) << 7);
    if (rumq) padrumble(rumq), rumq = 0;
    render();
    XPutImage(d, w, DefaultGC(d, 0), im, 0, 0, 0, 0, W*SC, H*SC);
    if ((t.n += 16666667) >= 1000000000) t.n -= 1000000000, t.s++;
    sc(267, 1, 1, (int)&t);            // clock_nanosleep until the next frame
  }
}
#else
// ---------- headless simulator: replays scripted input, reports deaths / goal ----------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
  // usage: sim LEVEL TASFILE [trace] [dumpframe out.ppm]
  lvl = atoi(argv[1]);
  load();
  FILE *f = fopen(argv[2], "r");
  int trace = argc > 3 && !strcmp(argv[3], "trace"), dump = argc > 4 ? atoi(argv[4]) : -1, n, frame = 0;
  char b[32];
  while (fscanf(f, "%d %31s", &n, b) == 2) {
    int k = 0;
    for (char *c = b; *c; c++) k |= *c=='L' ? 1 : *c=='R' ? 2 : *c=='U' ? 4 : *c=='D' ? 8 : *c=='J' ? 16 : *c=='C' ? 32 : 0;
    while (n--) {
      tick(k); frame++;
      if (rumq && getenv("RUMLOG")) printf("RUMBLE %d %d\n", frame, rumq);
      rumq = 0;
      if (trace) {
        printf("%d %s x=%.1f y=%.1f vx=%d vy=%d st=%d gnd=%d cap=%d", frame, b, hx/256., hy/256., hvx, hvy, st, gnd, cst);
        for (E *e = en; e < en+ne; e++) if (e->a && iabs(e->x-hx) < 160<<8) printf(" e=%d,%d", e->x >> 8, e->y >> 8);
        printf("\n");
      }
      if (dump >= 0) render();   // every frame, so the camera follows as in the game
      if (dump >= 0 && getenv("CAMLOG")) printf("CAM %d %d %d %d %d\n", frame, ox, oy, (hx*SC >> 8) - ox, (hy*SC >> 8) - oy);
      if (frame == dump) {
        printf("HERO %d %d\n", (hx*SC >> 8) - ox, (hy*SC >> 8) - oy);
        FILE *o = fopen(argv[5], "wb"); fprintf(o, "P6 %d %d 255\n", W*SC, H*SC);
        for (int i = 0; i < W*SC*H*SC; i++) { u32 c = big[0][i]; fputc(c >> 16, o); fputc(c >> 8 & 255, o); fputc(c & 255, o); }
        fclose(o);
      }
      if (st == DEAD && stt == 0) { printf("DIED frame %d at x=%d y=%d (tile %d,%d)\n", frame, hx >> 8, hy >> 8, hx >> 11, hy >> 11); return 1; }
      if (st == WIN && stt == 1) { printf("GOAL frame %d (%.2fs) coins %d\n", frame, frame/60., coins); return 0; }
    }
  }
  printf("END frame %d at x=%d y=%d (tile %d,%d) st=%d\n", frame, hx >> 8, hy >> 8, hx >> 11, hy >> 11, st);
  return 2;
}
#endif
