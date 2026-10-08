// More enemies (ideas.md, section 3), included by hatrick.c just before build().
// They live in the same enemy list as walkers and buzzers (E, t 60..68), so checkpoints save them
// and a death restores them like any other enemy. Map characters (levels.txt):
//   u shy-walker      a walker that turns to face a flying cap and blocks it with its mask; stomp it,
//                     or bump into its back
//   k shell walker    stomp it into a shell, then touch, roll into or cap the shell to send it sliding
//   v cap thief       a bird that snatches the thrown cap and flies off with it; touch it to get it back
//   L cloud rider     floats above Hatrick and drops walkers (at most 3 of its own at a time)
//   x chomp stake     a chained chomp lunges at whoever comes near; ground pound the stake three
//                     times and it breaks loose, smashing bricks the way Hatrick faces
//   p puffer buzzer   a buzzer the cap inflates into a bouncy ball for a few seconds
//   Q stone thwomp    16x16 (the cell is its top-left); drops when Hatrick is below, rideable back up
//   Y beat platform   a run of stone blocks that shifts two tiles on every fourth beat of the music
//   Z beat piston     a stone block that shoots spikes into its free side on beats 2 and 4
//   $ &               fire bar pivots (arms ~) that jump an eighth of a turn per beat, clockwise / not
// Beat-bound things count beats from the frame counter and the theme's bpm (music.txt), never from
// the audio device, so ./sim replays stay exact. The theme restarts whenever fr does (spawn()).

enum { T_SHY = 60, T_SHELL, T_THIEF, T_RIDER, T_CHOMP, T_PUFF, T_THWOMP, T_BEATPLAT, T_PISTON };
static const char EXTCHARS[] = "ukvLxpQYZ";   // in T_ order

static void wpx(int x, int y, u32 c);
static void sprx(const u16 *s, int h, int fx, int fy, int fl, int ang, int sx, int sy, const u32 *pal);

static const u16 SHY1[8] = { 0x0550, 0x15aa, 0x15bb, 0x15aa, 0x1569, 0x1554, 0x0550, 0x3c3c };
static const u16 SHY2[8] = { 0x0550, 0x15aa, 0x15bb, 0x15aa, 0x1569, 0x1554, 0x0550, 0x0ff0 };
static const u16 KOOP1[8] = { 0x00a0, 0x02b8, 0x15a0, 0x5d58, 0x5758, 0x1550, 0x0820, 0x3c3c };
static const u16 KOOP2[8] = { 0x00a0, 0x02b8, 0x15a0, 0x5d58, 0x5758, 0x1550, 0x0820, 0x0ff0 };
static const u16 SHELL[6] = { 0x0550, 0x1d74, 0x5d75, 0x755d, 0x2aa8, 0x0aa0 };
static const u16 CROW1[8] = { 0x4004, 0x5014, 0x1550, 0x1758, 0x155a, 0x0550, 0x03c0, 0x0c30 };
static const u16 CROW2[8] = { 0x0000, 0x0000, 0x1550, 0x5758, 0x555a, 0x4551, 0x03c0, 0x0c30 };
static const u16 RIDER[5] = { 0x0ff0, 0x355c, 0x1aa4, 0x1554, 0x0550 };
static const u32 SHYPAL[3] = { 0xd83a3a, 0xf4f0e8, 0x2a1418 };
static const u32 KOOPPAL[3] = { 0x3cb04a, 0xf8d878, 0x1c3020 };
static const u32 CROWPAL[3] = { 0x3a2c5a, 0xffc830, 0x1a1428 };
static const u32 RIDERPAL[3] = { 0xf0a040, 0x60d8ff, 0x302010 };
static const u32 EWALKPAL[3] = { 0x9a48d0, 0xffffff, 0x301040 };   // the walker's colours (GPAL)

// ---------- the beat ----------
static int ebpm = 10400, ebpmgen = -1;   // the current theme's tempo, beats per 100 minutes
static char ebpmof[64];
static int beatpos(void) {   // beats since the theme (re)started, in 1/256 beat
  const char *m = LV[lvl].music;
  if (ebpmgen != levelgen || strcmp(m, ebpmof)) {
    char dir[1024], path[1200], line[256];
    snprintf(ebpmof, sizeof ebpmof, "%s", m); ebpmgen = levelgen; ebpm = 10400;
    assetdir(dir, sizeof dir); snprintf(path, sizeof path, "%s/music/%s/music.txt", dir, m);
    FILE *f = fopen(path, "r");
    if (f) {
      while (fgets(line, sizeof line, f)) {
        char *p = line; while (*p == ' ' || *p == '\t') p++;
        if (!strncmp(p, "bpm", 3)) { double b = strtod(p+3, 0); if (b >= 30 && b <= 400) ebpm = (int)(b*100 + 0.5); }
      }
      fclose(f);
    }
  }
  return (int)((long long)fr * ebpm * 256 / 360000);
}
// A fire bar's angle (256 = one turn). Beat bars ($ &) carry speed +-1 and snap an eighth turn per beat.
static int barangle(const Bar *b) {
  if (iabs(b->speed) != 1) return (b->a0*256 + b->speed*fr) >> 8 & 255;
  int bp = beatpos(), f = bp & 255, step = (bp >> 8)*32 + (f < 40 ? f*32/40 - 32 : 0);
  return (b->a0 + b->speed*step) & 255;
}

