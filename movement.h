// Movement additions: wall slides, the flutter after an air catch twirl, swing poles, water and
// swimming, ice and conveyors, and the slope slide. Included by hatrick.c just before build(); the
// drawing half is movement_draw.h. The hooks into hatrick.c are mv_build (build), mv_reset
// (spawn), mv_pre and mv_move (hero), mv_slopepound and mv_slidetick (hero: the slope slide),
// mv_slidehits (enemies), mv_pose (drawhero) and mv_drawtiles / mv_drawwater (render).
//
// Map characters (levels.txt): I ice, < > conveyors moving left / right (all three solid ground),
// = a swing pole, w water. Water also fills the empty cells below a w down to the ground, so a
// pool only needs its surface row drawn. The level keeps its tile types; these characters are
// remembered in a separate surface layer.
enum { MV_NONE, MV_ICE, MV_CONVL, MV_CONVR, MV_POLE, MV_WATER };
#define MV_KNOWN "I<>=w"
#define MV_ICEBRAKE 8          // PHYSICS.md braking 40 -> 8 on ice
#define MV_CONVEYOR 128        // 0.5 px per frame
#define MV_SWIMMAX (MAXV*3/4)  // horizontal speed limit in water
#define MV_SLIDEACC 20         // slope slide: speed gained per frame going down a slope
#define MV_SLIDEDRAG 14        // slope slide: speed lost per frame on flat ground
static u8 mvs[NROOM][MH][MW];  // the surface layer of every area of the current level
static int mv_swim, mv_stroke, mv_flut, mv_flutok, mv_jpress, mv_wslide, mv_wcoy, mv_wdir, mv_wdrop;
static int mv_swing, mv_ang, mv_w, mv_st, mv_px, mv_py, mv_regrab, mv_conv, mv_hvx0;
static int mv_slide, mv_slidek;   // the slope slide; enemies it has run over

static int mvsurf(int tx, int ty) { return tx < 0 || tx >= lw || ty < 0 || ty >= lh ? 0 : mvs[room][ty][tx]; }
static int mvwater(int x, int y) { return mvsurf(x >> 3, y >> 3) == MV_WATER; }   // world px
// What the feet stand on: ice wins over a conveyor, a conveyor over plain ground.
static int mvfeet(int X, int Y) {
  int best = 0;
  for (int tx = X >> 3; tx <= (X+5) >> 3; tx++) {
    int s = mvsurf(tx, (Y+12) >> 3);
    if (s == MV_ICE) return s;
    if (s == MV_CONVL || s == MV_CONVR) best = s;
  }
  return best;
}

static void mv_build(void) {
  const Level *L = LV + lvl;
  memset(mvs, 0, sizeof mvs);
  for (int r = 0; r < L->nroom; r++) {
    const Room *R = L->room + r;
    for (int y = 0; y < R->h; y++) for (int x = 0; x < R->w; x++) {
      int c = GRID(R, x, y);
      mvs[r][y][x] = c == 'I' ? MV_ICE : c == '<' ? MV_CONVL : c == '>' ? MV_CONVR : c == '=' ? MV_POLE : c == 'w' ? MV_WATER : 0;
    }
    for (int x = 0; x < R->w; x++) for (int y = 1; y < R->h; y++)   // water fills down to the ground
      if (mvs[r][y-1][x] == MV_WATER && !mvs[r][y][x] && !(SOLID >> wd.rm[r][y][x] & 1)) mvs[r][y][x] = MV_WATER;
  }
}
static void mv_reset(void) {
  mv_swim = mv_stroke = mv_flut = mv_flutok = mv_wslide = mv_wcoy = mv_wdir = mv_wdrop = 0;
  mv_swing = mv_ang = mv_w = mv_st = mv_regrab = mv_conv = 0;
  mv_slide = mv_slidek = 0;
}
static void bubbles(int x, int y, int n) {   // x, y in px; they rise and wobble
  while (n--) part((x + rnd(5) - 2) << 8, (y + rnd(3)) << 8, rnd(120) - 60, -120 - rnd(120), 24 + rnd(16), -4, 0xd8f0ff);
}
static void splash(int x, int y) {
  for (int i = 0; i < 10; i++) part((x + rnd(9) - 4) << 8, y << 8, rnd(500) - 250, -300 - rnd(500), 18 + rnd(10), 40, i & 1 ? 0xe8f6ff : 0x7cc0f0);
  sfx(S_LAND);
}

