// ---------- bosses ----------
// Included by hatrick.c after the drawing code, so it can use everything above it.
// A level names its boss with boss= in its header and marks where it lives with 'N'; 'J' marks a
// mini-boss (mini=walker or mini=buzzer). Each one owns an arena: the 32 columns (one screen)
// around its marker. Walking into it shuts stone gates on both sides; beating the boss opens them.
// A death restarts the fight; a checkpoint touched past an arena keeps that boss beaten.
//
//   haberdasher  the main villain. Phase 1: scissor boomerangs (the cap knocks them down); after
//                two volleys he pants, and a stomp or the cap hurts him. Phase 2: his top hat flies
//                off; hit it with the cap to wear it, and only then can a stomp hurt him.
//   bigwalker    a giant walker on a seesaw over lava ('N' is the pivot, on the board's row).
//                Ground pound the side he stands on and he slides off into the lava.
//   cloudking    floats out of reach. Throw the cap into one of his lightning clouds and the
//                cloud zaps him. The clouds can be stood on; they strike the floor below them.
//   matriarch    a giant snapper in a whack-a-mole arena of tubes (the up/down tube mouths in the
//                arena). Only the cap hurts her; later she brings decoys.
//   mini=walker  Chief Walker: a big, fast walker. Stomp it three times; the cap only stuns it.
//   mini=buzzer  Buzz Boss: a big buzzer that swoops and drops stingers. Stomp it or cap it.

enum { BOSS_NONE, BOSS_HABERDASHER, BOSS_BIGWALKER, BOSS_CLOUDKING, BOSS_MATRIARCH, NBOSS };
enum { MINI_WALKER = 1, MINI_BUZZER, NMINI };
static const char *const BOSSKIND[NBOSS] = { "", "haberdasher", "bigwalker", "cloudking", "matriarch" };
static const char *const MINIKIND[NMINI] = { "", "walker", "buzzer" };
static const char *const BOSSTITLE[NBOSS] = { "", "THE HABERDASHER", "BIG WALKER", "CLOUD KING", "SNAPPER MATRIARCH" };
static const char *const MINITITLE[NMINI] = { "", "CHIEF WALKER", "BUZZ BOSS" };

// parseheader() hands every option to this first; returns 1 if it was a boss option.
static int bossopt(Level *L, const char *k, const char *v, const char *file, int line) {
  if (!strcmp(k, "boss")) {
    int i = 1; while (i < NBOSS && strcmp(v, BOSSKIND[i])) i++;
    if (i < NBOSS) L->boss = i;
    else levelerr(file, line, "unknown boss \"%s\" (use haberdasher, bigwalker, cloudking or matriarch)", v);
    return 1;
  }
  if (!strcmp(k, "mini")) {
    int i = 1; while (i < NMINI && strcmp(v, MINIKIND[i])) i++;
    if (i < NMINI) L->mini = i;
    else levelerr(file, line, "unknown mini-boss \"%s\" (use walker or buzzer)", v);
    return 1;
  }
  return 0;
}

typedef struct {
  int kind, mini, room, mx, my;   // kind BOSS_* (main) or mini MINI_*; the marker cell
  int on, ax0, ax1, ng;           // 0 waiting, 1 fighting, 2 beaten; gate columns; gate cells shut
  int x, y, vx, vy, face, gnd;    // 1/256 px (walkers: top-left of the body)
  int hp, maxhp, phase, s, t, hurt, n, intro, dead, vol;   // s: what it is doing, t: for how long
  short gc[2*MH][2];
} Boss;
static Boss bz[2];
static int nbz;
enum { BS_SCISSORS = 1, BS_SEED, BS_STING, BS_ZAP };
typedef struct { int a, kind, x, y, vx, vy, t, dir; } BShot;   // 1/256 px
static BShot bsh[24];
// the seesaw (bigwalker): pivot and tilt (px of drop per 256 px), ground-pound slam timer
#define SAWL 60
static int sawx, sawy, sawt, sawslam;
// the cloud king's lightning clouds (1/256 px); charge counts up to a strike
typedef struct { int a, x, y, vx, lane, t, charge, strike, back; } Cloud;
static Cloud bcl[3];
static int floorpx;   // the arena floor under the marker (px)
// the matriarch's tubes, and a decoy snapper
static int mtube[24], nmt, mcur = -1, mofs, dcur = -1, dofs, dph, dt;
// the haberdasher's top hat: 0 on his head, 1 flying, 2 worn by Hatrick (hatt frames left)
static int hatst, hatx, haty, hatt;
// Hatrick on a boss platform (seesaw, cloud): this frame, last frame; a ground pound landed on one
static int bostand, bowas, bopound, bocloud, bossgrace;   // bossgrace: frames bosses can't hurt him

static int bsign(int v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }
static void bshot(int kind, int x, int y, int vx, int vy, int dir) {
  for (BShot *s = bsh; s < bsh+24; s++) if (!s->a) { *s = (BShot){ 1, kind, x, y, vx, vy, 0, dir }; return; }
}
static Boss *mainboss(void) { for (int i = 0; i < nbz; i++) if (bz[i].kind) return bz + i; return 0; }

// Stone gates on the arena's two edge columns, wherever they are empty.
static void bossgates(Boss *b, int shut) {
  u8 (*m)[MW] = wd.rm[b->room];
  int h = LV[lvl].room[b->room].h;
  if (shut) {
    b->ng = 0;
    for (int c = 0; c < 2; c++) {
      int x = c ? b->ax1 : b->ax0;
      for (int y = 0; y < h; y++) if (!m[y][x]) { m[y][x] = 4; b->gc[b->ng][0] = x; b->gc[b->ng++][1] = y; }
      for (int y = 0; y < h; y += 3) part((x*8+4) << 8, (y*8+4) << 8, (c ? 1 : -1)*(rnd(200)+60), -rnd(200), 20+rnd(10), 20, 0xb0b8c4);
    }
    sfx(S_BRICK); kick(8); rumble(3);
  } else {
    for (int i = 0; i < b->ng; i++) {
      int x = b->gc[i][0], y = b->gc[i][1];
      if (m[y][x] == 4) m[y][x] = 0;
      if (!(i % 3)) burst(x*8+4, y*8+4, 0xb0b8c4, 2);
    }
    b->ng = 0;
  }
}
static void bosswin(Boss *b, int points, int x, int y) {
  b->on = 2; b->dead = 1; b->t = 0;
  bossgates(b, 0);
  addscore(points, x, y); sfx(S_BONUS); sfx(S_MOON); kick(14); rumble(8);
  for (int i = 0; i < 4; i++) burst(x + rnd(24)-12, y + rnd(16)-8, i & 1 ? 0xffe066 : 0xffffff, 10);
  for (BShot *s = bsh; s < bsh+24; s++) if (s->a && s->kind != BS_ZAP) burst(s->x >> 8, s->y >> 8, 0xffffff, 3), s->a = 0;
}
static int bosshurt(Boss *b, int x, int y) {   // one hit; returns 1 if that was the last
  if (b->hurt) return 0;
  b->hp--; b->hurt = 50;
  burst(x, y, 0xffffff, 8); sfx(S_STOMP); kick(9); rumble(4);
  return b->hp <= 0;
}

