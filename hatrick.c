// Hatrick: a tiny cap-throwing platformer for Linux/X11 (i386, no libc).
// Arrows move, Z/Y or Space jumps, X/C throws the cap, Down crouches / ground pounds.
// Down + cap rolls on the ground; Down + X dives in the air. Up + jump spins,
// Up + cap throws upward, Down + C throws downward in the air.
// Gamepads use the Super Mario Odyssey layout (see padkeys).
// F1 opens the movement playground. Up spins on the ground; C recalls an out cap.
// R restarts the level, M mutes, Esc opens the destination menu / resumes.
// Enter or Jump chooses a destination; Q quits. Controller Start opens the menu.
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
#define SOLID 0x33E      // full tiles 1..5 and triangular slopes 8..9 block movement
// Tiles: 1 ground, 2 brick, 3 spikes, 4 stone, 5 spring, 6 coin, 8/9 slopes.
// Physics is fixed point, 1/256 px, 60 steps per second.
#define GRAV 48
#define MAXV 400
#define ACC 18
#define AACC 14
#define FRIC (MAXV/10)  // Odyssey NormalBrakeFrame: 10; scaled to this game's run speed
#define CAPSTALL 8       // first air throw pauses vertical motion for 8 frames
#define CAP2 256         // keep the two cap buttons distinct until press edges are read
#define ANALOG 512       // signed stick axis, biased by 256, in bits 10..19
#define ROLLSTART (MAXV*20/14)
#define ROLLMAX (MAXV*35/14)
#define ROLLBOOST (MAXV*6/14)
#define TRIPLE_V 1220
#define CAPBOUNCE_V 768
#define CAPVAULT_V (CAPBOUNCE_V*32/26)
#define PRACTICE (1<<20)
#define BACK (1<<21)
#define START (1<<22)
#define QUIT (1<<23)
#define MENUBACK (1<<24)
#define MENUN (NLV+1)
#define GPJUMP_V 1400
enum { NORM, LONGJ, GPWIND, GPSLAM, GPLAND, DIVE, SLIDE, ROLL, SPINJ, GSPIN, HANG, CLIMB, DEAD, WIN };
enum { CAPFORWARD, CAPUP, CAPDOWN, CAPSPIN };

static u8 map[MH][MW];
static u32 big[H*SC][W*SC];   // the window image; the world is drawn straight into it at sub-pixel positions
static int lw, gx, gy, gb;   // level width, flag column / top row, pixel row where the pole meets the ground
static int hx, hy, hvx, hvy, face, st, stt, gnd, jn, landt, capok, diveok, stall, wall, coy, jbuf, lock, spin, cut, skid;
static int cst, cxp, cyp, cvx, cvy, ct, cready, ckind, throwt, oldhy;
static int duck, catcht, catchok, twirl, gpspin, rollbuf;
static int arcg, runt, rundir, launch, boostt, capbuf, capkeys, capextend, capreflect;
static int ledget, climbx, climby, slopedir, poundt;
static int lvl, deaths, coins, lcoins, tim, shake, done, prevk, fr, capless, capoff;
static int menu, menusel, menufr, menunav, menurepeat, resumable, quitting;
// Presentation only (never read by the game logic): camera, flips, squash and stretch.
static int cxf, cyf, look, camgy;          // camera position and look-ahead in 1/256 px, last standing height
static int spinlen, spind, sqv, turnt, lface, runph, vang, pgnd, pvy, dustt, rollph;
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
static int brake(int v, int amount) { return v > amount ? v-amount : v < -amount ? v+amount : 0; }
// Keyboard / D-pad takes priority; otherwise use the full stick range after its dead zone.
static int moveaxis(int k) {
  return k & 3 ? ((k >> 1 & 1) - (k & 1))*256 : k & ANALOG ? ((k >> 10) & 1023)-256 : 0;
}
static int stickaxis(int x, int mid, int range, int dz) {
  int v = x-mid, a = iabs(v);
  if (a <= dz || range <= dz) return 0;
  a = (a-dz)*256/(range-dz);
  if (a > 256) a = 256;
  return v < 0 ? -a : a;
}
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
      if (m >> t & 1) {
        if (t == 8 || t == 9) {
          int a = x > tx*8 ? x-tx*8 : 0, b = x+w-1 < tx*8+7 ? x+w-1-tx*8 : 7;
          int bottom = y+h-1 < ty*8+7 ? y+h-1-ty*8 : 7;
          if (bottom < (t == 8 ? 7-b : a)) continue;
        }
        htx = tx; hty = ty; return t;
      }
    }
  return 0;
}
static int ov(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
  return ax < bx+bw && bx < ax+aw && ay < by+bh && by < ay+ah;
}
// Surface under the whole foot: ramps have the same occupied pixels as their art.
static int floorat(int x, int feet, int *slope) {
  int best = MH*8+99; *slope = 0;
  for (int px = x; px < x+6; px++) for (int ty = (feet-4)>>3; ty <= (feet+5)>>3; ty++) {
    int t = tile(px>>3, ty), y = ty*8;
    if (!(SOLID >> t & 1)) continue;
    if (t == 8 || t == 9) y += t == 8 ? 7-(px&7) : px&7;
    if (y >= feet-4 && y <= feet+5 && y < best) { best = y; *slope = t == 8 ? -1 : t == 9 ? 1 : 0; }
  }
  return best;
}
static int freemove(void) { return st == NORM || st == LONGJ || st == SPINJ; }
// hy always keeps the standing body's top, so shrinking leaves the feet in place.
static void posture(int want) {
  if (want) duck = want;
  else if (!scan(hx >> 8, hy >> 8, 6, 11, SOLID)) duck = 0;
}
static void roll(int speed) {
  if (face*hvx < speed) hvx = face*speed;
  st = ROLL; stt = 0; boostt = 15; arcg = GRAV; posture(5); spin = throwt = 0; cut = 0; rollbuf = capbuf = poundt = 0;
}
static void longjump(void) {
  st = LONGJ; if (face*hvx < 700) hvx = face*700;
  hvy = -560; posture(0); cut = spin = 0; gnd = 0; coy = 99; jn = -1; launch = 0;
}
static void capthrow(int k, int downthrow, int rolling, int takeoff) {
  ckind = k & 4 ? CAPUP : downthrow || st == GPLAND ? CAPDOWN : st == SPINJ || st == GSPIN ? CAPSPIN : CAPFORWARD;
  cst = 1; ct = cready = capextend = capreflect = 0; cvx = cvy = 0;
  cxp = hx - 256 + face*(9 << 8); cyp = hy + (duck+3)*256;
  if (ckind == CAPUP) cxp = hx + 256, cyp = hy + (duck-6)*256, cvy = -1100;
  else if (downthrow) cxp = hx + 256, cyp = hy + (13 << 8), cvy = 1100;
  else if (ckind != CAPSPIN) cvx = face*1100;
  if ((ckind == CAPDOWN && gnd) || rolling) cyp = hy + (7 << 8);
  capreflect = rolling;
  for (int i = 0; i < 16 && scan(cxp >> 8, cyp >> 8, 8, 4, SOLID); i++) {
    if (cvy) cyp -= cvy > 0 ? 256 : -256;
    else cxp -= face*256;
  }
  if (scan(cxp >> 8, cyp >> 8, 8, 4, SOLID)) cst = 3;
  if (!gnd && st != SPINJ) {
    st = NORM; spin = cut = 0;
    // Preserve an actual jump launch; a same-frame throw must not pin it to the floor.
    if (stall && !takeoff) stall = 0, throwt = CAPSTALL;
  }
  sfx(2);
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
    if (t == 7 && !h) { map[y][x] = w; continue; }
    if (t == 7) { E *e = en + ne++; e->x = x << 11; e->y = e->h = y << 11; e->vx = -100; e->vy = 0; e->t = w; e->a = 1; continue; }
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) map[y+j][x+i] = t;
  }
  for (gb = gy*8; gb < MH*8 && !scan(gx*8+3, gb, 1, 1, SOLID); gb++);
  hvx = hvy = st = stt = jn = cst = lock = spin = skid = fr = gnd = jbuf = coy = wall = cut = capok = diveok = stall = cready = throwt = 0;
  duck = catcht = catchok = twirl = gpspin = rollbuf = cvy = ckind = 0;
  arcg = GRAV; runt = rundir = launch = boostt = capbuf = capkeys = capextend = capreflect = 0;
  ledget = climbx = climby = slopedir = poundt = 0;
  jn = -1; face = lface = 1; landt = 99; coins = lcoins;
  cxf = hx - (W/2 - 3 << 8); cyf = hy - (H*5/8 << 8); look = 0; camgy = hy; sqv = turnt = vang = pgnd = pvy = rollph = 0;
}

