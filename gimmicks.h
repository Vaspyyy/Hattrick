// Level gimmicks (ideas.md, section 4). hatrick.c includes this file three times, with GIM_PART
// 1 (types, after the Level struct), 2 (game logic, just before build()) and 3 (drawing, the
// coin rush and the gimmick gallery, just before render()). See MODDING.md, "Level gimmicks".
//   O  hat flip flower: 30 s of reversed gravity (bonus rooms), double speed or bricks <-> coins
//   A  gold block: bump it from below and it rides on Hatrick's head, spilling coins as he runs
//   V  seesaw pivot, its plank drawn with _ cells beside it
//   {  }  scale lifts: a { run and the next } run on its row hang from one rope over a pulley
//   D  door that turns its bonus room a quarter turn clockwise (Up in front of it)
//   E  cannon (fires cannonballs toward Hatrick), W propeller fan (blows him upward)
//   header: rise=lava|water[:px per s]  scroll=<px per s>  sky=night  copy=<level>  flip=gravity|speed|swap
//   rooms:  bg=8bit (chunky pixels and chiptune music), bg=night
//   keys:   F2 the gimmick gallery (lab levels), F3 on the map a coin rush (three levels, coins only)
#if GIM_PART == 1
enum { GO_RISE, GO_RISEV, GO_SCROLL, GO_NIGHT, GO_FLIP };   // Level.gim[]: rise 1 lava / 2 water, its speed px/s, scroll px/s, night, flip kind
enum { GR_NONE, GR_8BIT, GR_NIGHT };                        // Level.gimroom[]: an area's look
enum { GF_GRAVITY, GF_SPEED, GF_SWAP };
#define GIM_GALLERY (1<<28)   // F2
#define GIM_RUSH (1<<29)      // F3
#define GIM_NP 32
#define GIM_NO 96
#define GIM_NB 12
#define GIM_FLIPTIME (30*60)
#define GIM_RUSHTIME (100*60)
// x, y in 1/256 px. Lifts: x, y the plank's top-left, w its width (px), py the pulley's height,
// lo..hi how high and low the first of a pair may go, tilt the pair's summed heights.
// Seesaws: x, y the pivot's top, w the plank's half length (px), tilt its slope in 1/256.
typedef struct { int room, kind, x, y, w, vy, tilt, link, lo, hi, py; } GimPlat;
typedef struct { int room, kind, x, y, t; } GimObj;   // a map character's cell; t: used, or a timer
typedef struct { int room, x, y, vx, a; } GimBall;
typedef struct {
  GimPlat pl[GIM_NP]; int npl, ride;   // ride: the platform Hatrick stands on, +1
  GimObj ob[GIM_NO]; int nob;
  GimBall ball[GIM_NB];
  int rise, risedelay, sx;             // the lava's top and the auto-scroll camera, 1/256 px
  int xroom, rot, flip;                // the one turned or flipped area: quarter turns after a flip
  int flipt, flipk, fliproom;          // the hat flip under way
  int gold, goldrun, goldn;            // the gold block on Hatrick's head
  int rott, pvy, poldy;                // a door's turn in progress; Hatrick before this frame's step
} GimWorld;
static int gim_opt(Level *L, Room *R, char *w, char *eq, const char *file, int line);
static char *gim_expand(const char *text);

#elif GIM_PART == 2
#define GW (wd.gw)
static void startlevel(int l);
static void tomap(void);
static void tick(int k);
static void gim_rushend(int fail);
static int gim_keys(int k, int pr);
static int gim_rushnext(void);
static int gim_rush, gim_rushlv[3], gim_rushc0;   // the coin rush: its stage (1..3, 0 off) and levels