// What a body at (x, y, w, h) px does to Hatrick: 1 he lands on top of it, 2 he touches it.
static int bosstouch(int x, int y, int w, int h) {
  int X = hx >> 8, Y = (hy >> 8) + duck;
  if (st >= TUBE || !ov(X, Y, 6, 11-duck, x, y, w, h)) return 0;
  return (hvy > 0 || st == GPSLAM) && (hy >> 8) + 11 < y + 7 ? 1 : 2;
}
static void bossbounce(void) {   // off a boss's head, as off an enemy
  hvy = prevk & 16 ? -1000 : -650; st = NORM; arcg = GRAV; launch = 0; cut = 0; capok = diveok = stall = 1; spin = throwt = 0;
}
static int bosscap(int x, int y, int w, int h) { return cst && cst < 3 && ov(CAPBOX, x, y, w, h); }
static void capclink(void) { burst((cxp >> 8)+4, (cyp >> 8)+2, 0xffffff, 4); sfx(S_BOUNCE); cst = 3; }
// Anything a boss does to Hatrick: fatal, unless he wears the haberdasher's hat, which it knocks off.
static void bossharm(int fromx) {
  if (st >= TUBE || bossgrace) return;
  if (hatst == 2) {
    hatst = 1; hatx = (hx >> 8) + 3; haty = (hy >> 8) - 4; hatt = 0;
    hvx = (hx >> 8) + 3 < fromx ? -600 : 600; hvy = -700; st = NORM; gnd = 0; spin = throwt = 0; bossgrace = 60;
    burst(hatx, haty, 0x40304a, 8); sfx(S_BOUNCE); rumble(4); kick(6);
    return;
  }
  die();
}
// Walking bosses: gravity and the map, a body w x h px; returns 1 if it bumped into a wall.
static int bossmove(Boss *b, int w, int h) {
  b->vy += GRAV; if (b->vy > 1100) b->vy = 1100;
  b->y += b->vy; b->gnd = 0;
  int x = b->x >> 8, y = b->y >> 8, n = 0;
  if (scan(x, y, w, h, SOLID)) {
    int s = b->vy > 0 ? -1 : 1;
    do y += s; while (scan(x, y, w, h, SOLID) && ++n < 40);
    b->y = y << 8; if (b->vy > 0) b->gnd = 1; b->vy = 0;
  }
  b->x += b->vx; x = b->x >> 8;
  if (scan(x, y, w, h, SOLID)) { b->x -= b->vx; b->vx = -b->vx; return 1; }
  return 0;
}

// A boss platform: its top at surf (px) from x0 to x1. Hatrick lands and stands on it.
static int bossplat(int surf, int x0, int x1) {
  int X = hx >> 8, feet = (hy >> 8) + 11, pfeet = (oldhy >> 8) + 11;
  if (bostand || st >= TUBE || st == HANG || st == CLIMB || hvy < 0 || X+5 < x0 || X > x1) return 0;
  if (feet < surf - 3 || (pfeet > surf + 2 && !(bowas && feet <= surf + 8))) return 0;
  if (scan(X, surf - 11 + duck, 6, 11 - duck, SOLID)) return 0;
  hy = (surf - 11) << 8; hvy = 0;
  if (!bowas) {   // landing, as hero() lands on the map
    landt = 0;
    if (st == GPSLAM) { st = GPLAND; stt = 0; poundt = 31; kick(10); sfx(S_GPLAND); rumble(6); bopound = 1; }
    else if (st == DIVE) st = SLIDE, posture(5);
    else if (st == LONGJ || st == SPINJ) st = NORM;
  }
  gnd = 1; coy = 0; bostand = 1;
  return 1;
}

