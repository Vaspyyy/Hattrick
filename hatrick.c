// Hatrick: a tiny cap-throwing platformer for Linux/X11. Levels, music and sounds load from assets/ (MODDING.md).
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
#include "sound.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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
#define SOLID 0x1BF3E    // full tiles 1..5, slopes 8..9, tubes 10..12, crumble 13, found hidden block 15, fire bar pivot 16
// Tiles: see tiletype().
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
#define MENUN (NLV + (PLAY >= 0) + 1)   // levels, the playground, high scores
#define GPJUMP_V 1400
enum { NORM, LONGJ, GPWIND, GPSLAM, GPLAND, DIVE, SLIDE, ROLL, SPINJ, GSPIN, HANG, CLIMB, TUBE, DEAD, WIN };   // TUBE and up: untouchable
enum { CAPFORWARD, CAPUP, CAPDOWN, CAPSPIN };

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
// Sounds requested by the game logic this frame. The platform layer plays them; the simulator
// and tests just ignore the queue. hop (1..256, 0 = none) is the progress of the enemies' little
// hop on the music's "bah" accents; it is purely visual, so collisions never depend on audio.
static int sndq[16], nsnd, muted, loads, hop;
typedef struct { int x, y, vx, vy, t, a, h, r; } E;   // r: the room it lives in
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
static void sfx(int i) { if (nsnd < 16) sndq[nsnd++] = i; }
// Controller rumble: 1 cap bounce / wall jump, 2 hard landing, 3 brick, 4 stomp, 5 spring,
// 6 ground-pound landing, 7 death, 8 goal. The strongest request in a frame is played.
static int rumq;
static void rumble(int k) { if (k > rumq) rumq = k; }

// ---------- levels ----------
// levels.txt (see MODDING.md) is read at startup from assets/ next to the executable; the copy
// built into the binary (levels.h) is used when it is missing or has no valid level. Campaign
// levels come first in file order, then the "lab" levels; the last lab is the movement playground.
// A level is its main area plus up to three bonus rooms ("+" sections), linked by tubes.
enum { CARD_HILLS, CARD_BRICKS, CARD_SPIKES, CARD_SKY, CARD_CASTLE, CARD_PLAYGROUND, NCARD };
static const char *const CARDNAME[NCARD] = { "hills", "bricks", "spikes", "sky", "castle", "playground" };
#define NROOM 4
enum { T_UP, T_DOWN, T_LEFT, T_RIGHT };   // the way a tube mouth opens
typedef struct { int room, x, y, dir, id, link; } Tube;   // mouth's top-left cell; link: partner tube or -1
typedef struct { int room, x, y, len, speed, a0; } Bar;    // fire bar pivot cell; speed in 1/65536 turn per frame (+ clockwise)
typedef struct { int room, tube, spit; } Home;             // a tube dweller and the mouth it lives in
typedef struct { int w, cave; u8 grid[MH][MW]; } Room;
typedef struct {
  char name[40], music[64]; int card, lab, par, nroom, ntube, nbar, nhome;
  Room room[NROOM]; Tube tube[40]; Bar bar[24]; Home home[24];
} Level;
static Level *LV;
static int NLV, NLEVEL, PLAY = -1;   // campaign levels, all levels, playground index (-1: none)
static const char KNOWN[] = "#B^STo/\\|-M?C*%~:!0123456789@FKgbhnm";   // every map character but space

// What a map character becomes in the live map: 1 ground, 2 brick, 3 spikes, 4 stone, 5 spring,
// 6 coin, 8/9 slopes, 10/11 tube body (vertical / horizontal), 12 tube mouth, 13 crumble block,
// 14 hidden block (15 once found), 16 fire bar pivot. Objects and markers leave the cell empty.
static int tiletype(int c) {
  switch (c) {
    case '#': return 1; case 'B': return 2; case '^': return 3; case 'S': return 4; case 'T': return 5;
    case 'o': return 6; case '/': return 8; case '\\': return 9; case '|': return 10; case '-': return 11;
    case 'C': return 13; case '?': return 14; case '*': case '%': return 16;
  }
  return c == 'M' || (c >= '0' && c <= '9') ? 12 : 0;
}

static void levelerr(const char *file, int line, const char *fmt, ...) {
  va_list ap; va_start(ap, fmt);
  fprintf(stderr, "hatrick: %s:%d: ", file, line); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
  va_end(ap);
}
// Reads a level header "= 1 hills music=overworld card=hills par=90" or a room header "+ cellar bg=sky".
static void parseheader(Level *L, Room *R, char *h, const char *file, int line) {
  char name[40] = "", *w, *ws = 0;
  for (w = strtok_r(h, " \t", &ws); w; w = strtok_r(0, " \t", &ws)) {
    char *eq = strchr(w, '=');
    if (!eq) { if (strlen(name) + strlen(w) + 2 < sizeof name) strcat(strcat(name, *name ? " " : ""), w); continue; }
    *eq++ = 0;
    if (R) {
      if (!strcmp(w, "bg") && (!strcmp(eq, "cave") || !strcmp(eq, "sky"))) R->cave = !strcmp(eq, "cave");
      else levelerr(file, line, "unknown room option \"%s=%s\" (use bg=cave or bg=sky)", w, eq);
    } else if (!strcmp(w, "music")) snprintf(L->music, sizeof L->music, "%s", eq);
    else if (!strcmp(w, "par")) { L->par = atoi(eq); if (L->par < 1) levelerr(file, line, "par needs a number of seconds"), L->par = 100; }
    else if (!strcmp(w, "card")) {
      int c = 0; while (c < NCARD && strcmp(eq, CARDNAME[c])) c++;
      if (c < NCARD) L->card = c; else levelerr(file, line, "unknown card \"%s\" (use hills, bricks, spikes, sky, castle or playground)", eq);
    } else levelerr(file, line, "unknown option \"%s\" (use music=, card= or par=)", w);
  }
  if (R) return;
  L->lab = !strncmp(name, "lab", 3);
  char *p = name; while ((*p >= '0' && *p <= '9') || *p == ' ') p++;   // "1 hills" -> "HILLS"
  for (int i = 0; p[i] && i < (int)sizeof L->name - 1; i++) L->name[i] = p[i] >= 'a' && p[i] <= 'z' ? p[i]-32 : p[i];
}
// Fills one area's grid from its rows (sitting at the bottom of the map). Returns 0 if unusable.
static int parserows(Level *L, int r, char **ln, const int *row, int nr, const char *file, int head) {
  Room *R = L->room + r;
  if (nr > MH) { levelerr(file, head, "%s \"%s\" is taller than %d rows, skipped", r ? "bonus room of level" : "level", L->name, MH); return 0; }
  for (int i = 0; i < nr; i++) {
    const char *l = ln[row[i]]; int y = MH - nr + i, len = strlen(l);
    if (len > MW) { levelerr(file, row[i]+1, "level \"%s\" is wider than %d columns, skipped", L->name, MW); return 0; }
    if (len > R->w) R->w = len;
    for (int x = 0; x < len; x++) {
      char c = l[x];
      if (c != ' ' && !strchr(KNOWN, c)) { levelerr(file, row[i]+1, "unknown tile '%c' in column %d, left empty", c, x+1); c = ' '; }
      if (r && (c == '@' || c == 'F')) { levelerr(file, row[i]+1, "'%c' belongs in the level's main area, not a bonus room; left empty", c); c = ' '; }
      R->grid[y][x] = c == ' ' ? 0 : c;
    }
  }
  if (!R->w) R->w = 1;
  return 1;
}
// Tubes, fire bars and tube dwellers of one area; mistakes are reported and the object dropped.
static int isarm(int c) { return c == '~' || c == ':' || c == '!'; }
static void parseobjects(Level *L, int r, const char *file, const int *line) {
  u8 (*g)[MW] = L->room[r].grid, seen[MH][MW] = { { 0 } };
  #define AT(x, y) ((unsigned)(x) < MW && (unsigned)(y) < MH ? g[y][x] : 0)
  for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) {
    int c = g[y][x];
    if (!c || seen[y][x]) continue;
    if (c == 'M' || (c >= '0' && c <= '9')) {
      Tube t = { r, x, y, T_UP, c == 'M' ? -1 : c-'0', -1 };
      if (AT(x+1, y) == c && !seen[y][x+1]) {
        seen[y][x+1] = 1;
        t.dir = AT(x, y-1) == '|' && AT(x, y+1) != '|' ? T_DOWN : T_UP;
      } else if (AT(x, y+1) == c) {
        seen[y+1][x] = 1;
        t.dir = AT(x-1, y) == '-' && AT(x+1, y) != '-' ? T_RIGHT : T_LEFT;
      } else { levelerr(file, line[y], "tube mouth '%c' in column %d needs a second cell: side by side for a tube opening up or down, stacked for one opening sideways", c, x+1); continue; }
      if (L->ntube < 40) L->tube[L->ntube++] = t; else levelerr(file, line[y], "too many tube mouths (at most 40)");
    }
    if (c == '*' || c == '%') {
      static const int DX[4] = { 1, 0, -1, 0 }, DY[4] = { 0, 1, 0, -1 };
      int best = 0, dir = 0, kind = '~';
      for (int d = 0; d < 4; d++) {
        int n = 1; while (isarm(AT(x + n*DX[d], y + n*DY[d]))) n++;
        if (n-1 > best) best = n-1, dir = d, kind = AT(x + DX[d], y + DY[d]);
      }
      int speed = kind == ':' ? 182 : kind == '!' ? 437 : 273;   // a turn in 6, 4 or 2.5 s
      Bar b = { r, x, y, best ? best+1 : 5, c == '*' ? speed : -speed, dir*64 };
      if (L->nbar < 24) L->bar[L->nbar++] = b; else levelerr(file, line[y], "too many fire bars (at most 24)");
    }
  }
  for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) {
    int c = g[y][x], home = -1;
    if (c != 'n' && c != 'm') continue;
    for (int i = 0; i < L->ntube; i++) {
      Tube *t = L->tube + i;
      if (t->room == r && (t->x == x || t->x+1 == x) && ((t->dir == T_UP && t->y == y+1) || (t->dir == T_DOWN && t->y == y-1))) home = i;
    }
    if (home < 0) levelerr(file, line[y], "tube dweller '%c' in column %d must sit right above a tube opening up, or right below one opening down", c, x+1);
    else if (L->nhome < 24) L->home[L->nhome++] = (Home){ r, home, c == 'm' };
    else levelerr(file, line[y], "too many tube dwellers (at most 24)");
  }
  #undef AT
}
// Parses a whole levels.txt. Broken levels are reported (file name and line) and skipped;
// returns 0 if no campaign level is usable, leaving the current levels in place.
static int parselevels(const char *text, const char *file) {
  char *copy = strdup(text);
  int nl = 1; for (char *c = copy; *c; c++) nl += *c == '\n';
  char **ln = malloc(nl * sizeof *ln);
  ln[0] = copy; nl = 1;
  for (char *c = copy; *c; c++) if (*c == '\n') *c = 0, ln[nl++] = c+1;
  for (int i = 0; i < nl; i++) {   // trailing spaces and CRs never matter
    size_t len = strlen(ln[i]);
    while (len && (ln[i][len-1] == '\r' || ln[i][len-1] == ' ' || ln[i][len-1] == '\t')) ln[i][--len] = 0;
  }
  Level *out = 0; int n = 0;
  for (int h = 0; h < nl; h++) {
    if (ln[h][0] != '=') continue;
    out = realloc(out, (n+1) * sizeof *out);
    Level *L = out + n;
    memset(L, 0, sizeof *L); strcpy(L->music, "overworld"); L->par = 100;
    parseheader(L, 0, ln[h]+1, file, h+1);
    int end = h+1; while (end < nl && ln[end][0] != '=') end++;
    int ok = 1, lines[NROOM][MH];
    // sections: the main area, then each "+" room up to the next one
    for (int s = h; s < end && ok; ) {
      int next = s+1; while (next < end && ln[next][0] != '+') next++;
      int r = L->nroom;
      if (r == NROOM) { levelerr(file, s+1, "level \"%s\" has more than %d bonus rooms; the rest are ignored", L->name, NROOM-1); break; }
      if (r) { L->room[r].cave = 1; parseheader(L, L->room + r, ln[s]+1, file, s+1); }   // rooms default to a cave
      int row[MH + 1], nr = 0, last = -1;
      for (int i = s+1; i < next; i++) if (ln[i][0] != ';' && ln[i][0]) last = i;
      for (int i = s+1; i <= last; i++) if (ln[i][0] != ';') { if (nr <= MH) row[nr] = i; nr++; }
      ok = parserows(L, r, ln, row, nr, file, s+1);
      for (int y = 0; y < MH; y++) { int i = y - (MH - nr); lines[r][y] = ok && i >= 0 ? row[i]+1 : s+1; }
      L->nroom++;
      s = next;
    }
    int sx = -1, fx = -1, nen = 0;
    for (int r = 0; r < L->nroom && ok; r++) for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) {
      int c = L->room[r].grid[y][x];
      if (c == '@') sx = x; else if (c == 'F') fx = x; else if (c && strchr("gbh", c)) nen++;
    }
    if (ok && sx < 0) levelerr(file, h+1, "level \"%s\" has no start (@), skipped", L->name), ok = 0;
    if (ok && fx < 0) levelerr(file, h+1, "level \"%s\" has no flag (F), skipped", L->name), ok = 0;
    if (ok && nen > 48) levelerr(file, h+1, "level \"%s\" has too many enemies (at most 48), skipped", L->name), ok = 0;
    if (ok) {
      for (int r = 0; r < L->nroom; r++) parseobjects(L, r, file, lines[r]);
      for (int id = 0; id < 10; id++) {   // each digit links the two mouths that carry it
        int a = -1, b = -1, more = 0;
        for (int i = 0; i < L->ntube; i++) if (L->tube[i].id == id) { if (a < 0) a = i; else if (b < 0) b = i; else more = 1; }
        if (b >= 0) L->tube[a].link = b, L->tube[b].link = a;
        else if (a >= 0) levelerr(file, h+1, "level \"%s\": tube %d has no partner (give a second tube mouth the same digit); it can't be entered", L->name, id);
        if (more) levelerr(file, h+1, "level \"%s\": more than two tube mouths are numbered %d; only the first two are linked", L->name, id);
      }
      int nck = 0, ncr = 0;
      for (int r = 0; r < L->nroom; r++) for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++)
        nck += L->room[r].grid[y][x] == 'K', ncr += L->room[r].grid[y][x] == 'C';
      if (nck > 16) levelerr(file, h+1, "level \"%s\" has more than 16 checkpoints; the rest are ignored", L->name);
      if (ncr > 96) levelerr(file, h+1, "level \"%s\" has more than 96 crumble blocks; the rest stay solid", L->name);
    }
    n += ok;
    h = end - 1;
  }
  free(ln); free(copy);
  int camp = 0; for (int i = 0; i < n; i++) camp += !out[i].lab;
  if (!camp) { fprintf(stderr, "hatrick: %s: no playable level\n", file); free(out); return 0; }
  Level *sorted = malloc(n * sizeof *sorted); int k = 0;   // campaign first, then labs, in file order
  for (int pass = 0; pass < 2; pass++) for (int i = 0; i < n; i++) if (out[i].lab == pass) sorted[k++] = out[i];
  free(out); free(LV);
  LV = sorted; NLV = camp; NLEVEL = n; PLAY = n > camp ? n-1 : -1;
  return 1;
}
static int readlevels(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  char *text = 0; size_t n = 0, cap = 0, got;
  do { if (n + 4096 >= cap) text = realloc(text, cap = cap*2 + 8192); got = fread(text + n, 1, cap - n - 1, f); n += got; } while (got);
  fclose(f); text[n] = 0;
  int ok = parselevels(text, path);
  free(text);
  return ok;
}
__attribute__((constructor)) static void builtinlevels(void) { parselevels(LEVELS_TXT, "built-in levels.txt"); }