// ---------- levels.txt ----------
static void levelerr(const char *file, int line, const char *fmt, ...);
static int gim_opt(Level *L, Room *R, char *w, char *eq, const char *file, int line) {
  if (R) {
    int r = R - L->room;
    if (strcmp(w, "bg")) return 0;
    if (!strcmp(eq, "8bit")) { L->gimroom[r] = GR_8BIT; R->cave = 0; return 1; }
    if (!strcmp(eq, "night")) { L->gimroom[r] = GR_NIGHT; R->cave = 0; return 1; }
    return 0;
  }
  if (!strcmp(w, "rise")) {
    char *c = strchr(eq, ':'); int v = c ? atoi(c+1) : 10;
    if (c) *c = 0;
    if (!strcmp(eq, "lava")) L->gim[GO_RISE] = 1;
    else if (!strcmp(eq, "water")) L->gim[GO_RISE] = 2;
    else { levelerr(file, line, "rise= needs lava or water, e.g. rise=lava:12"); return 1; }
    if (v < 1 || v > 120) levelerr(file, line, "rise speed needs 1 to 120 px a second"), v = 10;
    L->gim[GO_RISEV] = v; return 1;
  }
  if (!strcmp(w, "scroll")) {
    int v = atoi(eq);
    if (v < 1 || v > 240) levelerr(file, line, "scroll= needs 1 to 240 px a second"), v = 0;
    L->gim[GO_SCROLL] = v; return 1;
  }
  if (!strcmp(w, "sky")) {
    if (!strcmp(eq, "night")) L->gim[GO_NIGHT] = 1, L->gimroom[0] = GR_NIGHT;
    else if (strcmp(eq, "day")) levelerr(file, line, "sky= needs day or night");
    return 1;
  }
  if (!strcmp(w, "flip")) {
    int k = !strcmp(eq, "gravity") ? GF_GRAVITY : !strcmp(eq, "speed") ? GF_SPEED : !strcmp(eq, "swap") ? GF_SWAP : -1;
    if (k < 0) levelerr(file, line, "flip= needs gravity, speed or swap"), k = 0;
    L->gim[GO_FLIP] = k; return 1;
  }
  return !strcmp(w, "copy");   // done by gim_expand
}
// A header's level name as copy= names it: no number, no options, lower case.
static void gim_name(const char *h, char *out, int n) {
  char buf[256], *ws = 0; snprintf(buf, sizeof buf, "%s", h); *out = 0;
  for (char *t = strtok_r(buf, " \t\r", &ws); t; t = strtok_r(0, " \t\r", &ws))
    if (!strchr(t, '=') && strlen(out) + strlen(t) + 2 < (size_t)n) { if (*out) strcat(out, " "); strcat(out, t); }
  char *p = out; while ((*p >= '0' && *p <= '9') || *p == ' ') p++;
  memmove(out, p, strlen(p)+1);
  for (p = out; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32; else if (*p == '_') *p = ' ';
}
// copy=<level>: the level's header is followed by every row and bonus room of the named level
// (spaces in the name written as _), then by anything of its own (more bonus rooms).
static char *gim_expand(const char *text) {
  if (!strstr(text, "copy=")) return strdup(text);
  char *src = strdup(text); int nl = 1;
  for (char *c = src; *c; c++) nl += *c == '\n';
  char **ln = malloc(nl * sizeof *ln); ln[0] = src; nl = 1;
  for (char *c = src; *c; c++) if (*c == '\n') *c = 0, ln[nl++] = c+1;
  size_t cap = strlen(text)*2 + 64, n = 0; char *out = malloc(cap);
  #define GIM_PUT(s) do { size_t m = strlen(s); while (n + m + 2 > cap) out = realloc(out, cap *= 2); memcpy(out+n, s, m); n += m; out[n++] = '\n'; } while (0)
  for (int i = 0; i < nl; i++) {
    GIM_PUT(ln[i]);
    const char *cp = ln[i][0] == '=' ? strstr(ln[i], "copy=") : 0;
    if (!cp || (cp > ln[i] && cp[-1] != ' ' && cp[-1] != '\t')) continue;
    char want[64], name[64]; int k = 0; cp += 5;
    while (*cp && *cp != ' ' && *cp != '\t' && *cp != '\r' && k < 63) want[k++] = *cp++;
    want[k] = 0; gim_name(want, want, sizeof want);
    int from = -1;
    for (int j = 0; j < nl && from < 0; j++)
      if (j != i && ln[j][0] == '=' && !strstr(ln[j], "copy=")) { gim_name(ln[j]+1, name, sizeof name); if (!strcmp(name, want)) from = j; }
    if (from < 0) { fprintf(stderr, "hatrick: levels: copy=%s: no level of that name\n", want); continue; }
    for (int j = from+1; j < nl && ln[j][0] != '='; j++) GIM_PUT(ln[j]);
  }
  #undef GIM_PUT
  out[n ? n-1 : 0] = 0;
  free(ln); free(src);
  return out;
}

// ---------- turning and flipping an area ----------
// Only one area is ever turned (GW.xroom), and only while Hatrick is in it: leaving puts it back.
// The live map, its enemies, crumble blocks, checkpoints and gimmicks turn with it (GW), and so
// do the level's own tubes, fire bars and moon coins (LV), which gim_lvsync keeps in step.
enum { XF_FLIP, XF_ROT };   // upside down; a quarter turn clockwise
static u8 gim_tmp[MH][MW];
static int gim_lvl = -1, gim_lvroom, gim_lvrot, gim_lvflip, gim_lvgen;
static void gim_cell(int op, int h, int *x, int *y) { if (op == XF_FLIP) *y = h-1-*y; else { int t = *x; *x = h-1-*y; *y = t; } }
static void gim_box(int op, int h, int *x, int *y, int bw, int bh) {   // an upright bw x bh px box, 1/256 px
  int H8 = h*8 << 8;
  if (op == XF_FLIP) *y = H8 - *y - (bh << 8);
  else { int cx = *x + (bw << 7), cy = *y + (bh << 7); *x = H8 - cy - (bw << 7); *y = cx - (bh << 7); }
}
static void gim_dims(int l, int r, int rot, int *w, int *h) {
  *w = LV[l].room[r].w; *h = LV[l].room[r].h;
  if (rot & 1) { int t = *w; *w = *h; *h = t; }
}
static void gim_state(int *rot, int *flip, int op) { if (op == XF_FLIP) *rot = (4 - *rot) & 3, *flip ^= 1; else *rot = (*rot + 1) & 3; }
static void gim_lvop(int l, int r, int op, int h) {
  static const int ROT[4] = { T_RIGHT, T_LEFT, T_UP, T_DOWN };   // T_UP, T_DOWN, T_LEFT, T_RIGHT turned clockwise
  Level *L = LV + l;
  for (Tube *t = L->tube; t < L->tube + L->ntube; t++) if (t->room == r) {
    int vert = t->dir == T_UP || t->dir == T_DOWN, x = t->x, y = t->y;
    if (op == XF_FLIP) { if (vert) t->y = h-1-y, t->dir = t->dir == T_UP ? T_DOWN : T_UP; else t->y = h-2-y; }
    else { t->x = vert ? h-1-y : h-2-y; t->y = x; t->dir = ROT[t->dir]; }
  }
  for (Bar *b = L->bar; b < L->bar + L->nbar; b++) if (b->room == r) {
    gim_cell(op, h, &b->x, &b->y);
    if (op == XF_FLIP) b->a0 = (256 - b->a0) & 255, b->speed = -b->speed; else b->a0 = (b->a0 + 64) & 255;
  }
  for (int i = 0; i < L->nmoon; i++) if (L->moon[i].room == r) gim_cell(op, h, &L->moon[i].x, &L->moon[i].y);
}
static void gim_lvstep(int op) {
  int w, h; gim_dims(gim_lvl, gim_lvroom, gim_lvrot, &w, &h);
  gim_lvop(gim_lvl, gim_lvroom, op, h); gim_state(&gim_lvrot, &gim_lvflip, op);
}
static void gim_lvsync(void) {   // the level's objects follow GW's orientation (and only the current level's)
  if (gim_lvl >= 0 && (gim_lvgen != levelgen || gim_lvl >= NLEVEL)) gim_lvl = -1, gim_lvrot = gim_lvflip = 0;   // a new level list
  int want = GW.rot || GW.flip;
  if (gim_lvl >= 0 && (gim_lvrot || gim_lvflip) &&
      (!want || gim_lvl != lvl || gim_lvroom != GW.xroom || gim_lvrot != GW.rot || gim_lvflip != GW.flip)) {
    while (gim_lvrot) gim_lvstep(XF_ROT);
    if (gim_lvflip) gim_lvstep(XF_FLIP);
  }
  if (!want || gim_lvrot || gim_lvflip) return;
  gim_lvl = lvl; gim_lvroom = GW.xroom; gim_lvgen = levelgen;
  if (GW.flip) gim_lvstep(XF_FLIP);
  while (gim_lvrot != GW.rot) gim_lvstep(XF_ROT);
}
static void gim_wdop(int r, int op, int w, int h) {   // the live area r, w x h tiles before the change
  int nw = op == XF_ROT ? h : w, nh = op == XF_ROT ? w : h, mw = w > nw ? w : nw, mh = h > nh ? h : nh;
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    int t = wd.rm[r][y][x], X = x, Y = y;
    gim_cell(op, h, &X, &Y);
    if (op == XF_ROT && (t == 10 || t == 11)) t ^= 1;   // tube bodies: upright <-> sideways
    gim_tmp[Y][X] = t;
  }
  for (int y = 0; y < mh; y++) memset(wd.rm[r][y], 0, mw);
  for (int y = 0; y < nh; y++) memcpy(wd.rm[r][y], gim_tmp[y], nw);
  for (E *e = en; e < en+ne; e++) if (e->r == r) {
    if (op == XF_FLIP) e->h = (h*8-8 << 8) - e->h;
    gim_box(op, h, &e->x, &e->y, 8, 8); e->vy = 0;
    if (op == XF_ROT) e->h = e->y;
  }
  for (Crumble *c = wd.cr; c < wd.cr + wd.ncr; c++) if (c->room == r) {
    gim_cell(op, h, &c->x, &c->y);
    if (c->state == 1) c->state = 2, c->t = 60;
  }
  for (Check *c = wd.ck; c < wd.ck + wd.nck; c++) if (c->room == r) gim_cell(op, h, &c->x, &c->y);
  for (Shot *s = wd.sh; s < wd.sh+8; s++) if (s->room == r) s->a = 0;
  for (GimObj *o = GW.ob; o < GW.ob + GW.nob; o++) if (o->room == r) gim_cell(op, h, &o->x, &o->y);
  for (GimPlat *p = GW.pl; p < GW.pl + GW.npl; p++) if (p->room == r) {   // they keep level and stop moving
    int bw = p->kind == 1 ? p->w : 2*p->w;
    if (p->kind == 2) p->x -= p->w << 8;
    gim_box(op, h, &p->x, &p->y, bw, 4);
    if (p->kind == 2) p->x += p->w << 8;
    p->vy = p->tilt = 0; p->lo = p->hi = p->y; p->py = p->y - (16 << 8);
  }
  for (GimBall *b = GW.ball; b < GW.ball + GIM_NB; b++) if (b->room == r) b->a = 0;
  if (r != room) return;
  gim_box(op, h, &hx, &hy, 6, 11);
  if (op == XF_FLIP) hvy = -hvy; else { int t = hvx; hvx = -hvy; hvy = t; }
  if (st == HANG || st == CLIMB || st == GPLAND || st == GSPIN || st == GPWIND || st == GPSLAM) st = NORM;
  gnd = 0; cst = 0; GW.ride = 0; lw = nw; lh = nh;
  for (P *p = pt; p < pt+NP; p++) p->l = 0;
  for (Pop *p = pops; p < pops+12; p++) p->t = 0;
  if (op == XF_FLIP) cyf = ((nh*8 - H) << 8) - cyf, camgy = hy;   // the mirrored view looks the same
}
static void gim_op(int r, int op) {   // r: the turned area, or one that isn't turned
  int w, h; gim_lvsync();
  gim_dims(lvl, r, r == GW.xroom ? GW.rot : 0, &w, &h);
  gim_wdop(r, op, w, h);
  GW.xroom = r; gim_state(&GW.rot, &GW.flip, op);
  gim_lvsync();
}
static void gim_unturn(void) {   // the turned area back the way the file has it
  int r = GW.xroom;
  while (GW.rot) gim_op(r, XF_ROT);
  if (GW.flip) gim_op(r, XF_FLIP);
}