// ---- the haberdasher ----
static void haberdasher(Boss *b) {
  int bx = b->x >> 8, by = b->y >> 8, X = hx >> 8, cx = bx + 5;
  int dx = X + 3 - cx, angry = b->phase == 2;
  if (b->dead) { b->vy += 20; b->y += b->vy; b->t++; if (!(b->t & 7)) burst(cx, by + 8, b->t & 8 ? 0xff7a1c : 0x40304a, 3); return; }
  if (b->intro) { b->intro--; b->face = bsign(dx) ? bsign(dx) : -1; bossmove(b, 10, 18); }
  else if (b->s == 0) {   // walk to a new spot, facing Hatrick
    if (!b->t) {
      int spot = (b->ax0+3)*8 + rnd((b->ax1 - b->ax0 - 6)*8);
      b->vx = bsign(spot - cx) * (angry ? 230 : 130); b->t = angry ? 50 + rnd(30) : 60 + rnd(40);
    }
    if (angry && hatst == 2) b->vx = bsign(dx) * 260, b->t = b->t > 2 ? b->t : 2;   // chasing his hat
    if (bossmove(b, 10, 18)) b->vx = -b->vx;
    if (angry && b->gnd && !rnd(hatst == 2 ? 50 : 110)) b->vy = -1000, b->vx = bsign(dx)*260;
    b->face = bsign(b->vx) ? bsign(b->vx) : b->face;
    if (--b->t <= 0) { b->s = 1; b->t = 0; b->n = 0; b->vx = 0; }
  } else if (b->s == 1) {   // throw scissors: a volley of 2 (3 when angry)
    bossmove(b, 10, 18);
    b->face = dx < 0 ? -1 : 1;
    if (hatst == 2) { b->s = 0; b->t = 0; }
    else if (++b->t == 22) {
      int aim = ((hy >> 8) + 5 - (by + 6)) * 256 / 70;
      bshot(BS_SCISSORS, (cx + b->face*6) << 8, (by + 6) << 8, b->face*(angry ? 1150 : 950), aim > 400 ? 400 : aim < -400 ? -400 : aim, b->face);
      sfx(S_THROW);
      b->t = 0;
      if (++b->n >= (angry ? 3 : 2)) { b->t = 0; b->n = 0; b->s = angry ? 0 : 2; }
    }
  } else if (b->s == 2) {   // a volley or two thrown: he waits for them to come back, then pants
    bossmove(b, 10, 18);
    int out = 0; for (BShot *s = bsh; s < bsh+24; s++) out += s->a && s->kind == BS_SCISSORS;
    if (!out) { if (++b->vol >= 2) b->s = 3, b->t = 110, b->vol = 0; else b->s = 0, b->t = 0; }
  } else if (b->s == 3) {   // panting: open to a stomp or the cap
    bossmove(b, 10, 18);
    if (--b->t <= 0) b->s = 0, b->t = 0;
  }
  if (b->hurt) b->hurt--;
  bx = b->x >> 8; by = b->y >> 8; cx = bx + 5;
  int open = b->s == 3 || hatst == 2, top = hatst == 0 ? by - 7 : by;
  int touch = bosstouch(bx, top, 10, 18 + by - top);
  if (touch == 1 && open && !b->hurt) {
    bossbounce();
    int last = bosshurt(b, cx, by);
    if (hatst == 2) hatst = 1, hatx = X + 3, haty = (hy >> 8) - 4, hatt = 0;   // the hat flies back off
    b->s = 0; b->t = 30; b->vy = -700; b->vx = bsign(-dx)*200;
    if (last && b->phase == 1) {   // phase 2: the hat flies off
      b->phase = 2; b->hp = b->maxhp = 3; hatst = 1; hatx = cx; haty = by - 6; b->hurt = 60;
      sfx(S_REVEAL); burst(cx, by - 4, 0x40304a, 12);
    } else if (last) bosswin(b, 10000, cx, by);
  } else if (touch == 1 && !b->hurt) { bossbounce(); sfx(S_BOUNCE); burst(cx, top, 0xffffff, 4); }   // the hat's pins: no harm done
  else if (touch == 2 && !b->hurt) bossharm(cx);
  if (bosscap(bx, top, 10, 18 + by - top) && !(hatst == 1 && bosscap(hatx - 5, haty - 4, 10, 8))) {   // the hat wins
    if (b->s == 3 && !b->hurt) {
      cst = 3; int last = bosshurt(b, cx, by); b->s = 0; b->t = 30; b->vy = -600;
      if (last) { b->phase = 2; b->hp = b->maxhp = 3; hatst = 1; hatx = cx; haty = by - 6; b->hurt = 60; sfx(S_REVEAL); burst(cx, by - 4, 0x40304a, 12); }
    } else capclink();
  }
  // the top hat: circles high over the arena; the cap catches it and Hatrick wears it a while
  if (hatst == 1) {
    int mid = (b->ax0 + b->ax1) * 4 + 4, tx = mid + SIN[(fr*2) & 255]*90/256, ty = floorpx - 60 + SIN[(fr*5) & 255]*12/256;
    hatx += bsign(tx - hatx) * (iabs(tx - hatx) > 2 ? 2 : 1); haty += bsign(ty - haty);
    if (bosscap(hatx - 5, haty - 4, 10, 8)) { hatst = 2; hatt = 420; cst = 3; sfx(S_CATCH); sfx(S_REVEAL); sparkle(hatx, haty, 0xfff0a8, 10); }
  } else if (hatst == 2 && (--hatt <= 0 || st >= TUBE)) { hatst = 1; hatx = X + 3; haty = (hy >> 8) - 4; burst(hatx, haty, 0x40304a, 5); }
}

// ---- the big walker on its seesaw ----
static int sawsurf(int x) { return sawy + (x - sawx) * sawt / 256; }
static void bigwalker(Boss *b) {
  int cx = (b->x >> 8) + 12, off = cx - sawx, hoff = (hx >> 8) + 3 - sawx;
  // the board: leans under the walker's weight (and Hatrick's), or slammed by a ground pound
  int want = off * 44 / SAWL + (bostand && !bocloud ? hoff * 20 / SAWL : 0);
  if (sawslam) sawslam--;
  else sawt += (want - sawt) / 8;
  if (b->dead) { b->y += 128; b->t++; if (!(b->t & 3)) part((cx + rnd(24) - 12) << 8, (lh*8 - 10) << 8, 0, -rnd(500), 20, 20, 0xff9a2a); return; }
  int speed = 90 + (b->maxhp - b->hp) * 55;
  if (b->intro) { b->intro--; b->y = (sawsurf(cx) - 24) << 8; }
  else if (b->s == 0) {   // walking the board, turning at its ends, now and then a stomp that shakes it
    if (iabs(off) > SAWL - 14 && bsign(off) == b->face) b->face = -b->face;
    b->x += b->face * speed;
    b->y = (sawsurf(cx) - 24) << 8;
    if (++b->t > 150 - (b->maxhp - b->hp) * 30) b->s = 1, b->t = 0, b->vy = -950;
  } else if (b->s == 1) {   // jump; landing shakes Hatrick off his feet
    b->vy += GRAV; b->y += b->vy; b->x += b->face * speed / 2;
    if (iabs(cx - sawx) > SAWL - 14) b->x -= b->face * speed / 2;
    if (b->vy > 0 && (b->y >> 8) + 24 >= sawsurf(cx)) {
      b->y = (sawsurf(cx) - 24) << 8; b->s = 0; b->t = 0; kick(10); sfx(S_GPLAND); rumble(5);
      if (bostand && !bocloud) hvy = -700, gnd = 0, bowas = 0;
      burst(cx, sawsurf(cx), 0xc8a070, 6);
    }
  } else if (b->s == 2) {   // the board slammed under him: he slides off its low end
    b->vx += bsign(sawt) * 40; b->x += b->vx;
    b->y = (sawsurf((b->x >> 8) + 12) - 24) << 8;
    if (iabs((b->x >> 8) + 12 - sawx) > SAWL + 6) b->s = 3, b->vy = -300;
  } else if (b->s == 3) {   // falling into the lava
    b->vy += GRAV; b->y += b->vy; b->x += b->vx / 3;
    if ((b->y >> 8) + 12 > lh*8 - 8) {
      for (int i = 0; i < 16; i++) part((cx + rnd(24) - 12) << 8, (lh*8 - 10) << 8, rnd(400)-200, -rnd(900)-200, 30, 30, i & 1 ? 0xff5a1a : 0xffd040);
      kick(12); rumble(6); sfx(S_STOMP);
      if (bosshurt(b, cx, lh*8 - 16)) { bosswin(b, 5000, cx, lh*8 - 40); return; }
      b->s = 4; b->t = 0; b->n = b->x;   // a flaming leap back to the middle of the board
    }
  } else if (b->s == 4) {
    int T = 70, t = ++b->t, x0 = b->n, x1 = (sawx - 12) << 8;
    b->x = x0 + (x1 - x0) / T * t;
    int top = sawsurf(sawx) - 24, base = lh*8 - 20, peak = top - 50;
    // a parabola from the lava up over the peak and down onto the board
    int p = t * 256 / T, y = base + (top - base) * p / 256 - (4 * (peak - (base + top)/2) * p / 256 * (256 - p) / 256) * -1;
    b->y = y << 8;
    if (!(t & 1)) part((cx + rnd(20) - 10) << 8, (b->y >> 8) + 20 << 8, 0, 200, 16, 0, t & 2 ? 0xff7a1a : 0xffd040);
    if (t >= T) { b->s = 0; b->t = 0; b->y = (sawsurf(sawx) - 24) << 8; kick(10); sfx(S_GPLAND); if (bostand && !bocloud) hvy = -700, gnd = 0, bowas = 0; }
  }
  if (b->hurt) b->hurt--;
  // a ground pound on the board: on his side it tips him off; on the other it just rocks it
  if (bopound && !bocloud && b->s == 0) {
    if (bsign(hoff) == bsign(off) && iabs(off) > 6) { sawt = bsign(off) * 120; sawslam = 70; b->s = 2; b->vx = 0; sfx(S_SKID); }
    else { sawt = bsign(hoff) * 70; sawslam = 20; }
  }
  int bx = b->x >> 8, by = b->y >> 8, touch = bosstouch(bx + 2, by + 3, 20, 21);
  if (touch == 1 && b->s != 4) { bossbounce(); sfx(S_BOUNCE); burst(cx, by + 2, 0xffffff, 4); b->face = -b->face; }
  else if (touch) bossharm(cx);
  if (bosscap(bx + 2, by + 3, 20, 21)) capclink();
}

