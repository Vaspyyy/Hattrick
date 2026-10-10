// Cap mechanics (ideas.md, section 1). hatrick.c includes this file twice: the game logic just
// before die(), and the drawing (CAPX_DRAW defined) just before render(). See MODDING.md.
//   H  cap post: a thrown cap sticks there for 2 s as a little platform; a cap button while it
//      is stuck pulls Hatrick to it like a grapple
//   X  cap switch: only the cap can hit it; each hit swaps the red (R) and blue (U) blocks
//   Hat swap: the cap knocking out a walker gives a heavy ground pound that lands hard (stone never breaks), a
//      buzzer a flutter (press and hold Jump while falling), a spitter seeds (every throw also
//      lobs one). A hit takes the power away instead of a life.
//   The cap carries the coins it scoops back to Hatrick; Up or Down bend a forward throw;
//   in bg=dark areas the cap is a lantern and leaves a fading trail of light; checkpoints are
//   hat racks that get a spare cap.
#ifndef CAPX_DRAW
// Live tile types (below 32: SOLID is a 32-bit mask). Off blocks are drawn as outlines.
#define TC_REDOFF 24
#define TC_BLUEOFF 25
#define TC_POST 26     // not solid
#define TC_SWITCH 27   // solid
#define TC_RED 28      // solid while red is on
#define TC_BLUE 29     // solid while blue is on
#define CAPX_STICK 120   // frames a cap stays on a post
#define CAPX_PULL 30     // the longest a grapple pull lasts
#define CAPX_GLOW 180    // frames a lantern spot keeps glowing
#define CAPX_NL 24
enum { POW_NONE, POW_HEAVY, POW_FLUTTER, POW_SEED };
static const u32 CAPX_COL[4] = { 0, 0x9a48d0, 0xffd23c, 0xe0586a };   // the cap's colour with each power
static void die(void);
static void kill(E *e);
static void smash(int tx, int ty);
static void addscore(int v, int x, int y);
static void dwellerspot(const Home *h, int *cx, int *my, int *up);
static int dwellerhit(const Dweller *d, const Home *h, int x, int y, int w, int hh);
typedef struct { int r, x, y, vx, vy, a; } CapSeed;   // a seed Hatrick lobbed, 1/256 px
typedef struct { int x, y, t; } CapLight;            // a spot the lantern cap lit, px; t: frames left
static CapSeed capx_seed[4];
static CapLight capx_light[CAPX_NL];
static int capx_pow, capx_inv, capx_flut, capx_fuel, capx_stuck, capx_pull, capx_carry, capx_bend, capx_pcst, capx_swt, capx_pk, capx_blue;

