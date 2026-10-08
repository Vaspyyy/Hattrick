// The new worlds (see worlds.h): their level file, themes, tiles, crabs and the avalanche.
// Included by hatrick.c just before render(), so everything of the game is in scope here.

// ---------- assets/worlds.txt: read after levels.txt, as if it followed it ----------
static int worldsplit;                       // lines of levels.txt before worlds.txt starts (0: none)
static char worldbase[1100], worldsfile[1100];
// Errors in the joined text name worlds.txt and its own line.
static void worldline(const char **file, int *line) {
  if (worldsplit && *line > worldsplit && !strcmp(*file, worldbase)) *file = worldsfile, *line -= worldsplit;
}
// levels.txt's text with worlds.txt (beside it) appended; frees text if it has to grow.
static char *worldjoin(char *text, const char *path) {
  worldsplit = 0;
  snprintf(worldbase, sizeof worldbase, "%s", path);
  snprintf(worldsfile, sizeof worldsfile, "%s", path);
  char *slash = strrchr(worldsfile, '/');
  snprintf(slash ? slash+1 : worldsfile, sizeof worldsfile - (slash ? slash+1 - worldsfile : 0), "worlds.txt");
  FILE *f = fopen(worldsfile, "rb");
  if (!f) return text;
  size_t n = strlen(text), cap = n + 8192, got;
  char *out = malloc(cap);
  memcpy(out, text, n);
  if (n && out[n-1] != '\n') out[n++] = '\n';
  for (size_t i = 0; i < n; i++) worldsplit += out[i] == '\n';
  do { if (n + 4096 >= cap) out = realloc(out, cap *= 2); got = fread(out + n, 1, cap - n - 1, f); n += got; } while (got);
  fclose(f); out[n] = 0;
  free(text);
  return out;
}

// ---------- header options ----------
static const char *const THEMENAME[NTHEME] = { "none", "beach", "frost", "crayon", "clock", "desert" };
static int worldopt(Level *L, const char *w, const char *eq, const char *file, int line) {
  if (!strcmp(w, "theme")) {
    int t = 0; while (t < NTHEME && strcmp(eq, THEMENAME[t])) t++;
    if (t < NTHEME) L->theme = t; else levelerr(file, line, "unknown theme \"%s\" (use beach, frost, crayon, clock, desert or none)", eq);
  } else if (!strcmp(w, "avalanche")) {
    L->avalanche = atoi(eq);
    if (L->avalanche < 1 || L->avalanche > 200) levelerr(file, line, "avalanche needs a speed of 1 to 200 px/s"), L->avalanche = 0;
  } else if (!strcmp(w, "before")) {   // before=hatrick, before=lab_movement_playground
    int i = 0;
    for (; eq[i] && i < (int)sizeof L->before - 1; i++) L->before[i] = eq[i] == '_' ? ' ' : eq[i] >= 'a' && eq[i] <= 'z' ? eq[i]-32 : eq[i];
    L->before[i] = 0;
  } else return 0;
  return 1;
}
// Campaign levels with before= move in front of the level they name (several keep their file order).
static void worldorder(Level *lv, int camp) {
  Level *out = malloc(camp * sizeof *out); char *used = calloc(camp, 1); int k = 0;
  #define TARGET(j) ({ int t_ = -1; if (lv[j].before[0]) for (int q = 0; q < camp; q++) if (q != (j) && !strcmp(lv[q].name, lv[j].before)) t_ = q; t_; })
  for (int i = 0; i < camp; i++) {
    if (TARGET(i) >= 0) continue;   // goes in with its target
    for (int j = 0; j < camp; j++) if (!used[j] && TARGET(j) == i) out[k++] = lv[j], used[j] = 1;
    if (!used[i]) out[k++] = lv[i], used[i] = 1;
  }
  for (int i = 0; i < camp; i++) if (!used[i]) out[k++] = lv[i];   // chains of before= end up last
  #undef TARGET
  memcpy(lv, out, camp * sizeof *out);
  free(out); free(used);
}