static void hero(int k, int pr) {
  int axis = moveaxis(k), dir = axis > 0 ? 1 : axis < 0 ? -1 : 0;
  int target = iabs(axis)*MAXV/256, D = k >> 3 & 1, U = k >> 2 & 1, g = GRAV, X, Y;
  int takeoff = 0, rollcancel = st == ROLL && !D;
  int cappress = pr & 32, downthrow = D && (pr & CAP2) && !(prevk & 32);
  if (st == DEAD) { hvy += GRAV; hy += hvy; if (++stt > 60) load(); return; }
  if (st == WIN) {
    hvx = 0; if (!scan(hx >> 8, (hy >> 8)+1, 6, 11, SOLID)) hy += 256;
    if (++stt == 90) { if (lvl >= NLV) load(); else if (lvl < NLV-1) { lvl++; lcoins = coins; load(); } else done = 1; }
    return;
  }
  if (ledget) ledget--;
  if (st == HANG) {
    hvx = hvy = 0;
    if (pr & 8 || dir == -face) { st = NORM; ledget = 15; coy = 99; pr &= ~8; }
    else if (pr & (4|16)) {
      if (!scan(climbx>>8, climby>>8, 6, 11, SOLID)) st = CLIMB, stt = 0;
    }
    if (st == HANG) return;
  }
  if (st == CLIMB) {
    hvx = hvy = 0;
    hx += (climbx-hx)/(12-stt); hy += (climby-hy)/(12-stt);
    if (++stt == 12) { hx = climbx; hy = climby; st = NORM; gnd = 1; coy = landt = 0; }
    return;
  }
  if (lock) lock--, dir = 0;
  if (runt) runt--;
  if (launch) launch--;
  if (boostt) boostt--;
  if (capbuf) capbuf--;
  if (poundt) poundt--;
  if (gnd && !D && dir && dir*hvx >= 150) runt = 10, rundir = dir;
  if (cappress && (!D || downthrow)) {
    if (!cst || (cst == 3 && !(pr & CAP2))) capbuf = 10, capkeys = k & (4|8|CAP2);
  }
  if (gnd && pr & 4 && !(pr & 16) && (st == NORM || st == GSPIN)) st = GSPIN, stt = 0;
  if (!gnd && launch && pr & 8 && !(k & 32) && (dir || runt)) {
    if (dir) face = dir; else face = rundir;
    longjump(); takeoff = 1;
  }
  jbuf = pr & 16 ? 6 : jbuf ? jbuf-1 : 0;
  if (gnd) arcg = GRAV, coy = 0, capok = diveok = stall = catchok = 1, throwt = twirl = 0; else coy++;
  if (landt < 99) landt++;
  if (spin) spin--;
  if (skid) skid--;
  if (catcht) catcht--;
  if (twirl) twirl--;
  if (rollbuf) rollbuf--;
  if ((st == GPWIND || st == GPSLAM || st == GPLAND) && D && cappress) rollbuf = 6;

  if (st == GPWIND) { posture(4); hvx = hvy = g = 0; if (++stt > 14) st = GPSLAM; }
  else if (st == GPSLAM) { hvx = g = 0; hvy = 1400; }
  else if (st == GPLAND) {
    hvx = 0; stt++;
    if (rollbuf) roll(gpspin ? MAXV*30/14 : ROLLSTART);
    else if (jbuf && stt >= 5) {
      jbuf = 0; hvy = -GPJUMP_V; st = NORM; posture(0); SPIN(40, face);
      cut = 0; gnd = 0; coy = 99; jn = -1; launch = 0; poundt = 0; takeoff = 1; sfx(1);
    } else if (stt >= 30 || (stt >= 24 && dir)) st = NORM;
  } else if (st == ROLL) {
    posture(5);
    if (D && cappress && !boostt) {
      boostt = 15; int speed = iabs(hvx)+ROLLBOOST;
      hvx = face*(speed > ROLLMAX ? ROLLMAX : speed); sfx(2);
    }
    if (gnd) hvx = D ? hvx*998/1000 : brake(hvx, FRIC);
    if (jbuf && coy < 6) {
      jbuf = 0;
      if (rollcancel && cappress) { st = NORM; hvy = -870; posture(0); gnd = 0; coy = 99; cut = 1; jn = 0; launch = 6; }
      else longjump();
      takeoff = 1; sfx(1);
    } else if (!D) st = NORM, posture(0);
  } else if (st == GSPIN) {
    posture(0); hvx = hvx*95/100;
    target = iabs(hvx) > MAXV*8/14 ? iabs(hvx) : MAXV*8/14;
    if (dir) { hvx += axis*AACC/256; if (iabs(hvx) > target) hvx = (hvx > 0 ? 1 : -1)*target; face = dir; }
    if (jbuf && coy < 6) { jbuf = 0; st = SPINJ; hvy = -560; cut = launch = 0; gnd = 0; coy = 99; jn = -1; takeoff = 1; }
    else if (++stt >= 90 || D || !gnd) st = NORM;
  } else if (st == SLIDE) {
    posture(5);
    if (gnd && D) roll(iabs(hvx) > ROLLSTART ? iabs(hvx) : ROLLSTART);
    else {
      hvx = brake(hvx, FRIC);
      if (iabs(hvx) < 100) st = NORM, hvx = 0, posture(0);
      else if (gnd && dir) { st = NORM; posture(0); } // directional input regains ground control
      if (jbuf && gnd) { jbuf = 0; st = NORM; hvy = -760; posture(0); cut = 0; gnd = 0; coy = 99; sfx(1); }
    }
  } else if (st != DIVE) {
    posture(gnd && D ? 4 : 0);
    if (gnd) {
      st = NORM; gpspin = 0;
      if (D) target = iabs(axis)*128/256;
      if (dir) {
        if (dir*hvx < 0) { if (!D && dir*hvx < -150) skid = 8; hvx += dir*FRIC; }
        else if (dir*hvx < target) { hvx += dir*ACC; if (dir*hvx > target) hvx = dir*target; }
        else if (dir*hvx > target) {
          hvx -= dir*(D ? FRIC : 12); if (dir*hvx < target) hvx = dir*target;
        }
        face = dir;
      } else hvx = brake(hvx, FRIC);
    } else if ((st == NORM || st == SPINJ) && dir) {
      if (dir*hvx < target) { hvx += dir*(dir*hvx < 0 ? AACC*2 : AACC); if (target < MAXV && dir*hvx > target) hvx = dir*target; }
      face = dir;
    }
    if (jbuf && coy < 6) {
      jbuf = 0; coy = 99; cut = 1; spin = 0; gnd = 0; posture(0); takeoff = 1; launch = 6;
      if (poundt && !D && !U) { hvy = -GPJUMP_V; cut = 0; poundt = 0; jn = -1; SPIN(40, face); }
      else if (U) { st = SPINJ; hvy = -560; jn = -1; cut = 0; launch = 0; }
      else if (D && (dir || runt || iabs(hvx) > 150)) { if (dir) face = dir; else if (runt) face = rundir; longjump(); }
      else if (D) { hvy = -1060; arcg = 32; hvx = -face*200; SPIN(40, -face); jn = -1; cut = launch = 0; }
      else if (skid && dir) { face = dir; hvx = dir*260; hvy = -1020; arcg = 32; SPIN(40, dir); jn = -1; cut = launch = 0; }
      else if (catcht) { hvy = -900; arcg = 42; catcht = 0; jn = -1; }
      else {
        // A stationary double is valid; only the third jump needs forward speed.
        jn = landt <= 10 && jn >= 0 && jn < 2 && (jn == 0 || face*hvx > 200) ? jn+1 : 0;
        hvy = jn == 2 ? -TRIPLE_V : jn ? -1030 : -870;
        if (jn == 2) { SPIN(44, face); arcg = 32; cut = launch = 0; }
      }
      sfx(1);
    } else if (jbuf && wall && !gnd) {
      jbuf = 0; hvx = -wall*440; hvy = -900; face = -wall; lock = 7; cut = 1; jn = -1; spin = 0;
      st = NORM; arcg = GRAV; launch = 0; posture(0); capok = diveok = stall = catchok = 1; throwt = twirl = 0; sfx(1); rumble(1);
    } else if (jbuf && catcht && catchok && !gnd) {
      jbuf = catcht = catchok = 0; st = NORM; spin = cut = 0;
      hvy = -320; arcg = 26; throwt = 0; twirl = 10; stall = 1; launch = 0; sfx(1);
    } else if (!takeoff && pr & 8 && !gnd && !(k & 32)) {
      gpspin = st == SPINJ; st = GPWIND; stt = 0; throwt = twirl = 0; SPIN(14, face);
    }
  }
  if (cappress || (pr & 8 && k & 32)) {
    if (gnd && D && (st == NORM || st == SLIDE)) {
      roll(ROLLSTART); sfx(2);
    } else if (D && !downthrow && diveok && !gnd && !(st == GPSLAM && scan(hx>>8, (hy>>8)+11, 6, 7, SOLID)) && (freemove() || st == GPWIND || st == GPSLAM)) {
      st = DIVE; posture(0);
      if (face*hvx < 760) hvx = face*760;
      if (hvy > -420) hvy = -420;
      g = arcg = GRAV; launch = capbuf = 0; diveok = 0; spin = throwt = twirl = cut = 0; sfx(2);
    } else if (cappress && cst && cst < 3 && (!D || downthrow)) {
      if (pr & CAP2) cst = 3; // the second throw button explicitly recalls the cap
      else if (!capextend && ckind != CAPSPIN) {
        // One append throw per flight. Aim at the nearest enemy in front, or extend forward.
        int dx = cvy ? 0 : face*1100, dy = cvy ? (cvy > 0 ? 1100 : -1100) : 0, best = 64*64;
        for (E *e = en; e < en+ne; e++) if (e->a) {
          int ex = (e->x-cxp)>>8, ey = (e->y-cyp)>>8, d = ex*ex+ey*ey;
          if (d < best && ex*face >= -4) { best = d; int scale = iabs(ex)>iabs(ey) ? iabs(ex) : iabs(ey); if (scale) dx = ex*1100/scale, dy = ey*1100/scale; }
        }
        cvx = dx; cvy = dy; cst = 1; ct = 0; capextend = 1;
      }
    }
  }
  if (capbuf && !cst && (freemove() || st == GSPIN || (st == GPLAND && !D))) {
    int ck = capkeys; capbuf = 0;
    capthrow(ck, (ck & (8|CAP2)) == (8|CAP2), rollcancel, takeoff);
  }
  // Odyssey gives both long jumps and dives a 0.5 u/frame counter-input brake.
  // Scale that like AACC (0.5), while retaining the 2.5 u/frame forward minimum.
  if ((st == LONGJ || st == DIVE) && dir*hvx < 0) {
    int minimum = MAXV*5/28, speed = iabs(hvx)-AACC;
    hvx = (hvx > 0 ? 1 : -1)*(speed < minimum ? minimum : speed);
  }
  if (st == NORM) g = arcg;
  if (st == LONGJ) g = 34;
  if (st == SPINJ) g = 13;
  if (hvy < 0 && cut && !(k & 16)) g *= 2;
  else if (st == NORM && cut && k & 16 && hvy > -200 && hvy < 200) g = g*5/8;
  if (throwt && st == NORM) { throwt--; hvy = g = 0; }
  hvy += g;
  if (st != GPSLAM) {
    int ws = !gnd && wall && wall == dir && hvy > 0 && st == NORM;
    if (ws && hvy > 200) hvy = hvy-100 > 200 ? hvy-100 : 200;
    if (hvy > 1100) hvy = 1100;
  }

  if (gnd && st == NORM && D && slopedir && !takeoff) {
    face = slopedir; roll(iabs(hvx) > 200 ? iabs(hvx) : 200);
  }
  if (gnd && st == ROLL && slopedir) {
    hvx += slopedir*18;
    if (iabs(hvx) > ROLLMAX) hvx = (hvx > 0 ? 1 : -1)*ROLLMAX;
    if (hvx) face = hvx > 0 ? 1 : -1;
  }
  hx += hvx; X = hx >> 8; Y = hy >> 8;
  if (gnd && hvy >= 0) {
    int slope, floor = floorat(X, Y+11, &slope);
    if (floor <= Y+16 && (slope || slopedir) && !scan(X, floor-11+duck, 6, 11-duck, SOLID)) {
      hy = (floor-11)*256; Y = hy>>8;
    }
  }
  if (scan(X, Y+duck, 6, 11-duck, SOLID)) {
    int s = hvx > 0 ? -1 : 1;
    do X += s; while (scan(X, Y+duck, 6, 11-duck, SOLID));
    hx = X << 8; hvx = 0;
  }
  int was = gnd, vy0 = hvy; gnd = 0;
  hy += hvy; Y = hy >> 8;
  if (st == GPSLAM) while (scan(X, Y+duck, 6, 12-duck, 4) == 2) brk(htx, hty);
  if (hvy < 0) while (scan(X, Y+duck, 6, 11-duck, 4) == 2) brk(htx, hty), hvy = 0;
  if (scan(X, Y+duck, 6, 11-duck, SOLID)) {
    int s = hvy > 0 ? -1 : 1;
    do Y += s; while (scan(X, Y+duck, 6, 11-duck, SOLID));
    hy = Y << 8;
    if (hvy > 0) gnd = 1;
    hvy = 0;
  } else if (hvy >= 0 && scan(X, Y+11, 6, 1, SOLID)) gnd = 1, hy = Y << 8, hvy = 0;
  slopedir = 0;
  if (gnd) {
    int slope; floorat(X, Y+11, &slope); slopedir = slope; launch = 0;
    capok = diveok = stall = catchok = 1; throwt = twirl = 0;
    if (!was) {
      landt = 0;
      if (st == GPSLAM) {
        st = GPLAND; stt = 0; poundt = 31; shake = 8; sfx(4); rumble(6);
        if (rollbuf) roll(gpspin ? MAXV*30/14 : ROLLSTART);
      } else if ((st == DIVE || st == LONGJ) && D) roll(iabs(hvx) > ROLLSTART ? iabs(hvx) : ROLLSTART);
      else if (st == DIVE) st = SLIDE, posture(5);
      else if (st == LONGJ || st == SPINJ) st = NORM;
      if (vy0 > 900 && st != GPLAND) rumble(2);
    }
    if (scan(X, Y+11, 6, 1, 32)) {
      hvy = -1500; gnd = 0; st = NORM; arcg = GRAV; launch = 0; posture(0); coy = 99; jbuf = 0;
      SPIN(30, face); cut = 0; sfx(8); rumble(5);
    }
  }
  wall = 0;
  if (!gnd) wall = scan(X+6, Y+duck+1, 1, 9-duck, SOLID) ? 1 : scan(X-1, Y+duck+1, 1, 9-duck, SOLID) ? -1 : 0;
  if (!gnd && !ledget && wall && dir == wall && hvy >= 0 && st == NORM && !duck) {
    int side = wall > 0 ? X+6 : X-1, tx = side>>3;
    for (int ty = (Y-2)>>3; ty <= (Y+6)>>3; ty++) {
      int t = tile(tx, ty), top = ty*8, edge = wall > 0 ? tx*8 : (tx+1)*8;
      if ((t == 1 || t == 2 || t == 4) && top >= Y-2 && top <= Y+6 && !scan(side, top-11, 1, 11, SOLID)) {
        face = wall; hx = (wall > 0 ? edge-6 : edge)*256; hy = (top+2)*256;
        climbx = (wall > 0 ? edge+1 : edge-7)*256; climby = (top-11)*256;
        st = HANG; hvx = hvy = spin = throwt = launch = 0; jn = -1; break;
      }
    }
  }
  while (scan(X, Y+duck, 6, 11-duck, 64)) { map[hty][htx] = 0; coins++; sfx(7); }
  if (scan(X-1, Y+duck+2, 8, 8-duck, 8) || scan(X+1, Y+duck-1, 4, 13-duck, 8) || Y > MH*8+8) die();
  if (X+6 > gx*8+2 && X < gx*8+6 && Y < gb && st < DEAD) { st = WIN; stt = 0; hvx = hvy = 0; hx = gx*8-3 << 8; sfx(6); rumble(8); }
}