// ---------- building and restarting ----------
static void gim_build(void) {   // from build(): wd is fresh, so nothing is turned
  const Level *L = LV + lvl;
  gim_lvsync();
  for (int r = 0; r < L->nroom; r++) {
    const Room *R = L->room + r;
    for (int y = 0; y < R->h; y++) for (int x = 0; x < R->w; x++) {
      int c = GRID(R, x, y);
      if (strchr("OADEW", c) && c && GW.nob < GIM_NO) GW.ob[GW.nob++] = (GimObj){ r, c, x, y, c == 'E' ? x*37 % 140 : 0 };
      if (c == 'V' && GW.npl < GIM_NP) {   // a seesaw: the plank is its _ cells
        int a = x, b = x;
        while (a > 0 && GRID(R, a-1, y) == '_') a--;
        while (b+1 < R->w && GRID(R, b+1, y) == '_') b++;
        int cx = x*8+4, hl = cx - a*8 > (b+1)*8 - cx ? cx - a*8 : (b+1)*8 - cx;
        GW.pl[GW.npl++] = (GimPlat){ r, 2, cx << 8, (y*8+2) << 8, hl, 0, 0, -1 };
      }
      if (c == '{' && (x == 0 || GRID(R, x-1, y) != '{') && GW.npl+1 < GIM_NP) {   // a scale lift: this { run and the next } run
        int a = x; while (a < R->w && GRID(R, a, y) == '{') a++;
        int b = a; while (b < R->w && GRID(R, b, y) != '}') b++;
        if (b == R->w) continue;
        int e = b; while (e < R->w && GRID(R, e, y) == '}') e++;
        int i = GW.npl; GW.npl += 2;
        GW.pl[i] = (GimPlat){ r, 1, x*8 << 8, y*8 << 8, (a-x)*8, 0, 0, i+1 };
        GW.pl[i+1] = (GimPlat){ r, 1, b*8 << 8, y*8 << 8, (e-b)*8, 0, 0, i };
      }
    }
  }
  for (int i = 0; i < GW.npl; i++) {   // a lift pair's reach: up to below the pulley, down to the ground
    GimPlat *p = GW.pl + i, *q = GW.pl + p->link;
    if (p->kind != 1 || p->link < i) continue;
    const Room *R = L->room + p->room;
    int bot[2];
    for (int k = 0; k < 2; k++) {
      const GimPlat *s = k ? q : p; int tx0 = s->x >> 11, tx1 = ((s->x >> 8) + s->w - 1) >> 3, ty = (s->y >> 11) + 1, hit = 0;
      for (; ty < R->h && !hit; ty++) for (int tx = tx0; tx <= tx1; tx++) if (SOLID >> wd.rm[p->room][ty][tx] & 1) hit = 1;
      bot[k] = hit ? (ty-1)*8 - 4 << 8 : (R->h*8 + 32) << 8;
    }
    int py = (p->y < q->y ? p->y : q->y) - (40 << 8), top = py + (8 << 8);
    if (py < 0) py = 0, top = 8 << 8;
    p->py = q->py = py; p->tilt = p->y + q->y;
    p->lo = top > p->tilt - bot[1] ? top : p->tilt - bot[1];
    p->hi = bot[0] < p->tilt - top ? bot[0] : p->tilt - top;
  }
  if (L->gim[GO_NIGHT])   // night: the walkers take wing and the bobbing buzzers walk
    for (E *e = en; e < en+ne; e++) {
      if (e->t == 1) e->t = 2, e->y -= 20 << 8, e->h = e->y;
      else if (e->t == 2) e->t = 1;
    }
  GW.rise = (L->room[0].h*8 + 24) << 8; GW.risedelay = 120;
  GW.sx = startx*8 > 48 ? (startx*8 - 48) << 8 : 0;
}
static void gim_restart(void) {   // from spawn(): the level's start, a checkpoint or a death
  const Level *L = LV + lvl;
  gim_lvsync();
  if ((GW.rot || GW.flip) && GW.xroom == room) gim_dims(lvl, room, GW.rot, &lw, &lh);
  GW.ride = GW.rott = 0; GW.poldy = hy;
  if (L->gim[GO_RISE] && room == 0) {   // some room to breathe after a checkpoint
    int floor = hy + (11 << 8) + (40 << 8);
    if (GW.rise < floor) GW.rise = floor;
    GW.risedelay = 90;
  }
  if (L->gim[GO_SCROLL] && room == 0 && (hx >> 8) < (GW.sx >> 8) + 24) GW.sx = hx > 48 << 8 ? hx - (48 << 8) : 0;
  if (gim_rush && left == LIMIT) left = GIM_RUSHTIME;
}