// The swing pole: Hatrick turns around the bar. Each turn speeds the swing up, and Jump lets go
// forward and upward, faster the longer he swung. Down drops off.
static void mv_swingtick(int k, int pr) {
  int dir = moveaxis(k) > 0 ? 1 : moveaxis(k) < 0 ? -1 : 0;
  mv_st++;
  if (dir == face && mv_w < 2600) mv_w += 14;        // pumping
  else if (dir == -face && mv_w > 700) mv_w -= 20;   // holding back slows it
  mv_ang = (mv_ang + mv_w) & 0xffff;
  int a = (face > 0 ? -(mv_ang >> 8) : mv_ang >> 8) & 255;
  hx = mv_px*256 - SIN[a]*7 - 3*256;
  hy = mv_py*256 + SIN[(a+64) & 255]*7 - (11 << 7);
  hvx = hvy = 0; gnd = 0; coy = 99; jbuf = 0; st = NORM; posture(0);
  capok = diveok = stall = catchok = 1; throwt = twirl = 0;
  if (!(mv_st & 15)) sfx(S_SPIN);
  if (pr & (16|8)) {
    mv_swing = 0; mv_regrab = 18; arcg = GRAV; launch = 0; cut = 0; jn = -1;
    if (pr & 16) {
      int t = mv_st > 150 ? 150 : mv_st, sp = 520 + t*4 + (mv_w - 700)/6;
      hvx = face*(sp > 1150 ? 1150 : sp); hvy = -(760 + t*3 > 1240 ? 1240 : 760 + t*3);
      SPIN(36, face); sfx(S_JUMP3); rumble(2);
      sparkle(mv_px, mv_py, 0xfff0a8, 6);
    }
  }
}

// Runs at the top of hero() for every live state. Returns the press bits hero() should see, or -1
// when it handled the whole frame (swinging).
static int mv_pre(int k, int pr) {
  int X = hx >> 8, Y = hy >> 8;
  mv_hvx0 = hvx; mv_jpress = pr & 16;
  if (st != SLIDE) mv_slide = 0;   // a roll, a death, a spring or anything else ends the slope slide
  if (mv_regrab) mv_regrab--;
  if (mv_stroke) mv_stroke--;
  if (mv_wcoy) mv_wcoy--;
  if (mv_wdrop) mv_wdrop--;
  if (mv_swing) { mv_swingtick(k, pr); if (mv_swing) return -1; return 0; }
  if (st == HANG || st == CLIMB) { mv_wslide = 0; return pr; }
  // grab a pole: the hands (top of the head) reach the bar while airborne
  if (!gnd && !mv_regrab && !mv_swim && (freemove() || st == DIVE)) {
    int tx = (X+3) >> 3, ty = (Y+1) >> 3;
    if (mvsurf(tx, ty) == MV_POLE || mvsurf(tx, (Y+4) >> 3) == MV_POLE) {
      if (mvsurf(tx, ty) != MV_POLE) ty = (Y+4) >> 3;
      mv_swing = 1; mv_st = 0; mv_px = tx*8+4; mv_py = ty*8+4; mv_wslide = mv_flut = 0;
      int d = moveaxis(k); if (d) face = d > 0 ? 1 : -1; else if (hvx) face = hvx > 0 ? 1 : -1;
      mv_w = 800 + (iabs(hvx) + iabs(hvy)) / 2; if (mv_w > 1800) mv_w = 1800;
      mv_ang = 0; spin = 0; cst = cst == 1 || cst == 2 ? 3 : cst;
      sfx(S_LEDGE); rumble(1);
      mv_swingtick(k, 0);
      return -1;
    }
  }
  // water: in when the middle of the body is wet
  int wet = mvwater(X+3, Y+6);
  if (wet && !mv_swim) {
    if (hvy > 300) splash(X+3, Y+6), bubbles(X+3, Y+8, 5);
    if (hvy > 0) hvy /= 3;
    if (st == LONGJ || st == SPINJ || st == DIVE || st == GPWIND || st == GPSLAM || mv_slide) st = NORM, posture(0);
    mv_flut = mv_wslide = mv_slide = 0;
  } else if (!wet && mv_swim && hvy < 0) splash(X+3, Y+10);
  mv_swim = wet;
  if (mv_swim) {
    if (st == LONGJ || st == SPINJ || st == DIVE || st == GPWIND || st == GPSLAM) st = NORM, spin = 0;
    diveok = 0;
    if (pr & 16) {   // a stroke; near the surface it leaps out
      int top = !mvwater(X+3, Y-3);
      hvy = top ? -900 : hvy > -200 ? -430 : hvy - 230;
      if (hvy < -900) hvy = -900;
      gnd = 0; coy = 99; jbuf = 0; cut = 0; mv_stroke = 12; st = NORM;
      sfx(top ? S_JUMP : S_DIVE); bubbles(X+3, Y+10, 3);
    }
    pr &= ~16;
    if (!gnd) pr &= ~8;   // no ground pound under water: Down sinks faster
    if (!(fr & 31)) bubbles(X + 3 + face*2, Y + 1, 1);
  }
  // wall slide: Down lets go, pushing away leaves a few frames to still kick off
  if (mv_wslide && pr & 8) { pr &= ~8; mv_wslide = 0; mv_wdrop = 12; mv_wcoy = 0; }
  if (!gnd && !wall && mv_wcoy && pr & 16 && st == NORM) wall = mv_wdir;
  return pr;
}