static void capupd(int k) {
  if (!cst) return;
  if (ckind == CAPSPIN && cst < 3) {
    int a = ++ct*256/24;
    int nx = hx + 256 + SIN[(a+64)&255]*14, ny = hy + (5 << 8) + SIN[a&255]*5;
    if (!scan(nx >> 8, ny >> 8, 8, 4, SOLID)) cxp = nx, cyp = ny;
    if (ct >= 24) cst = 3;
  } else if (cst == 1) {
    int nx = cxp + cvx, ny = cyp + cvy;
    if (scan(nx >> 8, ny >> 8, 8, 4, SOLID)) {
      if (capreflect && cvx) { cvx = -cvx; capreflect = 0; }
      else cst = 2, ct = 0;
    }
    else {
      cxp = nx; cyp = ny;
      if (cvx) cvx -= cvx > 0 ? 72 : -72;
      if (cvy) cvy -= cvy > 0 ? 72 : -72;
      if (iabs(cvx) < 100 && iabs(cvy) < 100) cst = 2, ct = 0;
    }
  } else if (cst == 2) {
    if (ct < 24) ct++;
    if (ct == 24 && !(k & 32)) cst = 3;
  } else {
    int dx = hx + 256 - cxp, dy = hy + (duck+3)*256 - cyp;
    cxp += dx > 1000 ? 1000 : dx < -1000 ? -1000 : dx;
    cyp += dy > 1000 ? 1000 : dy < -1000 ? -1000 : dy;
    if (iabs(dx) < 1024 && iabs(dy) < 1024) cst = 0, catcht = 10;
  }
  if (cst && cst < 3) while (scan(cxp >> 8, cyp >> 8, 8, 4, 64)) { map[hty][htx] = 0; coins++; sfx(7); }
  int touching = ov(hx >> 8, (hy >> 8)+duck, 6, 11-duck, cxp >> 8, cyp >> 8, 8, 5);
  if (!touching) cready = 1;
  int landing = hvy >= 0 && oldhy + (11 << 8) <= cyp + 256 && hy + (11 << 8) >= cyp;
  int diving = st == DIVE && hy < cyp + (2 << 8);
  int vault = gnd && st == NORM && cst == 2 && k & 32;
  if (ckind != CAPSPIN && cst && cst < 3 && cready && (capok || vault) && (freemove() || st == DIVE) && touching && (vault || (!gnd && (landing || diving)))) {
    if (!gnd) capok = 0;
    else if (face*hvx < 700) hvx = face*700;
    diveok = stall = 1; hvy = vault ? -CAPVAULT_V : -CAPBOUNCE_V; arcg = 32; launch = 0; cut = 0; st = NORM; posture(0); gnd = 0; jn = -1; spin = throwt = twirl = 0; coy = 99; cst = 3;
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
    if (st < DEAD && ov(X, Y+duck, 6, 11-duck, ex+1, ey+1, 6, 7)) {
      if ((hvy > 0 || st == GPSLAM) && Y+11 < ey+6) {
        kill(e); hvy = k & 16 ? -1000 : -650; st = NORM; arcg = GRAV; launch = 0; cut = 0; capok = diveok = stall = 1; spin = throwt = 0;
      } else die();
    }
    if (e->a && cst && cst < 3 && ov(cxp >> 8, cyp >> 8, 8, 5, ex, ey, 8, 8)) kill(e);
  }
}