// ---- the cloud king ----
static void cloudking(Boss *b) {
  int mid = (b->ax0 + b->ax1) * 4 + 4, range = (b->ax1 - b->ax0) * 4 - 40, lost = b->maxhp - b->hp;
  if (b->dead) { b->vy += 12; b->y += b->vy; b->t++; if (!(b->t & 3)) part(b->x + (rnd(24) - 12 << 8), b->y, 0, 0, 20, 0, 0xfff0a0); return; }
  b->t++;
  int sp = 2 + lost;   // he drifts across the sky, faster when hurt
  b->x = (mid << 8) + SIN[(b->t*sp/2) & 255] * range;
  b->y = (b->my*8 + 4 << 8) + SIN[(b->t*5) & 255] * 6;
  if (b->hurt) b->hurt--;
  int kx = b->x >> 8, ky = b->y >> 8;
  if (bosscap(kx - 13, ky - 9, 26, 18)) capclink();
  if (bosstouch(kx - 12, ky - 8, 24, 16)) bossharm(kx);
  if (b->intro) { b->intro--; return; }
  // his clouds: puffed out of him, they sink to a lane and drift; they charge, then strike
  for (int i = 0; i < 3; i++) {
    Cloud *c = bcl + i;
    int lane = floorpx - 30 - (i == 1) * 28;
    if (!c->a) {
      if (c->back > 0) { c->back--; continue; }
      *c = (Cloud){ 1, b->x, b->y, (i & 1 ? 1 : -1) * (90 + lost*30), lane, 60 + i*70, 0, 0, 0 };
      sfx(S_EMERGE); burst(kx, ky + 6, 0xd8e0f0, 6);
    }
    if (c->y >> 8 < c->lane) c->y += 256;
    c->x += c->vx;
    if (c->x >> 8 < (b->ax0+1)*8 + 2 || (c->x >> 8) + 24 > b->ax1*8 - 2) c->vx = -c->vx, c->x += c->vx;
    if (c->strike) { if (--c->strike == 0) c->t = 160 - lost*35 + rnd(60); }
    else if (c->charge) { if (++c->charge > 50) c->charge = 0, c->strike = 22, kick(4), sfx(S_GPLAND), rumble(2); }
    else if (--c->t <= 0) c->charge = 1;
    int cx = (c->x >> 8) + 12, cy = c->y >> 8;
    if (c->strike && st < TUBE && ov(hx >> 8, (hy >> 8) + duck, 6, 11-duck, cx - 3, cy + 8, 6, floorpx - cy - 8)) bossharm(cx);
    if (bosscap(cx - 12, cy - 4, 24, 12)) {   // the cap in a cloud: it zaps the king
      cst = 3; c->a = 0; c->back = 150;
      bshot(BS_ZAP, cx << 8, cy << 8, 0, -900, 0);
      sfx(S_CATCH); sparkle(cx, cy, 0xfff070, 10); burst(cx, cy, 0xd8e0f0, 8);
    }
  }
}

// ---- the snapper matriarch ----
static void mspot(int i, int *cx, int *my, int *up) {
  const Tube *t = LV[lvl].tube + i;
  *cx = t->x*8+8; *up = t->dir == T_UP; *my = *up ? t->y*8 : t->y*8+8;
}
static int mpick(int not1, int not2) {   // a tube away from Hatrick
  int best = -1, tries = 0;
  while (nmt && tries++ < 30) {
    int i = mtube[rnd(nmt)], cx, my, up; mspot(i, &cx, &my, &up);
    if (i == not1 || i == not2) continue;
    best = i;
    if (iabs((hx >> 8) + 3 - cx) > 28) break;
  }
  return best;
}
static int mhit(int tube, int ofs, int half, int x, int y, int w, int h) {   // the body ofs px out, 2*half wide
  int cx, my, up; mspot(tube, &cx, &my, &up);
  return ofs > 3 && ov(x, y, w, h, cx - half, up ? my - ofs + 1 : my, 2*half, ofs - 1);
}
static void matriarch(Boss *b) {
  int X = hx >> 8, Y = (hy >> 8) + duck, lost = b->maxhp - b->hp;
  if (b->hurt) b->hurt--;
  if (b->dead) { if (mofs > 0) mofs--; if (dofs > 0) dofs--; return; }
  if (b->intro) { b->intro--; return; }
  if (mcur < 0) { mcur = mpick(-1, dcur); if (mcur < 0) return; b->s = 0; b->t = 50; }
  if (b->s == 0) { if (--b->t <= 0) b->s = 1, sfx(S_EMERGE); }
  else if (b->s == 1) { mofs += 2; if (mofs >= 28) mofs = 28, b->s = 2, b->t = 90 - lost*10; }
  else if (b->s == 2) {
    if (b->t == 60 - lost*8 || (lost >= 2 && b->t == 30)) {   // a fan of seeds at Hatrick
      int cx, my, up; mspot(mcur, &cx, &my, &up);
      int dx = (X + 3 - cx) * 256 / 80;
      for (int k = -1; k <= 1; k++) {
        int vx = dx + k*140; vx = vx > 420 ? 420 : vx < -420 ? -420 : vx;
        bshot(BS_SEED, cx << 8, (up ? my - 26 : my + 26) << 8, vx, up ? -620 : 0, 0);
      }
      sfx(S_SPIT);
    }
    if (--b->t <= 0) b->s = 3;
  } else if (mofs > 0) { mofs -= b->hurt ? 4 : 2; if (mofs <= 0) { mofs = 0; int was = mcur; mcur = mpick(was, dcur); b->s = 0; b->t = 40 - lost*5; } }
  if (mcur >= 0 && st < TUBE && mhit(mcur, mofs, 6, X, Y, 6, 11-duck)) die();
  if (mcur >= 0 && cst && cst < 3 && mhit(mcur, mofs, 7, CAPBOX) && b->s != 3) {
    int cx, my, up; mspot(mcur, &cx, &my, &up);
    cst = 3;
    if (bosshurt(b, cx, up ? my - mofs/2 : my + mofs/2)) { bosswin(b, 5000, cx, up ? my - 20 : my + 20); b->s = 3; return; }
    b->s = 3;
  }
  // a decoy: an ordinary snapper in another tube, once she's been hurt twice
  if (lost >= 2) {
    if (dcur < 0 && !dofs) { if (--dt <= 0) { dcur = mpick(mcur, -1); dph = 1; } }
    else if (dph == 1) { if (++dofs >= 16) dph = 2, dt = 80; }
    else if (dph == 2) { if (--dt <= 0) dph = 3; }
    else if (dph == 3 && --dofs <= 0) dofs = 0, dcur = -1, dph = 0, dt = 60 + rnd(60);
    if (dcur >= 0 && st < TUBE && mhit(dcur, dofs, 3, X, Y, 6, 11-duck)) die();
    if (dcur >= 0 && cst && cst < 3 && mhit(dcur, dofs, 4, CAPBOX) && dph != 3) {
      int cx, my, up; mspot(dcur, &cx, &my, &up);
      burst(cx, up ? my - dofs/2 : my + dofs/2, 0x2f8f9a, 8); sfx(S_STOMP); addscore(200, cx, up ? my - dofs : my);
      dofs = 0; dcur = -1; dph = 0; dt = 120;
    }
  }
}