// Runs in hero() after gravity, before Hatrick moves: water, the flutter, wall slides, ice and
// conveyors. g is the gravity hero() just added.
static void mv_move(int k, int dir, int g) {
  int X = hx >> 8, Y = hy >> 8, D = k >> 3 & 1;
  if (gnd) mv_flutok = 0, mv_flut = 0;
  if (twirl == 10) mv_flutok = 1;   // an air catch twirl this frame
  if (mv_swim) {
    hvy -= g;
    if (!gnd) {
      hvy += D ? 26 : 10;
      int sink = D ? 460 : 180;
      if (hvy > sink) hvy = hvy - 40 > sink ? hvy - 40 : sink;
      if (!dir) hvx = hvx*15/16;
    }
    if (hvx > MV_SWIMMAX) hvx = MV_SWIMMAX;
    if (hvx < -MV_SWIMMAX) hvx = -MV_SWIMMAX;
    if (cst == 1 && ckind == CAPFORWARD && mvwater((cxp >> 8) + 4, (cyp >> 8) + 2)) {   // the cap spins on far under water
      if (iabs(cvx) > 300) cvx += cvx > 0 ? 48 : -48;
      if (!(fr & 3)) bubbles((cxp >> 8) + 4, (cyp >> 8) + 2, 1);
    }
    mv_wslide = 0;
    return;
  }
  // the flutter: one per air catch twirl; pressing Jump again once falling hovers while it is held
  if (!gnd && st == NORM && mv_flutok && !twirl && mv_jpress && !wall && hvy > 0 && !capx_flut) mv_flutok = 0, mv_flut = 44, sfx(S_SPIN);
  if (mv_flut) {
    if (gnd || st != NORM || !(k & 16)) mv_flut = 0;
    else {
      mv_flut--;
      int want = mv_flut > 20 ? -110 : 70;
      hvy -= g;
      hvy += hvy < want ? 30 : hvy > want ? -30 : 0;
      if (!(fr & 3)) dust(hx + (3 << 8), hy + (11 << 8), 0, 1);
    }
  }
  // wall slide: starts by holding toward the wall and keeps going until pushing away
  int was = mv_wslide;
  mv_wslide = !gnd && wall && st == NORM && hvy > 0 && !duck && !mv_wdrop && (dir == wall || (was == wall && dir != -wall)) ? wall : 0;
  if (mv_wslide && hvy > 200) hvy = hvy - 100 > 200 ? hvy - 100 : 200;
  if (was && !mv_wslide && !gnd) mv_wcoy = 6, mv_wdir = was;
  if (mv_wslide) mv_wdir = mv_wslide, mv_flut = capx_flut = 0;   // one flutter at a time, and none on a wall
  // ice and conveyors, read from what the feet stood on last frame
  int surf = gnd ? mvfeet(X, Y) : 0;
  if (surf == MV_ICE && (st == NORM || st == SLIDE || st == ROLL || st == GSPIN)) {
    int dv = hvx - mv_hvx0;
    if (iabs(hvx) < iabs(mv_hvx0) || (long)hvx*mv_hvx0 < 0) { if (dv > MV_ICEBRAKE) dv = MV_ICEBRAKE; if (dv < -MV_ICEBRAKE) dv = -MV_ICEBRAKE; }
    else if (st == NORM) dv = dv / 2 + (dv & 1);
    hvx = mv_hvx0 + dv;
    if (dir && dir*hvx < -150 && !(fr & 3)) dust(hx + (3 << 8), hy + (11 << 8), hvx > 0 ? 1 : -1, 1);
  }
  if (surf == MV_CONVL || surf == MV_CONVR) mv_conv = surf == MV_CONVL ? -MV_CONVEYOR : MV_CONVEYOR, hx += mv_conv;
  else if (!gnd && mv_conv) hvx += mv_conv, mv_conv = 0;   // jumping off keeps the belt's push
  else mv_conv = 0;
}