// ---------- every frame ----------
static int gim_plattop(const GimPlat *p, int X, int *sy) {   // the platform's surface under feet X..X+5, 1/256 px
  if (p->kind == 1) { int px = p->x >> 8; if (X+6 <= px || X >= px + p->w) return 0; *sy = p->y; return 1; }
  int cx = p->x >> 8, fx = X+3;
  if (fx < cx - p->w || fx > cx + p->w) return 0;
  *sy = p->y + (fx - cx)*p->tilt; return 1;
}
// Before objects() and hero(): platforms move and carry Hatrick. Returns 1 to skip the frame.
static int gim_pre(int k, int pr) {
  if (gim_rush) {
    if (lvl != gim_rushlv[gim_rush-1]) gim_rush = 0;
    else if (st == DEAD && stt >= 60) { gim_rushend(1); return 1; }   // a death ends the rush
  }
  if (GW.rott) { GW.rott--; return 1; }   // a door is turning the room
  if ((GW.rot || GW.flip) && GW.xroom != room) { if (GW.flipk == GF_GRAVITY) GW.flipt = 0; gim_unturn(); }
  for (int i = 0; i < GW.npl; i++) {
    GimPlat *p = GW.pl + i;
    if (p->room != room) continue;
    int riding = GW.ride == i+1;
    if (p->kind == 1 && p->link > i) {   // a scale lift pair, driven from its first half
      GimPlat *q = GW.pl + p->link;
      int push = riding ? 1 : GW.ride == p->link+1 ? -1 : 0, y0 = p->y;
      if (push) p->vy += push*6; else p->vy = brake(p->vy, 4);
      if (p->vy > 200) p->vy = 200;
      if (p->vy < -200) p->vy = -200;
      p->y += p->vy;
      if (p->y < p->lo) p->y = p->lo, p->vy = 0;
      if (p->y > p->hi) p->y = p->hi, p->vy = 0;
      q->y = p->tilt - p->y;   // one rope: their heights add up
      if (riding) hy += p->y - y0;
      if (GW.ride == p->link+1) hy -= p->y - y0;
    } else if (p->kind == 2) {   // a seesaw leans toward the weight on it
      int want = 0, t0 = p->tilt, fx = (hx >> 8) + 3 - (p->x >> 8);
      if (riding) { want = fx*72 / p->w; if (want > 72) want = 72; if (want < -72) want = -72; }
      int step = riding ? 2 : 1;
      p->tilt += want > p->tilt + step ? step : want < p->tilt - step ? -step : want - p->tilt;
      if (riding) hy += fx * (p->tilt - t0);
    }
  }
  GW.pvy = hvy; GW.poldy = hy;
  return 0;
}
static void gim_flipswap(int r) {   // bricks become coins and coins bricks (never inside Hatrick)
  const Level *L = LV + lvl; int w, h;
  gim_dims(lvl, r, r == GW.xroom ? GW.rot : 0, &w, &h);
  (void)L;
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    u8 *t = &wd.rm[r][y][x];
    if (*t == 2) *t = 6;
    else if (*t == 6 && !(r == room && ov(hx >> 8, hy >> 8, 6, 11, x*8, y*8, 8, 8))) *t = 2;
  }
}
static void gim_flipend(void) {
  GW.flipt = 0;
  if (GW.flipk == GF_GRAVITY && GW.flip && GW.xroom == room) gim_op(room, XF_FLIP);
  if (GW.flipk == GF_SWAP) gim_flipswap(GW.fliproom);
  sfx(S_TUBE); kick(6);
}
static void gim_flipstart(int x, int y) {
  int k = LV[lvl].gim[GO_FLIP];
  if (k == GF_GRAVITY && room == 0) k = GF_SPEED;   // the main area never turns over: its flag stays put
  if (GW.flipt) gim_flipend();
  GW.flipk = k; GW.flipt = GIM_FLIPTIME; GW.fliproom = room;
  static const u32 C[4] = { 0xff80e0, 0xffd84a, 0x60d8ff, 0xffffff };
  for (int i = 0; i < 4; i++) burst(x, y, C[i], 8);
  sfx(S_REVEAL); sfx(S_MOON); kick(10); rumble(5); addscore(1000, x, y-6);
  if (k == GF_GRAVITY) gim_op(room, XF_FLIP);
  if (k == GF_SWAP) gim_flipswap(room);
}
static void gim_door(GimObj *o) {   // a quarter turn clockwise
  int w, h, i = o - GW.ob;
  gim_dims(lvl, room, room == GW.xroom ? GW.rot : 0, &w, &h);
  if (w > MH || h > MW) { static int told; if (!told++) fprintf(stderr, "hatrick: a door can only turn a room up to %d columns wide\n", MH); return; }
  gim_op(room, XF_ROT);   // Hatrick turns with the room, then steps out of a door with floor under it: this one if it has
  GimObj *to = 0; int best = 1 << 30;
  for (GimObj *d = GW.ob; d < GW.ob + GW.nob; d++) {
    if (d->kind != 'D' || d->room != room || scan(d->x*8+1, d->y*8-3, 6, 11, SOLID) || !scan(d->x*8+1, d->y*8+8, 6, 9, SOLID)) continue;   // floor at most a tile below
    int dx = d->x*8+1 - (hx >> 8), dy = d->y*8-3 - (hy >> 8), dist = d - GW.ob == i ? -1 : dx*dx + dy*dy;
    if (dist < best) best = dist, to = d;
  }
  if (to) hx = (to->x*8+1) << 8, hy = (to->y*8-3) << 8;
  hvx = hvy = 0; st = NORM; gnd = 0; face = 1;
  for (int n = 1; n < 48 && scan(hx >> 8, hy >> 8, 6, 11, SOLID); n++) hy += (n & 1 ? n : -n) << 8;   // nearest free spot, up or down
  GW.poldy = hy; GW.rott = 24;
  cxf = hx - (W/2 - 3 << 8); cyf = hy - (H*5/8 << 8); camgy = hy;
  sfx(S_TUBE); sfx(S_GPLAND); kick(8); rumble(6);
}
static void gim_landed(GimPlat *p) {   // what landing on the ground does, for a platform
  landt = 0;
  if (st == GPSLAM) {
    st = GPLAND; stt = 0; poundt = 31; kick(10); sfx(S_GPLAND); rumble(6);
    if (p->kind == 2) p->tilt = (hx >> 8) + 3 > p->x >> 8 ? 72 : -72;   // a pound slams the seesaw down
  } else if (st == DIVE) st = SLIDE, posture(5);
  else if (st == LONGJ || st == SPINJ) st = NORM;
  if (GW.pvy > 900) sfx(S_LAND), rumble(2);
}
// After hazards(): landing on platforms, and every other gimmick's rules.
static void gim_post(int k, int pr) {
  const Level *L = LV + lvl;
  int X = hx >> 8, Y = hy >> 8, alive = st < TUBE;
  if ((GW.rot || GW.flip) && GW.xroom == room) gim_dims(lvl, room, GW.rot, &lw, &lh);   // tubes and spawns read the file's size
  // platforms: one-way, from above
  int was = GW.ride; GW.ride = 0;
  if (alive && st != HANG && st != CLIMB && hvy >= 0 && !gnd)
    for (int i = 0; i < GW.npl; i++) {
      GimPlat *p = GW.pl + i; int sy;
      if (p->room != room || !gim_plattop(p, X, &sy)) continue;
      int feet = hy + (11 << 8), pfeet = GW.poldy + (11 << 8);
      if (feet >= sy - 256 && pfeet <= sy + ((was == i+1 ? 5 : 3) << 8)) {
        hy = sy - (11 << 8); Y = hy >> 8; hvy = 0; gnd = 1; GW.ride = i+1;
        if (was != i+1) gim_landed(p);
        break;
      }
    }
  // gold block bumped from below
  if (GW.pvy < 0 && hvy == 0 && alive)
    for (GimObj *o = GW.ob; o < GW.ob + GW.nob; o++)
      if (o->kind == 'A' && !o->t && o->room == room && Y+duck == o->y*8+8 && o->x*8 < X+6 && o->x*8+8 > X) {
        o->t = 1; map[o->y][o->x] = 15; bumpx = o->x; bumpy = o->y; bumpt = 10;
        GW.gold = 20*60; GW.goldn = GW.goldrun = 0; sfx(S_REVEAL); sfx(S_BONUS); sparkle(o->x*8+4, o->y*8, 0xffe066, 10);
        break;
      }
  if (GW.gold) {   // running spills coins from it: faster running, more coins
    GW.gold--;
    if (alive) {
      GW.goldrun += iabs(hvx);
      if (GW.goldrun >= 6 << 8) {
        GW.goldrun -= 6 << 8; GW.goldn++; coins++; score += 100; sfx(S_COIN);
        pop(X - 1, Y - 12, 0);
      }
    } else if (st == DEAD) GW.gold = 0;
    if (GW.goldn >= 100) GW.gold = 0;
    if (!GW.gold && alive) burst(X+3, Y-4, 0xffd84a, 10), sfx(S_BRICK);
  }
  for (GimObj *o = GW.ob; o < GW.ob + GW.nob; o++) {
    if (o->room != room) continue;
    int cx = o->x*8, cy = o->y*8;
    if (o->kind == 'O' && !o->t && alive && ov(X, Y+duck, 6, 11-duck, cx, cy, 8, 8)) { o->t = 1; gim_flipstart(cx+4, cy+4); X = hx >> 8; Y = hy >> 8; }
    else if (o->kind == 'D' && room && alive && pr & 4 && gnd && (st == NORM || st == GSPIN) && ov(X, Y, 6, 11, cx, cy-6, 8, 14)) { gim_door(o); return; }
    else if (o->kind == 'E' && ++o->t >= 140) {   // a cannon fires toward Hatrick when he's near
      int dx = X+3 - (cx+4), dir = dx > 0 ? 1 : -1;
      o->t = 0;
      if (alive && iabs(dx) > 12 && iabs(dx) < 220 && iabs(Y - cy) < 120 && !scan(cx + dir*8, cy, 8, 8, SOLID))
        for (GimBall *b = GW.ball; b < GW.ball + GIM_NB; b++) if (!b->a) {
          *b = (GimBall){ room, (cx + dir*8) << 8, cy << 8, dir*352, 1 };
          sfx(S_SPIT); kick(2); burst(cx + 4 + dir*6, cy+4, 0xc8c8c8, 4); break;
        }
    } else if (o->kind == 'W') {   // a fan's updraft, up to 10 tiles while nothing solid is in the way
      int top = o->y;
      while (top > 0 && o->y - top < 10 && !(SOLID >> tile(o->x, top-1) & 1)) top--;
      if (alive && st != HANG && st != CLIMB && st != GPWIND && ov(X, Y, 6, 11, cx-1, top*8, 10, cy - top*8)) {
        int pull = 60 + 60*(Y + 11 - top*8)/(cy - top*8 + 1);
        hvy -= pull; if (hvy < -680) hvy = -680;
        if (st == GPSLAM || st == DIVE) st = NORM;
        capok = 1;
      }
      if (!(fr & 7)) part((cx + 1 + rnd(6)) << 8, cy << 8, rnd(40)-20, -500, (cy - top*8)*256/500 + 1, 0, 0xe8f4ff);
    }
  }
  for (GimBall *b = GW.ball; b < GW.ball + GIM_NB; b++) {   // cannonballs: stomp them, or the cap
    if (!b->a) continue;
    if (b->room != room) { b->a = 0; continue; }
    b->x += b->vx;
    int bx = b->x >> 8, by = b->y >> 8;
    if (scan(bx+1, by+1, 6, 6, SOLID) || iabs(bx - X) > 400) { b->a = 0; burst(bx+4, by+4, 0x505060, 5); continue; }
    if (alive && ov(X, Y+duck, 6, 11-duck, bx+1, by+1, 6, 6)) {
      if ((hvy > 0 || st == GPSLAM) && Y+11 < by+6) {
        b->a = 0; burst(bx+4, by+4, 0x505060, 8); sfx(S_STOMP); rumble(4); kick(5); addscore(200, bx+4, by);
        hvy = k & 16 ? -1000 : -650; st = NORM; arcg = GRAV; launch = 0; cut = 0; capok = diveok = stall = 1; spin = throwt = 0;
      } else die();
    }
    if (b->a && cst && cst < 3 && ov(cxp >> 8, cyp >> 8, 8, 5, bx, by, 8, 8)) { b->a = 0; burst(bx+4, by+4, 0x505060, 8); sfx(S_STOMP); addscore(200, bx+4, by); }
  }
  if (L->gim[GO_RISE] && room == 0 && st != WIN && !done) {   // the rising kill line
    if (GW.risedelay) GW.risedelay--;
    else if (st < DEAD) { GW.rise -= L->gim[GO_RISEV]*256/60; if (GW.rise < 0) GW.rise = 0; }
    if (st < TUBE && hy + (9 << 8) > GW.rise) doom();
    for (E *e = en; e < en+ne; e++) if (e->a && e->r == 0 && e->y > GW.rise) e->a = 0;
  }
  if (L->gim[GO_SCROLL] && room == 0) {   // the auto-scroll: the screen moves on, and Hatrick can't leave it
    int maxsx = lw*8 > W ? (lw*8 - W) << 8 : 0;
    if (st < DEAD && !done && fr > 60) { GW.sx += L->gim[GO_SCROLL]*256/60; if (GW.sx > maxsx) GW.sx = maxsx; }
    if (alive) {
      int sl = GW.sx >> 8;
      if ((hx >> 8) < sl) {
        hx = sl << 8; if (hvx < 0) hvx = 0;
        if (scan(sl, (hy >> 8)+duck, 6, 11-duck, SOLID)) doom();   // squeezed against a wall
      }
      if ((hx >> 8) + 6 > sl + W) { hx = (sl + W - 6) << 8; if (hvx > 0) hvx = 0; }
    }
  }
  if (GW.flipt) {
    if (GW.flipt <= 180 && !(GW.flipt % 30)) sfx(S_TICK);
    if (!--GW.flipt) gim_flipend();
  }
}
static int gim_again_on;
static void gim_again(int k) {   // the speed flip: a second step of the whole game each frame (but not of the clocks)
  if (gim_again_on || !GW.flipt || GW.flipk != GF_SPEED || menu || st >= DEAD) return;
  int l0 = left, t0 = tim, f0 = fr;
  gim_again_on = 1; tick(k); gim_again_on = 0;
  if (left == l0 - 1) left = l0;
  if (tim == t0 + 1) tim = t0;
  if (fr == f0 + 1) fr = f0;   // the frame clock too: beat hazards follow it and must keep time with the music
}
static void gim_camera(void) {   // presentation: the auto-scroll owns the camera's x
  if (LV[lvl].gim[GO_SCROLL] && room == 0) cxf = GW.sx;
}
static int gim_fastmusic(void) { return GW.flipt && GW.flipk == GF_SPEED; }
static int gim_8bit(void) { return !menu && LV[lvl].gimroom[room] == GR_8BIT; }
static const char *gim_theme(const char *want) {   // an 8-bit area plays the theme's chiptune version, if there is one
  static char t[80]; char dir[1024], path[1200];
  if (!gim_8bit()) return want;
  snprintf(t, sizeof t, "%s-8bit", want);
  assetdir(dir, sizeof dir); snprintf(path, sizeof path, "%s/music/%s", dir, t);
  return access(path, F_OK) ? want : t;
}