// ---- mini-bosses: a buffed walker or buzzer in a locked room ----
static void miniwalker(Boss *b) {
  int bx = b->x >> 8, by = b->y >> 8, dx = (hx >> 8) + 3 - (bx + 8);
  if (b->dead) { b->vy += GRAV; b->y += b->vy; return; }
  if (b->hurt) b->hurt--;
  if (b->intro) { b->intro--; bossmove(b, 16, 16); }
  else if (b->s == 1) { b->vx = 0; bossmove(b, 16, 16); if (--b->t <= 0) b->s = 0; }   // stunned by the cap
  else {
    int speed = 110 + (b->maxhp - b->hp) * 50;
    if (b->gnd && !(fr & 31)) b->face = dx < 0 ? -1 : 1;
    if (b->hurt > 30) b->vx = -bsign(dx) * 200;   // backs off after a hit
    else b->vx = b->face * speed;
    if (bossmove(b, 16, 16)) b->face = -b->face;
    if (b->gnd && ++b->t > 100 - (b->maxhp - b->hp)*20) b->t = 0, b->vy = -850, kick(2);
  }
  bx = b->x >> 8; by = b->y >> 8;
  int touch = bosstouch(bx + 1, by + 2, 14, 14);
  if (touch == 1 && !b->hurt) { bossbounce(); if (bosshurt(b, bx + 8, by)) bosswin(b, 1000, bx + 8, by); }
  else if (touch == 1) bossbounce();
  else if (touch == 2 && !b->hurt) bossharm(bx + 8);
  if (bosscap(bx, by, 16, 16)) { capclink(); if (b->s != 1) b->s = 1, b->t = 45, sparkle(bx + 8, by, 0xfff0a8, 6); }
  if (by > lh*8) bosswin(b, 1000, bx + 8, lh*8 - 16);
}
static void minibuzzer(Boss *b) {
  int cx = (b->x >> 8) + 8, cy = (b->y >> 8) + 8, lost = b->maxhp - b->hp;
  if (b->dead) { b->vy += GRAV; b->y += b->vy; return; }
  if (b->hurt) b->hurt--;
  b->t++;
  int hx0 = b->mx*8 + 4, hy0 = b->my*8 + 4;
  if (b->intro) b->intro--;
  else if (b->s == 0) {   // a figure of eight around its home, dropping stingers
    int a = b->t * (3 + lost) / 2;
    int tx = hx0 + SIN[a & 255] * 72 / 256, ty = hy0 + SIN[(a*2) & 255] * 18 / 256;
    b->x += ((tx - 8 << 8) - b->x) / 6; b->y += ((ty - 8 << 8) - b->y) / 6;
    if (!(b->t % (70 - lost*15))) bshot(BS_STING, cx << 8, (cy + 6) << 8, 0, 420, 0), sfx(S_SPIT);
    if (b->t % 240 == 239) b->s = 1, b->n = 0, b->vx = ((hx >> 8) + 3 - cx) * 256 / 40, b->vy = ((hy >> 8) + 2 - cy) * 256 / 40;
  } else {   // a swoop at where Hatrick was, then back up
    b->x += b->vx; b->y += b->vy;
    if (++b->n == 40) b->vx = -b->vx, b->vy = -b->vy;
    if (b->n >= 80) b->s = 0;
    if (scan(b->x >> 8, b->y >> 8, 16, 16, SOLID) && b->n < 40) b->vx = -b->vx, b->vy = -b->vy, b->n = 80 - b->n;
  }
  int bx = b->x >> 8, by = b->y >> 8, touch = bosstouch(bx + 1, by + 2, 14, 12);
  if (touch == 1 && !b->hurt) { bossbounce(); if (bosshurt(b, cx, cy)) bosswin(b, 1000, cx, cy), b->vy = -400; }
  else if (touch == 1) bossbounce();
  else if (touch == 2 && !b->hurt) bossharm(cx);
  if (bosscap(bx, by, 16, 14) && !b->hurt) { cst = 3; if (bosshurt(b, cx, cy)) bosswin(b, 1000, cx, cy), b->vy = -400; }
}