// ---------- the live level: everything a checkpoint saves and a death restores ----------
typedef struct { int room, x, y, state, t, fy, vy, stood; } Crumble;   // state 0 in place (t: shaking), 1 falling, 2 gone (t: until back)
typedef struct { int phase, t, ofs, a; } Dweller;               // phase 0 hidden, 1 rising, 2 out, 3 sinking; ofs: px out of the tube
typedef struct { int room, x, y, vx, vy, a; } Shot;             // a spitter's seed, 1/256 px
typedef struct { int room, x, y, up; } Check;
static struct World {
  u8 rm[NROOM][MH][MW];
  E en[48]; int ne;
  Crumble cr[96]; int ncr;
  Dweller dw[24];
  Shot sh[8];
  Check ck[16]; int nck;
  int found;      // bonus rooms already visited (bit per room), for the find bonus
} wd, saved;
#define map (wd.rm[room])
#define en (wd.en)
#define ne (wd.ne)
static int room, startx, starty, haveck, ckroom, ckx, cky;
static int score, lscore, savedcoins, savedscore, lstart, split;   // split: frames the finished level took
static int tubefrom, tubeto, tubelock, ride, wphase, wt, tally, flagy, skipclear, donet;
// Presentation of scoring and finds (never read by the game logic).
typedef struct { int x, y, v, t; } Pop;   // a score number (v > 0) or a popped coin (v = 0) rising from x, y (px)
static Pop pops[12];
static int bumpx, bumpy, bumpt;          // a hidden block jumping when found
// The assets folder next to the running executable.
static void assetdir(char *dir, size_t size) {
  ssize_t n = readlink("/proc/self/exe", dir, size - 16);
  if (n > 0) { dir[n] = 0; char *slash = strrchr(dir, '/'); if (slash) *slash = 0; } else strcpy(dir, ".");
  strcat(dir, "/assets");
}
static void modlevels(void) {
  char dir[1024], path[1100];
  assetdir(dir, sizeof dir); snprintf(path, sizeof path, "%s/levels.txt", dir);
  readlevels(path);
}

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
  sfx(S_THROW);
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
static void die(void) { if (st < TUBE) { st = DEAD; stt = 0; hvy = -900; hvx = 0; deaths++; sfx(S_DEATH); rumble(7); } }
static void pop(int x, int y, int v) {
  Pop *p = pops; for (Pop *q = pops; q < pops+12; q++) if (q->t < p->t) p = q;
  p->x = x; p->y = y; p->v = v; p->t = 45;
}
static void addscore(int v, int x, int y) { score += v; pop(x, y, v); }
static void smash(int tx, int ty) { map[ty][tx] = 0; burst(tx*8+4, ty*8+4, 0xd0602a, 6); sfx(S_BRICK); rumble(3); addscore(50, tx*8+4, ty*8); }
static Crumble *crumbleat(int r, int x, int y) {
  for (Crumble *c = wd.cr; c < wd.cr + wd.ncr; c++) if (c->room == r && c->x == x && c->y == y) return c;
  return 0;
}

// The level as the file describes it, before anything happened.
static void build(void) {
  const Level *L = LV + lvl;
  memset(&wd, 0, sizeof wd);
  for (int r = 0; r < L->nroom; r++) for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) {
    int c = L->room[r].grid[y][x];
    wd.rm[r][y][x] = tiletype(c);
    if (r == 0 && c == '@') startx = x, starty = y;
    if (r == 0 && c == 'F') gx = x, gy = y;
    if (c == 'g' || c == 'b' || c == 'h') { E *n = en + ne++; n->x = x << 11; n->y = n->h = y << 11; n->vx = -100; n->vy = 0; n->t = c == 'g' ? 1 : c == 'b' ? 2 : 3; n->a = 1; n->r = r; }
    if (c == 'C' && wd.ncr < 96) wd.cr[wd.ncr++] = (Crumble){ r, x, y };
    if (c == 'K' && wd.nck < 16) wd.ck[wd.nck++] = (Check){ r, x, y };
  }
  for (int i = 0; i < L->nhome; i++) wd.dw[i] = (Dweller){ 0, 40 + i*53 % 100, 0, 1 };   // staggered
  room = 0; lw = L->room[0].w;
  for (gb = gy*8; gb < MH*8 && !scan(gx*8+3, gb, 1, 1, SOLID); gb++);
}
// Puts Hatrick in area r at (x, y) (1/256 px) with a fresh state: level start, checkpoint or tube.
static void spawn(int r, int x, int y) {
  room = r; lw = LV[lvl].room[r].w;
  hx = x; hy = y;
  hvx = hvy = st = stt = jn = cst = lock = spin = skid = fr = gnd = jbuf = coy = wall = cut = capok = diveok = stall = cready = throwt = 0;
  duck = catcht = catchok = twirl = gpspin = rollbuf = cvy = ckind = 0;
  arcg = GRAV; runt = rundir = launch = boostt = capbuf = capkeys = capextend = capreflect = 0;
  ledget = climbx = climby = slopedir = poundt = 0;
  loads++;
  jn = -1; face = lface = 1; landt = 99;
  cxf = hx - (W/2 - 3 << 8); cyf = hy - (H*5/8 << 8); look = 0; camgy = hy; sqv = turnt = vang = pgnd = pvy = rollph = 0;
}
static void load(void) {   // (re)start the level from the top
  build(); haveck = 0;
  spawn(0, startx << 11, (starty-1) << 11);
  coins = lcoins; score = lscore;
}
static void respawn(void) {   // after a death: from the last checkpoint touched, else from the top
  if (!haveck) { load(); return; }
  wd = saved; coins = savedcoins; score = savedscore;
  spawn(ckroom, ckx, cky);
}

// ---------- tubes ----------
// Entering slides Hatrick into the mouth for 20 frames; then he comes out of the linked mouth
// (maybe in another area) for 20 frames. He can't be hurt meanwhile.
static void tubeexit(const Tube *t, int *x, int *y, int *vx, int *vy) {   // where the way out ends, and its direction
  *vx = *vy = 0; *x = t->x*8; *y = t->y*8;
  if (t->dir == T_UP) *x = t->x*8+5, *y = t->y*8-11, *vy = -160;
  if (t->dir == T_DOWN) *x = t->x*8+5, *y = t->y*8+8, *vy = 160;
  if (t->dir == T_LEFT) *x = t->x*8-7, *y = t->y*8+5, *vx = -128;
  if (t->dir == T_RIGHT) *x = t->x*8+9, *y = t->y*8+5, *vx = 128;
}
static void entertube(int i) {
  const Tube *t = LV[lvl].tube + i;
  st = TUBE; stt = 0; tubefrom = i; tubeto = t->link;
  hvx = hvy = spin = throwt = 0; duck = 0; cst = 0; gnd = 0;
  if (t->dir == T_UP || t->dir == T_DOWN) hx = (t->x*8+5) << 8;
  sfx(S_TUBE);
}
static void tubemove(void) {
  const Level *L = LV + lvl;
  const Tube *a = L->tube + tubefrom, *b = L->tube + tubeto;
  int x, y, vx, vy;
  if (stt < 20) {   // in: the reverse of the mouth's way out
    tubeexit(a, &x, &y, &vx, &vy);
    hx -= vx; hy -= vy;
  } else {
    tubeexit(b, &x, &y, &vx, &vy);
    if (stt == 20) {   // inside the far mouth, out of sight
      if (b->room != room) {
        room = b->room; lw = L->room[room].w;
        if (room && !(wd.found >> room & 1)) wd.found |= 1 << room, addscore(2000, x+3, y-4), sfx(S_REVEAL);   // a bonus room found
      }
      hx = (x << 8) - vx*20; hy = (y << 8) - vy*20; sfx(S_TUBE);
      if (vx) face = vx > 0 ? 1 : -1;
      cxf = hx - (W/2 - 3 << 8); cyf = hy - (H*5/8 << 8); camgy = hy;   // cut to the new place
    }
    hx += vx; hy += vy;
  }
  if (++stt == 40) { st = NORM; coy = 99; jn = -1; landt = 99; tubelock = tubeto+1; }   // no instant way back in
}
static int tubecheck(int k, int dir, int X, int Y, int vy0) {   // enter a tube this frame?
  const Level *L = LV + lvl;
  if (st != NORM) return 0;
  for (int i = 0; i < L->ntube; i++) {
    const Tube *t = L->tube + i;
    if (t->room != room || t->link < 0) continue;
    int c = X+3, mid = c >= t->x*8+3 && c <= t->x*8+13, at, push;
    if (t->dir == T_UP) at = gnd && Y+11 == t->y*8 && mid, push = k & 8;
    else if (t->dir == T_DOWN) at = !gnd && vy0 < 0 && Y+duck == t->y*8+8 && mid, push = k & 4;
    else if (t->dir == T_LEFT) at = gnd && X+6 == t->x*8 && Y+11 == t->y*8+16, push = dir > 0;
    else at = gnd && X == t->x*8+8 && Y+11 == t->y*8+16, push = dir < 0;
    if (tubelock == i+1) {   // just came out here: the way back in opens once Hatrick moves off or lets go
      if (!at || !push) tubelock = 0;
      continue;
    }
    if (at && push) { entertube(i); return 1; }
  }
  return 0;
}