// ---------- tiles ----------
static int worldtile(int c) {
  switch (c) {
    case 's': return WT_SNOW; case 'q': return WT_SAND; case 'j': return WT_JELLY; case 'f': return WT_FLOWER;
    case '[': return WT_TICKA; case ']': return WT_TICKB+1;   // the clock starts with the '[' blocks solid
  }
  return 0;
}
#define MAXTICK 960
static struct { int room, x, y, kind; } tick_[MAXTICK];
static int ntick, tickwarn;
// Clock blocks swap every two beats of the level's music (beatpos() of enemies.h: from fr and
// the theme's bpm, so replays stay exact), and blink through the last half beat before they go.
static int flowertotal, flowerleft, jelx, jely, jelt, avx, insand;

// Called whenever Hatrick is (re)placed in the level: the start, a checkpoint, a restart.
static void worldspawn(void) {
  const Level *L = LV + lvl;
  ntick = flowertotal = flowerleft = jelt = insand = 0;
  for (int r = 0; r < L->nroom; r++) {
    const Room *R = L->room + r;
    for (int y = 0; y < R->h; y++) for (int x = 0; x < R->w; x++) {
      int c = GRID(R, x, y);
      if ((c == '[' || c == ']') && ntick < MAXTICK) tick_[ntick].room = r, tick_[ntick].x = x, tick_[ntick].y = y, tick_[ntick++].kind = c == ']';
      flowertotal += c == 'f';
      flowerleft += wd.rm[r][y][x] == WT_FLOWER;
    }
  }
  avx = (room == 0 ? hx : startx << 11) - (150 << 8);   // the avalanche starts well behind
}

// Snow: slow going, rolls bog down. Runs early in hero(), from last frame's footing.
static void worldbefore(int *target) {
  if (!gnd || !scan(hx >> 8, (hy >> 8)+11, 6, 1, 1 << WT_SNOW)) return;
  *target = *target * 9 / 16;
  if (st == ROLL) hvx = hvx * 95 / 100;
  else if (st == NORM && iabs(hvx) > MAXV*9/16) hvx -= hvx > 0 ? 10 : -10;
  if (iabs(hvx) > 120 && !(fr & 7)) dust(hx + (3 << 8), hy + (11 << 8), hvx > 0 ? -1 : 1, 1);
}

// Quicksand, jelly blocks and flowers. Runs at the end of hero(), after the move.
static void worldafter(int was, int vy0, int k) {
  int X = hx >> 8, Y = hy >> 8;
  if (st >= TUBE) return;
  if (scan(X, Y+duck, 6, 11-duck, 1 << WT_SAND)) {   // quicksand: sinking slowly, a jump gets out
    if (!insand) for (int i = 0; i < 6; i++) part((X+3) << 8, (Y+10) << 8, rnd(400)-200, -rnd(300)-100, 20+rnd(10), 30, 0xd8b070);
    insand = 1;
    if (st == GPSLAM || st == GPWIND || st == DIVE || st == LONGJ || st == SPINJ || st == ROLL || st == SLIDE) st = NORM, posture(0);
    if (hvy > -8) hvy = -8;   // gravity brings it back to a slow sink next frame
    if (hvx > 150) hvx = 150; else if (hvx < -150) hvx = -150;
    gnd = 1; capok = diveok = 1;
    if (tile((X+3) >> 3, (Y+1) >> 3) == WT_SAND) doom();   // sunk over his head
    return;
  }
  insand = 0;
  if (gnd && !was && scan(X, Y+11, 6, 1, 1 << WT_JELLY) && (vy0 > 700 || st == GPLAND || (k & 16 && vy0 > 300))) {
    int v = st == GPLAND ? 1500 : vy0*7/8 + (k & 16 ? 250 : 0);   // holding Jump builds up height
    v = v < 650 ? 650 : st != GPLAND && v > 1300 ? 1300 : v;
    jelx = htx; jely = hty; jelt = 24;
    st = NORM; posture(0); hvy = -v; gnd = 0; coy = 99; jn = -1; cut = 0; arcg = GRAV; launch = poundt = 0; capok = diveok = stall = 1;
    if (v >= 1300) SPIN(30, face);
    sfx(S_SPRING); rumble(1); sparkle(X+3, Y+11, 0xb8ffb0, 6);
  }
  while (scan(X, Y+duck, 6, 11-duck, 1 << WT_FLOWER)) {   // flowers: all of a level's together are a big bonus
    map[hty][htx] = 0; flowerleft--; sfx(S_COIN); sparkle(htx*8+4, hty*8+4, 0xff7ab0, 10);
    if (flowerleft > 0) addscore(1000, htx*8+4, hty*8);
    else { addscore(10000, htx*8+4, hty*8); sfx(S_MOON); burst(htx*8+4, hty*8+4, 0xffe040, 12); burst(htx*8+4, hty*8+4, 0xff7ab0, 12); }
  }
}