#elif GIM_PART == 3
// ---------- drawing ----------
static void gim_rect(int x, int y, int w, int h, u32 c) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) wpx(x+i, y+j, c); }
static u32 gim_mul(u32 c, int r, int g, int b) { return ((c >> 16 & 255)*r >> 8) << 16 | ((c >> 8 & 255)*g >> 8) << 8 | (c & 255)*b >> 8; }
static int gim_night(void) { return LV[lvl].gimroom[room] == GR_NIGHT; }
static void gim_bg(int lo) {   // after drawbg(): 8-bit and night areas get their own backdrop
  int kind = LV[lvl].gimroom[room];
  if (kind == GR_8BIT) {
    for (int x = 0; x < SW; x++) {
      int wx = (x + ox/2) / SC, m = wx % 96 - 48, top = H - 18 - (48 - (m < 0 ? -m : m))/2;
      top = top & ~1;
      for (int y = 0; y < SH; y++) {
        int yy = fdiv(y - lo/2, SC);
        big[y][x] = yy > top ? (yy == top+1 || (wx/2 + yy/2) % 9 == 0 ? 0x005800 : 0x00a800) : 0x5c94fc;
      }
    }
  } else if (kind == GR_NIGHT) {
    int mx = SW*3/4, my = SH/5, mr = 10*SC;
    for (int y = 0; y < SH; y++) {
      u32 sky = mixcolor(0x080c24, 0x2a3866, y, SH);
      int yf = fdiv(y - lo/3, SC), yn = fdiv(y - lo/2, SC), sy = y / SC;
      for (int x = 0; x < SW; x++) {
        u32 c = sky;
        int sx = (x + ox/8) / SC;
        unsigned hsh = (unsigned)sx*73856093u ^ (unsigned)sy*19349663u;
        if (hsh % 263 == 0) c = (hsh >> 9) + fr/8 & 7 ? 0xc8d0ff : 0xffffff;   // stars, a few twinkling
        int dx = x - mx, dy = y - my;
        if (dx*dx + dy*dy < mr*mr) c = (dx+3*SC)*(dx+3*SC) + (dy-2*SC)*(dy-2*SC) < 9*SC*SC ? 0xd8d4b0 : 0xf4f0d0;   // the moon
        int m = (x + ox/4) / SC % 160 - 80, fh = H - 46 + m*m/150;
        int n = (x + ox/2) / SC % 112 - 56, nh = H - 26 + n*n/110;
        if (yf > fh) c = 0x1a2a48;
        if (yn > nh) c = yn == nh+1 ? 0x2a4a5a : 0x1c3440;
        big[y][x] = c;
      }
    }
  }
}
static void gim_flower(int x, int y) {   // a spinning flower with a little top hat for a middle
  static const u32 PET[2] = { 0xff80e0, 0xffd84a };
  int bob = SIN[fr*4 & 255] / 128;
  for (int j = 4; j < 8; j++) wpx(x+3, y+j, 0x2e9a40), wpx(x+4, y+j, 0x2e9a40);
  for (int i = 0; i < 5; i++) {
    int a = fr*3 + i*51, px = x+3 + SIN[(a+64) & 255]*3/256, py = y+2+bob + SIN[a & 255]*3/256;
    wpx(px, py, PET[i & 1]); wpx(px+1, py, PET[i & 1]); wpx(px, py+1, PET[i & 1]); wpx(px+1, py+1, PET[i & 1]);
  }
  gim_rect(x+3, y+1+bob, 2, 2, 0x733431); gim_rect(x+2, y+3+bob, 4, 1, 0x733431);
}
static void gim_gold(int x, int y, int shine) {   // 8x8 gold block
  for (int v = 0; v < 8; v++) for (int u = 0; u < 8; u++) {
    u32 c = u == 0 || v == 0 ? 0xfff2a0 : u == 7 || v == 7 ? 0x9a6a10 : (u + v + shine/3) % 12 == 0 ? 0xffffff : 0xf0c030;
    if ((u == 2 || u == 5) && v >= 2 && v <= 5) c = 0xc89020;   // two little slots
    wpx(x+u, y+v, c);
  }
}
static void gim_draw(void) {   // after the tiles: platforms, gimmick blocks and objects, cannonballs
  for (const GimPlat *p = GW.pl; p < GW.pl + GW.npl; p++) {
    if (p->room != room) continue;
    if (p->kind == 1) {
      int x = p->x >> 8, y = p->y >> 8, py = p->py >> 8;
      for (int yy = py; yy < y; yy++) wpx(x+2, yy, 0xc8b490), wpx(x+p->w-3, yy, 0xc8b490);   // ropes
      if (p->link > p - GW.pl) {   // the pulley bar between the pair, and its wheels
        const GimPlat *q = GW.pl + p->link; int qx = q->x >> 8;
        gim_rect(x+2, py-1, qx + q->w - 3 - x - 1, 2, 0x8a94a0);
        for (int k = 0; k < 2; k++) { int wx = k ? qx + q->w/2 : x + p->w/2; gim_rect(wx-2, py-3, 4, 4, 0x586070); wpx(wx, py-2, 0xe8ecf0); }
      }
      for (int u = 0; u < p->w; u++) for (int v = 0; v < 4; v++)
        wpx(x+u, y+v, v == 0 ? 0xf0b070 : v == 3 ? 0x7a4220 : (u & 7) == 0 ? 0x9a5a28 : 0xc87a3a);
    } else {
      int cx = p->x >> 8, y = p->y >> 8;
      for (int j = 0; j < 6; j++) for (int i = -j/2; i <= j/2; i++) wpx(cx+i, y+2+j, j == 5 ? 0x586070 : 0x8a94a0);   // the pivot
      for (int u = -p->w; u < p->w; u++) {
        int sy = (p->y + u*p->tilt) >> 8;
        wpx(cx+u, sy, 0xf0b070); wpx(cx+u, sy+1, (u & 7) == 0 ? 0x9a5a28 : 0xc87a3a); wpx(cx+u, sy+2, 0x7a4220);
      }
    }
  }
  for (const GimObj *o = GW.ob; o < GW.ob + GW.nob; o++) {
    if (o->room != room) continue;
    int x = o->x*8, y = o->y*8;
    if (o->kind == 'O' && !o->t) gim_flower(x, y);
    else if (o->kind == 'A' && !o->t) gim_gold(x, y, fr);
    else if (o->kind == 'D') {   // a door with a turning arrow on it
      for (int v = -6; v < 8; v++) for (int u = 0; u < 8; u++)
        wpx(x+u, y+v, u == 0 || u == 7 || v == -6 ? 0x5a3418 : (u + v) % 4 == 0 ? 0x8a5428 : 0xa66a34);
      for (int i = 0; i < 6; i++) { int a = i*36 + fr*2; wpx(x+4 + SIN[(a+64) & 255]*2/256, y-1 + SIN[a & 255]*2/256, 0xffd84a); }
      wpx(x+6, y+3, 0xffd84a);   // the knob
    } else if (o->kind == 'E') {   // a cannon block, its muzzle toward Hatrick
      int dir = (hx >> 8) + 3 > x + 4 ? 1 : -1;
      for (int v = 0; v < 8; v++) for (int u = 0; u < 8; u++)
        wpx(x+u, y+v, u == 0 || v == 0 ? 0x5a5a6a : u == 7 || v == 7 ? 0x14141c : (u == 1 || u == 6) && (v == 1 || v == 6) ? 0xa0a0b0 : 0x2a2a36);
      int mx = dir > 0 ? x+5 : x;
      gim_rect(mx, y+2, 3, 4, 0x0c0c12); wpx(mx + (dir > 0 ? 0 : 2), y+2, 0x7a7a8a);
    } else if (o->kind == 'W') {   // a propeller fan: blades spin on top of a grey base
      for (int v = 3; v < 8; v++) for (int u = 0; u < 8; u++) wpx(x+u, y+v, v == 3 ? 0xc0c8d0 : v == 7 ? 0x404850 : 0x7c8796);
      int w = SIN[(fr*40 + 64) & 255] * 4 / 256;
      gim_rect(x+4 - (w < 0 ? -w : w), y+1, 2*(w < 0 ? -w : w) + 1, 2, 0xe8ecf0);
      wpx(x+3, y+2, 0x586070); wpx(x+4, y+2, 0x586070);
    }
  }
  for (const GimBall *b = GW.ball; b < GW.ball + GIM_NB; b++) if (b->a && b->room == room) {
    int bx = b->x >> 8, by = b->y >> 8;
    for (int v = 0; v < 8; v++) for (int u = 0; u < 8; u++) {
      int dx = 2*u - 7, dy = 2*v - 7;
      if (dx*dx + dy*dy > 56) continue;
      wpx(bx+u, by+v, (dx+3)*(dx+3) + (dy+3)*(dy+3) < 6 ? 0xc8c8d8 : dx*dx + dy*dy > 36 ? 0x0c0c12 : 0x2a2a36);
    }
  }
}
static u32 NESPAL[] = { 0x000000, 0x7c7c7c, 0xbcbcbc, 0xf8f8f8, 0x0000fc, 0x0078f8, 0x3cbcfc, 0xa4e4fc, 0x940084, 0xd800cc,
                        0xf878f8, 0xa80020, 0xf83800, 0xf87858, 0xf0d0b0, 0xac7c00, 0xf8b800, 0xf8d878, 0x00b800, 0x58d854,
                        0x005800, 0x00a800, 0xb8f8b8, 0x008888, 0x00e8d8, 0x503000, 0x881400, 0xe45c10, 0xfca044, 0x5c94fc,
                        0x6844fc, 0xd8b8f8 };