// ---------- the course clear ----------
// Hatrick slides down the pole with the flag, poses, the time left under par is counted into the
// score, then the next level. A new jump press skips straight to the end of all of it.
static void nextlevel(void) {
  if (lvl >= NLV) load();
  else if (lvl < NLV-1) { lvl++; lcoins = coins; lscore = score; lstart = tim; load(); }
  else done = 1;
}
static void win(int pr) {
  stt++;
  if (stt > 6 && pr & 16 && !done) {
    score += tally*50; tally = 0; skipclear = 1; nextlevel(); return;
  }
  if (wphase == 0) {   // slide down with the flag
    if (!scan(hx >> 8, (hy >> 8)+1, 6, 11, SOLID)) hy += 256;
    if (flagy < gb-9) flagy++;
    if (scan(hx >> 8, (hy >> 8)+1, 6, 11, SOLID) && flagy >= gb-9) {
      wphase = 1; wt = 0; face = 1;
      if (!scan(gx*8+6, hy >> 8, 6, 11, SOLID)) hx = (gx*8+6) << 8;   // hop off beside the pole
    }
  } else if (wphase == 1) {   // pose
    if (++wt == 40) wphase = 2, wt = 0;
  } else if (wphase == 2) {   // count the time bonus in
    if (!tally) wphase = 3, wt = 0, sfx(S_BONUS);
    else if (!(++wt & 1)) {
      int n = tally > 30 ? tally/15 : 1;
      tally -= n; score += n*50; sfx(S_TICK);
    }
  } else if (wphase == 3 && ++wt == 45) nextlevel();
}
static void touchflag(int Y) {
  int top = gy*8, h = gb - top, f = h > 0 ? (gb - (Y+11)) * 100 / h : 100;
  st = WIN; stt = 0; hvx = hvy = 0; hx = gx*8-3 << 8; sfx(S_CLEAR); rumble(8);
  split = tim - lstart; wphase = wt = 0; flagy = top+1; skipclear = 0;
  tally = LV[lvl].par*60 > split ? (LV[lvl].par*60 - split) / 60 : 0;   // whole seconds under par
  addscore(f >= 90 ? 5000 : f >= 65 ? 2000 : f >= 40 ? 800 : f >= 20 ? 400 : 100, gx*8+8, Y);
}