static void destination(int choice) {
  lvl = choice == NLV ? NLV+1 : choice;
  deaths = coins = lcoins = tim = done = shake = rumq = 0;
  for (P *p = pt; p < pt+NP; p++) p->l = 0;
  menu = 0; resumable = 1; load(); sfx(2);
}
static void openmenu(int k) {
  menu = 1; menusel = lvl >= NLV ? NLV : lvl;
  menunav = 0; menurepeat = 0; menufr = 0;
  // Don't move the selection just because movement was held when pausing.
  int axis = moveaxis(k);
  menunav = k & 12 ? (k & 8 ? 3 : -3) : axis > 128 ? 1 : axis < -128 ? -1 : 0;
  menurepeat = 18;
}
static void menutick(int k, int pr) {
  menufr++;
  if (pr & BACK) { if (resumable) menu = 0; else quitting = 1; return; }
  if (pr & MENUBACK) { if (resumable) menu = 0; return; }
  if (pr & 32 && resumable) { menu = 0; return; }
  if (pr & (16|START)) { destination(menusel); return; }
  if (pr & PRACTICE) { destination(NLV); return; }
  int axis = moveaxis(k);
  int nav = k & 12 ? (k & 8 ? 3 : -3) : axis > 128 ? 1 : axis < -128 ? -1 : 0;
  if (nav && (nav != menunav || --menurepeat <= 0)) {
    menusel = (menusel+nav+MENUN) % MENUN; sfx(7);
    menurepeat = nav != menunav ? 18 : 6;
  }
  menunav = nav;
}
static void tick(int k) {
  int pr = k & ~prevk;
  prevk = k;
  if (k & CAP2) k |= 32;
  if (pr & CAP2) pr |= 32;   // pressing the other face button is a real new action
  if (pr & 128 && shm) shm[2] ^= 1;
  if (pr & QUIT) { quitting = 1; return; }
  if (menu) { menutick(k, pr); return; }
  if (pr & (BACK|START)) { openmenu(k); return; }
  if (pr & PRACTICE) { lvl = lvl >= NLV ? 0 : NLV+1; done = lcoins = 0; load(); return; }
  if (pr & 64) { if (done) lvl = deaths = lcoins = tim = done = 0; load(); return; }
  fr++;
  if (!done) tim++;
  if (shake) shake--;
  oldhy = hy;
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

// The menu has its own 768x432 canvas. Physics, gameplay art and mouse hit boxes
// stay in the existing 256x144 world; the menu is sampled into the same window.
#define MENUW (W*3)
#define MENUH (H*3)
static void mrect(int x, int y, int w, int h, u32 c) {
  int ax=x*SW/MENUW, ay=y*SH/MENUH;
  blk(ax,ay,(x+w)*SW/MENUW-ax,(y+h)*SH/MENUH-ay,c);
}
static u32 mixcolor(u32 a, u32 b, int weight, int total) {
  u32 c=0;
  for(int shift=0;shift<=16;shift+=8)
    c |= (((a>>shift&255)*(total-weight)+(b>>shift&255)*weight)/total)<<shift;
  return c;
}
static void mcover(int x, int y, int z, u32 c, int coverage) {
  if(coverage==3) {mrect(x,y,z,z,c);return;}
  int ax=x*SW/MENUW, ay=y*SH/MENUH, bx=(x+z)*SW/MENUW, by=(y+z)*SH/MENUH;
  for(int py=ay;py<by;py++)for(int px=ax;px<bx;px++)
    if((unsigned)px<SW && (unsigned)py<SH)big[py][px]=mixcolor(big[py][px],c,coverage,3);
}
static void mround(int x,int y,int w,int h,int r,u32 c) {
  for(int row=0;row<h;row++) {
    int dy=row<r?r-row-1:row>=h-r?row-(h-r):0, inset=0;
    if(dy)while((r-inset)*(r-inset)+dy*dy>r*r)inset++;
    mrect(x+inset,y+row,w-2*inset,1,c);
  }
}
static void mellipse(int cx,int cy,int rx,int ry,u32 c) {
  for(int y=-ry;y<=ry;y++) {
    int x=rx;
    while(x*x*ry*ry+y*y*rx*rx>rx*rx*ry*ry)x--;
    mrect(cx-x,cy+y,2*x+1,1,c);
  }
}
static void mline(int x,int y,int bx,int by,u32 c) {
  int dx=iabs(bx-x),dy=-iabs(by-y),sx=x<bx?1:-1,sy=y<by?1:-1,e=dx+dy;
  for(;;) {
    mrect(x,y,1,1,c);if(x==bx && y==by)break;
    int e2=2*e;if(e2>=dy)e+=dy,x+=sx;if(e2<=dx)e+=dx,y+=sy;
  }
}
static int textwidth(const char *s,int z) {
  int n=0;while(*s++)n++;
  return n?(n*18-3)*z:0;
}
static void menutext(const char *s,int x,int y,int z,u32 c) {
  for(;*s;s++,x+=18*z) {
    int i=*s>='A'&&*s<='Z'?*s-'A':*s>='0'&&*s<='9'?*s-'0'+26:
          *s=='-'?36:*s=='/'?37:*s==':'?38:*s=='.'?39:*s=='?'?40:-1;
    if(i<0)continue;
    for(int v=0;v<21;v++)for(int u=0;u<15;u++) {
      int coverage=MENUFONT[i][v]>>((14-u)*2)&3;
      if(coverage)mcover(x+u*z,y+v*z,z,c,coverage);
    }
  }
}
static void centered(const char *s,int y,int z,u32 c) {menutext(s,(MENUW-textwidth(s,z))/2,y,z,c);}
static int menuhit(int x,int y) {
  int page=menusel/6*6;
  for(int i=0;i<6 && page+i<MENUN;i++) {
    int cx=22+i%3*73,cy=62+i/3*32;
    if(x>=cx && x<cx+66 && y>=cy && y<cy+27)return page+i;
  }
  return -1;
}
static void mcloud(int x,int y,int w) {
  mround(x,y+7,w,10,5,0xd9edf2);
  mellipse(x+w/3,y+7,w/6,7,0xffffef);mellipse(x+w*2/3,y+8,w/5,8,0xffffef);
  mround(x+2,y+9,w-4,6,3,0xffffef);
}
static void mhat(int x,int y) {
  mround(x+3,y,20,10,4,0x733431);mround(x+4,y+1,18,8,3,0xf07b35);
  mround(x,y+7,30,5,2,0x733431);mround(x+1,y+7,28,3,1,0xffa449);
  mrect(x+7,y+3,9,1,0xffce77);mrect(x+5,y+6,17,2,0xa5482d);
}
static void menupreview(int choice,int x,int y) {
  u32 sky=choice==1?0x7da8d3:choice==3?0x546cbe:0x6cb9e7;
  for(int row=0;row<45;row++)mrect(x,y+row,186,1,mixcolor(sky,0xbde6ee,row,70));
  mcloud(x+12,y+4,43);mcloud(x+112,y+2,53);
  for(int u=0;u<186;u++) {
    int a=(u+21)%120-60,h=21+a*a/180;
    if(h<45)mrect(x+u,y+h,1,45-h,0x8dc983);
    int b=(u+70)%110-55,near=30+b*b/190;
    if(near<45)mrect(x+u,y+near,1,45-near,0x5aaa66);
  }
  if(choice==3) {
    for(int i=0;i<3;i++) {
      int xx=x+23+i*52,yy=y+32-i*9;
      mround(xx,yy,42,8,4,0xaccde9);mround(xx,yy-2,42,7,3,0xffffed);
      mrect(xx+8,yy-3,26,1,0xffffff);
    }
    for(int i=0;i<4;i++){int xx=x+74+i*25,yy=y+7+(i&1)*5;mline(xx-2,yy,xx+2,yy,0xfff1bb);mline(xx,yy-2,xx,yy+2,0xfff1bb);}
  } else {
    mrect(x,y+36,186,9,0x9b623f);mrect(x,y+33,186,3,0x327d4f);mrect(x,y+32,186,1,0xb9e381);
    for(int u=0;u<186;u+=7)mrect(x+u,y+36+(u%3),2,1,0xc2864e);
    if(choice==1)for(int i=0;i<5;i++) {
      int xx=x+50+i*23,yy=y+27-(i&1)*8;
      mrect(xx,yy,21,11,0x9f4e39);mrect(xx+1,yy+1,19,9,0xda8d50);
      mrect(xx+2,yy+1,17,1,0xffcd83);mrect(xx+10,yy+2,1,7,0xae603d);
    }
    if(choice==2)for(int i=0;i<6;i++) {
      int xx=x+59+i*14;
      for(int v=0;v<9;v++){mrect(xx-v/2,y+25+v,v+1,1,0x4c6383);mrect(xx-v/2,y+25+v,v/2+1,1,0xe8f4f3);}
    }
    if(choice==4) {
      mrect(x+109,y+9,50,25,0x637e9d);mrect(x+111,y+10,46,24,0xb9c7cd);
      for(int row=0;row<3;row++)for(int i=0;i<4;i++)mrect(x+112+i*12+(row&1)*5,y+13+row*8,9,1,0x90a5b6);
      for(int i=0;i<4;i++){mrect(x+108+i*14,y+4,9,7,0x6a849e);mrect(x+109+i*14,y+4,7,5,0xdde6dc);}
      mround(x+129,y+22,12,15,5,0x405c7a);mrect(x+134,y+27,1,8,0x1f3e61);
      mline(x+142,y+4,x+142,y-1,0xffefc5);mrect(x+143,y-1,9,3,0xe56c59);
    }
    if(choice==NLV) {
      for(int u=0;u<63;u++) {int h=u<33?u/3:(62-u)/3;mrect(x+89+u,y+32-h,1,h+1,0x368950);mrect(x+89+u,y+31-h,1,1,0xb5e687);}
      mrect(x+148,y+24,24,9,0x829daf);mrect(x+148,y+23,24,2,0xc7d8d4);
    }
    if(choice==0) {
      mline(x+139,y+15,x+139,y+33,0x466983);mline(x+140,y+15,x+140,y+33,0xfff5c7);
      for(int u=0;u<16;u++)mrect(x+141+u,y+15,1,7-u/3,0xe76d5b);
      for(int i=0;i<3;i++){int xx=x+64+i*14;mline(xx,y+29,xx,y+32,0x378552);mellipse(xx,y+27,2,2,0xffec9f);mrect(xx,y+27,1,1,0xe99554);}
    }
  }
}
static void menurender(void) {
  ox=oy=capless=capoff=0;
  for(int y=0;y<MENUH;y++)mrect(0,y,MENUW,1,mixcolor(0x739fd1,0xbce2e1,y,MENUH));
  // Quiet scenery behind the title and cards, with softer layers and tiny stars.
  for(int x=58;x<MENUW-58;x++) {
    int a=(x+36)%222-111,h=315+a*a/360;
    if(h<378)mrect(x,h,1,378-h,0x83bb88);
  }
  for(int i=0;i<12;i++) {
    int x=83+i*51,y=140+(i%3)*7;
    mrect(x,y,1,1,0xddebe1);
  }
  // Velvet folds vary smoothly at the higher resolution; gold edging and fringe
  // keep the storybook stage shape while giving it finer seams and highlights.
  for(int side=0;side<2;side++)for(int y=0;y<378;y++) {
    int width=y<129?66-y/7:48+(y-129)/12;
    for(int u=0;u<width;u++) {
      int wave=SIN[(u*256/24+side*28)&255]+256;
      u32 c=mixcolor(0x84233e,0xe56365,wave,512);
      int xx=side?MENUW-1-u:u;mrect(xx,y,1,1,c);
    }
    int edge=side?MENUW-width:width-1;
    mrect(edge,y,1,1,0xffe7a8);mrect(edge+(side?1:-1),y,2,1,0xdca16a);
    if(!(y%5))mrect(edge+(side?3:-3),y,1,2,0xf9d891);
  }
  for(int x=0;x<MENUW;x++) {
    int a=x%96-48,bottom=39-a*a/96;
    for(int y=0;y<bottom;y++) {
      int wave=SIN[(x*256/24)&255]+256;
      mrect(x,y,1,1,y<5?0x742238:mixcolor(0x9c2e43,0xe97069,wave,512));
    }
    mrect(x,bottom,1,2,0xffe8ad);mrect(x,bottom+2,1,1,0xd69b67);
    if(!(x%9))mround(x-1,bottom+3,3,5,1,0xffdca0);
  }
  // Stage boards and a restrained inlay replace the empty footer strip.
  for(int y=372;y<MENUH;y++)mrect(0,y,MENUW,1,mixcolor(0x345171,0x263b58,y-372,60));
  mrect(0,372,MENUW,2,0xffe1a0);mrect(0,374,MENUW,3,0xb47953);
  for(int x=0;x<MENUW;x+=24)mrect(x,425,23,7,x&24?0x965351:0xaf665d);
  mline(45,409,723,409,0x4a657e);mline(45,411,723,411,0x263b58);
  // A gold-edged plaque, inset beads, and a shallow title extrusion.
  mround(151,57,468,87,8,0x4c5871);mround(147,51,468,87,8,0xa67150);
  mround(147,49,468,85,8,0xffd894);mround(150,52,462,79,6,0xc8615d);
  mround(154,56,454,71,4,0x9b3e50);mround(156,58,450,67,3,0xc0515c);
  mline(159,59,600,59,0xe78073);mline(159,123,600,123,0x913750);
  for(int x=165;x<602;x+=16){mrect(x,54,2,1,0xffecc0);mrect(x,129,2,1,0xd8a16d);}
  int tx=(MENUW-textwidth("HATRICK",3))/2;
  menutext("HATRICK",tx+3,64,3,0x762b46);menutext("HATRICK",tx+2,62,3,0x8e3b4d);
  menutext("HATRICK",tx,60,3,0xfff3d1);
  centered("CHOOSE YOUR ADVENTURE",155,1,0xdcecf0);centered("CHOOSE YOUR ADVENTURE",153,1,0x2b5075);
  int page=menusel/6*6;
  for(int i=0;i<6 && page+i<MENUN;i++) {
    int choice=page+i,x=66+i%3*219,y=186+i/3*96,active=choice==menusel;
    mround(x+3,y+5,198,81,5,0x6684a0);
    mround(x,y,198,81,5,active?0xad7244:0x56779a);
    mround(x,y-1,198,79,5,active?0xffdb87:0xe8e6d2);
    mround(x+2,y+1,194,75,3,active?0xeaaa5d:0x658baa);
    menupreview(choice,x+6,y+5);
    mrect(x+6,y+50,186,1,active?0xba884f:0x7694a4);
    for(int row=0;row<24;row++)mrect(x+6,y+51+row,186,1,mixcolor(active?0xffedb2:0xfff5dc,active?0xf5d388:0xe7e8d7,row,32));
    const char *name=choice==NLV?"PLAYGROUND":LNAME[choice];
    int width=textwidth(name,1);char number[2]={'1'+choice,0};
    if(width>186)name=number,width=textwidth(number,1);
    menutext(name,x+(198-width)/2+1,y+54,1,0xd2c4a1);
    menutext(name,x+(198-width)/2,y+53,1,0x2c4162);
    // Fine corner studs and a floating cap keep the selected card easy to spot.
    if(active) {
      mrect(x+3,y+3,2,2,0xffffdf);mrect(x+193,y+3,2,2,0xffffdf);
      mrect(x+3,y+72,2,2,0xffedb7);mrect(x+193,y+72,2,2,0xffedb7);
      mhat(x+158,y+9+SIN[(menufr*5)&255]*2/256);
    }
  }
  // Tiny dais decorations. No control hints are drawn on the menu.
  for(int side=0;side<2;side++) {
    int x=side?710:58;
    mline(x,403,x,425,0x66a16c);mellipse(x-3,414,4,2,0x83bc83);
    mellipse(x,400,5,5,0xf3c4b7);mellipse(x,400,2,2,0xffe3a4);
  }
  sprx(HSTAND,12,10*256,142*256,0,0,256,256,HPAL);
  sprx(GRUM1,8,246*256,142*256,1,0,256,256,GPAL);
}

static u32 tilepx(int t, int tx, int ty, int u, int v) {
  int up = SOLID >> tile(tx, ty-1) & 1;
  if (t == 8 || t == 9) {
    int surface = t == 8 ? 7-u : u;
    if (v < surface) return 0;
    return v == surface ? 0x8be05a : v <= surface+2 ? 0x4cb83c : 0xa8642c;
  }
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
  if (menu) { menurender(); return; }
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
    const u16 *f = HJUMP; int sh = 12, fl = face < 0, ang = 0, tang = 0, sx = 256, sy, bob = 0, fx = hx + (3 << 8), fy = hy + (11 << 8);
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
    else if (st == ROLL) {
      f = HROLL; sh = 8; ang = rollph = (rollph + hvx/32) & 255;
      if (gnd && !(fr & 3)) dust(fx - face*(3 << 8), fy, -face, 1);
    } else if (st == DIVE || st == SLIDE) {
      tang = face*64;
      if (st == SLIDE && !(fr & 3)) dust(fx - face*(3 << 8), fy, -face, 1);
    } else if (st == HANG || st == CLIMB) { f = HHANG; fl = face < 0; }
    else if (twirl || st == SPINJ || st == GSPIN) {
      f = HSPIN; int w = SIN[(fr*20+64)&255]; fl = w < 0; sx = iabs(w) < 64 ? 64 : iabs(w);
    } else if (throwt) { f = HTHROW; tang = -face*12; }
    else if (st == LONGJ) tang = face*36;
    else if (duck) {
      f = HCROUCH; capoff = 5;
      if (gpspin && st == GPSLAM) { int w = SIN[(fr*24+64)&255]; fl = w < 0; sx = iabs(w) < 64 ? 64 : iabs(w); }
    }
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
    if (st != DEAD && st != ROLL) {
      ang = vang;
      if (spin) {   // flips: one smooth turn, eased in and out
        int t = (spinlen - spin) * 256 / spinlen;
        ang += spind * (t*t*(768 - 2*t) >> 16);
      }
    }
    if (turnt) { if (!spin && st < DIVE) sx = sx * (256 - turnt*44) >> 8; turnt--; }   // quick turn, not mid-flip
    sy = 256 + sqv; sx = sx * (256 - sqv/2) >> 8; sqv = sqv * 13 / 16;
    sprx(f, sh, fx, fy + bob + iabs(vang) * 512 / 64, fl, ang, sx, sy, HPAL);
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
  if ((unsigned)shm >= (unsigned)-4095) { shm = 0; return; }
  int parent = sc(20, 0, 0, 0);        // getpid before fork, for the parent-death race
  if (sc(2, 0, 0, 0)) return;          // the game continues in the parent, also on fork failure
  sc(172, 1, 9, 0);                    // die with the game (PR_SET_PDEATHSIG, SIGKILL)
  if (sc(64, 0, 0, 0) != parent || sc(42, (int)fd, 0, 0) < 0) sc(1, 0, 0, 0);
  parent = sc(20, 0, 0, 0);
  int player = sc(2, 0, 0, 0);
  if (player < 0) sc(1, 0, 0, 0);
  if (!player) {                      // grandchild: aplay reads the pipe
    // Parent-death signals are cleared by fork: aplay needs its own registration.
    sc(172, 1, 9, 0);
    if (sc(64, 0, 0, 0) != parent) sc(1, 0, 0, 0);
    sc(63, fd[0], 0, 0);
    if (fd[0]) sc(6, fd[0], 0, 0);
    sc(6, fd[1], 0, 0);               // no writer in the reader: synth exit must produce EOF
    sc(11, (int)"/usr/bin/aplay", (int)av, (int)envp);
    sc(1, 0, 0, 0);
  }
  sc(6, fd[0], 0, 0);                 // synth only owns the write end
  sc(55, fd[1], 1031, 4096);           // F_SETPIPE_SZ: keep latency low
  synth(fd[1]);
}

// ---------- gamepads: every evdev gamepad is read directly, Super Mario Odyssey layout ----------
// A/B jump, X/Y cap, LT/RT (or LB/RB) crouch / ground pound, left stick or D-pad move,
// Start opens the menu, View mutes. Pads are rescanned every 2 s, so hotplugging works, and pads
// with force feedback get rumble effects uploaded (played from rumble()).
static const short PADB[] = { 304, 305, 307, 308, 310, 311, 312, 313, 314, 315, 546, 547, 544, 545 };
static const unsigned PADK[] = { 16, 16|MENUBACK, 32, CAP2, 8, 8, 8, 8, 128, START, 1, 2, 4, 8 };
static struct { int fd, num, held, x, hx, y, hy, ymid, yrange, ydz, t1, t2, mid, range, dz, tq, c1, c2, fx[8]; } pad[4];   // fd is stored +1, 0 = free slot
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
      p->fd = fd+1; p->num = i; p->held = p->hx = p->hy = p->t1 = p->t2 = p->y = p->ymid = p->yrange = p->ydz = 0; p->x = ai[0];
      p->mid = (ai[1]+ai[2]) / 2; p->range = (ai[2]-ai[1]) / 2; p->dz = p->range / 6;
      if (sc(54, fd, 0x80184541, (int)ai) >= 0) {
        p->y = ai[0]; p->ymid = (ai[1]+ai[2])/2; p->yrange = (ai[2]-ai[1])/2; p->ydz = p->yrange/3;
      }
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
  int k = 0, axis = 0, n, ev[64];
  if (--wait < 0) wait = 120, padscan();
  for (int j = 0; j < 4; j++) {
    typeof(pad[0]) *p = pad + j;
    if (!p->fd) continue;
    while ((n = sc(3, p->fd-1, (int)ev, sizeof ev)) > 0)
      for (int *e = ev; e < ev + n/4; e += 4) {           // struct input_event: time, u16 type, u16 code, s32 value
        int type = e[2] & 0xffff, code = e[2] >> 16, v = e[3];
        if (type == 1) for (int i = 0; i < sizeof PADB/sizeof *PADB; i++) { if (PADB[i] == code) p->held = v ? p->held | 1 << i : p->held & ~(1 << i); }
        if (type == 3) {
          if (code == 0) p->x = v;
          if (code == 16) p->hx = v;
          if (code == 1) p->y = v;
          if (code == 17) p->hy = v;
          if (code == p->c1) p->t1 = v;
          if (code == p->c2) p->t2 = v;
        }
      }
    if (n != -11) { sc(6, p->fd-1, 0, 0); p->fd = 0; continue; }  // anything but EAGAIN: unplugged
    for (int i = 0; i < sizeof PADB/sizeof *PADB; i++) if (p->held >> i & 1) k |= PADK[i];
    int a = stickaxis(p->x, p->mid, p->range, p->dz);
    if (iabs(a) > iabs(axis)) axis = a;
    int ya = stickaxis(p->y, p->ymid, p->yrange, p->ydz);
    if (p->hy < 0 || ya < -128) k |= 4;
    if (p->hy > 0 || ya > 128) k |= 8;
    if (p->hx < 0) k |= 1;
    if (p->hx > 0) k |= 2;
    if (p->t1 > p->tq || p->t2 > p->tq) k |= 8;
  }
  return k | (axis ? ANALOG | ((axis+256) << 10) : 0);
}