// ---------- shared helpers ----------
static int capthief;   // the enemy (index + 1) flying off with the cap, 0 if none
static int exride;     // the thwomp (index + 1) Hatrick stands on this frame
static int capstolen(void) {   // the base enemy loop leaves the cap harmless while a bird carries it
  if (!capthief || capthief > ne) return 0;
  const E *e = en + capthief - 1;
  return e->a && e->t == T_THIEF && e->s == 2 && cst;
}
static void ecapback(void) { capthief = 0; if (cst) cst = 3, ct = 0; }   // the cap flies home
static void ekill(E *e, u32 c, int pts) {
  if (e->t == T_THIEF && capthief == e - en + 1) ecapback();
  e->a = 0; burst((e->x >> 8)+4, (e->y >> 8)+4, c, 8); sfx(S_STOMP); rumble(4); kick(5); addscore(pts, (e->x >> 8)+4, e->y >> 8);
}
// Hatrick against a box (px): 0 apart, 1 a stomp from above, 2 any other touch.
static int ehero(int x, int y, int w, int h) {
  int X = hx >> 8, Y = hy >> 8;
  if (st >= TUBE || !ov(X, Y+duck, 6, 11-duck, x, y, w, h)) return 0;
  return (hvy > 0 || st == GPSLAM) && Y+11 < y + h*3/4 ? 1 : 2;
}
static void ebounce(int k) { hvy = k & 16 ? -1000 : -650; st = NORM; arcg = GRAV; launch = 0; cut = 0; capok = diveok = stall = 1; spin = throwt = 0; }
static int ecap(int x, int y, int w, int h) { return cst && cst < 3 && !capstolen() && ov(cxp >> 8, cyp >> 8, 8, 5, x, y, w, h); }
static int ekillable(const E *o) {
  return o->a && o->r == room && (o->t <= 3 || o->t == T_SHY || o->t == T_SHELL || o->t == T_THIEF || o->t == T_RIDER || o->t == T_PUFF);
}
static void esmashall(E *self, int x, int y, int w, int h) {   // a shell, thwomp or loose chomp runs enemies over
  for (E *o = en; o < en+ne; o++)
    if (o != self && ekillable(o) && ov(x, y, w, h, (o->x >> 8)+1, (o->y >> 8)+1, 6, 7)) ekill(o, 0x9a48d0, 200);
}
static int isqrt(int v) { int r = 0; while ((r+1)*(r+1) <= v) r++; return r; }
// Walker physics: gravity, a floor, turning at walls (and at ledges if edges).
static void ewalk(E *e, int edges) {
  e->vy += GRAV; if (e->vy > 900) e->vy = 900;
  e->y += e->vy; int ex = e->x >> 8, ey = e->y >> 8;
  if (scan(ex, ey, 8, 8, SOLID)) {
    int s = e->vy < 0 ? 1 : -1;
    do ey += s; while (scan(ex, ey, 8, 8, SOLID) && iabs(ey - (e->y >> 8)) < 16);
    e->y = ey << 8; e->vy = 0;
  }
  e->x += e->vx; ex = e->x >> 8;
  if (scan(ex, ey, 8, 8, SOLID) || (edges && !e->vy && !scan(e->vx > 0 ? ex+8 : ex-1, ey+8, 1, 1, SOLID)))
    e->x -= e->vx, e->vx = -e->vx;
  if (ey > lh*8) e->a = 0;
}
static int chompstake(const E *e, int *sx, int *sy) { *sx = e->h >> 9; *sy = e->h & 511; return 0; }

// The level file's character c at cell (x, y) of area r: one of ours?
static void extspawn(int r, int x, int y, int c) {
  const char *p = c ? strchr(EXTCHARS, c) : 0;
  if (!p || ne >= MAXEN) return;
  const Room *R = LV[lvl].room + r;
  #define EMPTY(X, Y) ((unsigned)(X) < (unsigned)R->w && (unsigned)(Y) < (unsigned)R->h && (GRID(R, X, Y) == 0 || GRID(R, X, Y) == ' '))
  int t = T_SHY + (int)(p - EXTCHARS);
  if (t == T_BEATPLAT && x > 0 && GRID(R, x-1, y) == 'Y') return;   // one entry per run of blocks
  E *e = en + ne++;
  memset(e, 0, sizeof *e);
  e->t = t; e->a = 1; e->r = r; e->x = x << 11; e->y = e->h = y << 11;
  if (t == T_SHY || t == T_SHELL) e->vx = -100;
  if (t == T_THIEF) e->w = e->x;   // its perch
  if (t == T_CHOMP) { e->h = x << 9 | y; e->x = (x*8 - 10) << 8; e->y = (y*8 - 2) << 8; }   // the ball rests beside its stake
  if (t == T_BEATPLAT) {
    while (x + e->w < R->w && GRID(R, x + e->w, y) == 'Y') e->w++;
    e->vx = EMPTY(x + e->w, y) && EMPTY(x + e->w + 1, y) ? 1 : EMPTY(x-1, y) && EMPTY(x-2, y) ? -1 : 0;
  }
  if (t == T_PISTON) {   // spikes go out of the first free side: up, left, right, down
    static const int DX[4] = { 0, -1, 1, 0 }, DY[4] = { -1, 0, 0, 1 };
    int d = 0; while (d < 3 && !EMPTY(x + DX[d], y + DY[d])) d++;
    e->vx = DX[d]; e->vy = DY[d];
  }
  #undef EMPTY
}