// ---- boss shots: scissors, seeds, stingers, and the zaps that hit the cloud king ----
static void bossshots(void) {
  Boss *k = mainboss();
  for (BShot *s = bsh; s < bsh+24; s++) {
    if (!s->a) continue;
    s->t++;
    if (s->kind == BS_SCISSORS) {   // out, slowing, then back to the haberdasher
      s->vx -= s->dir * 22;
      if (k && s->t > 30) { int ty = (k->y >> 8) + 6 << 8; s->vy += bsign(ty - s->y) * 12; if (s->vy > 300) s->vy = 300; if (s->vy < -300) s->vy = -300; }
      if (k && s->t > 40 && iabs(s->x - (k->x + (5 << 8))) < 6 << 8 && iabs(s->y - (k->y + (6 << 8))) < 12 << 8) { s->a = 0; continue; }
      if (s->t > 260) { s->a = 0; continue; }
    } else if (s->kind == BS_SEED) s->vy += 14;
    else if (s->kind == BS_ZAP && k) {   // homes in on the king
      int dx = k->x - s->x, dy = k->y - s->y, d = iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy);
      if (d < 10 << 8) {
        s->a = 0;
        if (!k->dead && bosshurt(k, k->x >> 8, k->y >> 8)) bosswin(k, 5000, k->x >> 8, k->y >> 8);
        sparkle(k->x >> 8, k->y >> 8, 0xfff070, 12);
        continue;
      }
      if (d) { s->vx += dx * 160 / d - s->vx / 8; s->vy += dy * 160 / d - s->vy / 8; }
      if (!(fr & 1)) part(s->x, s->y, 0, 0, 10, 0, 0xfff070);
    }
    s->x += s->vx; s->y += s->vy;
    int sx = s->x >> 8, sy = s->y >> 8;
    if (sy > lh*8 + 16 || sy < -64 || sx < -16 || sx > lw*8 + 16) { s->a = 0; continue; }
    if (s->kind == BS_ZAP) continue;
    if ((s->kind == BS_SEED || s->kind == BS_STING) && SOLID >> tile(sx >> 3, sy >> 3) & 1) { s->a = 0; burst(sx, sy, 0x9a6a3a, 2); continue; }
    if (st < TUBE && ov(hx >> 8, (hy >> 8) + duck, 6, 11-duck, sx-3, sy-3, 6, 6)) { bossharm(sx); s->a = 0; continue; }
    if (cst && cst < 3 && ov(CAPBOX, sx-3, sy-3, 6, 6)) { s->a = 0; burst(sx, sy, 0xc0c8d0, 5); addscore(100, sx, sy); sfx(S_BOUNCE); }
  }
}

// Sets up the bosses of the current level; called whenever Hatrick (re)starts it.
static void bossstart(void) {
  const Level *L = LV + lvl;
  nbz = 0; memset(bsh, 0, sizeof bsh); memset(bcl, 0, sizeof bcl);
  bostand = bowas = bopound = bocloud = bossgrace = 0; hatst = hatt = 0; nmt = 0; mcur = dcur = -1; mofs = dofs = dph = 0; dt = 60; sawslam = 0;
  for (int r = 0; r < L->nroom; r++) for (int y = 0; y < L->room[r].h; y++) for (int x = 0; x < L->room[r].w; x++) {
    int c = GRID(L->room + r, x, y);
    if ((c != 'N' || !L->boss) && (c != 'J' || !L->mini) || nbz == 2) continue;
    Boss *b = bz + nbz++;
    memset(b, 0, sizeof *b);
    b->room = r; b->mx = x; b->my = y; b->face = -1; b->intro = 60;
    if (c == 'N') b->kind = L->boss; else b->mini = L->mini;
    int w = L->room[r].w;
    b->ax0 = x - 15 < 0 ? 0 : x - 15; b->ax1 = x + 16 > w-1 ? w-1 : x + 16;
    b->hp = b->maxhp = 3; b->phase = 1;
    if (b->kind == BOSS_MATRIARCH) b->hp = b->maxhp = 5;
    b->x = (x*8 - 1) << 8; b->y = (y*8 - 10) << 8;   // a walking body stands on the marker's floor
    if (b->mini == MINI_WALKER) b->x = (x*8 - 4) << 8, b->y = (y*8 - 8) << 8;
    if (b->mini == MINI_BUZZER) b->x = (x*8 - 4) << 8, b->y = (y*8 - 4) << 8;
    if (b->kind) {
      int fy = y; while (fy < L->room[r].h - 1 && !(SOLID >> tiletype(GRID(L->room + r, x, fy)) & 1)) fy++;
      floorpx = fy*8;
    }
    if (b->kind == BOSS_BIGWALKER) { sawx = x*8 + 4; sawy = y*8; sawt = 0; b->x = (sawx - 12) << 8; b->y = (sawy - 24) << 8; b->face = 1; }
    if (b->kind == BOSS_CLOUDKING) b->x = (x*8 + 4) << 8, b->y = (y*8 + 4) << 8;
    if (b->kind == BOSS_MATRIARCH)
      for (int i = 0; i < L->ntube && nmt < 24; i++) {
        const Tube *t = L->tube + i;
        if (t->room == r && t->x > b->ax0 && t->x < b->ax1 && (t->dir == T_UP || t->dir == T_DOWN)) mtube[nmt++] = i;
      }
    if (haveck && ckroom == r && (ckx >> 11) > b->ax1) b->on = 2, b->dead = 2;   // beaten before the checkpoint
  }
}

// The camera settles on the arena during a fight (called at the end of camera()).
static void bosscam(void) {
  for (Boss *b = bz; b < bz + nbz; b++) if (b->room == room && b->on == 1 && !b->dead) { cxf += ((b->ax0*8 << 8) - cxf) / 3; return; }
}
static void bosstick(void) {
  bossnear = 0;   // the music's danger strings, while a fight is on
  if (!nbz) return;
  bowas = bostand; bostand = 0;
  if (bossgrace) bossgrace--;
  int X = hx >> 8;
  for (Boss *b = bz; b < bz + nbz; b++) {
    if (b->room != room || b->dead == 2) continue;
    if (!b->on) {   // waiting for Hatrick to walk in
      if (st < TUBE && X >= (b->ax0+2)*8 && X+6 <= (b->ax1-1)*8) { b->on = 1; bossgates(b, 1); sfx(S_REVEAL); }
      else if (b->kind != BOSS_BIGWALKER && b->kind != BOSS_CLOUDKING) continue;
    }
    if (b->on == 2 && b->dead && b->t > 200) continue;
    if (b->on == 0) continue;
    switch (b->kind) {
      case BOSS_HABERDASHER: haberdasher(b); break;
      case BOSS_BIGWALKER: bigwalker(b); break;
      case BOSS_CLOUDKING: cloudking(b); break;
      case BOSS_MATRIARCH: matriarch(b); break;
      default: if (b->mini == MINI_WALKER) miniwalker(b); else minibuzzer(b);
    }
    if (b->dead) b->t++;
    if (b->on == 1 && !b->dead) bossnear = 1;
  }
  bossshots();
  // platforms: the seesaw and the clouds
  bopound = 0; int was = bocloud; bocloud = 0;
  for (Boss *b = bz; b < bz + nbz; b++) if (b->room == room) {
    if (b->kind == BOSS_BIGWALKER) {
      int x = (hx >> 8) + 3;
      if (x >= sawx - SAWL && x <= sawx + SAWL) bossplat(sawsurf(x), sawx - SAWL, sawx + SAWL);
      if (b->on && st < TUBE && (hy >> 8) + 11 > lh*8 - 8 && tile(x >> 3, lh - 1) == 0) doom();   // the lava
    }
    if (b->kind == BOSS_CLOUDKING && !b->dead)
      for (Cloud *c = bcl; c < bcl+3; c++) if (c->a && (c->y >> 8) >= c->lane - 1) {
        if (bossplat((c->y >> 8) - 4, (c->x >> 8) + 1, (c->x >> 8) + 23)) { hx += c->vx; bocloud = 1; }
      }
  }
  (void)was;
}