// spawn(): a fresh Hatrick has no power, no carried coins and no cap on a post. The red / blue
// state is read back from the map, which a checkpoint restores.
static struct { int x, y; } capx_wait[16]; static int capx_nwait;   // blocks that came on inside Hatrick: still off until he steps out
static void capx_reset(void) {
  const Level *L = LV + lvl;
  capx_pow = capx_inv = capx_flut = capx_fuel = capx_stuck = capx_pull = capx_carry = capx_bend = capx_pcst = capx_swt = capx_blue = capx_nwait = 0;
  memset(capx_seed, 0, sizeof capx_seed); memset(capx_light, 0, sizeof capx_light);
  for (int r = 0; r < L->nroom; r++) for (int y = 0; y < L->room[r].h; y++) for (int x = 0; x < L->room[r].w; x++)
    if (wd.rm[r][y][x] == TC_BLUE || wd.rm[r][y][x] == TC_REDOFF) { capx_blue = 1; return; }
}
static int capx_inside(int x, int y) { return ov(hx >> 8, (hy >> 8)+duck, 6, 11-duck, x*8, y*8, 8, 8); }
static void capx_waiting(int force) {   // capx_tick(): a waiting block comes on once he's clear (force: now)
  for (int i = 0; i < capx_nwait; ) {
    u8 *t = &map[capx_wait[i].y][capx_wait[i].x];
    int on = *t == TC_REDOFF ? !capx_blue : *t == TC_BLUEOFF ? capx_blue : -1;   // -1: changed meanwhile (a respawn)
    if (on > 0 && !force && capx_inside(capx_wait[i].x, capx_wait[i].y)) { i++; continue; }
    if (on > 0) *t = *t == TC_REDOFF ? TC_RED : TC_BLUE;
    capx_wait[i] = capx_wait[--capx_nwait];
  }
}
static void capx_toggle(void) {   // the cap hit a switch: red and blue blocks swap, in every area
  const Level *L = LV + lvl;
  capx_waiting(1);
  capx_blue ^= 1;
  for (int r = 0; r < L->nroom; r++) for (int y = 0; y < L->room[r].h; y++) for (int x = 0; x < L->room[r].w; x++) {
    u8 *t = &wd.rm[r][y][x];
    *t = *t == TC_RED ? TC_REDOFF : *t == TC_REDOFF ? TC_RED : *t == TC_BLUE ? TC_BLUEOFF : *t == TC_BLUEOFF ? TC_BLUE : *t;
    if (r == room && (*t == TC_RED || *t == TC_BLUE) && capx_nwait < 16 && capx_inside(x, y))
      *t = *t == TC_RED ? TC_REDOFF : TC_BLUEOFF, capx_wait[capx_nwait].x = x, capx_wait[capx_nwait++].y = y;
  }
  sfx(S_REVEAL); rumble(3);
}
static void capx_gain(int p, int x, int y) {   // the cap knocked out an enemy that carries power p
  if (!p) return;
  capx_pow = p; capx_flut = 0;
  burst(x, y, CAPX_COL[p], 10); burst((hx >> 8)+3, (hy >> 8)+2, CAPX_COL[p], 8); sfx(S_REVEAL);
}
static void capx_enemy(const E *e) {   // walkers give the heavy pound, buzzers the flutter
  capx_gain(e->t == 1 ? POW_HEAVY : e->t == 2 || e->t == 3 ? POW_FLUTTER : POW_NONE, (e->x >> 8)+4, (e->y >> 8)+4);
}
// die() asks first: a worn power takes the hit, and Hatrick blinks for a moment. Pits and the
// level timer always count.
static int capx_absorb(void) {
  if (st >= TUBE || (hy >> 8) > lh*8 || !left) return 0;
  if (capx_inv) return 1;
  if (!capx_pow) return 0;
  burst((hx >> 8)+3, (hy >> 8)+2, CAPX_COL[capx_pow], 12);
  capx_pow = capx_flut = 0; capx_inv = 90; if (hvy > -600) hvy = -600;
  sfx(S_BOUNCE); rumble(2);
  return 1;
}
static void capx_scoop(int tx, int ty) {   // capupd(): a coin rides home on the cap
  capx_carry++; sfx(S_COIN); burst(tx*8+4, ty*8+4, 0xffd84a, 3);
}
// hero(), after gravity: the buzzer's flutter. A new Jump press while falling starts it, holding
// Jump keeps Hatrick hovering for up to 48 frames per air time.
static int mv_swim;   // movement.h: swimming has its own strokes
static void capx_flutter(int k, int pr) {
  if (gnd) { capx_fuel = 48; capx_flut = 0; return; }
  if (capx_pow != POW_FLUTTER || st != NORM || mv_swim) { capx_flut = 0; return; }
  if (pr & 16 && hvy > -200 && capx_fuel) capx_flut = 1;
  if (!capx_flut) return;
  if (!(k & 16) || !capx_fuel) { capx_flut = 0; return; }
  capx_fuel--; hvy -= GRAV + (hvy > 0 ? 60 : 24); if (hvy < -240) hvy = -240;   // brakes a fall fast, then a gentle lift
  if (!(capx_fuel & 3)) part(hx + (3 << 8) + (rnd(5)-2)*256, hy + (11 << 8), rnd(120)-60, 120, 14, 0, 0xfff0a0);
}
static void capx_heavy(int X, int top, int h) {   // hero(): a heavy ground pound lands hard on stone, but stone never breaks:
  if (capx_pow != POW_HEAVY) return;               // rooms are walled and floored with it, and a hole there is a fall out of the level
  if (scan(X, top, 6, h, 16) == 4) shake = 6;
}
// hero(), after the vertical move: feet coming down onto a cap stuck on a post.
static int capx_stand(int X, int *Y) {
  if (cst != 4 || capx_pull || hvy < 0) return 0;
  int cx = cxp >> 8, cy = cyp >> 8;
  if (X+6 <= cx || X >= cx+8 || (oldhy >> 8)+11 > cy+1 || *Y+11 < cy) return 0;
  hy = (cy-11) << 8; *Y = cy-11; hvy = 0;
  return 1;
}
static void capx_lob(void) {   // the spitter's seed, lobbed forward with each throw
  for (CapSeed *s = capx_seed; s < capx_seed+4; s++) if (!s->a) {
    *s = (CapSeed){ room, hx + (3 << 8) + face*(4 << 8), hy + (3 << 8), face*560 + hvx/2, -460, 1 };
    sfx(S_SPIT); return;
  }
}
static void capx_seeds(void) {   // seeds knock out enemies and tube dwellers and smash bricks
  const Level *L = LV + lvl;
  for (CapSeed *s = capx_seed; s < capx_seed+4; s++) {
    if (!s->a) continue;
    if (s->r != room) { s->a = 0; continue; }
    s->vy += 22; s->x += s->vx; s->y += s->vy;
    int sx = s->x >> 8, sy = s->y >> 8, t = tile(sx >> 3, sy >> 3);
    if (sy > lh*8+16) { s->a = 0; continue; }
    if (t == 2 && sx >= 0) { smash(sx >> 3, sy >> 3); s->a = 0; continue; }
    if (SOLID >> t & 1) { s->a = 0; burst(sx, sy, 0x9a6a3a, 3); continue; }
    for (E *e = en; e < en+ne && s->a; e++)
      if (e->a && e->r == room && ov(sx-3, sy-3, 6, 6, e->x >> 8, e->y >> 8, 8, 8)) kill(e), s->a = 0;
    for (int i = 0; i < L->nhome && s->a; i++) {
      Dweller *d = wd.dw + i; const Home *h = L->home + i;
      if (d->a && h->room == room && dwellerhit(d, h, sx-2, sy-2, 4, 4)) {
        int cx, my, up; dwellerspot(h, &cx, &my, &up);
        d->a = 0; s->a = 0; burst(cx, my, h->spit ? 0xe0586a : 0x2f8f9a, 10); sfx(S_STOMP); rumble(4); addscore(500, cx, my);
      }
    }
  }
}
// capupd() calls this first, every frame. Returns 1 when it moved the cap itself (on a post).
static int capx_tick(int k) {
  int pr = k & ~capx_pk; capx_pk = k;
  if (capx_inv) capx_inv--;
  if (capx_swt) capx_swt--;
  if (capx_nwait) capx_waiting(0);
  for (CapLight *l = capx_light; l < capx_light + CAPX_NL; l++) if (l->t) l->t--;
  if (cst && LV[lvl].room[room].cave == 2 && !(fr & 3)) {   // the lantern leaves light behind
    CapLight *o = capx_light; for (CapLight *l = capx_light; l < capx_light + CAPX_NL; l++) if (l->t < o->t) o = l;
    *o = (CapLight){ (cxp >> 8)+4, (cyp >> 8)+2, CAPX_GLOW };
  }
  capx_seeds();
  if (cst == 1 && !capx_pcst) { capx_bend = 0; if (capx_pow == POW_SEED) capx_lob(); }   // a new throw
  if (!cst && capx_carry) {   // the cap is home with its coins
    coins += capx_carry; addscore(100*capx_carry, (hx >> 8)+3, (hy >> 8)-2); sfx(S_COIN); capx_carry = 0;
  }
  if (cst == 4) {   // on a post: a platform until it times out, or a grapple
    int cx = (cxp >> 8)+1, cy = cyp >> 8;
    if (pr & 32 && !capx_pull && (freemove() || st == DIVE || st == SLIDE || st == ROLL || st == GSPIN))
      capx_pull = CAPX_PULL, st = NORM, posture(0), spin = throwt = twirl = 0, sfx(S_THROW);
    if (capx_pull) {
      int dx = cx - (hx >> 8), dy = cy - 11 - (hy >> 8), m = iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy);
      if (--capx_pull == 0 || m < 6 || st != NORM) {
        capx_pull = 0; cst = 3; capok = diveok = 1;
        if (st == NORM && hvy > -700) hvy = -700;
      } else hvx = dx*1000/m, hvy = dy*1000/m, gnd = 0, coy = 99, jn = -1;
    } else if (--capx_stuck <= 0) cst = 3;
    capx_pcst = cst;
    return 1;
  }
  if (cst == 1) {
    int v = capaim(k) & 12;
    if (ckind == CAPFORWARD && v && v != 12) {   // Up / Down bend a forward throw (a sideways-held Up doesn't)
      if (!capx_bend) capx_bend = cvx >= 0 ? 1 : -1;
      int s = (k & 8 ? 1 : -1) * capx_bend, vx = cvx;
      cvx -= s*cvy*26/256; cvy += s*vx*26/256;
    }
    int nx = (cxp + cvx) >> 8, ny = (cyp + cvy) >> 8;
    if (!capx_swt && scan(nx, ny, 8, 4, 1 << TC_SWITCH)) capx_toggle(), capx_swt = 12, burst(htx*8+4, hty*8+4, 0xffffff, 6);
    if (ckind != CAPSPIN && scan(nx, ny, 8, 4, 1 << TC_POST)) {   // caught on a post
      cxp = htx*8 << 8; cyp = (hty*8+1) << 8; cvx = cvy = 0;
      cst = 4; capx_stuck = CAPX_STICK; capx_pull = 0; capx_pcst = cst;
      burst(htx*8+4, hty*8+2, 0xfff0b0, 5); sfx(S_CATCH); rumble(1);
      return 1;
    }
  }
  capx_pcst = cst;
  return 0;
}
// drawhero(): the worn power colours the cap; after a hit Hatrick blinks.
static const u32 *capx_pal(const u32 *base) {
  static u32 p[3];
  p[0] = capx_pow ? CAPX_COL[capx_pow] : base[0]; p[1] = base[1]; p[2] = base[2];
  if (capx_inv && fr & 4) p[0] = p[1] = 0xffffff;
  return p;
}