// Stacks: walkers, crabs, shy-walkers and shell walkers (a resting shell too) land on each other's
// heads and ride along, so a stack walks and turns with its bottom enemy. Knock one out and the ones
// above drop down. Runs after every enemy has moved; x0 holds where the first n0 started the frame.
static int estackable(const E *e) {
  return e->a && e->r == room && (e->t == 1 || e->t == E_CRAB || e->t == T_SHY || (e->t == T_SHELL && e->s < 2));
}
static void estack(const int *x0, int n0) {
  static E *s[MAXEN]; int n = 0;
  for (E *e = en; e < en+ne; e++) if (estackable(e)) {   // lowest first, so a rider's base has settled
    int i = n++; while (i > 0 && s[i-1]->y < e->y) s[i] = s[i-1], i--; s[i] = e;
  }
  for (int i = 1; i < n; i++) {
    E *e = s[i]; if (e->vy <= 0) continue;   // on the ground, or on the way up
    int bot = e->y + (8 << 8), was = bot - e->vy;
    for (int j = 0; j < i; j++) {
      E *o = s[j];
      if (iabs(o->x - e->x) >= 6 << 8 || bot + (2 << 8) < o->y || was > o->y + (2 << 8) + (o->vy > 0 ? o->vy : 0)) continue;
      int ie = e - en, io = o - en, nx = (ie < n0 ? x0[ie] : e->x) + (io < n0 ? o->x - x0[io] : 0), ny = o->y - (8 << 8);
      nx += (o->x - nx) / 8;   // and eased onto the middle of its base
      if (!scan(nx >> 8, ny >> 8, 8, 8, SOLID)) {   // carried along, unless that runs it into a wall
        e->x = nx;
        if (e->t != T_SHY && !(e->t == T_SHELL && e->s) && o->vx) e->vx = o->vx;   // a shy-walker keeps its own facing
      }
      e->y = ny; e->vy = 0;
      break;
    }
  }
}

// Standing on a thwomp (called by hero() when no tile holds Hatrick up).
static int extride(void) {
  int X = hx >> 8, Y = hy >> 8;
  exride = 0;
  for (E *e = en; e < en+ne; e++) if (e->a && e->r == room && e->t == T_THWOMP) {
    int ex = e->x >> 8, ey = e->y >> 8;
    if (X+6 > ex && X < ex+16 && Y+11 >= ey-2 && Y+11 <= ey + 6 + (hvy >> 8)) {
      hy = (ey - 11) << 8; hvy = 0; gnd = 1; exride = e - en + 1; return 1;
    }
  }
  return 0;
}