// The stack gets envp for aplay and stays 16-byte aligned as the ABI expects.
asm(".globl _start\n_start: mov (%esp),%eax\n lea 8(%esp,%eax,4),%eax\n sub $12,%esp\n push %eax\n call run");

__attribute__((noreturn)) void run(char **envp) {
  audio(envp);
  Display *d = XOpenDisplay(0);
  if (!d) sc(1, 1, 0, 0);
  Window w = XCreateSimpleWindow(d, RootWindow(d, 0), 0, 0, W*SC, H*SC, 0, 0, 0);
  XStoreName(d, w, "Hatrick");
  XSelectInput(d, w, ButtonPressMask | PointerMotionMask);
  Atom wmdelete = XInternAtom(d, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(d, w, &wmdelete, 1);
  XMapWindow(d, w);
  XImage *im = XCreateImage(d, DefaultVisual(d, 0), 24, ZPixmap, 0, (char *)big, W*SC, H*SC, 32, 0);
  struct { int s, n; } t;
  char km[32];
  load(); menu = 1;
  sc(265, 1, (int)&t, 0);              // clock_gettime(CLOCK_MONOTONIC)
  for (;;) {
    while (XPending(d)) {
      XEvent e; XNextEvent(d, &e);
      if (e.type == ClientMessage && (Atom)e.xclient.data.l[0] == wmdelete) quitting = 1;
      if (menu && (e.type == MotionNotify || (e.type == ButtonPress && e.xbutton.button == 1))) {
        int x = e.type == MotionNotify ? e.xmotion.x : e.xbutton.x;
        int y = e.type == MotionNotify ? e.xmotion.y : e.xbutton.y;
        int hit = menuhit(x/SC,y/SC);
        if (hit >= 0) {
          if (menusel != hit) menusel = hit, sfx(7);
          if (e.type == ButtonPress) destination(hit);
        }
      }
    }
    XQueryKeymap(d, km);
    Window focused; int revert;
    XGetInputFocus(d, &focused, &revert);
    int active = focused == w;
    if (!active) {
      for (int i = 0; i < 32; i++) km[i] = 0;
      if (resumable && !menu) openmenu(0);
    }
#define K(c) (km[c >> 3] >> (c & 7) & 1)
    tick((active ? padkeys() : 0) | K(113) | K(114) << 1 | K(111) << 2 | K(116) << 3 | (K(52) | K(29) | K(65) | K(36) | K(104)) << 4 | K(53) << 5 | K(27) << 6 | K(58) << 7 | K(54) << 8 | K(67) << 20 | K(9) << 21 | K(24) << 23);
    if (quitting) sc(1, 0, 0, 0);
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
    for (char *c = b; *c; c++) k |= *c=='L' ? 1 : *c=='R' ? 2 : *c=='U' ? 4 : *c=='D' ? 8 : *c=='J' ? 16 : *c=='C' ? 32 : *c=='V' ? CAP2 : 0;
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