// Clock blocks and the avalanche, once a frame after everything else moved.
static void worldtick(void) {
  const Level *L = LV + lvl;
  if (jelt) jelt--;
  if (ntick) {
    int bp = beatpos(), ph = bp >> 9 & 1;
    tickwarn = (bp & 511) >= 384;
    for (int i = 0; i < ntick; i++) {
      int base = tick_[i].kind ? WT_TICKB : WT_TICKA, x = tick_[i].x, y = tick_[i].y, r = tick_[i].room;
      u8 *cell = &wd.rm[r][y][x];
      if ((tick_[i].kind == 0) != (ph == 0)) *cell = base+1;
      else if (*cell != base && !(r == room && st < TUBE && ov(hx >> 8, (hy >> 8)+duck, 6, 11-duck, x*8, y*8, 8, 8))) {
        *cell = base;   // never closes on Hatrick: it waits until he is out of the way
        if (r == room) for (E *e = en; e < en+ne; e++) if (e->a && e->r == r && ov(e->x >> 8, e->y >> 8, 8, 8, x*8, y*8, 8, 8)) kill(e);
      }
    }
  }
  if (L->avalanche && room == 0 && st < TUBE && !done) {
    int stop = (gx*8 - 56) << 8;
    if (avx < stop) avx += L->avalanche * 256 / 60;
    if (hx < avx) doom();
    if (!(fr % 3)) part(avx + (rnd(8) << 8), (hy & ~255) + (rnd(90) - 60) * 256, 200 + rnd(300), -rnd(300), 20 + rnd(20), 20, 0xf4f8ff);
    if (hx - avx < 90 << 8 && !(fr % 30)) kick(2);
  }
}

// Crabs raise their claws now and then: a stomp then hurts (a ground pound or the cap still wins).
static int worldclaws(const E *e) { return e->t == E_CRAB && (fr + (e->h >> 11)*37) % 150 < 55; }