static void extenemy(E *e, int k) {
  int me = e - en + 1, X = hx >> 8, Y = hy >> 8, alive = st < TUBE;
  int ex = e->x >> 8, ey = e->y >> 8, h;
  switch (e->t) {
  case T_SHY: {   // u: frames until it finishes turning toward the cap
    ewalk(e, 1); ex = e->x >> 8; ey = e->y >> 8;
    int dir = e->vx > 0 ? 1 : -1, ccx = (cxp >> 8) + 4, ccy = (cyp >> 8) + 2, out = cst && cst < 3 && !capstolen();
    if (e->u && !--e->u) e->vx = -e->vx, dir = -dir;
    else if (!e->u && out && (ccx > ex+4 ? 1 : -1) != dir && iabs(ccx - ex-4) < 48 && iabs(ccy - ey-4) < 20) e->u = 6;   // it sees the cap coming
    if (ecap(ex, ey, 8, 8)) {
      if ((ccx > ex+4 ? 1 : -1) == dir && !e->u) {   // the mask blocks it
        ecapback(); sfx(S_BOUNCE); burst(ex+4+dir*4, ey+3, 0xffffff, 4); if (!e->vy) e->vy = -320;
      } else { ekill(e, 0xd83a3a, 300); break; }
    }
    if ((h = ehero(ex+1, ey+1, 6, 7)) == 1) { ekill(e, 0xd83a3a, 300); ebounce(k); }
    else if (h == 2) { if ((X+3 > ex+4 ? 1 : -1) != dir) ekill(e, 0xd83a3a, 300); else die(); }   // from behind it topples over
    break;
  }
  case T_SHELL: {   // s: 0 walking, 1 a resting shell (w: frames), 2 sliding; u: frames the slide can't hurt
    if (e->s == 2) {
      e->vy += GRAV; if (e->vy > 900) e->vy = 900;
      e->y += e->vy; ey = e->y >> 8;
      if (scan(ex, ey, 8, 8, SOLID)) { int s = e->vy < 0 ? 1 : -1; do ey += s; while (scan(ex, ey, 8, 8, SOLID) && iabs(ey - (e->y >> 8)) < 16); e->y = ey << 8; e->vy = 0; }
      e->x += e->vx; ex = e->x >> 8;
      if (scan(ex, ey, 8, 8, SOLID)) {
        int hit = 0;
        while (scan(ex, ey, 8, 8, 4)) smash(htx, hty), hit = 1;   // bricks break and it bounces back
        e->x -= e->vx; e->vx = -e->vx; ex = e->x >> 8;
        if (!hit && iabs(ex - X) < 160) sfx(S_LAND), kick(2);
      }
      if (ey > lh*8) { e->a = 0; break; }
      esmashall(e, ex, ey, 8, 8);
    } else {
      ewalk(e, e->s == 0); ex = e->x >> 8; ey = e->y >> 8;
      if (!e->a) break;
      if (e->s == 1 && ++e->w > 420) e->s = 0, e->w = 0, e->vx = X < ex ? -100 : 100;   // walks back out
    }
    if (e->u) e->u--;
    if (ecap(ex, ey, 8, 8) && !e->u) {
      if (e->s == 0) { e->s = 1; e->vx = 0; e->w = 0; sfx(S_STOMP); addscore(100, ex+4, ey); }
      else if (e->s == 1) { e->s = 2; e->vx = (cxp >> 8)+4 < ex+4 ? 640 : -640; e->u = 14; sfx(S_STOMP); }
      e->u = e->u ? e->u : 14;
    }
    h = ehero(ex+1, ey+1, 6, 7);
    if (e->s == 0) {
      if (h == 1) { e->s = 1; e->vx = 0; e->w = 0; e->u = 10; ebounce(k); sfx(S_STOMP); rumble(3); addscore(100, ex+4, ey); }
      else if (h == 2) die();
    } else if (e->s == 1) {
      if (h && !e->u) {
        int fast = st == ROLL || st == DIVE || st == SLIDE;
        e->s = 2; e->vx = (ex+4 > X+3 ? 1 : -1) * (fast ? 1000 : 640); e->u = 14; e->w = 0;
        sfx(S_STOMP); rumble(3); kick(2);
        if (h == 1) ebounce(k);
      }
    } else if (h == 1 && !e->u) { e->s = 1; e->vx = 0; e->w = 0; e->u = 10; ebounce(k); sfx(S_STOMP); }
    else if (h == 2 && !e->u) die();
    break;
  }
  case T_THIEF: {   // s: 0 perched, 1 swooping at the cap, 2 flying off with it, 3 going home
    if (capthief == me && (e->s != 2 || !cst)) capthief = 0;
    if (e->s == 2 && capthief != me) e->s = 3;
    int ecx = ex+4, ecy = ey+4, ccx = (cxp >> 8)+4, ccy = (cyp >> 8)+2;
    if (e->s == 0) {
      e->y = e->h + SIN[fr*4 & 255]*2;
      if (cst && cst < 3 && ckind != CAPSPIN && !capthief && iabs(ccx - ecx) < 64 && iabs(ccy - ecy) < 40) e->s = 1, sfx(S_EMERGE);
    } else if (e->s == 1) {
      if (!cst || cst == 3 || capthief) e->s = 3;
      else {
        int dx = (ccx - ecx) * 256, dy = (ccy - ecy) * 256, d = isqrt((dx >> 8)*(dx >> 8) + (dy >> 8)*(dy >> 8)) + 1;
        e->vx = dx / d * 2; e->vy = dy / d * 2; e->x += e->vx; e->y += e->vy;
        if (ov(ex, ey, 8, 8, cxp >> 8, cyp >> 8, 8, 5)) e->s = 2, capthief = me, cst = 2, ct = 0, sfx(S_CATCH), burst(ccx, ccy, 0xffffff, 5);
      }
    } else if (e->s == 2) {   // keeps just out of reach, waiting when far ahead
      int away = ecx > X+3 ? 1 : -1, dist = iabs(ecx - X-3);
      int sp = dist > 150 ? 0 : dist > 90 ? 140 : 300;
      e->vx += e->vx < away*sp ? 30 : e->vx > away*sp ? -30 : 0;
      int ty = ((hy >> 8) - 30) << 8, vy = (ty - e->y) / 12;
      e->vy = vy > 200 ? 200 : vy < -200 ? -200 : vy;
      if (!scan((e->x + e->vx) >> 8, ey, 8, 8, SOLID) && (e->x + e->vx) >> 8 > 0 && (e->x + e->vx) >> 8 < lw*8-8) e->x += e->vx;
      else e->vx = 0, e->vy = -200;   // over the wall
      if (!scan(e->x >> 8, (e->y + e->vy) >> 8, 8, 8, SOLID) && (e->y + e->vy) >> 8 > 0) e->y += e->vy;
      cst = 2; ct = 0; cxp = e->x; cyp = e->y + (7 << 8);   // the cap dangles from its feet
    } else {
      int dx = e->w - e->x, dy = e->h - e->y;
      e->vx = dx > 300 ? 300 : dx < -300 ? -300 : dx; e->x += e->vx;
      e->y += dy > 200 ? 200 : dy < -200 ? -200 : dy;
      if (iabs(dx) < 256 && iabs(dy) < 256) e->s = 0;
    }
    ex = e->x >> 8; ey = e->y >> 8;
    if ((h = ehero(ex, ey+1, 8, 6)) == 1) { ekill(e, 0x3a2c5a, 400); ebounce(k); }
    else if (h == 2 && e->s == 2) { ecapback(); e->s = 3; sfx(S_CATCH); sparkle(ex+4, ey+4, 0xffe066, 6); }   // snatched back
    break;
  }
  case T_RIDER: {   // s: 0 waiting, 1 following Hatrick; u: frames to the next drop
    if (e->s == 0) { if (iabs(X - ex) < 200) e->s = 1; break; }
    int tx = (X - 6 + SIN[fr*2 & 255]*48/256) * 256, d = (tx - e->x) / 24;
    e->vx = d > 380 ? 380 : d < -380 ? -380 : d;
    e->x += e->vx; e->y = e->h + SIN[fr*3 & 255]*3;
    ex = e->x >> 8; ey = e->y >> 8;
    if (++e->u >= 150) {
      e->u = 0;
      int mine = 0; for (E *o = en; o < en+ne; o++) mine += o->a && o->t == 1 && o->s == 1000 + me;
      if (mine < 3 && ne < MAXEN && alive) {
        E *n = en + ne++;
        memset(n, 0, sizeof *n);
        n->t = 1; n->a = 1; n->r = e->r; n->x = e->x + (2 << 8); n->y = n->h = e->y - (2 << 8);
        n->vx = X < ex ? -100 : 100; n->vy = -500; n->s = 1000 + me;
        sfx(S_SPIT);
        e = en + me - 1;
      }
    }
    if (ecap(ex, ey, 14, 9)) { ekill(e, 0xf4f8ff, 800); break; }
    if ((h = ehero(ex, ey, 14, 9)) == 1) { ekill(e, 0xf4f8ff, 800); ebounce(k); }
    else if (h == 2) die();
    break;
  }
  case T_CHOMP: {   // s: 0 resting, 1 winding up, 2 lunging, 3 holding, 4 pulling back, 10 loose; w: pounds taken
    int sx, sy; chompstake(e, &sx, &sy);
    int ax = sx*8+4, ay = sy*8+4, R = 52, bcx = ex+5;
    if (e->s < 10 && st == GPSLAM && ov(X, Y, 6, 12, sx*8+2, sy*8+3, 4, 5)) {   // a pound on the stake
      hvy = -700; st = NORM; arcg = GRAV; launch = 0; cut = 0; capok = diveok = stall = 1; spin = throwt = 0;
      kick(8); rumble(5); sfx(S_GPLAND); burst(ax, sy*8+6, 0x8a5a30, 6);
      if (++e->w >= 3) { e->s = 10; e->vx = face*420; e->vy = -600; sfx(S_REVEAL); addscore(1000, ax, ay-8); break; }
    }
    if (e->s == 10) {   // loose: bounds along, smashing bricks, harmless to Hatrick
      e->vy += GRAV; if (e->vy > 900) e->vy = 900;
      e->y += e->vy; ey = e->y >> 8;
      if (scan(ex, ey, 10, 10, SOLID) && e->vy > 0) { do ey--; while (scan(ex, ey, 10, 10, SOLID)); e->y = ey << 8; e->vy = -420; }
      e->x += e->vx; ex = e->x >> 8;
      while (scan(ex, ey, 10, 9, 4)) smash(htx, hty);
      if (scan(ex, ey, 10, 9, SOLID)) { burst(ex+5, ey+5, 0x303040, 12); sfx(S_BRICK); kick(6); addscore(500, ex+5, ey); e->a = 0; break; }
      if (ey > lh*8 || ex < -16 || ex > lw*8+16) { e->a = 0; break; }
      esmashall(e, ex, ey, 10, 10);
      break;
    }
    int rx = ax + (bcx < ax ? -11 : 11), dist = isqrt((X+3-ax)*(X+3-ax) + (Y+6-ay)*(Y+6-ay));
    if (e->u) e->u--;
    if (e->s == 0) {
      int gx0 = ey;   // rest on the ground beside the stake, hopping now and then
      e->vy += GRAV; if (e->vy > 600) e->vy = 600; e->y += e->vy; ey = e->y >> 8;
      if (scan(ex, ey, 10, 10, SOLID)) { do ey--; while (scan(ex, ey, 10, 10, SOLID) && ey > gx0-12); e->y = ey << 8; e->vy = 0; }
      e->x += (rx - bcx) * 32;
      if (!e->vy && !(fr % 50)) e->vy = -260;
      if (!e->u && alive && dist < R) e->s = 1, e->u = 22;
    } else if (e->s == 1) {
      e->x += ((fr & 2) ? 128 : -128);
      if (!e->u) {
        int dx = X+3 - ax, dy = Y+6 - ay, d = isqrt(dx*dx + dy*dy) + 1;
        e->vx = (ax + dx*R/d - 5) << 8; e->vy = (ay + dy*R/d - 5) << 8;   // where the chain runs out
        e->s = 2; sfx(S_DIVE);
      }
    } else if (e->s == 2) {
      int dx = e->vx - e->x, dy = e->vy - e->y, d = isqrt((dx >> 8)*(dx >> 8) + (dy >> 8)*(dy >> 8));
      if (d <= 6) e->x = e->vx, e->y = e->vy, e->s = 3, e->u = 24, kick(3);
      else {
        int nx = e->x + dx/d*6, ny = e->y + dy/d*6;
        if (scan(nx >> 8, ny >> 8, 10, 10, SOLID)) e->s = 3, e->u = 24, kick(3);
        else e->x = nx, e->y = ny;
      }
    } else if (e->s == 3) { if (!e->u) e->s = 4; }
    else {
      int dx = (rx-5 << 8) - e->x, dy = (ay-6 << 8) - e->y, d = isqrt((dx >> 8)*(dx >> 8) + (dy >> 8)*(dy >> 8));
      if (d <= 2) e->s = 0, e->u = 40;
      else e->x += dx/d*2, e->y += dy/d*2;
    }
    ex = e->x >> 8; ey = e->y >> 8;
    if (ecap(ex, ey, 10, 10)) { ecapback(); sfx(S_BOUNCE); burst(ex+5, ey+5, 0xffffff, 4); if (e->s != 0) e->s = 4; }
    if ((h = ehero(ex+1, ey+1, 8, 8)) == 1) { hvy = -800; st = NORM; arcg = GRAV; capok = diveok = stall = 1; sfx(S_BOUNCE); }   // too tough to stomp
    else if (h == 2) die();
    break;
  }
  case T_PUFF: {   // s: 0 buzzing, 1 puffed up (u: frames left); w: frames it can't hurt after deflating
    if (e->s == 0) {
      int ph = (fr + (e->h >> 9)) & 127, o = (ph < 64 ? ph : 128-ph) - 32;
      e->y = e->h + o*192; ey = e->y >> 8;
      if (e->w) e->w--;
      if (ecap(ex, ey, 8, 8)) { e->s = 1; e->u = 240; ecapback(); sfx(S_SPRING); burst(ex+4, ey+4, 0xffa0c8, 6); break; }
      if ((h = ehero(ex+1, ey+1, 6, 7)) == 1) { ekill(e, 0xff80b0, 200); ebounce(k); }
      else if (h == 2 && !e->w) die();
    } else {
      if (e->vx) e->vx--;   // squash after a bounce
      if (--e->u <= 0) { e->s = 0; e->w = 40; break; }
      int top = ey-4;
      if (alive && hvy >= 0 && X+6 > ex-4 && X < ex+12 && Y+11 >= top && Y+11 <= top + 8 + (hvy >> 8)) {
        hy = (top - 11) << 8; hvy = k & 16 ? -1300 : -1100; st = NORM; arcg = GRAV; launch = 0; cut = 0; capok = diveok = stall = 1; spin = throwt = 0; gnd = 0; coy = 99;
        e->vx = 8; sfx(S_SPRING); rumble(2); sparkle(ex+4, top, 0xffd0e8, 6);
      }
    }
    break;
  }
  case T_THWOMP: {   // s: 0 waiting, 1 falling, 2 landed, 3 rising; u: frames
    int y0 = e->y;
    if (e->s == 0) {
      if (e->u) e->u--;
      else if (alive && X+6 > ex-6 && X < ex+22 && Y > ey+8 && Y < ey+160) e->s = 1, e->vy = 0;
    } else if (e->s == 1) {
      e->vy = e->vy + 60 > 1000 ? 1000 : e->vy + 60;
      int ny = (e->y + e->vy) >> 8;
      if (scan(ex, ny, 16, 16, SOLID)) {
        while (scan(ex, ny, 16, 16, SOLID) && ny > ey) ny--;
        e->y = ny << 8; e->s = 2; e->u = 50; e->vy = 0;
        if (iabs(ex - X) < 200) kick(9), rumble(3);
        sfx(S_GPLAND); burst(ex+2, ny+16, 0xd0c8b8, 4); burst(ex+14, ny+16, 0xd0c8b8, 4);
      } else e->y += e->vy;
      if (e->y >> 8 > lh*8) { e->a = 0; break; }
      esmashall(e, ex, e->y >> 8, 16, 16);
    } else if (e->s == 2) { if (--e->u <= 0) e->s = 3; }
    else { e->y -= 128; if (e->y <= e->h) e->y = e->h, e->s = 0, e->u = 40; }
    if (exride == me) hy += e->y - y0;   // carries whoever stands on it
    ex = e->x >> 8; ey = e->y >> 8; X = hx >> 8; Y = hy >> 8;
    if (alive && exride != me && ov(X, Y+duck, 6, 11-duck, ex, ey, 16, 16) && Y+11 > ey+6) {
      if (e->s == 1 && Y+6 > ey+8) die();   // flattened
      else { hx = (X+3 < ex+8 ? ex-6 : ex+16) << 8; hvx = 0; }   // it's a stone wall from the side
    }
    break;
  }
  case T_BEATPLAT: {   // s: how many tiles it has moved from home; vx: the way it goes
    int b = beatpos() >> 8, target = (b >> 2 & 1) * 2 * e->vx;
    if (e->s != target) {
      int d = target > e->s ? 1 : -1, tx0 = (e->x >> 11) + e->s, ty = e->y >> 11;
      int lead = d > 0 ? tx0 + e->w : tx0 - 1, trail = d > 0 ? tx0 : tx0 + e->w - 1;
      u8 *row = wd.rm[e->r][ty];
      if (lead >= 0 && lead < lw && !row[lead]) {
        int on = gnd && Y+11 == ty*8 && X+6 > tx0*8 && X < (tx0 + e->w)*8;
        row[lead] = 4; row[trail] = 0; e->s += d;
        if ((on || ov(X, Y+duck, 6, 11-duck, lead*8, ty*8, 8, 8)) && !scan(X + d*8, Y+duck, 6, 11-duck, SOLID)) hx += d*8*256;   // d can be -1: no left shift of a negative
      }
    }
    break;
  }
  case T_PISTON: {   // s: spikes out; u: the beat fraction, for drawing
    int bp = beatpos(), b = bp >> 8, f = bp & 255;
    e->s = (b & 1) && f < 170; e->u = f; e->w = b;
    int cx = (ex >> 3) + e->vx, cy = (ey >> 3) + e->vy;
    if (e->s && alive && ov(X, Y+duck, 6, 11-duck, cx*8+1, cy*8+1, 6, 6)) die();
    if (e->s) esmashall(e, cx*8+1, cy*8+1, 6, 6);
    break;
  }
  }
}