static void hero(int k, int pr) {
  int axis = moveaxis(k), dir = axis > 0 ? 1 : axis < 0 ? -1 : 0;
  int target = iabs(axis)*MAXV/256, D = k >> 3 & 1, U = k >> 2 & 1, g = GRAV, X, Y;
  int takeoff = 0, rollcancel = st == ROLL && !D;
  int cappress = pr & 32, downthrow = D && (pr & CAP2) && !(prevk & 32);
  if (st == DEAD) { hvy += GRAV; hy += hvy; if (++stt > 60) respawn(); return; }
  if (st == WIN) { win(pr); return; }
  if (st == TUBE) { tubemove(); return; }
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
  if (gnd && pr & 4 && !(pr & 16) && (st == NORM || st == GSPIN)) { if (st == NORM) sfx(S_SPIN); st = GSPIN, stt = 0; }
  if (!gnd && launch && pr & 8 && !(k & 32) && (dir || runt)) {
    if (dir) face = dir; else face = rundir;
    longjump(); takeoff = 1; sfx(S_LONGJ);
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
      cut = 0; gnd = 0; coy = 99; jn = -1; launch = 0; poundt = 0; takeoff = 1; sfx(S_JUMP3);
    } else if (stt >= 30 || (stt >= 24 && dir)) st = NORM;
  } else if (st == ROLL) {
    posture(5);
    if (D && cappress && !boostt) {
      boostt = 15; int speed = iabs(hvx)+ROLLBOOST;
      hvx = face*(speed > ROLLMAX ? ROLLMAX : speed); sfx(S_ROLL);
    }
    if (gnd) hvx = D ? hvx*998/1000 : brake(hvx, FRIC);
    if (jbuf && coy < 6) {
      jbuf = 0;
      if (rollcancel && cappress) { st = NORM; hvy = -870; posture(0); gnd = 0; coy = 99; cut = 1; jn = 0; launch = 6; }
      else longjump();
      takeoff = 1; sfx(rollcancel && cappress ? S_JUMP : S_LONGJ);
    } else if (!D) st = NORM, posture(0);
  } else if (st == GSPIN) {
    posture(0); hvx = hvx*95/100;
    target = iabs(hvx) > MAXV*8/14 ? iabs(hvx) : MAXV*8/14;
    if (dir) { hvx += axis*AACC/256; if (iabs(hvx) > target) hvx = (hvx > 0 ? 1 : -1)*target; face = dir; }
    if (jbuf && coy < 6) { jbuf = 0; st = SPINJ; hvy = -560; cut = launch = 0; gnd = 0; coy = 99; jn = -1; takeoff = 1; sfx(S_SPIN); }
    else if (++stt >= 90 || D || !gnd) st = NORM;
  } else if (st == SLIDE) {
    posture(5);
    if (gnd && D) roll(iabs(hvx) > ROLLSTART ? iabs(hvx) : ROLLSTART);
    else {
      hvx = brake(hvx, FRIC);
      if (iabs(hvx) < 100) st = NORM, hvx = 0, posture(0);
      else if (gnd && dir) { st = NORM; posture(0); } // directional input regains ground control
      if (jbuf && gnd) { jbuf = 0; st = NORM; hvy = -760; posture(0); cut = 0; gnd = 0; coy = 99; sfx(S_JUMP); }
    }
  } else if (st != DIVE) {
    posture(gnd && D ? 4 : 0);
    if (gnd) {
      st = NORM; gpspin = 0;
      if (D) target = iabs(axis)*128/256;
      if (dir) {
        if (dir*hvx < 0) { if (!D && dir*hvx < -150) { if (!skid) sfx(S_SKID); skid = 8; } hvx += dir*FRIC; }
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
      int js = S_JUMP;
      jbuf = 0; coy = 99; cut = 1; spin = 0; gnd = 0; posture(0); takeoff = 1; launch = 6;
      if (poundt && !D && !U) { hvy = -GPJUMP_V; cut = 0; poundt = 0; jn = -1; SPIN(40, face); js = S_JUMP3; }
      else if (U) { st = SPINJ; hvy = -560; jn = -1; cut = 0; launch = 0; js = S_SPIN; }
      else if (D && (dir || runt || iabs(hvx) > 150)) { if (dir) face = dir; else if (runt) face = rundir; longjump(); js = S_LONGJ; }
      else if (D) { hvy = -1060; arcg = 32; hvx = -face*200; SPIN(40, -face); jn = -1; cut = launch = 0; js = S_FLIP; }
      else if (skid && dir) { face = dir; hvx = dir*260; hvy = -1020; arcg = 32; SPIN(40, dir); jn = -1; cut = launch = 0; js = S_FLIP; }
      else if (catcht) { hvy = -900; arcg = 42; catcht = 0; jn = -1; js = S_JUMP2; }
      else {
        // A stationary double is valid; only the third jump needs forward speed.
        jn = landt <= 10 && jn >= 0 && jn < 2 && (jn == 0 || face*hvx > 200) ? jn+1 : 0;
        hvy = jn == 2 ? -TRIPLE_V : jn ? -1030 : -870;
        if (jn == 2) { SPIN(44, face); arcg = 32; cut = launch = 0; }
        js = jn == 2 ? S_JUMP3 : jn ? S_JUMP2 : S_JUMP;
      }
      sfx(js);
    } else if (jbuf && wall && !gnd) {
      jbuf = 0; hvx = -wall*440; hvy = -900; face = -wall; lock = 7; cut = 1; jn = -1; spin = 0;
      st = NORM; arcg = GRAV; launch = 0; posture(0); capok = diveok = stall = catchok = 1; throwt = twirl = 0; sfx(S_WALLJ); rumble(1);
    } else if (jbuf && catcht && catchok && !gnd) {
      jbuf = catcht = catchok = 0; st = NORM; spin = cut = 0;
      hvy = -320; arcg = 26; throwt = 0; twirl = 10; stall = 1; launch = 0; sfx(S_SPIN);
    } else if (!takeoff && pr & 8 && !gnd && !(k & 32)) {
      gpspin = st == SPINJ; st = GPWIND; stt = 0; throwt = twirl = 0; SPIN(14, face); sfx(S_GPSPIN);
    }
  }
  if (cappress || (pr & 8 && k & 32)) {
    if (gnd && D && (st == NORM || st == SLIDE)) {
      roll(ROLLSTART); sfx(S_ROLL);
    } else if (D && !downthrow && diveok && !gnd && !(st == GPSLAM && scan(hx>>8, (hy>>8)+11, 6, 7, SOLID)) && (freemove() || st == GPWIND || st == GPSLAM)) {
      st = DIVE; posture(0);
      if (face*hvx < 760) hvx = face*760;
      if (hvy > -420) hvy = -420;
      g = arcg = GRAV; launch = capbuf = 0; diveok = 0; spin = throwt = twirl = cut = 0; sfx(S_DIVE);
    } else if (cappress && cst && cst < 3 && (!D || downthrow)) {
      if (pr & CAP2) cst = 3; // the second throw button explicitly recalls the cap
      else if (!capextend && ckind != CAPSPIN) {
        // One append throw per flight. Aim at the nearest enemy in front, or extend forward.
        int dx = cvy ? 0 : face*1100, dy = cvy ? (cvy > 0 ? 1100 : -1100) : 0, best = 64*64;
        for (E *e = en; e < en+ne; e++) if (e->a && e->r == room) {
          int ex = (e->x-cxp)>>8, ey = (e->y-cyp)>>8, d = ex*ex+ey*ey;
          if (d < best && ex*face >= -4) { best = d; int scale = iabs(ex)>iabs(ey) ? iabs(ex) : iabs(ey); if (scale) dx = ex*1100/scale, dy = ey*1100/scale; }
        }
        cvx = dx; cvy = dy; cst = 1; ct = 0; capextend = 1; sfx(S_THROW);
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
  int was = gnd, vy0 = hvy, top0 = Y+duck; gnd = 0;
  hy += hvy; Y = hy >> 8;
  if (vy0 < 0) for (int tx = X >> 3, ty = (Y+duck) >> 3; tx <= (X+5) >> 3; tx++)   // hidden blocks: only a bump from below finds them
    if (tile(tx, ty) == 14 && top0 >= ty*8+8) {
      map[ty][tx] = 15; coins++; bumpx = tx; bumpy = ty; bumpt = 10; pop(tx*8, ty*8-8, 0);
      addscore(1100, tx*8+4, ty*8-4); sfx(S_REVEAL); sfx(S_COIN);
    }
  if (st == GPSLAM) while (scan(X, Y+duck, 6, 12-duck, 4) == 2) smash(htx, hty);
  if (hvy < 0) while (scan(X, Y+duck, 6, 11-duck, 4) == 2) smash(htx, hty), hvy = 0;
  if (scan(X, Y+duck, 6, 11-duck, SOLID)) {
    int s = hvy > 0 ? -1 : 1;
    do Y += s; while (scan(X, Y+duck, 6, 11-duck, SOLID));
    hy = Y << 8;
    if (hvy > 0) gnd = 1;
    hvy = 0;
  } else if (hvy >= 0 && scan(X, Y+11, 6, 1, SOLID)) gnd = 1, hy = Y << 8, hvy = 0;
  ride = 0;
  if (!gnd && hvy >= 0)   // riding a falling crumble block
    for (Crumble *c = wd.cr; c < wd.cr + wd.ncr; c++)
      if (c->room == room && c->state == 1 && X+6 > c->x*8 && X < c->x*8+8 && Y+11 >= c->fy >> 8 && Y+11 <= (c->fy >> 8) + 6 + (c->vy >> 8)) {
        hy = c->fy - (11 << 8); Y = hy >> 8; gnd = 1; hvy = 0; ride = c - wd.cr + 1; break;
      }
  slopedir = 0;
  if (gnd) {
    for (int tx = X >> 3; tx <= (X+5) >> 3; tx++) {
      Crumble *c = tile(tx, (Y+12) >> 3) == 13 ? crumbleat(room, tx, (Y+12) >> 3) : 0;
      if (c && !c->state) c->stood = 1;   // objects() shakes it next frame
    }
    int slope; floorat(X, Y+11, &slope); slopedir = slope; launch = 0;
    capok = diveok = stall = catchok = 1; throwt = twirl = 0;
    if (!was) {
      landt = 0;
      if (st == GPSLAM) {
        st = GPLAND; stt = 0; poundt = 31; shake = 8; sfx(S_GPLAND); rumble(6);
        if (rollbuf) roll(gpspin ? MAXV*30/14 : ROLLSTART);
      } else if ((st == DIVE || st == LONGJ) && D) roll(iabs(hvx) > ROLLSTART ? iabs(hvx) : ROLLSTART);
      else if (st == DIVE) st = SLIDE, posture(5);
      else if (st == LONGJ || st == SPINJ) st = NORM;
      if (vy0 > 900 && st != GPLAND) rumble(2), sfx(S_LAND);
    }
    if (scan(X, Y+11, 6, 1, 32)) {
      hvy = -1500; gnd = 0; st = NORM; arcg = GRAV; launch = 0; posture(0); coy = 99; jbuf = 0;
      SPIN(30, face); cut = 0; sfx(S_SPRING); rumble(5);
    }
  }
  wall = 0;
  if (!gnd) wall = scan(X+6, Y+duck+1, 1, 9-duck, SOLID) ? 1 : scan(X-1, Y+duck+1, 1, 9-duck, SOLID) ? -1 : 0;
  if (!gnd && !ledget && wall && dir == wall && hvy >= 0 && st == NORM && !duck) {
    int side = wall > 0 ? X+6 : X-1, tx = side>>3;
    for (int ty = (Y-2)>>3; ty <= (Y+6)>>3; ty++) {
      int t = tile(tx, ty), top = ty*8, edge = wall > 0 ? tx*8 : (tx+1)*8;
      // A hanging body sits below the lip. Keep two tiles on its side clear so
      // snapping into HANG cannot put its feet inside the lower step of stairs.
      int airx = wall > 0 ? edge-8 : edge;
      if ((t == 1 || t == 2 || t == 4) && top >= Y-2 && top <= Y+6 &&
          !scan(side, top-11, 1, 11, SOLID) && !scan(airx, top, 8, 16, SOLID)) {
        face = wall; hx = (wall > 0 ? edge-6 : edge)*256; hy = (top+2)*256;
        climbx = (wall > 0 ? edge+1 : edge-7)*256; climby = (top-11)*256;
        st = HANG; hvx = hvy = spin = throwt = launch = 0; jn = -1; sfx(S_LEDGE); break;
      }
    }
  }
  while (scan(X, Y+duck, 6, 11-duck, 64)) { map[hty][htx] = 0; coins++; sfx(S_COIN); addscore(100, htx*8+4, hty*8); }
  if (scan(X-1, Y+duck+2, 8, 8-duck, 8) || scan(X+1, Y+duck-1, 4, 13-duck, 8) || Y > MH*8+8) die();
  if (room == 0 && X+6 > gx*8+2 && X < gx*8+6 && Y < gb && st < TUBE) touchflag(Y);
  else if (st < TUBE && !lock) tubecheck(k, dir, X, Y, vy0);
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
    if (iabs(dx) < 1024 && iabs(dy) < 1024) cst = 0, catcht = 10, sfx(S_CATCH);
  }
  if (cst && cst < 3) while (scan(cxp >> 8, cyp >> 8, 8, 4, 64)) { map[hty][htx] = 0; coins++; sfx(S_COIN); addscore(100, htx*8+4, hty*8); }
  int touching = ov(hx >> 8, (hy >> 8)+duck, 6, 11-duck, cxp >> 8, cyp >> 8, 8, 5);
  if (!touching) cready = 1;
  int landing = hvy >= 0 && oldhy + (11 << 8) <= cyp + 256 && hy + (11 << 8) >= cyp;
  int diving = st == DIVE && hy < cyp + (2 << 8);
  int vault = gnd && st == NORM && cst == 2 && k & 32;
  if (ckind != CAPSPIN && cst && cst < 3 && cready && (capok || vault) && (freemove() || st == DIVE) && touching && (vault || (!gnd && (landing || diving)))) {
    if (!gnd) capok = 0;
    else if (face*hvx < 700) hvx = face*700;
    diveok = stall = 1; hvy = vault ? -CAPVAULT_V : -CAPBOUNCE_V; arcg = 32; launch = 0; cut = 0; st = NORM; posture(0); gnd = 0; jn = -1; spin = throwt = twirl = 0; coy = 99; cst = 3;
    burst((cxp >> 8)+4, cyp >> 8, 0xffffff, 5); sfx(S_BOUNCE); rumble(1);
  }
}

static void kill(E *e) { e->a = 0; burst((e->x >> 8)+4, (e->y >> 8)+4, 0x9a48d0, 8); sfx(S_STOMP); rumble(4); addscore(200, (e->x >> 8)+4, e->y >> 8); }

static void enemies(int k) {
  int X = hx >> 8, Y = hy >> 8;
  for (E *e = en; e < en+ne; e++) {
    if (!e->a || e->r != room) continue;
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
    if (st < TUBE && ov(X, Y+duck, 6, 11-duck, ex+1, ey+1, 6, 7)) {
      if ((hvy > 0 || st == GPSLAM) && Y+11 < ey+6) {
        kill(e); hvy = k & 16 ? -1000 : -650; st = NORM; arcg = GRAV; launch = 0; cut = 0; capok = diveok = stall = 1; spin = throwt = 0;
      } else die();
    }
    if (e->a && cst && cst < 3 && ov(cxp >> 8, cyp >> 8, 8, 5, ex, ey, 8, 8)) kill(e);
  }
}

// Crumble blocks: shake while stood on, fall after half a second (carrying whoever stands on
// them), and come back in place a few seconds later. Runs before hero() so a ride moves along.
static void objects(void) {
  for (Crumble *c = wd.cr; c < wd.cr + wd.ncr; c++) {
    if (c->state == 0) {
      if (c->stood) { if (!c->t) sfx(S_CRUMBLE); if (++c->t >= 30) c->state = 1, c->fy = c->y*8 << 8, c->vy = 0, wd.rm[c->room][c->y][c->x] = 0; }
      else if (c->t) c->t--;
      c->stood = 0;
    } else if (c->state == 1) {
      c->vy = c->vy + 20 > 640 ? 640 : c->vy + 20; c->fy += c->vy;
      if (ride == c - wd.cr + 1 && room == c->room) hy += c->vy;
      if (c->fy >> 8 > MH*8+16) c->state = 2, c->t = 150;
    } else if (--c->t <= 0) {
      if (c->room == room && ov(hx >> 8, hy >> 8, 6, 11, c->x*8, c->y*8, 8, 8)) c->t = 10;   // not into Hatrick
      else { c->state = c->t = 0; wd.rm[c->room][c->y][c->x] = 13; if (c->room == room) burst(c->x*8+4, c->y*8+4, 0xf0d098, 4); }
    }
  }
}
// The tube a dweller lives in: the opening's x centre and the y where it comes out (px).
static void dwellerspot(const Home *h, int *cx, int *my, int *up) {
  const Tube *t = LV[lvl].tube + h->tube;
  *cx = t->x*8+8; *up = t->dir == T_UP; *my = *up ? t->y*8 : t->y*8+8;
}
// Its visible body: 6 px wide, ofs px out of the tube.
static int dwellerhit(const Dweller *d, const Home *h, int x, int y, int w, int hh) {
  int cx, my, up; dwellerspot(h, &cx, &my, &up);
  return d->ofs > 2 && ov(x, y, w, hh, cx-3, up ? my-d->ofs+1 : my, 6, d->ofs-1);
}
// Checkpoints, fire bars, tube dwellers and their seeds. Runs after hero() and the cap.
static void hazards(void) {
  const Level *L = LV + lvl;
  int X = hx >> 8, Y = (hy >> 8) + duck, hh = 11 - duck, alive = st < TUBE, capon = cst && cst < 3;
  for (Check *c = wd.ck; c < wd.ck + wd.nck; c++) {
    if (c->up && c->up < 20) c->up++;
    if (c->room == room && alive && !c->up && ov(X, Y, 6, hh, c->x*8+1, c->y*8-8, 6, 16)) {
      haveck = 1; ckroom = room; ckx = (c->x*8+1) << 8; cky = (c->y*8-3) << 8;
      c->up = 20; saved = wd; savedcoins = coins; savedscore = score;   // the respawn snapshot has it raised
      c->up = 1; sfx(S_CHECK);
    }
  }
  for (const Bar *b = L->bar; b < L->bar + L->nbar; b++) {
    if (b->room != room || !alive) continue;
    int a = (b->a0*256 + b->speed*fr) >> 8 & 255;
    for (int i = 1; i < b->len; i++) {
      int fx = b->x*8+4 + SIN[(a+64) & 255]*i*8/256, fy = b->y*8+4 + SIN[a]*i*8/256;
      if (ov(X, Y, 6, hh, fx-2, fy-2, 4, 4)) die();
    }
  }
  for (int i = 0; i < L->nhome; i++) {
    Dweller *d = wd.dw + i; const Home *h = L->home + i;
    if (!d->a) continue;
    int cx, my, up; dwellerspot(h, &cx, &my, &up);
    int here = h->room == room, used = st == TUBE && (tubefrom == h->tube || tubeto == h->tube);
    int near = here && iabs(X+3 - cx) < 22 && (up ? Y+hh >= my-40 && Y <= my+16 : Y <= my+40 && Y+hh >= my-16);
    if (used) d->phase = 0, d->ofs = 0, d->t = d->t < 60 ? 60 : d->t;
    else if (d->phase == 0) {
      if (--d->t <= 0) { if (near) d->t = 8; else { d->phase = 1; if (here && iabs(X - cx) < 200) sfx(S_EMERGE); } }
    }
    else if (d->phase == 1) { if (++d->ofs >= 16) d->phase = 2, d->t = 70; }
    else if (d->phase == 2) {
      if (h->spit && here && (d->t == 50 || d->t == 22))   // a seed lobbed toward Hatrick
        for (Shot *p = wd.sh; p < wd.sh+8; p++) if (!p->a) {
          int dx = (X+3 - cx)*256/80;
          *p = (Shot){ room, cx << 8, (up ? my-14 : my+14) << 8, dx > 300 ? 300 : dx < -300 ? -300 : dx, up ? -560 : 0, 1 };
          sfx(S_SPIT); break;
        }
      if (--d->t <= 0) d->phase = 3;
    } else if (--d->ofs <= 0) d->ofs = 0, d->phase = 0, d->t = 100;
    if (here && alive && dwellerhit(d, h, X, Y, 6, hh)) die();   // no stomping these
    if (here && capon && dwellerhit(d, h, cxp >> 8, cyp >> 8, 8, 5)) {
      d->a = 0; burst(cx, up ? my-d->ofs/2 : my+d->ofs/2, h->spit ? 0xe0586a : 0x2f8f9a, 10); sfx(S_STOMP); rumble(4);
      addscore(500, cx, up ? my-d->ofs : my);
    }
  }
  for (Shot *p = wd.sh; p < wd.sh+8; p++) {
    if (!p->a) continue;
    p->vy += 14; p->x += p->vx; p->y += p->vy;
    int sx = p->x >> 8, sy = p->y >> 8;
    if (p->room != room || sy > MH*8+16 || (SOLID >> tile(sx >> 3, sy >> 3) & 1)) { p->a = 0; continue; }
    if (alive && ov(X, Y, 6, hh, sx-2, sy-2, 4, 4)) die();
    if (capon && ov(cxp >> 8, cyp >> 8, 8, 5, sx-2, sy-2, 4, 4)) p->a = 0, burst(sx, sy, 0x9a6a3a, 4), addscore(50, sx, sy);
  }
}

// ---------- high scores: the top ten with initials, in ~/.hatrick_scores ("ABC 12345" lines) ----------
// HATRICK_SCORES names another file; the simulator and tests only ever use that.
typedef struct { char ini[4]; int score; } Hi;
static Hi hi[10];
static int nhi, hinew = -1, naming, namepos, scoreview;
static char initials[4] = "AAA";
static const char *hipath(void) {
  static char p[1100];
  const char *e = getenv("HATRICK_SCORES"), *home = getenv("HOME");
  if (e && *e) return e;
#ifdef SIM
  return 0;
#endif
  if (!home) return 0;
  snprintf(p, sizeof p, "%s/.hatrick_scores", home);
  return p;
}
static void hiload(void) {
  const char *p = hipath(); FILE *f = p ? fopen(p, "r") : 0;
  char ini[16]; int v;
  nhi = 0;
  if (!f) return;
  while (nhi < 10 && fscanf(f, "%15s %d", ini, &v) == 2) {
    if (strlen(ini) != 3 || v <= 0) continue;
    int i = nhi++;
    while (i > 0 && hi[i-1].score < v) hi[i] = hi[i-1], i--;   // keep it sorted, best first
    memcpy(hi[i].ini, ini, 4); hi[i].score = v;
  }
  fclose(f);
}
static void hisave(void) {
  const char *p = hipath(); FILE *f = p ? fopen(p, "w") : 0;
  if (!f) { if (p) fprintf(stderr, "hatrick: cannot save the high scores to %s\n", p); return; }
  for (int i = 0; i < nhi; i++) fprintf(f, "%s %d\n", hi[i].ini, hi[i].score);
  fclose(f);
}
static int hiqualifies(int v) { return v > 0 && (nhi < 10 || v > hi[nhi-1].score); }
static int hiinsert(const char *ini, int v) {   // returns its place
  int i = nhi < 10 ? nhi++ : 9;
  while (i > 0 && hi[i-1].score < v) hi[i] = hi[i-1], i--;
  memcpy(hi[i].ini, ini, 4); hi[i].score = v;
  return i;
}
// Initials: Up/Down change the letter, Left/Right or Jump/Cap move between the three, the last
// Jump (or Start) saves; then the table shows with the new entry, and the title menu follows.
static void nametick(int k, int pr) {
  int axis = moveaxis(k), nav = k & 12 ? (k & 4 ? 1 : -1) : 0, side = axis > 128 ? 1 : axis < -128 ? -1 : 0;
  int key = nav ? nav*2 : side;
  menufr++;
  if (key && (key != menunav || --menurepeat <= 0)) {
    if (nav) initials[namepos] = 'A' + (initials[namepos] - 'A' + 26 + nav) % 26;
    else namepos = namepos + side < 0 ? 0 : namepos + side > 2 ? 2 : namepos + side;
    menurepeat = key != menunav ? 18 : 5; sfx(S_MENUMOVE);
  }
  menunav = key;
  if (pr & 32 && namepos) namepos--, sfx(S_MENUBACK);
  else if ((pr & 16 && namepos < 2)) namepos++, sfx(S_MENUMOVE);
  else if (pr & (16|START)) {
    hiload(); hinew = hiinsert(initials, score); hisave();
    naming = 0; menu = 1; resumable = 0; scoreview = 1; menusel = MENUN-1; menufr = 0;
    lvl = deaths = lcoins = score = lscore = lstart = tim = done = donet = 0; load();
    sfx(S_MENUOK);
  }
}

static void destination(int choice) {
  if (choice == MENUN-1) { scoreview = 1; hinew = -1; hiload(); menufr = 0; sfx(S_MENUOK); return; }   // the high-score card
  lvl = choice == NLV ? PLAY : choice;
  deaths = coins = lcoins = score = lscore = lstart = tim = done = donet = shake = rumq = 0;
  for (P *p = pt; p < pt+NP; p++) p->l = 0;
  menu = 0; resumable = 1; load(); sfx(S_MENUOK);
}
static void openmenu(int k) {
  menu = 1; menusel = lvl >= NLV ? NLV : lvl; sfx(S_PAUSE);
  menunav = 0; menurepeat = 0; menufr = 0;
  // Don't move the selection just because movement was held when pausing.
  int axis = moveaxis(k);
  menunav = k & 12 ? (k & 8 ? 3 : -3) : axis > 128 ? 1 : axis < -128 ? -1 : 0;
  menurepeat = 18;
}
static void menutick(int k, int pr) {
  menufr++;
  if (scoreview) {   // the table: any button goes back to the cards
    if (pr & (16|32|START|BACK|MENUBACK)) scoreview = 0, hinew = -1, sfx(S_MENUBACK);
    return;
  }
  if (pr & BACK) { if (resumable) menu = 0, sfx(S_MENUBACK); else quitting = 1; return; }
  if (pr & MENUBACK) { if (resumable) menu = 0, sfx(S_MENUBACK); return; }
  if (pr & 32 && resumable) { menu = 0; sfx(S_MENUBACK); return; }
  if (pr & (16|START)) { destination(menusel); return; }
  if (pr & PRACTICE && PLAY >= 0) { destination(NLV); return; }
  int axis = moveaxis(k);
  int nav = k & 12 ? (k & 8 ? 3 : -3) : axis > 128 ? 1 : axis < -128 ? -1 : 0;
  if (nav && (nav != menunav || --menurepeat <= 0)) {
    menusel = (menusel+nav+MENUN) % MENUN; sfx(S_MENUMOVE);
    menurepeat = nav != menunav ? 18 : 6;
  }
  menunav = nav;
}
static void tick(int k) {
  int pr = k & ~prevk;
  prevk = k;
  if (k & CAP2) k |= 32;
  if (pr & CAP2) pr |= 32;   // pressing the other face button is a real new action
  if (pr & 128) muted ^= 1;
  if (pr & QUIT) { quitting = 1; return; }
  if (naming) { nametick(k, pr); return; }
  if (menu) { menutick(k, pr); return; }
  if (pr & (BACK|START)) { openmenu(k); return; }
  if (pr & PRACTICE && PLAY >= 0) { lvl = lvl >= NLV ? 0 : PLAY; done = lcoins = 0; load(); return; }
  if (pr & 64) { if (done) lvl = deaths = lcoins = score = lscore = lstart = tim = done = donet = 0; load(); return; }
  fr++;
  if (!done) tim++;
  else if (++donet == 150 && lvl < NLV) { hiload(); if (hiqualifies(score)) naming = 1, namepos = 0, menunav = 0, menufr = 0, sfx(S_BONUS); }
  if (shake) shake--;
  oldhy = hy;
  objects();
  hero(k, pr);
  capupd(k);
  enemies(k);
  hazards();
  for (Pop *p = pops; p < pops+12; p++) if (p->t) p->t--;
  if (bumpt) bumpt--;
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
static void menupreview(int card,int x,int y) {
  u32 sky=card==CARD_BRICKS?0x7da8d3:card==CARD_SKY?0x546cbe:0x6cb9e7;
  for(int row=0;row<45;row++)mrect(x,y+row,186,1,mixcolor(sky,0xbde6ee,row,70));
  mcloud(x+12,y+4,43);mcloud(x+112,y+2,53);
  for(int u=0;u<186;u++) {
    int a=(u+21)%120-60,h=21+a*a/180;
    if(h<45)mrect(x+u,y+h,1,45-h,0x8dc983);
    int b=(u+70)%110-55,near=30+b*b/190;
    if(near<45)mrect(x+u,y+near,1,45-near,0x5aaa66);
  }
  if(card==CARD_SKY) {
    for(int i=0;i<3;i++) {
      int xx=x+23+i*52,yy=y+32-i*9;
      mround(xx,yy,42,8,4,0xaccde9);mround(xx,yy-2,42,7,3,0xffffed);
      mrect(xx+8,yy-3,26,1,0xffffff);
    }
    for(int i=0;i<4;i++){int xx=x+74+i*25,yy=y+7+(i&1)*5;mline(xx-2,yy,xx+2,yy,0xfff1bb);mline(xx,yy-2,xx,yy+2,0xfff1bb);}
  } else {
    mrect(x,y+36,186,9,0x9b623f);mrect(x,y+33,186,3,0x327d4f);mrect(x,y+32,186,1,0xb9e381);
    for(int u=0;u<186;u+=7)mrect(x+u,y+36+(u%3),2,1,0xc2864e);
    if(card==CARD_BRICKS)for(int i=0;i<5;i++) {
      int xx=x+50+i*23,yy=y+27-(i&1)*8;
      mrect(xx,yy,21,11,0x9f4e39);mrect(xx+1,yy+1,19,9,0xda8d50);
      mrect(xx+2,yy+1,17,1,0xffcd83);mrect(xx+10,yy+2,1,7,0xae603d);
    }
    if(card==CARD_SPIKES)for(int i=0;i<6;i++) {
      int xx=x+59+i*14;
      for(int v=0;v<9;v++){mrect(xx-v/2,y+25+v,v+1,1,0x4c6383);mrect(xx-v/2,y+25+v,v/2+1,1,0xe8f4f3);}
    }
    if(card==CARD_CASTLE) {
      mrect(x+109,y+9,50,25,0x637e9d);mrect(x+111,y+10,46,24,0xb9c7cd);
      for(int row=0;row<3;row++)for(int i=0;i<4;i++)mrect(x+112+i*12+(row&1)*5,y+13+row*8,9,1,0x90a5b6);
      for(int i=0;i<4;i++){mrect(x+108+i*14,y+4,9,7,0x6a849e);mrect(x+109+i*14,y+4,7,5,0xdde6dc);}
      mround(x+129,y+22,12,15,5,0x405c7a);mrect(x+134,y+27,1,8,0x1f3e61);
      mline(x+142,y+4,x+142,y-1,0xffefc5);mrect(x+143,y-1,9,3,0xe56c59);
    }
    if(card==CARD_PLAYGROUND) {
      for(int u=0;u<63;u++) {int h=u<33?u/3:(62-u)/3;mrect(x+89+u,y+32-h,1,h+1,0x368950);mrect(x+89+u,y+31-h,1,1,0xb5e687);}
      mrect(x+148,y+24,24,9,0x829daf);mrect(x+148,y+23,24,2,0xc7d8d4);
    }
    if(card==NCARD) {   // the high-score card: a trophy on a little podium
      mrect(x+70,y+26,46,7,0x8a5c1c);mrect(x+72,y+26,42,2,0xf0c860);
      mround(x+80,y+3,26,16,7,0xc8902e);mround(x+82,y+4,22,13,6,0xf0c860);mrect(x+86,y+6,4,8,0xfff0b0);
      mrect(x+90,y+18,6,5,0xc8902e);mrect(x+85,y+22,16,4,0xa87424);
      mellipse(x+79,y+9,3,4,0xa87424);mellipse(x+107,y+9,3,4,0xa87424);
      for(int i=0;i<3;i++){int xx=x+30+i*55,yy=y+10+(i&1)*6;mline(xx-2,yy,xx+2,yy,0xfff1bb);mline(xx,yy-2,xx,yy+2,0xfff1bb);}
    }
    if(card==CARD_HILLS) {
      mline(x+139,y+15,x+139,y+33,0x466983);mline(x+140,y+15,x+140,y+33,0xfff5c7);
      for(int u=0;u<16;u++)mrect(x+141+u,y+15,1,7-u/3,0xe76d5b);
      for(int i=0;i<3;i++){int xx=x+64+i*14;mline(xx,y+29,xx,y+32,0x378552);mellipse(xx,y+27,2,2,0xffec9f);mrect(xx,y+27,1,1,0xe99554);}
    }
  }
}
static void menudais(void) {
  // Tiny dais decorations. No control hints are drawn on the menu.
  for(int side=0;side<2;side++) {
    int x=side?710:58;
    mline(x,403,x,425,0x66a16c);mellipse(x-3,414,4,2,0x83bc83);
    mellipse(x,400,5,5,0xf3c4b7);mellipse(x,400,2,2,0xffe3a4);
  }
  sprx(naming?HPOSE:HSTAND,12,10*256,142*256,0,0,256,256,HPAL);
  sprx(GRUM1,8,246*256,142*256,1,0,256,256,GPAL);
}
// The high-score board on the stage: the table, or the initials being entered.
static void menuscores(void) {
  char line[32];
  const char *title=naming?"NEW HIGH SCORE":"HIGH SCORES";
  centered(title,155,1,0xdcecf0);centered(title,153,1,0x2b5075);
  mround(150,180,468,186,8,0x4c5871);mround(147,176,468,186,8,0xe8e6d2);mround(150,179,462,180,6,0x2c4162);
  if(naming) {
    snprintf(line,sizeof line,"%d",score);
    centered(line,206,2,0xffd894);
    for(int i=0;i<3;i++) {
      int x=384-75+i*57,y=270,active=i==namepos,bob=active?SIN[(menufr*6)&255]*3/256:0;
      char c[2]={initials[i],0};
      mround(x-6,y-8,48,58,6,active?0xffdb87:0x46607e);mround(x-4,y-6,44,54,5,active?0xeaaa5d:0x34506e);
      menutext(c,x+3,y+2+bob,2,active?0xfff3d1:0xc8d4dc);
      if(active)for(int j=0;j<6;j++){mrect(x+18-j,y-16+j,2*j+1,1,0xffd894);mrect(x+18-j,y+62-j,2*j+1,1,0xffd894);}
    }
    return;
  }
  if(!nhi){centered("NO SCORES YET",260,1,0xc8d4dc);return;}
  for(int i=0;i<nhi;i++) {
    int y=190+i*17; u32 c=i==hinew?0xffd894:0xf2efe0;
    if(i==hinew)mrect(170,y-2,428,16,0x46607e);
    snprintf(line,sizeof line,"%d.",i+1);menutext(line,200+(i<9?18:0),y,1,c);
    menutext(hi[i].ini,270,y,1,c);
    snprintf(line,sizeof line,"%d",hi[i].score);menutext(line,570-textwidth(line,1),y,1,c);
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
  if(naming||scoreview) {menuscores();menudais();return;}
  centered("CHOOSE YOUR ADVENTURE",155,1,0xdcecf0);centered("CHOOSE YOUR ADVENTURE",153,1,0x2b5075);
  int page=menusel/6*6;
  for(int i=0;i<6 && page+i<MENUN;i++) {
    int choice=page+i,x=66+i%3*219,y=186+i/3*96,active=choice==menusel;
    mround(x+3,y+5,198,81,5,0x6684a0);
    mround(x,y,198,81,5,active?0xad7244:0x56779a);
    mround(x,y-1,198,79,5,active?0xffdb87:0xe8e6d2);
    mround(x+2,y+1,194,75,3,active?0xeaaa5d:0x658baa);
    menupreview(choice==MENUN-1?NCARD:LV[choice==NLV?PLAY:choice].card,x+6,y+5);
    mrect(x+6,y+50,186,1,active?0xba884f:0x7694a4);
    for(int row=0;row<24;row++)mrect(x+6,y+51+row,186,1,mixcolor(active?0xffedb2:0xfff5dc,active?0xf5d388:0xe7e8d7,row,32));
    const char *name=choice==MENUN-1?"HIGH SCORES":choice==NLV?"PLAYGROUND":LV[choice].name;
    char number[12];snprintf(number,sizeof number,"%d",choice+1);
    int width=textwidth(name,1);
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
  menudais();
}

// Brass tubes: shading across a 16 px wide tube (a = 0..15), and the bands around it every 16 px.
static u32 brass(int a, int along) {
  if (a == 0 || a == 15) return 0x3e2a10;
  if ((along & 15) == 0) return a == 3 || a == 12 ? 0xfff0b0 : 0x8a5c1c;   // band with two rivets
  return a < 3 ? 0xf0c860 : a < 5 ? 0xfff0b0 : a < 10 ? 0xc8902e : a < 14 ? 0xa87424 : 0x8a5c1c;
}
static const Tube *tubeat(int tx, int ty) {
  const Level *L = LV + lvl;
  for (const Tube *t = L->tube; t < L->tube + L->ntube; t++)
    if (t->room == room && ((t->x == tx && t->y == ty) || (t->dir < T_LEFT ? t->x+1 == tx && t->y == ty : t->x == tx && t->y+1 == ty))) return t;
  return 0;
}
static u32 coinpx(int u, int v, int f) {   // the spinning coin, frame counter f
  int ph = f >> 3 & 3, hw = ph == 0 ? 3 : ph == 2 ? 0 : 2, rw = v == 1 || v == 6 ? hw-1 : hw, d = iabs(2*u-7);
  if (v < 1 || v > 6 || d > 2*rw+1) return 0;
  return d > 2*rw-2 ? 0xc88a18 : 0xffd84a;
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
  if (t == 6) return coinpx(u, v, fr);
  if (t == 10 || t == 11) {   // tube bodies, 2 cells across: which half is this one?
    int n = 0;
    if (t == 10) { while (tile(tx-1-n, ty) == 10) n++; int a = u + (n & 1)*8; return a == 0 || a == 15 ? 0 : brass(a < 1 ? 1 : a > 14 ? 14 : a, ty*8+v+1); }
    while (tile(tx, ty-1-n) == 11) n++;
    int a = v + (n & 1)*8; return a == 0 || a == 15 ? 0 : brass(a, tx*8+u+1);
  }
  if (t == 12) {   // a mouth: a wide collar with the dark opening on its open side
    const Tube *m = tubeat(tx, ty);
    int dir = m ? m->dir : T_UP, across, along;
    if (dir < T_LEFT) across = u + (m && tx > m->x)*8, along = dir == T_UP ? v : 7-v;
    else across = v + (m && ty > m->y)*8, along = dir == T_LEFT ? u : 7-u;
    if (along == 0 || along == 7) return 0x3e2a10;
    if (along < 3 && across > 1 && across < 14) return along == 1 ? 0x140c04 : 0x2a1a08;   // the opening
    if (along == 5 && (across == 3 || across == 12)) return 0xfff0b0;                      // rivets
    return across == 0 || across == 15 ? 0x3e2a10 : brass(across, 1);
  }
  if (t == 13) {   // crumble block: a cracked biscuit
    if (u == 0 || v == 0) return 0xf6dca0;
    if (u == 7 || v == 7) return 0x6e4420;
    if ((v == 3 && u < 5) || (u == 4 && v > 3) || (u == 2 && v < 3) || (v == 5 && u > 4)) return 0x9a6630;
    return 0xe2b56a;
  }
  if (t == 15) {   // a found hidden block
    if (u == 0 || v == 0) return 0xa6dce0;
    if (u == 7 || v == 7) return 0x2e5a66;
    if ((v == 3 || v == 4) && u > 1 && u < 6 && !(v == 4 && (u == 2 || u == 5))) return 0x3c7480;   // a little cap stamp
    return 0x5e9aa6;
  }
  if (t == 16) {   // fire bar pivot: iron with an ember
    if ((u == 3 || u == 4) && (v == 3 || v == 4)) return (fr >> 2 & 1) ? 0xffe08a : 0xffb040;
    if (u > 1 && u < 6 && v > 1 && v < 6) return 0xc8501c;
    if (u == 0 || v == 0) return 0x7a8290;
    if (u == 7 || v == 7) return 0x262a32;
    return 0x4a4f5a;
  }
  return 0;   // 14: hidden until found
}
static void drawtile(int t, int tx, int ty, int dx, int dy) {
  for (int v = 0; v < 8; v++) for (int u = 0; u < 8; u++) {
    u32 c = tilepx(t, tx, ty, u, v);
    if (c) wpx(tx*8+u+dx, ty*8+v+dy, c);
  }
}
static void wnum(int v, int x, int y, u32 c) {   // a small number at world px (score pop-ups)
  char d[12]; int n = snprintf(d, sizeof d, "%d", v);
  for (int i = 0; i < n; i++) for (int j = 0; j < 15; j++)
    if (FONT[d[i]-'0'] >> (14-j) & 1) wpx(x + i*4 + j%3 + 1, y + j/3 + 1, 0x182030), wpx(x + i*4 + j%3, y + j/3, c);
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

static void drawhero(void) {
  const u16 *f = HJUMP; int sh = 12, fl = face < 0, ang = 0, tang = 0, sx = 256, sy, bob = 0, fx = hx + (3 << 8), fy = hy + (11 << 8);
  if (face != lface) turnt = 4, lface = face;
  if (st < TUBE) {
    if (gnd && !pgnd) {                                  // landing: squash, dust on hard landings
      sqv = -(pvy > 1400 ? 1400 : pvy) / 10;
      if (pvy > 500) dust(fx, fy, -1, 3 + pvy/400), dust(fx, fy, 1, 3 + pvy/400);
    }
    if (!gnd && pgnd && hvy < -300) sqv = 56, dust(fx, fy, 0, 2);   // takeoff: stretch
  }
  pgnd = gnd; pvy = hvy;
  capoff = 0;
  if (st == DEAD) ang = 128;
  else if (st == WIN && wphase) f = wphase == 1 && wt < 8 ? HJUMP : HPOSE, fl = 0;
  else if (st == TUBE) f = HSTAND;
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
  if (st == WIN && wphase == 1 && wt >= 8) bob = -SIN[(wt*8) & 127]*3;   // a little hop of joy
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
static const u32 SNAPPAL[3] = { 0x2f8f9a, 0xf4e6c0, 0x1b2433 }, SPITPAL[3] = { 0xe0586a, 0xf4e6c0, 0x3a1424 };

static void render(void) {
  static int cm[SW], fh[SW], nh[SW];
  const Level *L = LV + lvl;
  if (menu || naming) { menurender(); return; }
  camera();
  ox = cxf * SC >> 8;
  oy = (cyf * SC >> 8) + (shake ? (shake & 2 ? 2*SC : -2*SC) : 0);
  int lo = (((MH*8-H) << 8) - cyf) * SC >> 8;     // camera height above the bottom, screen px
  if (L->room[room].cave) {   // bonus rooms: a dim cave with a few glinting stones
    for (int y = 0; y < SH; y++) {
      u32 *row = big[y], c = mixcolor(0x1c1a2c, 0x34283a, y, SH);
      for (int x = 0; x < SW; x++) {
        int gx2 = (x + ox/2) / SC, gy2 = (y + oy/2) / SC;
        row[x] = (gx2*7 + gy2*13) % 61 == 0 && (gx2 ^ gy2) % 3 == 0 ? 0x5a4c66 : c;
      }
    }
  } else {   // sky, clouds and two layers of hills, each scrolling at its own rate
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
  }
  // behind the tiles: tube dwellers and Hatrick inside a tube, so the brass hides them
  for (int i = 0; i < L->nhome; i++) {
    const Dweller *d = wd.dw + i; const Home *h = L->home + i;
    if (!d->a || h->room != room || !d->ofs) continue;
    int cx, my, upw; dwellerspot(h, &cx, &my, &upw);
    const u16 *f = d->phase == 2 && (fr >> 3 & 1) ? SNAP2 : SNAP1;
    int lean = d->phase == 2 ? SIN[(fr*4) & 255] / 64 : 0;
    if (upw) sprx(f, 16, (cx << 8) + lean*64, (my + 16 - d->ofs) << 8, 0, lean, 300, 256, h->spit ? SPITPAL : SNAPPAL);
    else sprx(f, 16, (cx << 8) + lean*64, (my - 16 + d->ofs) << 8, 0, 128 + lean, 300, 256, h->spit ? SPITPAL : SNAPPAL);
  }
  if (st == TUBE) drawhero();
  for (int ty = fdiv(oy, 8*SC); ty <= (oy+SH) / (8*SC); ty++)
    for (int tx = fdiv(ox, 8*SC); tx <= (ox+SW) / (8*SC); tx++) {
      int t = tile(tx, ty), dx = 0, dy = 0;
      if (!t) continue;
      if (t == 13) { const Crumble *c = crumbleat(room, tx, ty); if (c && c->t) dx = (fr >> 1 & 1) ? 1 : -1, dy = c->t > 20 && (fr & 2); }
      if (t == 15 && bumpt && tx == bumpx && ty == bumpy) dy = -SIN[bumpt*12 & 255] * 3 / 256;
      drawtile(t, tx, ty, dx, dy);
    }
  for (const Crumble *c = wd.cr; c < wd.cr + wd.ncr; c++)   // falling crumble blocks
    if (c->room == room && c->state == 1) for (int v = 0; v < 8; v++) for (int u = 0; u < 8; u++) wpx(c->x*8+u, (c->fy >> 8)+v, tilepx(13, c->x, c->y, u, v));
  for (const Check *c = wd.ck; c < wd.ck + wd.nck; c++) if (c->room == room) {   // checkpoints: a post and a banner that rises
    int x = c->x*8+3, base = c->y*8+8, by = base-5 - (c->up ? (c->up >= 20 ? 11 : c->up*11/20) : 0);
    for (int y = base-16; y < base; y++) wpx(x, y, 0xe8ecf0), wpx(x+1, y, 0x8a94a0);
    for (int i = -1; i < 3; i++) wpx(x+i, base-1, 0x586070);
    wpx(x, base-17, c->up ? 0xffd84a : 0xc0c8d0); wpx(x+1, base-17, c->up ? 0xc88a18 : 0x8a94a0);
    for (int j = 0; j < 4; j++) for (int i = 0; i < 5 - (j == 0 || j == 3); i++) {
      int wave = c->up ? (fr >> 3) + i/2 & 1 : 0;
      wpx(x+2+i, by+j+wave*(i > 2), c->up ? (j == 1 || j == 2) && i == 1 ? 0xffffff : 0x2fd0b4 : 0x7c8796);
    }
  }
  // goal: pole down to the ground and a pennant (sliding down with Hatrick at the end)
  int py = st == WIN ? flagy : gy*8+1;
  for (int y = 0; !scan(gx*8+3, gy*8+y, 1, 1, SOLID) && y < 256 && room == 0; y++) wpx(gx*8+3, gy*8+y, 0xd8dde4), wpx(gx*8+4, gy*8+y, 0xa0a8b4);
  if (room == 0) {
    for (int i = 0; i < 9; i++) wpx(gx*8+2+i%3, gy*8-3+i/3, 0xffd84a);
    for (int j = 0; j < 8; j++) for (int i = 0; i < 8-j; i++) wpx(gx*8+2-i, py+j/2+(j > 3 ? j-3 : 0)/2+((fr >> 3)+i/3 & 1), 0x2ec85a);
  }
  for (E *e = en; e < en+ne; e++)
    if (e->a && e->r == room) {
      int lift = hop ? SIN[hop/2] * 3 : 0, stretch = hop ? SIN[hop/2] / 6 : 0;   // hop on the "bah"
      if (e->t == 1) sprx(fr & 8 ? GRUM2 : GRUM1, 8, e->x + (4 << 8), e->y + (8 << 8) - lift, e->vx > 0, 0, 256 - stretch/2, 256 + stretch, GPAL);
      else sprx(fr & 4 ? BUZZ2 : BUZZ1, 8, e->x + (4 << 8), e->y + (8 << 8) - lift*2/3, 0, 0, 256, 256 + stretch/2, BPAL);
    }
  for (const Shot *p = wd.sh; p < wd.sh+8; p++) if (p->a && p->room == room)   // spitter seeds
    for (int j = -2; j < 2; j++) for (int i = -2; i < 2; i++) if ((i != -2 && i != 1) || (j != -2 && j != 1))
      wpx((p->x >> 8)+i, (p->y >> 8)+j, i+j == -2 ? 0xe0b070 : (i+j+(fr >> 2)) & 1 ? 0x7a4a24 : 0x5a3418);
  for (const Bar *b = L->bar; b < L->bar + L->nbar; b++) if (b->room == room) {   // fire bars: a line of embers
    int a = (b->a0*256 + b->speed*fr) >> 8 & 255;
    for (int i = 0; i < b->len; i++) {
      int fx = b->x*8+4 + SIN[(a+64) & 255]*i*8/256, fy = b->y*8+4 + SIN[a]*i*8/256, fl = (fr + i) >> 1 & 1;
      for (int j = -2; j < 2; j++) for (int k = -2; k < 2; k++) {
        int edge = (k == -2 || k == 1) + (j == -2 || j == 1);
        if (edge == 2) continue;
        wpx(fx+k, fy+j, edge ? (fl ? 0xd8401c : 0xff7a20) : (fl ? 0xfff0a0 : 0xffc040));
      }
    }
  }
  capless = 0;
  if (cst) {   // the thrown cap spins: its width follows a cosine
    int w = SIN[(fr*24 + 64) & 255];
    sprx(CAP, 4, cxp + (4 << 8), cyp + (4 << 8), w < 0, 0, iabs(w) < 48 ? 48 : iabs(w), 256, HPAL);
  }
  capless = cst != 0;
  if (st != TUBE) drawhero();
  for (const Pop *p = pops; p < pops+12; p++) if (p->t) {   // score numbers drift up; found coins hop out
    if (p->v) wnum(p->v, p->x - 6, p->y - 6 - (45 - p->t)/3, p->t > 10 || (p->t & 1) ? 0xffffff : 0xffd84a);
    else if (p->t > 25) { int h = (45 - p->t)*(p->t - 5)/14; for (int v = 0; v < 8; v++) for (int u = 0; u < 8; u++) { u32 c = coinpx(u, v, p->t*4); if (c) wpx(p->x+u, p->y-h+v, c); } }
  }
  {   // iris wipe: opens on Hatrick after a (re)start, closes before a restart or the next level
    int t = fr < 24 ? fr*256/24 : st == DEAD && stt > 36 ? (60-stt)*256/24 : st == WIN && wphase == 3 && wt > 21 && !(lvl == NLV-1) ? (45-wt)*256/24 : 256;
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
  // HUD: level, deaths, coins, score, run time
  txt(I_FLAG, 5, 25, 4, 4, 1, 0xffffff); num(lvl+1, 1, 11, 4, 1, 0xffffff);
  txt(I_SKULL, 5, 25, 24, 4, 1, 0xffffff); num(deaths, 1, 31, 4, 1, 0xffffff);
  txt(I_COIN, 5, 25, 56, 4, 1, 0xffd84a); num(coins, 1, 63, 4, 1, 0xffffff);
  txt(I_STAR, 5, 25, 84, 4, 1, 0xffd84a); num(score, 6, 91, 4, 1, 0xffffff);
  {
    int s = done ? 2 : 1, x = done ? 84 : W-48, yy = done ? 52 : 4;
    u32 c = done ? 0xffd84a : 0xffffff;
    x = num(tim/3600, 1, x, yy, s, c);
    txt(FONT[10], 3, 15, x, yy, s, c);
    x = num(tim/60 % 60, 2, x+4*s, yy, s, c);
    txt(FONT[11], 3, 15, x, yy, s, c);
    num(tim % 60 * 100 / 60, 2, x+4*s, yy, s, c);
    if (done) { txt(I_STAR, 5, 25, 84, 72, 2, 0xffd84a); num(score, 6, 98, 72, 2, 0xffffff); }
  }
  if (st == WIN && wphase && !done) {   // course clear: this level's time, the score, the bonus still to count
    int x = 88, y = 40;
    txt(I_CLOCK, 5, 25, x, y, 2, 0xffffff);
    x = num(split/3600, 1, x+14, y, 2, 0xffffff); txt(FONT[10], 3, 15, x, y, 2, 0xffffff);
    x = num(split/60 % 60, 2, x+8, y, 2, 0xffffff); txt(FONT[11], 3, 15, x, y, 2, 0xffffff);
    num(split % 60 * 100 / 60, 2, x+8, y, 2, 0xffffff);
    txt(I_STAR, 5, 25, 88, y+16, 2, 0xffd84a); num(score, 6, 102, y+16, 2, 0xffd84a);
    if (tally) { txt(FONT[12], 3, 15, 102, y+32, 2, 0xffffff); num(tally*50, 1, 110, y+32, 2, 0xffffff); }
  }
}

#ifndef SIM
// ---------- platform: X11 window, frame timing, gamepads (evdev) and the audio director ----------
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "audio.h"

// ---------- gamepads: every evdev gamepad is read directly, Super Mario Odyssey layout ----------
// A/B jump, X/Y cap, LT/RT (or LB/RB) crouch / ground pound, left stick or D-pad move,
// Start opens the menu, View mutes. Pads are rescanned every 2 s, so hotplugging works, and pads
// with force feedback get rumble effects uploaded (played from rumble()).
static const short PADB[] = { BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR, BTN_TL2, BTN_TR2, BTN_SELECT, BTN_START,
                              BTN_DPAD_LEFT, BTN_DPAD_RIGHT, BTN_DPAD_UP, BTN_DPAD_DOWN };
static const unsigned PADK[] = { 16, 16|MENUBACK, 32, CAP2, 8, 8, 8, 8, 128, START, 1, 2, 4, 8 };
static struct { int fd, num, held, x, hx, y, hy, ymid, yrange, ydz, t1, t2, mid, range, dz, tq, c1, c2, fx[8]; } pad[4];   // fd is stored +1, 0 = free slot
// Rumble effects 1..8 (see rumble()): strong motor, weak motor, length in ms.
static const unsigned short RUM[8][3] = {
  { 0x0000, 0x4800,  50 }, { 0x3000, 0x3800,  70 }, { 0x5000, 0x3000,  80 }, { 0x6800, 0x5000, 100 },
  { 0x6000, 0x7000, 150 }, { 0xb800, 0x9000, 180 }, { 0xffff, 0xc000, 380 }, { 0x5000, 0x9000, 450 },
};
#define BIT(a, n) ((a)[(n)/8] >> ((n)%8) & 1)

static void padscan(void) {
  static u32 nopad;   // devices known not to be gamepads: opening some (audio jacks) takes ~10 ms
  for (int i = 0; i < 32; i++) {
    int free = -1, fd;
    char path[32];
    for (int j = 0; j < 4; j++) { if (pad[j].fd && pad[j].num == i) free = -2; if (!pad[j].fd && free == -1) free = j; }
    if (free < 0) continue;
    snprintf(path, sizeof path, "/dev/input/event%d", i);
    if (nopad >> i & 1) { if (access(path, F_OK) < 0) nopad &= ~(1u << i); continue; }   // gone: recheck if reused
    if ((fd = open(path, O_RDWR | O_NONBLOCK)) < 0 && (fd = open(path, O_RDONLY | O_NONBLOCK)) < 0) continue;
    u8 keys[KEY_MAX/8 + 1] = { 0 }, abs[ABS_MAX/8 + 1] = { 0 }, ff[FF_MAX/8 + 1] = { 0 };
    struct input_absinfo ai;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) > BTN_SOUTH/8 && BIT(keys, BTN_SOUTH) && ioctl(fd, EVIOCGABS(ABS_X), &ai) >= 0) {
      typeof(pad[0]) *p = pad + free;
      memset(p, 0, sizeof *p);
      p->fd = fd+1; p->num = i; p->x = ai.value;
      p->mid = (ai.minimum+ai.maximum) / 2; p->range = (ai.maximum-ai.minimum) / 2; p->dz = p->range / 6;
      if (ioctl(fd, EVIOCGABS(ABS_Y), &ai) >= 0) {
        p->y = ai.value; p->ymid = (ai.minimum+ai.maximum)/2; p->yrange = (ai.maximum-ai.minimum)/2; p->ydz = p->yrange/3;
      }
      ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs), abs);    // Bluetooth pads put the triggers on ABS_BRAKE / ABS_GAS
      p->c1 = BIT(abs, ABS_GAS) ? ABS_BRAKE : ABS_Z; p->c2 = BIT(abs, ABS_GAS) ? ABS_GAS : ABS_RZ;
      p->tq = ioctl(fd, EVIOCGABS(p->c1), &ai) < 0 ? 64 : ai.maximum / 4;
      for (int j = 0; j < 8; j++) p->fx[j] = -1;
      // Rumble only on physical pads. Effect uploads to a virtual (uinput) pad, e.g. Steam's,
      // wait up to 30 s each for the program behind it, and lock the device for everyone else
      // (logind included) meanwhile, so a stalled program could freeze the desktop session.
      char sys[64], lk[256] = { 0 };
      snprintf(sys, sizeof sys, "/sys/class/input/event%d", i);
      ssize_t n = readlink(sys, lk, sizeof lk - 1);     // ../../devices/virtual/input/... for uinput
      if (n > 0 && !strstr(lk, "/virtual/") && ioctl(fd, EVIOCGBIT(EV_FF, sizeof ff), ff) > FF_RUMBLE/8 && BIT(ff, FF_RUMBLE))
        for (int j = 0; j < 8; j++) {
          struct ff_effect e = { .type = FF_RUMBLE, .id = -1 };
          e.replay.length = RUM[j][2]; e.u.rumble.strong_magnitude = RUM[j][0]; e.u.rumble.weak_magnitude = RUM[j][1];
          if (ioctl(fd, EVIOCSFF, &e) >= 0) p->fx[j] = e.id;
        }
    } else close(fd), nopad |= 1u << i;
  }
}

static void padrumble(int k) {   // play effect k (1..8) on every pad that has rumble
  for (int j = 0; j < 4; j++)
    if (pad[j].fd && pad[j].fx[k-1] >= 0) {
      struct input_event ev = { .type = EV_FF, .code = pad[j].fx[k-1], .value = 1 };
      if (write(pad[j].fd-1, &ev, sizeof ev) < 0) { /* unplugged: padkeys notices */ }
    }
}

static int padkeys(void) {
  static int wait;
  int k = 0, axis = 0;
  struct input_event ev[64];
  if (--wait < 0) wait = 120, padscan();
  for (int j = 0; j < 4; j++) {
    typeof(pad[0]) *p = pad + j;
    if (!p->fd) continue;
    ssize_t n;
    while ((n = read(p->fd-1, ev, sizeof ev)) > 0)
      for (struct input_event *e = ev; e < ev + n / sizeof *ev; e++) {
        if (e->type == EV_KEY) for (int i = 0; i < sizeof PADB/sizeof *PADB; i++) { if (PADB[i] == e->code) p->held = e->value ? p->held | 1 << i : p->held & ~(1 << i); }
        if (e->type == EV_ABS) {
          if (e->code == ABS_X) p->x = e->value;
          if (e->code == ABS_HAT0X) p->hx = e->value;
          if (e->code == ABS_Y) p->y = e->value;
          if (e->code == ABS_HAT0Y) p->hy = e->value;
          if (e->code == p->c1) p->t1 = e->value;
          if (e->code == p->c2) p->t2 = e->value;
        }
      }
    if (n == 0 || errno != EAGAIN) { close(p->fd-1); p->fd = 0; continue; }   // unplugged
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

// ---------- audio director: picks the theme and stem levels from the game state ----------
static void director(void) {
  static char theme[64];
  static int seen = -1, fastt, arpt, quiet, dead;
  const float MUSIC = 0.75f;
  // this frame's sounds, panned a little by where Hatrick is on screen
  float pan = ((hx >> 8) + 3 - (cxf >> 8) - W/2) / (float)(W/2) * 0.6f;
  for (int i = 0; i < nsnd; i++) {
    if (sndq[i] == S_BOUNCE || (sndq[i] == S_STOMP && !gnd)) arpt = 150;   // cap-jump / stomp chains
    snd_play(sndq[i], menu ? 0 : pan < -0.6f ? -0.6f : pan > 0.6f ? 0.6f : pan);
  }
  nsnd = 0;
  // theme (levels.txt music=): the title menu plays a calm mix of the first level's;
  // restart after a (re)load
  int title = (menu && !resumable) || naming;
  const char *want = LV[title ? 0 : lvl].music;
  if (skipclear) snd_stop(S_CLEAR), skipclear = 0;   // the course clear was skipped
  if (st == DEAD) dead = 1;
  if (strcmp(want, theme)) { snprintf(theme, sizeof theme, "%s", want); snd_theme(theme, 1); seen = loads; dead = 0; }
  else if (loads != seen && !menu) { snd_theme(theme, 1); seen = loads; if (dead) quiet = 80; dead = 0; }
  if (quiet) quiet--;   // after a death the theme waits for the death jingle
  // stems
  int fast = (gnd && iabs(hvx) >= 380) || st == LONGJ || st == DIVE || st == SLIDE || st == ROLL || st == SPINJ || (spin && jn == 2);
  if (fast && !menu) fastt = 90; else if (fastt) fastt--;
  if (arpt) arpt--;
  float g[NSTEM] = { 1, 1, 1, 1, fastt ? 1 : 0, arpt ? 1 : 0, 1 };
  int ms = 600;
  if (title) g[STEM_LEAD] = 0, g[STEM_PERC] = 0.45f, g[STEM_BASS] = 0.8f, g[STEM_FAST] = g[STEM_ARP] = 0;
  else if (menu) { for (int s = 0; s < NSTEM; s++) g[s] *= 0.3f; ms = 200; }   // paused: duck
  if (!title && (st == DEAD || st == WIN || quiet)) { for (int s = 0; s < NSTEM; s++) g[s] = 0; ms = st == WIN ? 300 : 150; }
  for (int s = 0; s < NSTEM; s++)
    snd_stem(s, g[s] * MUSIC, s == STEM_FAST && g[s] ? 400 : s == STEM_ARP && g[s] ? 150 : (s >= STEM_FAST && !g[s] && !menu ? 1500 : ms));
  // enemies hop for a quarter second after each "bah" (a little earlier than the device latency)
  double t = snd_bah(-0.03);
  hop = t >= 0 && t < 0.25 ? 1 + (int)(t / 0.25 * 255) : 0;
}

int main(int argc, char **argv) {
  int silent = 0;
  const char *dump = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--silent")) silent = 1;
    else if (!strcmp(argv[i], "--dump") && i+1 < argc) dump = argv[++i], silent = 1;
    else { fprintf(stderr, "usage: %s [--silent] [--dump out.wav]\n", argv[0]); return 2; }
  }
  char dir[1024];
  assetdir(dir, sizeof dir);
  modlevels();
  if (!snd_init(dir, silent, dump)) fprintf(stderr, "hatrick: playing without sound\n");
  Display *d = XOpenDisplay(0);
  if (!d) { fprintf(stderr, "hatrick: cannot open display\n"); return 1; }
  Window w = XCreateSimpleWindow(d, RootWindow(d, 0), 0, 0, W*SC, H*SC, 0, 0, 0);
  XStoreName(d, w, "Hatrick");
  XSelectInput(d, w, ButtonPressMask | PointerMotionMask);
  Atom wmdelete = XInternAtom(d, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(d, w, &wmdelete, 1);
  XMapWindow(d, w);
  XImage *im = XCreateImage(d, DefaultVisual(d, 0), 24, ZPixmap, 0, (char *)big, W*SC, H*SC, 32, 0);
  struct timespec t;
  char km[32];
  int volume = 8, volkeys = 0;
  load(); menu = 1; hiload();
  clock_gettime(CLOCK_MONOTONIC, &t);
  for (;;) {
    while (XPending(d)) {
      XEvent e; XNextEvent(d, &e);
      if (e.type == ClientMessage && (Atom)e.xclient.data.l[0] == wmdelete) quitting = 1;
      if (menu && !scoreview && (e.type == MotionNotify || (e.type == ButtonPress && e.xbutton.button == 1))) {
        int x = e.type == MotionNotify ? e.xmotion.x : e.xbutton.x;
        int y = e.type == MotionNotify ? e.xmotion.y : e.xbutton.y;
        int hit = menuhit(x/SC,y/SC);
        if (hit >= 0) {
          if (menusel != hit) menusel = hit, sfx(S_MENUMOVE);
          if (e.type == ButtonPress && !scoreview) destination(hit);
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
    // volume: + / - (German layout keys) or keypad + / -, ten steps
    int vk = (K(35) | K(86)) | (K(61) | K(82)) << 1, vp = vk & ~volkeys;
    volkeys = vk;
    if (vp & 1 && volume < 10) volume++;
    if (vp & 2 && volume > 0) volume--;
    snd_volume(volume * volume / 100.0f, muted);
    director();
    if (quitting) { snd_quit(); return 0; }
    if (rumq) padrumble(rumq), rumq = 0;
    render();
    XPutImage(d, w, DefaultGC(d, 0), im, 0, 0, 0, 0, W*SC, H*SC);
    if ((t.tv_nsec += 16666667) >= 1000000000) t.tv_nsec -= 1000000000, t.tv_sec++;
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, 0);   // until the next frame
  }
}
#else
// ---------- headless simulator: replays scripted input, reports deaths / goal ----------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
  // usage: sim LEVEL TASFILE [trace] [dumpframe out.ppm]   (levels: assets/levels.txt next to sim)
  //        sim --check    reports mistakes in assets/levels.txt; exit status 1 if there are any
  //        SIM_RESPAWN=1  keeps replaying after a death (checkpoints), instead of stopping there
  if (argc > 1 && !strcmp(argv[1], "--check")) {
    char dir[1024], path[1100]; assetdir(dir, sizeof dir); snprintf(path, sizeof path, "%s/levels.txt", dir);
    fflush(stderr); int saved = dup(2); FILE *t = tmpfile(); dup2(fileno(t), 2);
    int ok = readlevels(path);
    fflush(stderr); dup2(saved, 2); close(saved);
    long n = ftell(t); rewind(t); char buf[4096]; size_t got;
    while ((got = fread(buf, 1, sizeof buf, t))) fwrite(buf, 1, got, stderr);
    fclose(t);
    if (ok) printf("levels: %d campaign + %d lab, %s\n", NLV, NLEVEL - NLV, n ? "with the problems above" : "no problems");
    return !ok || n;
  }
  if (argc < 3) { fprintf(stderr, "usage: sim LEVEL TASFILE [trace] [frame out.ppm] | sim --check\n"); return 3; }
  modlevels();
  lvl = atoi(argv[1]);
  if (lvl < 0 || lvl >= NLEVEL) { fprintf(stderr, "sim: no level %d (there are %d)\n", lvl, NLEVEL); return 3; }
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
        printf("%d %s x=%.1f y=%.1f vx=%d vy=%d st=%d gnd=%d cap=%d room=%d score=%d wall=%d", frame, b, hx/256., hy/256., hvx, hvy, st, gnd, cst, room, score, wall);
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
      if (st == DEAD && stt == 0 && !(getenv("SIM_RESPAWN") && *getenv("SIM_RESPAWN"))) { printf("DIED frame %d at x=%d y=%d (tile %d,%d)\n", frame, hx >> 8, hy >> 8, hx >> 11, hy >> 11); return 1; }
      if (st == WIN && stt == 1) { printf("GOAL frame %d (%.2fs) coins %d score %d deaths %d\n", frame, frame/60., coins, score, deaths); return 0; }
    }
  }
  printf("END frame %d at x=%d y=%d (tile %d,%d) st=%d\n", frame, hx >> 8, hy >> 8, hx >> 11, hy >> 11, st);
  return 2;
}
#endif