static void gim_chunky(void) {   // an 8-bit area: 2x2 world pixels per block, in the NES palette
  static u8 near[32768], ready[32768 / 8];
  int B = 2*SC, x0 = -(((ox % B) + B) % B), y0 = -(((oy % B) + B) % B);
  for (int by = y0; by < SH; by += B) for (int bx = x0; bx < SW; bx += B) {
    int sx = bx + B/2, sy = by + B/2;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx >= SW) sx = SW-1;
    if (sy >= SH) sy = SH-1;
    u32 c = big[sy][sx]; int key = (c >> 9 & 0x7c00) | (c >> 6 & 0x3e0) | (c >> 3 & 31);
    if (!(ready[key >> 3] >> (key & 7) & 1)) {
      int best = 0, bd = 1 << 30;
      for (int i = 0; i < (int)(sizeof NESPAL / sizeof *NESPAL); i++) {
        int dr = (int)(c >> 16 & 255) - (int)(NESPAL[i] >> 16), dg = (int)(c >> 8 & 255) - (int)(NESPAL[i] >> 8 & 255), db = (int)(c & 255) - (int)(NESPAL[i] & 255);
        int d = dr*dr*3 + dg*dg*4 + db*db*2;
        if (d < bd) bd = d, best = i;
      }
      near[key] = best; ready[key >> 3] |= 1 << (key & 7);
    }
    blk(bx, by, B, B, NESPAL[near[key]]);
  }
}
static void gim_spin(void) {   // a door's turn: the new view starts a quarter turn back and settles
  static u32 copy[SH][SW];
  memcpy(copy, big, sizeof copy);
  int a = GW.rott*64/24, c = SIN[(a+64) & 255], s = SIN[a & 255], cx = SW/2, cy = SH/2;
  for (int y = 0; y < SH; y++) for (int x = 0; x < SW; x++) {
    int dx = x - cx, dy = y - cy, sx = cx + (dx*c - dy*s)/256, sy = cy + (dx*s + dy*c)/256;
    big[y][x] = sx >= 0 && sx < SW && sy >= 0 && sy < SH ? copy[sy][sx] : 0x0c0e18;
  }
}
static void gim_overlay(void) {   // render(), over the world and before the HUD
  const Level *L = LV + lvl;
  if (GW.gold && st < TUBE && (GW.gold > 180 || fr & 4)) gim_gold((hx >> 8) - 1, (hy >> 8) + duck - 8, fr);
  if (L->gim[GO_RISE] && room == 0) {   // lava (or water) up to its wavy top
    int lava = L->gim[GO_RISE] == 1, top = GW.rise >> 8;
    for (int wx = ox/SC - 1; wx <= (ox+SW)/SC; wx++) {
      int surf = top + SIN[(wx*9 + fr*3) & 255]*3/2/256, ys = surf*SC - oy;
      for (int y = ys < 0 ? 0 : ys; y < SH; y++) {
        int d = (y - ys)/SC; u32 c;
        if (lava) c = d < 1 ? 0xfff0a0 : d < 3 ? 0xffb030 : (wx*7 + d*13 + fr/4) % 53 == 0 ? 0xffd060 : mixcolor(0xf05018, 0x801008, d > 40 ? 40 : d, 40);
        for (int x = wx*SC - ox; x < wx*SC - ox + SC; x++) if (x >= 0 && x < SW) {
          if (!lava) { u32 b = big[y][x]; c = d < 1 ? 0xd0f0ff : mixcolor(b, d < 3 ? 0x60a8e8 : 0x1c50b0, 160, 256); }
          big[y][x] = c;
        }
      }
    }
  }
  if (gim_night()) for (int y = 0; y < SH; y++) for (int x = 0; x < SW; x++) big[y][x] = gim_mul(big[y][x], 170, 185, 235);
  if (L->gimroom[room] == GR_8BIT) gim_chunky();
  if (GW.flip && GW.xroom == room) for (int y = 0; y < SH/2; y++) {   // reversed gravity: the world is drawn upside down
    static u32 row[SW];
    memcpy(row, big[y], sizeof row); memcpy(big[y], big[SH-1-y], sizeof row); memcpy(big[SH-1-y], row, sizeof row);
  }
  if (GW.rott) gim_spin();
}