// ---------- drawing (called by render() for each live enemy of ours in this area) ----------
static void edisc(int cx, int cy, int r, u32 c) { for (int y = -r; y <= r; y++) for (int x = -r; x <= r; x++) if (x*x + y*y <= r*r + r) wpx(cx+x, cy+y, c); }
static void extdraw(const E *e) {
  int lift = hop ? SIN[hop/2] * 3 : 0, stretch = hop ? SIN[hop/2] / 6 : 0;   // the hop on the "bah"
  int ex = e->x >> 8, ey = e->y >> 8, X = hx >> 8;
  switch (e->t) {
  case T_SHY:
    sprx(fr & 8 ? SHY2 : SHY1, 8, e->x + (4 << 8), e->y + (8 << 8) - lift, e->vx < 0, e->u ? (e->u & 2 ? 8 : -8) : 0, 256 - stretch/2, 256 + stretch, SHYPAL);
    break;
  case T_SHELL:
    if (e->s == 0) sprx(fr & 8 ? KOOP2 : KOOP1, 8, e->x + (4 << 8), e->y + (8 << 8) - lift, e->vx < 0, 0, 256 - stretch/2, 256 + stretch, KOOPPAL);
    else {
      int wob = e->s == 1 && e->w > 360 ? ((fr >> 1 & 1) ? 256 : -256) : 0;   // about to come out
      sprx(SHELL, 6, e->x + (4 << 8) + wob, e->y + (8 << 8), e->s == 2 && (fr & 4), 0, 256, 256, KOOPPAL);
    }
    break;
  case T_THIEF: {
    int flap = e->s == 0 ? fr & 16 : fr & 4;
    sprx(flap ? CROW2 : CROW1, 8, e->x + (4 << 8), e->y + (8 << 8) - (e->s == 0 ? lift/2 : 0), e->s == 0 ? X < ex : e->vx < 0, 0, 256, 256, CROWPAL);
    break;
  }
  case T_RIDER: {
    for (int i = 0; i < 3; i++) edisc(ex + 3 + i*4, ey + 5 - (i == 1), 3, i == 1 ? 0xf4f8ff : 0xe0e8f4);
    for (int i = 1; i < 13; i++) wpx(ex+i, ey+8, 0xb8c4d8);
    sprx(RIDER, 5, e->x + (7 << 8), e->y + (3 << 8), X < ex, 0, 256, 256, RIDERPAL);
    if (e->s && e->u > 125) sprx(GRUM1, 8, e->x + (7 << 8), e->y - (1 << 8), 0, 0, 256, 256, EWALKPAL);   // about to drop one
    break;
  }
  case T_CHOMP: {
    int sx, sy; chompstake(e, &sx, &sy);
    int ax = sx*8+4, ay = sy*8+4, bcx = ex+5, bcy = ey+5, sink = e->w;
    for (int y = sy*8+2+sink; y < sy*8+8; y++) wpx(ax-1, y, 0x8a5a30), wpx(ax, y, 0xb07a40), wpx(ax+1, y, 0x6a4020);   // the stake
    if (e->s >= 10) { edisc(bcx, bcy, 5, 0x181820); wpx(bcx+(e->vx > 0 ? 2 : -3), bcy-2, 0xffffff); break; }
    for (int i = 1; i < 5; i++) {   // chain links
      int lx = ax + (bcx - ax)*i/5, ly = ay + (bcy - ay)*i/5 + (e->s == 0 ? i*(5-i)/3 : 0);
      wpx(lx, ly, 0x606878); wpx(lx+1, ly, 0x8890a0);
    }
    int sh = e->s == 1 ? (fr & 2 ? 1 : -1) : 0, look = X+3 > bcx ? 1 : -1;
    edisc(bcx + sh, bcy, 5, 0x181820);
    wpx(bcx+sh+look*2, bcy-2, 0xffffff); wpx(bcx+sh+look*2+look, bcy-2, 0xffffff); wpx(bcx+sh+look*2, bcy-1, 0xffffff);
    if (e->s >= 1 && e->s <= 3) for (int i = -3; i <= 3; i++) wpx(bcx+sh+look*(i < 0 ? -i : i)/2+look, bcy+1+(i > 0), i & 1 ? 0xffffff : 0xd02838);   // jaws
    break;
  }
  case T_PUFF:
    if (e->s == 0) {
      edisc(ex+4, ey+4 - lift/256/2, 3, 0xf080a8);
      wpx(ex+5, ey+3, 0x1a1020); wpx(ex+1, ey+3 + (fr >> 2 & 1), 0xffd0e0); wpx(ex+7, ey+3 + (fr >> 2 & 1), 0xffd0e0);
    } else {
      int r = e->vx ? 7 - (e->vx > 4) : 7, blink = e->u < 60 && (fr & 4);
      edisc(ex+4, ey+4, r, blink ? 0xffffff : 0xffa0c8);
      edisc(ex+4, ey+3, r-3, blink ? 0xffffff : 0xffc8dc);
      for (int a = 0; a < 256; a += 32) wpx(ex+4 + SIN[(a+64) & 255]*(r+1)/256, ey+4 + SIN[a]*(r+1)/256, 0xc04870);
      wpx(ex+6, ey+2, 0x1a1020); wpx(ex+2, ey+2, 0x1a1020);
    }
    break;
  case T_THWOMP: {
    int angry = e->s == 1 || e->s == 2 || (e->s == 0 && iabs(X+3 - ex-8) < 40);
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
      int edge = x == 0 || y == 0 || x == 15 || y == 15;
      wpx(ex+x, ey+y, edge ? 0x3a4050 : (x+y*3) % 7 == 0 ? 0x7a8494 : y < 3 ? 0xb8c0cc : 0x98a2b2);
    }
    for (int i = 0; i < 2; i++) {   // eyes and brows
      int cx = ex + 4 + i*7;
      for (int y = 0; y < 3; y++) for (int x = 0; x < 3; x++) wpx(cx+x, ey+5+y, 0xffffff);
      wpx(cx+1 + (X+3 > ex+8 ? 1 : -1), ey+6, 0x101018);
      if (angry) for (int x = 0; x < 4; x++) wpx(cx-1+x, ey+4 - (i ? 3-x : x)/2, 0x202430);
    }
    for (int x = 3; x < 13; x++) wpx(ex+x, ey+11, 0xffffff), wpx(ex+x, ey+12, x & 1 ? 0x404858 : 0xffffff);   // teeth
    break;
  }
  case T_BEATPLAT: {
    int bp = beatpos(), soon = (bp >> 8 & 3) == 3 && e->vx, pulse = (bp & 255) < 48;
    int tx0 = (e->x >> 11) + e->s, ty = e->y >> 11;
    u32 c = soon && (fr & 4) ? 0xffffff : pulse ? 0xff70d0 : 0xb060ff;
    for (int i = 0; i < e->w; i++) {
      int x = (tx0+i)*8, y = ty*8;
      for (int u = 0; u < 8; u++) wpx(x+u, y, c);
      wpx(x+3, y+3, c); wpx(x+4, y+3, c); wpx(x+4, y+4, c); wpx(x+3, y+5, c); wpx(x+2, y+5, c);   // a little note
    }
    break;
  }
  case T_PISTON: {
    int tx = ex >> 3, ty = ey >> 3, f = e->u, warn = !(e->w & 1) && f > 200;
    for (int i = 0; i < 8; i++) {   // hazard stripes on the face that shoots
      int px = e->vx ? (e->vx > 0 ? tx*8+7 : tx*8) : tx*8+i, py = e->vy ? (e->vy > 0 ? ty*8+7 : ty*8) : ty*8+i;
      wpx(px, py, warn && (fr & 4) ? 0xff4030 : (i >> 1 & 1) ? 0xffd030 : 0x202020);
    }
    int len = e->s ? (f < 24 ? f/3 : 8) : warn ? 2 : 0;
    for (int j = 0; j < len; j++) for (int i = 0; i < 8; i++) {   // four spikes, pointing out
      int w = 3 - (len - 1 - j)*3/8, m = i & 3;   // narrower toward the tips
      if (m == 0 || iabs(m*2 - 3) > w) continue;
      int along = j, across = i;
      int px = e->vx ? (e->vx > 0 ? tx*8+8+along : tx*8-1-along) : tx*8+across;
      int py = e->vy ? (e->vy > 0 ? ty*8+8+along : ty*8-1-along) : ty*8+across;
      wpx(px, py, j == len-1 ? 0xffffff : 0xc8ccd8);
    }
    break;
  }
  }
}