// ---------- art ----------
static u32 worldtilepx(int t, int tx, int ty, int u, int v) {
  int top = !(SOLID >> tile(tx, ty-1) & 1) && tile(tx, ty-1) != t;
  switch (t) {
    case WT_SNOW:
      if (top && v < 3) return v < 2 ? 0xffffff : (u + tx) & 1 ? 0xe4eefa : 0xffffff;
      if (u == 7 || v == 7) return 0x9ab0c8;
      return (u*5 + v*3 + tx*7 + ty*3) % 13 ? 0xd8e6f6 : 0xb8cce4;
    case WT_SAND: {
      if (top && v == 0) return (u + (fr >> 3) + tx) % 4 ? 0xf0d090 : 0xd8b070;
      int s = (u*3 + v*5 + tx*7 + ty*5 + (fr >> 4)) % 9;
      return s == 0 ? 0xa88040 : s == 4 ? 0xe8c888 : 0xd0a860;
    }
    case WT_JELLY: {
      int sq = jelt && tx == jelx && ty == jely ? SIN[(jelt*21) & 255] * 2 / 256 : 0;   // a squash when bounced on
      int vv = v - sq;
      if (vv < 0 || vv > 7) return 0;
      if ((u == 0 || u == 7) && (vv == 0 || vv == 7)) return 0;
      if (u == 0 || u == 7 || vv == 0 || vv == 7) return 0x2a7a3a;
      if (vv == 1 && u > 1 && u < 4) return 0xe0ffd8;
      if (u == 1 && vv == 2) return 0xc0ffb8;
      return vv > 4 ? 0x48b858 : 0x6ad870;
    }
    case WT_FLOWER: {
      int du = 2*u - 7, dv = 2*v - 7 + (SIN[(fr*4 + tx*40) & 255] > 200), d = du*du + dv*dv;   // a smiling flower, bobbing
      if (d <= 10) return (v == 3 && (u == 2 || u == 5)) ? 0x3a2a10 : 0xffe040;
      if (d <= 26) return 0xffe040;
      if (d > 58) return 0;
      if (d >= 44) return 0xc02a50;
      return (du > 0) != (dv > 0) ? 0xffffff : 0xff5a7a;
    }
    case WT_TICKA: case WT_TICKA+1: case WT_TICKB: case WT_TICKB+1: {
      int red = t >= WT_TICKB, ghost = t == WT_TICKA+1 || t == WT_TICKB+1;
      u32 dark = red ? 0x6a1420 : 0x14246a, mid = red ? 0xe03a4a : 0x3a6ae0, light = red ? 0xffa0a8 : 0x9ab8ff;
      int edge = u == 0 || v == 0 || u == 7 || v == 7;
      if (ghost) return edge && (u + v + (fr >> 3)) % 3 == 0 ? mid : 0;
      if (tickwarn && fr & 4) mid = light;   // about to vanish
      if (edge) return u == 0 || v == 0 ? light : dark;
      if ((u == 3 || u == 4) && v > 1 && v < 5) return 0xfff4d0;   // a clock hand
      if (v == 4 && u > 4 && u < 6) return 0xfff4d0;
      return mid;
    }
  }
  return 0;
}
// Ground and grass in the colours of the level's theme.
static u32 worldtint(u32 c) {
  static const u32 FROM[5] = { 0x8be05a, 0x4cb83c, 0x6a3a1a, 0xa8642c, 0x7a4420 };
  static const u32 TO[NTHEME][5] = {
    { 0 },
    { 0xfff4c0, 0xf0d890, 0xc8a060, 0xe8c880, 0xc8a464 },   // beach: sand
    { 0xffffff, 0xd8e8f8, 0x8aa0c0, 0x8a9ab8, 0x6a7890 },   // frost: snow on blue rock
    { 0xb8f890, 0x78d868, 0x8a5a9a, 0xf4bcd4, 0xd890b8 },   // crayon: pastels
    { 0xf0d070, 0xc89838, 0x4a2a18, 0x8a6450, 0x664636 },   // clock: brass and wood
    { 0xf8d888, 0xe0b060, 0xa86c30, 0xd89a58, 0xb07838 },   // desert
  };
  int th = LV[lvl].theme;
  if (th) for (int i = 0; i < 5; i++) if (c == FROM[i]) return TO[th][i];
  return c;
}
// The theme's background (only without a sky.png); returns 0 for the stock one.
static int worldbg(int lo) {
  int th = LV[lvl].theme;
  if (!th) return 0;
  static const u32 TOP[NTHEME] = { 0, 0x58b8f0, 0x8aa8d8, 0xfff6e0, 0x3a2a5a, 0xf0a050 };
  static const u32 BOT[NTHEME] = { 0, 0xc8ecff, 0xe8f0fa, 0xfde8c8, 0xf0a060, 0xffe0a0 };
  for (int y = 0; y < SH; y++) {
    u32 *row = big[y], sky = mixcolor(TOP[th], BOT[th], y, SH);
    int yf = fdiv(y - lo/3, SC), yn = fdiv(y - lo/2, SC), ys = y / SC;
    for (int x = 0; x < SW; x++) {
      u32 c = sky;
      int xf = (x + ox/4) / SC, xn = (x + ox/2) / SC, xs = x / SC;
      if (th == TH_BEACH) {
        int sd = (xs-200)*(xs-200) + (ys-24)*(ys-24);
        if (sd < 100) c = sd < 64 ? 0xfff8d0 : 0xffe890;                                  // the sun
        if (yf > H-50) c = yf % 3 == 0 && (xf + yf*5 + (fr >> 4)) % 14 < 3 ? 0xc8f0ff : yf < H-47 ? 0x6ac8f0 : 0x2a88c8;   // the sea
        int m = xn % 140 - 70, dune = H-24 + m*m/180;
        if (yn > dune) c = yn == dune+1 ? 0xd8b878 : 0xf0d898;                              // dunes
        int p = xn % 140;                                                                 // palms on the dunes
        if (p > 66 && p < 69 && yn > H-50 + (p-66) && yn <= dune) c = 0x8a5a30;
        if (iabs(p-67) < 9 && iabs(yn - (H-50) - iabs(p-67)/2) < 2) c = 0x3a9a48;
      } else if (th == TH_FROST) {
        int m = xf % 120 - 60, peak = H-84 + iabs(m)*5/4;
        if (yf > peak) c = yf < peak + 9 - iabs(m)/12 ? 0xffffff : 0x8898c0;              // mountains with snow caps
        int t2 = xn % 24 - 12, tree = H-38 + iabs(t2)*2;
        if (yn > tree) c = 0x2a5a4a;                                                      // pines
        if (yn > H-22) c = yn == H-21 ? 0xffffff : 0xe0eaf6;                              // a snow field
      } else if (th == TH_CRAYON) {
        if ((xs + ys*2) % 9 == 0) c = 0xf6e4c4;                                            // paper grain
        int cm = (x + ox/8) / SC % 200 - 100, cy = ys - 26;
        if (cm*cm/12 + cy*cy*4 < 300) c = (xs + ys) % 4 ? 0xffffff : 0xd8e4ff;             // scribbled clouds
        int m = xf % 150 - 75, hill = H-52 + m*m/120;
        if (yf > hill) c = yf == hill+1 ? 0xa080c8 : (xf + yf*2) % 5 ? 0xdcc4f4 : 0xc8a8e8;  // crayon hills, hatched
        int n = xn % 100 - 50, hill2 = H-28 + n*n/90;
        if (yn > hill2) c = yn == hill2+1 ? 0x58a858 : (xn*2 + yn) % 6 ? 0xb4ecb0 : 0x90d890;
        int f = xn % 37;                                                                   // flowers in the grass
        if (yn == hill2+3 && f == 9) c = 0xff6a8a;
      } else if (th == TH_CLOCK) {
        int m = xf % 320 - 160, dy = yf - (H-84), d = m*m + dy*dy;
        if (d < 34*34) c = d > 31*31 ? 0x6a4a2a : (d < 4 ? 0x3a2a1a : 0xf4ead0);           // the big clock face
        int tw = xn % 64, top = H-64 + (xn/64*37) % 22;
        if (tw < 30 && yn > top) c = (tw % 6 == 2 && yn % 8 == 3) ? 0xffd080 : 0x4a3050;   // towers with lit windows
        if (tw >= 12 && tw < 18 && yn > top-10 && yn <= top && iabs(tw-15) <= (yn-(top-10))/3) c = 0x4a3050;   // spires
      } else if (th == TH_DESERT) {
        int m = xf % 140 - 70, pyr = H-60 + iabs(m);
        if (yf > pyr && iabs(m) < 34) c = (yf - pyr) % 6 == 0 ? 0xc89050 : 0xe0b070;       // pyramids
        int n = xn % 110 - 55, dune = H-26 + n*n/120;
        if (yn > dune) c = yn == dune+1 ? 0xd8a050 : 0xf0c070;
      }
      row[x] = c;
    }
  }
  if (th == TH_CLOCK) {   // the hands of every big clock face on screen turn with the level clock
    for (int k = (ox/4/SC - 200) / 320; k <= (ox/4/SC + W + 200) / 320; k++) {
      int cx = k*320 + 160 - ox/4/SC, cy = H-84 + lo/3/SC, bp = beatpos();
      for (int hand = 0; hand < 2; hand++) {
        int a = hand ? (bp >> 9) * 8 & 255 : (bp >> 8) * 16 & 255, len = hand ? 18 : 26;   // ticking on the beat
        for (int i = 2; i < len; i++) blk((cx*SC + SIN[(a+192) & 255]*i*SC/256), (cy*SC + SIN[(a+128) & 255]*i*SC/256), SC, SC, 0x3a2a1a);
      }
    }
  }
  if (th == TH_FROST) for (int i = 0; i < 70; i++) {   // falling snow
    int sx = (i*97 + fr*(1 + i%3)/3 - ox/(SC*3)) % W, sy = (i*53 + fr*(1 + i%2)/2 + SIN[(fr + i*30) & 255]/64) % H;
    if (sx < 0) sx += W;
    blk(sx*SC, sy*SC, SC, SC, 0xffffff);
  }
  return 1;
}
static void worldcrab(const E *e) {
  int x = e->x >> 8, y = e->y >> 8, claws = worldclaws(e), legs = fr >> 2 & 1;
  int lift = hop ? SIN[hop/2] * 3 >> 8 : 0;
  y -= lift;
  for (int v = 3; v < 7; v++) for (int u = 0; u < 8; u++) {   // the shell
    int edge = v == 3 && (u == 0 || u == 7);
    if (edge) continue;
    wpx(x+u, y+v, v == 3 || u == 0 || u == 7 ? 0xa02a1a : v == 4 && u > 1 && u < 6 ? 0xff8a6a : 0xe8503a);
  }
  for (int i = 0; i < 2; i++) {   // eyes on stalks
    int ex = x + 2 + i*3;
    wpx(ex, y+2, 0xa02a1a); wpx(ex, y+1, 0xffffff); wpx(ex+(e->vx > 0), y+1, 0x1a1010);
  }
  for (int i = 0; i < 3; i++) {   // legs
    wpx(x+1+i*2 + legs, y+7, 0xa02a1a); wpx(x+6-i*2 - legs, y+7, 0xa02a1a);
  }
  for (int s = 0; s < 2; s++) {   // claws, raised now and then
    int cx = s ? x+8 : x-2, cy = claws ? y-2 : y+3;
    wpx(cx, cy, 0xe8503a); wpx(cx+1, cy, 0xe8503a); wpx(cx, cy+1, 0xa02a1a); wpx(cx+1, cy+1, 0xe8503a);
    wpx(s ? cx+1 : cx, cy-1, 0xff8a6a);
    if (claws) wpx(s ? x+7 : x, y+2, 0xa02a1a), wpx(s ? x+8 : x-1, y+1, 0xa02a1a);
  }
}
// The avalanche: a tumbling wall of snow on the left of the screen.
static void worlddraw(void) {
  const Level *L = LV + lvl;
  if (!L->avalanche || room) return;
  int front = avx >> 8, left_ = ox / SC - 2;
  if (front < left_) return;
  for (int y = oy / SC - 1; y <= (oy + SH) / SC + 1; y++) {
    int edge = front + (SIN[(y*9 + fr*5) & 255]*6 + SIN[(y*23 - fr*7) & 255]*3) / 256;
    for (int x = left_; x <= edge; x++) {
      int d = edge - x, ball = ((x*7 + y*13 + fr*3) / 5) % 17 == 0;
      wpx(x, y, d < 1 ? 0xb8cce4 : d < 3 ? 0xe0eaf6 : ball ? 0xc8d8ec : 0xf8fbff);
    }
  }
}
// Quicksand drawn again over Hatrick so he sinks into it; the flower count in the HUD.
static void worldhud(void) {
  if (insand) for (int ty = (hy >> 11) - 1; ty <= (hy >> 11) + 2; ty++) for (int tx = (hx >> 11) - 1; tx <= (hx >> 11) + 2; tx++)
    if (tile(tx, ty) == WT_SAND) for (int v = 0; v < 8; v++) for (int u = 0; u < 8; u++) wpx(tx*8+u, ty*8+v, worldtilepx(WT_SAND, tx, ty, u, v));
  if (flowertotal && !(menu && !resumable)) {
    char t[16];
    for (int i = 0; i < 5; i++) mellipse(40 + SIN[(i*51 + 64) & 255]*8/256, 76 + SIN[i*51 & 255]*8/256, 6, 6, i & 1 ? 0xffffff : 0xff5a7a);
    mellipse(40, 76, 6, 6, 0xffe040);
    snprintf(t, sizeof t, "%d/%d", flowertotal - flowerleft, flowertotal); hudtext(t, 62, 64, flowerleft ? 0xffffff : 0xffd84a);
  }
}
// The new map landmarks: a palm beach, a snowy peak, a crayon tree, a clock tower.
static void worldlandmark(int card, int x, int y, int dim) {
  #define C(c) (dim ? (((c) >> 1) & 0x7f7f7f) + 0x303840 : (c))
  if (card == CARD_BEACH) {
    for (int i = -7; i <= 7; i++) { int h = 3 - i*i/16; mrectw(x+7+i, y-h, 1, h+1, C(0xf0d898)); }
    for (int j = 0; j < 12; j++) wpx(x+8 + j/5, y-3-j, C(0x8a5a30));
    for (int i = -5; i <= 5; i++) wpx(x+10+i, y-15 + iabs(i)/2, C(0x3a9a48)), wpx(x+10+i/2, y-16 + iabs(i), C(0x4cb83c));
  } else if (card == CARD_PEAK) {
    for (int j = 0; j < 15; j++) { int w = j; mrectw(x+9-w/2, y-14+j, w+1, 1, C(j < 5 ? 0xffffff : 0x8898c0)); }
    mrectw(x+1, y, 17, 1, C(0xe0eaf6));
  } else if (card == CARD_WOODS) {
    mrectw(x+7, y-6, 3, 7, C(0x8a5a9a));
    mdisc(x+8, y-11, 6, C(0x78d868)); mdisc(x+6, y-12, 3, C(0xb8f890));
    mdisc(x+15, y-1, 2, C(0xff5a7a)); wpx(x+15, y-1, C(0xffe040));
  } else if (card == CARD_CLOCK) {
    mrectw(x+3, y-18, 11, 19, C(0x4a3050)); mrectw(x+4, y-17, 9, 18, C(0x6a4a70));
    for (int j = 0; j < 5; j++) mrectw(x+8-j, y-23+j, 1+2*j, 1, C(0x4a3050));
    mdisc(x+8, y-11, 3, C(0xf4ead0)); wpx(x+8, y-12, C(0x3a2a1a)); wpx(x+8, y-13, C(0x3a2a1a)); wpx(x+9, y-11, C(0x3a2a1a));
    mrectw(x+7, y-4, 3, 5, C(0x262a32));
  }
  #undef C
}