// ---------- the coin rush and the gallery ----------
static int gim_rushcoins, gim_rushbest = -2, gim_rushshow, gim_rushfail, gim_rushnew, gim_galt;
static void gim_rushload(void) {
  const char *p = savepath("HATRICK_RUSH", ".hatrick_rush"); FILE *f = p ? fopen(p, "r") : 0;
  gim_rushbest = -1;
  if (f) { if (fscanf(f, "%d", &gim_rushbest) != 1) gim_rushbest = -1; fclose(f); }
}
static struct { int score, lscore, coins, lcoins, deaths, tim, lstart, runok, runnext; } gim_run;   // the run, kept aside during a rush
static void gim_rushend(int fail) {   // fail: 1 a death, 2 left through the pause screen
  score = gim_run.score; lscore = gim_run.lscore; coins = gim_run.coins; lcoins = gim_run.lcoins; deaths = gim_run.deaths;
  tim = gim_run.tim; lstart = gim_run.lstart; runok = gim_run.runok; runnext = gim_run.runnext;   // a rush is its own game: the run comes back as it was
  gim_rush = 0; gim_rushfail = fail; gim_rushshow = 600; gim_rushnew = 0;
  if (gim_rushbest < -1) gim_rushload();
  if (!fail && gim_rushcoins > gim_rushbest) {
    const char *p = savepath("HATRICK_RUSH", ".hatrick_rush"); FILE *f = p ? fopen(p, "w") : 0;
    gim_rushbest = gim_rushcoins; gim_rushnew = 1;
    if (f) fprintf(f, "%d\n", gim_rushbest), fclose(f);
  }
  if (fail != 2) tomap();
}
static void gim_rushstart(void) {   // three levels: cleared ones if there are three, else any
  int pool[64], n = 0;
  for (int l = 0; l < NLV && n < 64; l++) if (cleared(l)) pool[n++] = l;
  if (n < 3) { n = 0; for (int l = 0; l < NLV && n < 64; l++) pool[n++] = l; }
  if (!n) return;
  seed ^= menufr * 2654435761u;
  for (int i = 0; i < 3; i++) {
    if (n >= 3) { int j = i + rnd(n - i), t = pool[i]; pool[i] = pool[j]; pool[j] = t; gim_rushlv[i] = pool[i]; }
    else gim_rushlv[i] = pool[rnd(n)];
  }
  gim_run.score = score; gim_run.lscore = lscore; gim_run.coins = coins; gim_run.lcoins = lcoins; gim_run.deaths = deaths;
  gim_run.tim = tim; gim_run.lstart = lstart; gim_run.runok = runok; gim_run.runnext = runnext;
  gim_rush = 1; gim_rushcoins = 0; gim_rushshow = 0;
  startlevel(gim_rushlv[0]); gim_rushc0 = coins;
}
static int gim_rushnext(void) {   // from nextlevel(): on to the rush's next level, or its result
  if (!gim_rush) return 0;
  gim_rushcoins += coins - gim_rushc0;
  if (gim_rush < 3) { int l = gim_rushlv[gim_rush++]; startlevel(l); gim_rushc0 = coins; }
  else gim_rushend(0);
  return 1;
}
static int gim_keys(int k, int pr) {   // from tick(): F2 the gallery, F3 a coin rush (on the map)
  (void)k;
  if (gim_rush && menu && !resumable) gim_rushend(2);   // back on the map from the pause screen
  if (pr & GIM_GALLERY && !naming && !(menu && resumable)) {   // the lab levels but the playground, in turn
    int first = -1, next = -1;
    for (int l = NLV; l < NLEVEL; l++) if (l != PLAY) { if (first < 0) first = l; if (next < 0 && l > lvl) next = l; }
    if (first < 0) return 0;
    if (menu || lvl < NLV || lvl == PLAY || next < 0) next = first;   // from the map or a campaign level: the first one
    if (resumable && !menu && !done) { score = lscore; coins = lcoins; if (lvl >= NLV) tim = lstart; }   // left uncleared: its gains don't count (as quitlevel)
    gim_rush = 0; startlevel(next); gim_galt = 180;
    return 1;
  }
  if (pr & GIM_RUSH && menu && !resumable && !scoreview && !naming) { gim_rushstart(); return 1; }
  return 0;
}
static void gim_hud(void) {   // render(), after the HUD
  char t[48];
  if (gim_rush) {
    snprintf(t, sizeof t, "COIN RUSH %d/3", gim_rush); hudtext(t, 30, 64, 0xffd84a);
    snprintf(t, sizeof t, "%d", gim_rushcoins + coins - gim_rushc0); hudtext(t, 30, 92, 0xffffff);
  }
  if (GW.flipt) {   // the hat flip's time running out
    int w = GW.flipt * 200 / GIM_FLIPTIME;
    mrect(MENUW/2 - 104, 62, 208, 14, 0x14100c);
    mrect(MENUW/2 - 100, 66, w, 6, GW.flipt > 180 || fr & 8 ? 0xff80e0 : 0xffffff);
    hudtext("HAT FLIP", (MENUW - textwidth("HAT FLIP", 1)) / 2, 82, 0xffffff);
  }
  if (gim_galt) { gim_galt--; hudtext(LV[lvl].name, (MENUW - textwidth(LV[lvl].name, 1)) / 2, MENUH/2 - 40, 0xffffff); }
}
static void gim_maphud(void) {   // the map: how to start a coin rush, and the last one's result
  char t[48];
  if (scoreview || naming) return;
  if (gim_rushbest < -1) gim_rushload();
  menutext("F3 COIN RUSH", MENUW - textwidth("F3 COIN RUSH", 1) - 14, MENUH - 50, 1, 0xffffff);
  menutext("F2 GIMMICKS", MENUW - textwidth("F2 GIMMICKS", 1) - 14, MENUH - 26, 1, 0xffffff);
  if (gim_rushshow) {
    gim_rushshow--;
    plaque(MENUW/2 - 180, 130, 360, 130);
    centered("COIN RUSH", 146, 1, 0xffd84a);
    if (gim_rushfail) centered(gim_rushfail == 1 ? "OUT OF LUCK" : "LEFT EARLY", 180, 1, 0xffffff);
    else { snprintf(t, sizeof t, "%d COINS", gim_rushcoins); centered(t, 180, 1, gim_rushnew ? 0xffd84a : 0xffffff); }
    if (gim_rushbest >= 0) { snprintf(t, sizeof t, "BEST %d", gim_rushbest); centered(t, 214, 1, 0xc8d0e0); }
  }
}
#endif