// ---- drawing ----
static void wbox(int x, int y, int w, int h, u32 c) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) wpx(x+i, y+j, c); }
static void wcirc(int cx, int cy, int rx, int ry, u32 c) {
  for (int y = -ry; y <= ry; y++) for (int x = -rx; x <= rx; x++) if (x*x*ry*ry + y*y*rx*rx <= rx*rx*ry*ry + rx*ry) wpx(cx+x, cy+y, c);
}
static void drawtophat(int cx, int bottom, int tilt) {   // the haberdasher's hat, brim at bottom
  for (int j = 0; j < 7; j++) for (int i = -3; i <= 3; i++) wpx(cx + i + (tilt*(7-j))/8, bottom - 8 + j, j == 4 || j == 5 ? 0xc8283c : i == -3 ? 0x5a4a66 : 0x241c2c);
  for (int i = -5; i <= 5; i++) wpx(cx + i, bottom - 1, 0x241c2c);
  wpx(cx - 2 + tilt, bottom - 7, 0x6a5a78);
}
static void drawscissors(int x, int y, int t) {
  int a = t * 24;
  for (int k = 0; k < 2; k++) {
    int an = (a + k*64) & 255, c = SIN[(an+64) & 255], s = SIN[an];
    for (int i = -4; i <= 4; i++) wpx(x + c*i/256, y + s*i/256, i > 0 ? 0xe8eef4 : 0x9aa4b0);
    int hxp = x - c*5/256, hyp = y - s*5/256;
    wpx(hxp, hyp-1, 0xd02838); wpx(hxp-1, hyp, 0xd02838); wpx(hxp+1, hyp, 0xd02838); wpx(hxp, hyp+1, 0xd02838);
  }
}
static const u32 BIGPAL[3] = { 0xe0602a, 0xffe0b0, 0x401808 }, FLAMEPAL[3] = { 0xffc040, 0xffffff, 0xd03010 };
static const u32 CHIEFPAL[3] = { 0x3a8a48, 0xffffff, 0x10301a }, BUZZBOSSPAL[3] = { 0xe84a3c, 0xffe8e0, 0x2a1010 };
static const u32 MATPAL[3] = { 0x8a3ab0, 0xffe0f4, 0x241030 }, DECOYPAL[3] = { 0x2f8f9a, 0xf4e6c0, 0x1b2433 };
static void drawsnapper(int tube, int ofs, int scale, const u32 *pal, int crown) {   // only the part out of the tube
  int cx, my, up; mspot(tube, &cx, &my, &up);
  const u16 *f = fr >> 3 & 1 ? SNAP2 : SNAP1;
  int hgt = 16*scale;
  for (int v = 0; v < hgt && v < ofs + 2; v++) for (int u = 0; u < 8*scale; u++) {
    int c = f[v/scale] >> (14 - 2*(u/scale)) & 3;
    if (!c) continue;
    int y = up ? my - ofs + v : my + ofs - 1 - v;
    if (up ? y >= my : y < my) continue;
    wpx(cx - 4*scale + u, y, pal[c-1]);
  }
  if (crown && ofs > 4) {   // a little gold crown on her head
    int y = up ? my - ofs - 3 : my + ofs;
    for (int i = -4; i <= 4; i++) wpx(cx + i, up ? y + 2 : y, 0xffd84a);
    for (int i = -4; i <= 4; i += 4) wpx(cx + i, up ? y + 1 : y + 1, 0xffd84a), wpx(cx + i, up ? y : y + 2, 0xfff0a0);
  }
}