#else   // ---------- drawing ----------

static u32 capx_cellpx(int t, int u, int v) {
  int red = t == TC_RED || t == TC_REDOFF, edge = u == 0 || v == 0 || u == 7 || v == 7;
  u32 lit = red ? 0xff8a7a : 0x9ab8ff, mid = red ? 0xd83a3a : 0x3a62e0, dark = red ? 0x701818 : 0x182c78;
  switch (t) {
    case TC_POST: {   // a brass hat hook: a knob that glints
      int d = (2*u-7)*(2*u-7) + (2*v-7)*(2*v-7);
      if (d > 34) return 0;
      if (d > 20 || d < 5) return 0x3e2a10;   // a brass ring, dark in the middle
      return u < 4 && v < 4 ? 0xfff0b0 : (fr >> 4 & 3) == 0 ? 0xfff0b0 : 0xc8902e;
    }
    case TC_SWITCH: {   // the colour that is on, with a white cap on it
      lit = capx_blue ? 0x9ab8ff : 0xff8a7a; mid = capx_blue ? 0x3a62e0 : 0xd83a3a; dark = capx_blue ? 0x182c78 : 0x701818;
      if (u == 7 || v == 7) return dark;
      if (u == 0 || v == 0) return lit;
      if (v >= 2 && v <= 5 && CAP[v-2] >> (14 - 2*u) & 3) return 0xffffff;
      return mid;
    }
    case TC_RED: case TC_BLUE:
      if (u == 7 || v == 7) return dark;
      if (u == 0 || v == 0) return lit;
      return (u == 2 || u == 5) && (v == 2 || v == 5) ? lit : mid;
    case TC_REDOFF: case TC_BLUEOFF:   // only a dotted outline while off
      return edge && (u + v) & 1 ? mid : 0;
  }
  return 0;
}
static void capx_coin(int x, int y) {   // a small coin, 4 px, centred on x, y
  for (int v = -2; v < 2; v++) for (int u = -2; u < 2; u++)
    if (iabs(2*u+1) + iabs(2*v+1) < 6) wpx(x+u, y+v, u == -2 || v == -2 ? 0xfff0a8 : 0xffc93a);
}
// render(), after the tiles: posts, switches, red and blue blocks, a cap stuck on a post, the
// coins the cap carries and Hatrick's seeds.
static void capx_draw(void) {
  for (int ty = fdiv(oy, 8*SC); ty <= (oy+SH) / (8*SC); ty++)
    for (int tx = fdiv(ox, 8*SC); tx <= (ox+SW) / (8*SC); tx++) {
      int t = tile(tx, ty);
      if (t < TC_REDOFF || t > TC_BLUE) continue;
      for (int v = 0; v < 8; v++) for (int u = 0; u < 8; u++) { u32 c = capx_cellpx(t, u, v); if (c) wpx(tx*8+u, ty*8+v, c); }
    }
  if (cst == 4 && (capx_stuck > 30 || capx_pull || fr & 2))   // hangs on the hook, blinking before it lets go
    sprx(CAP, 4, cxp + (4 << 8), cyp + (4 << 8), face < 0, capx_stuck < 30 && !capx_pull ? SIN[fr*16 & 255]/32 : 0, 256, 256, HPAL);
  for (int i = 0; i < capx_carry && i < 6 && cst; i++)   // coins riding along behind the cap
    capx_coin((cxp >> 8)+4 - (cvx > 0) * (i+1)*4 + (cvx < 0) * (i+1)*4, (cyp >> 8) - 2 + SIN[(fr*8 + i*40) & 255]/128);
  for (const CapSeed *s = capx_seed; s < capx_seed+4; s++) if (s->a && s->r == room)
    for (int j = -2; j < 2; j++) for (int i = -2; i < 2; i++)
      if (!((i == -2 || i == 1) && (j == -2 || j == 1))) wpx((s->x >> 8)+i, (s->y >> 8)+j, i+j == -2 ? 0xe0b070 : (i+j+(fr >> 2)) & 1 ? 0x7a4a24 : 0x5a3418);
}
// render(): a checkpoint is a hat rack; touching it hangs a spare cap on its hook. Returns 1
// (drawn), so the old flag isn't.
static int capx_rack(const Check *c) {
  int x = c->x*8+3, base = c->y*8+8;
  for (int y = base-16; y < base; y++) wpx(x, y, 0xc08850), wpx(x+1, y, 0x6a4020);   // the pole
  for (int i = -2; i < 4; i++) wpx(x+i, base-1, 0x5a3418);                           // its feet
  wpx(x-1, base-2, 0x8a5a2c); wpx(x+2, base-2, 0x8a5a2c);
  wpx(x, base-17, 0xf0c860); wpx(x+1, base-17, 0xc8902e);                             // a brass knob
  wpx(x-1, base-14, 0xc08850); wpx(x-2, base-15, 0xc08850);                           // two hooks
  wpx(x+2, base-14, 0xc08850); wpx(x+3, base-15, 0xc08850);
  if (c->up) {   // the spare cap drops onto the right hook and settles
    int drop = c->up < 20 ? (20 - c->up)*(20 - c->up)/20 : 0, ang = c->up < 20 ? (20 - c->up)*6 : 0;
    sprx(CAP, 4, (x+5) << 8, (base-12-drop) << 8, 0, ang, 256, 256, HPAL);
    if (c->up < 20) for (int i = 0; i < 3; i++) wpx(x+2 + SIN[(c->up*20 + i*85) & 255]/24, base-14 + SIN[(c->up*20 + i*85 + 64) & 255]/24, 0xfff0a0);
  }
  return 1;
}
// render(), before the HUD: in a bg=dark area only the light around Hatrick, the cap and the
// spots the cap lit shows, in a few steps of brightness.
static void capx_dark(void) {
  if (LV[lvl].room[room].cave != 2) return;
  int lx[CAPX_NL+2], ly[CAPX_NL+2], lr[CAPX_NL+2], n = 0;
  lx[n] = (hx >> 8)+3; ly[n] = (hy >> 8)+5; lr[n++] = 28;
  if (cst) lx[n] = (cxp >> 8)+4, ly[n] = (cyp >> 8)+2, lr[n++] = 46;
  for (const CapLight *l = capx_light; l < capx_light + CAPX_NL; l++)
    if (l->t) lx[n] = l->x, ly[n] = l->y, lr[n++] = 14 + 26*l->t/CAPX_GLOW;
  for (int by = 0; by < SH; by += SC) for (int bx = 0; bx < SW; bx += SC) {
    int wx = (bx + ox) / SC, wy = (by + oy) / SC, best = 0;
    for (int i = 0; i < n && best < 256; i++) {
      int dx = wx - lx[i], dy = wy - ly[i], d = dx*dx + dy*dy, r = lr[i]*lr[i];
      if (d < r) { int b = (r - d)*512/r; if (b > best) best = b; }
    }
    int keep = best >= 256 ? 256 : 24 + (best*232/256 & ~31);
    if (keep >= 256) continue;
    for (int y = by; y < by+SC && y < SH; y++) for (int x = bx; x < bx+SC && x < SW; x++) {
      u32 c = big[y][x];
      big[y][x] = ((c >> 16 & 255)*keep >> 8) << 16 | ((c >> 8 & 255)*keep >> 8) << 8 | (c & 255)*keep >> 8;
    }
  }
}
#endif