// The slope slide (NSMB2): a ground pound that lands on a slope sits Hatrick down and he slides
// downhill (in the SLIDE state), gathering speed on the way down and running enemies over. Jump
// leaps out of it with the speed kept, crouch + cap rolls, and it ends by itself when he slows
// down on flat ground or going uphill. Holding back brakes.
// hero(): a ground pound just landed. Returns 1 when it landed on a slope and the slide began.
static int mv_slopepound(void) {
  if (!slopedir || rollbuf || mv_swim) return 0;   // a ground pound with the cap held rolls instead
  st = SLIDE; stt = 0; mv_slide = 1; mv_slidek = 0; face = slopedir; hvx = slopedir*MAXV; posture(5);
  kick(6); sfx(S_GPLAND); rumble(6); dust(hx + (3 << 8), hy + (11 << 8), -face, 4);
  return 1;
}
// hero(): runs in place of the dive's belly slide while the slope slide is on. Returns 0 when off.
static int mv_slidetick(int dir) {
  if (!mv_slide) return 0;
  posture(5); stt++;
  int s = hvx > 0 ? 1 : hvx < 0 ? -1 : slopedir, stuck = !hvx && stt > 4;   // stuck: a wall stopped him
  if (gnd && slopedir) {
    hvx += slopedir*MV_SLIDEACC;
    if (iabs(hvx) > ROLLMAX) hvx = (hvx > 0 ? 1 : -1)*ROLLMAX;
  } else if (gnd) hvx = brake(hvx, MV_SLIDEDRAG);
  if (gnd && dir && dir*hvx < 0) hvx = brake(hvx, MV_SLIDEDRAG);   // leaning back
  if (hvx) face = hvx > 0 ? 1 : -1;
  if (jbuf && coy < 6) {   // leap out, keeping the speed
    jbuf = 0; st = NORM; mv_slide = 0; hvy = -870; posture(0); cut = 1; gnd = 0; coy = 99; jn = -1; launch = 6; sfx(S_JUMP);
  } else if (stuck || (gnd && iabs(hvx) < 120 && (!slopedir || s != slopedir) && stt > 4)) {   // out of speed
    st = NORM; mv_slide = 0; posture(0);
  }
  return 1;
}
static int ekillable(const E *o);
static void ekill(E *e, u32 c, int pts);
// enemies(): whatever the slide runs into is knocked out, more points for each one in a row.
static void mv_slidehits(void) {
  if (!mv_slide || st != SLIDE) return;
  int X = hx >> 8, Y = hy >> 8;
  for (E *o = en; o < en+ne; o++)
    if (ekillable(o) && ov(X, Y+duck, 6, 11-duck, (o->x >> 8)+1, (o->y >> 8)+1, 6, 7)) {
      ekill(o, 0x9a48d0, 200 << (mv_slidek < 3 ? mv_slidek : 3)); mv_slidek++;
    }
}