static void bossdraw(void) {
  for (Boss *b = bz; b < bz + nbz; b++) {
    if (b->room != room || b->dead == 2 || (b->dead && b->t > 200)) continue;
    int blink = b->hurt && (fr & 2), bx = b->x >> 8, by = b->y >> 8;
    if (b->kind == BOSS_BIGWALKER) {
      // lava under the arena, the board and its post
      for (int x = b->ax0*8; x < b->ax1*8 + 8; x++) if (tile(x >> 3, lh - 1) == 0)
        for (int y = lh*8 - 10; y < lh*8; y++) wpx(x, y, y == lh*8 - 10 + ((x + fr/4) % 9 == 0) ? 0xffd040 : y < lh*8 - 7 ? 0xff7a1a : 0xc83a10);
      for (int y = sawy + 3; y < lh*8 - 10; y++) for (int i = -2; i <= 2; i++) wpx(sawx + i, y, i == -2 ? 0x8a94a0 : i == 2 ? 0x404850 : 0x6a7480);
      for (int x = sawx - SAWL; x <= sawx + SAWL; x++) {
        int s = sawsurf(x);
        for (int j = 0; j < 4; j++) wpx(x, s + j, j == 0 ? 0xf0c888 : (x & 15) == 0 ? 0x6a3a18 : j == 3 ? 0x7a4420 : 0xb87838);
      }
      for (int i = -3; i <= 3; i++) wpx(sawx + i, sawy + 3 + iabs(i)/2, 0xffd84a);
    }
    if (b->kind == BOSS_MATRIARCH) {
      if (mcur >= 0 && mofs && !blink) drawsnapper(mcur, mofs, 2, MATPAL, 1);
      if (dcur >= 0 && dofs) drawsnapper(dcur, dofs, 1, DECOYPAL, 0);
    }
    if (blink) continue;
    if (b->kind == BOSS_HABERDASHER) {
      int cx = bx + 5, f = b->face, red = b->phase == 2;
      int step = b->vx && b->gnd ? (fr >> 3 & 1) : 0, dead = b->dead;
      u32 coat = red ? 0xa02838 : 0x5a2a7a, coat2 = red ? 0xd04050 : 0x7a44a0;
      if (dead) { by += 0; }
      // legs, coat, tape measure, head, monocle and moustache
      wbox(cx - 3 - step, by + 13, 2, 5, 0x2a2430); wbox(cx + 1 + step, by + 13, 2, 5, 0x2a2430);
      wbox(cx - 4, by + 6, 8, 8, coat); wbox(cx - 4 + (f > 0 ? 0 : 6), by + 6, 2, 8, coat2);
      for (int i = 0; i < 3; i++) wpx(cx + f, by + 8 + i*2, 0xffd84a);
      for (int i = 0; i < 6; i++) wpx(cx - 3 + i, by + 6 + (i > 2), 0xf0d040);
      wbox(cx - 3, by, 6, 6, 0xf4c8a0);
      wpx(cx + f*2, by + 2, 0x1a1420); wpx(cx + f*2 + f, by + 2, 0xd8e8f8); wpx(cx + f*2, by + 1, 0xd8e8f8);
      wbox(cx - 2 + (f > 0), by + 4, 4, 1, 0x40281a);
      if (b->s == 3 && !dead) for (int i = 0; i < 2; i++) wpx(cx - f*4, by + 1 + ((fr >> 3) + i) % 4, 0x80c8ff);   // panting
      if (b->s == 1) wpx(cx + f*6, by + 8, 0xe8eef4), wpx(cx + f*5, by + 9, 0xe8eef4);                       // scissors in hand
      if (hatst == 0) drawtophat(cx, by, 0);
    }
    if (b->kind == BOSS_BIGWALKER) {
      int cx = bx + 12, ang = b->s == 4 ? (b->t * 8) & 255 : sawt * 41 / 256 & 255;
      sprx(fr & 8 && b->s != 4 ? GRUM2 : GRUM1, 8, (cx << 8), (by + 24) << 8, b->face > 0, b->s <= 2 ? ang : b->s == 4 ? ang : 0, 768, 768, b->s == 4 ? FLAMEPAL : BIGPAL);
      if (b->s == 4 && !(fr & 1)) part((cx + rnd(20) - 10) << 8, (by + rnd(20)) << 8, 0, -200, 12, 0, fr & 2 ? 0xff7a1a : 0xffd040);
    }
    if (b->kind == BOSS_CLOUDKING) {
      int cx = bx, cy = by, angry = b->hp < b->maxhp;
      u32 c1 = angry ? 0x5a5a78 : 0x7a7e98, c2 = angry ? 0x7a7a98 : 0x9aa0b8;
      wcirc(cx - 7, cy + 2, 7, 5, c1); wcirc(cx + 7, cy + 2, 7, 5, c1); wcirc(cx, cy - 2, 9, 7, c2);
      for (int i = -6; i <= 6; i += 6) wcirc(cx + i, cy + 5, 4, 2, c1);
      wbox(cx - 4, cy - 2, 2, 2, 0xffffff); wbox(cx + 2, cy - 2, 2, 2, 0xffffff);
      wpx(cx - 3, cy - 1, 0x1a1420); wpx(cx + 3, cy - 1, 0x1a1420);
      wpx(cx - 5, cy - 4, 0x1a1420); wpx(cx - 4, cy - 3, 0x1a1420); wpx(cx + 5, cy - 4, 0x1a1420); wpx(cx + 4, cy - 3, 0x1a1420);
      for (int i = -3; i <= 3; i++) wpx(cx + i, cy + 3, 0x2a2a3a);
      for (int i = -5; i <= 5; i++) wpx(cx + i, cy - 9, 0xffd84a);   // the crown
      for (int i = -5; i <= 5; i += 5) wpx(cx + i, cy - 10, 0xffd84a), wpx(cx + i, cy - 11, 0xfff0a0);
      wpx(cx, cy - 10, 0xe04050);
      if (!b->dead) for (Cloud *c = bcl; c < bcl+3; c++) if (c->a) {
        int x = (c->x >> 8) + 12, y = c->y >> 8, flash = c->charge && (fr >> (c->charge > 30 ? 1 : 2) & 1);
        u32 k1 = flash ? 0xfff0a0 : 0x8890a8, k2 = flash ? 0xffffff : 0xb0b8cc;
        wcirc(x - 6, y + 1, 6, 3, k1); wcirc(x + 6, y + 1, 6, 3, k1); wcirc(x, y - 1, 7, 4, k2);
        if (c->strike) for (int yy = y + 4; yy < floorpx; yy++) {   // the bolt: a jagged line to the floor
          int j = SIN[(yy*37 + fr*50) & 255] * 2 / 256;
          wpx(x + j, yy, 0xffffff); wpx(x + j - 1, yy, 0xfff070); wpx(x + j + 1, yy, 0xfff070);
        }
        if (c->charge > 20 && !(fr & 3)) part((x + rnd(16) - 8) << 8, (y + 4) << 8, 0, 300, 8, 0, 0xfff070);
      }
    }
    if (b->mini == MINI_WALKER) sprx(fr & 8 ? GRUM2 : GRUM1, 8, (bx + 8) << 8, (by + 16) << 8, b->face > 0, b->s == 1 ? SIN[(fr*16) & 255]/24 : 0, 512, 512, CHIEFPAL);
    if (b->mini == MINI_BUZZER) sprx(fr & 4 ? BUZZ2 : BUZZ1, 8, (bx + 8) << 8, (by + 16) << 8, 0, b->s ? bsign(b->vx)*16 & 255 : 0, 512, 512, BUZZBOSSPAL);
  }
  // the haberdasher's hat flying, or on Hatrick's head
  if (hatst == 1) drawtophat(hatx, haty + 4, SIN[(fr*6) & 255] / 128);
  if (hatst == 2 && (hatt > 90 || fr & 4)) drawtophat((hx >> 8) + 3, (hy >> 8) + duck + 1, 0);
  for (const BShot *s = bsh; s < bsh+24; s++) if (s->a) {
    int x = s->x >> 8, y = s->y >> 8;
    if (s->kind == BS_SCISSORS) drawscissors(x, y, s->t);
    else if (s->kind == BS_SEED) wcirc(x, y, 2, 2, (s->t >> 2) & 1 ? 0x7a2a5a : 0x5a1a40), wpx(x - 1, y - 1, 0xe080c0);
    else if (s->kind == BS_STING) { wbox(x - 1, y - 3, 2, 5, 0xffd23c); wpx(x - 1, y + 2, 0x2a2a2a); wpx(x, y + 2, 0x2a2a2a); }
    else wcirc(x, y, 2, 2, fr & 2 ? 0xffffff : 0xfff070);
  }
  // the fight's banner: the boss's name and its health
  for (Boss *b = bz; b < bz + nbz; b++) if (b->room == room && b->on == 1 && !b->dead) {
    const char *name = b->kind ? BOSSTITLE[b->kind] : MINITITLE[b->mini];
    hudtext(name, (MENUW - textwidth(name, 1)) / 2, 62, b->kind ? 0xffd84a : 0xc8f0a8);
    int n = b->maxhp, x0 = MENUW/2 - (n*26 - 6)/2;
    for (int i = 0; i < n; i++) {
      mellipse(x0 + i*26 + 10, 102, 11, 11, 0x14100c);
      mellipse(x0 + i*26 + 10, 102, 8, 8, i < b->hp ? (b->phase == 2 ? 0xff7a1c : 0xe8384a) : 0x4a4050);
    }
    break;
  }
}
// items.h: is x (px) inside a boss arena of this area? (no flag can be planted there)
static int bossinarena(int x) {
  for (Boss *b = bz; b < bz + nbz; b++) if (b->room == room && x >= b->ax0*8 && x < (b->ax1+1)*8) return 1;
  return 0;
}
