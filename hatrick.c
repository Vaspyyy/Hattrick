// Hatrick: a tiny cap-throwing platformer for Linux/X11. Levels, music and sounds load from assets/ (MODDING.md).
// Arrows move, Z/Y or Space jumps, X/C throws the cap, Down crouches / ground pounds.
// Down + cap rolls on the ground; Down + X dives in the air. Up + jump spins,
// Up + cap throws upward, Down + C throws downward in the air.
// Gamepads use the Super Mario Odyssey layout (see padkeys).
// F1 opens the movement playground. Up spins on the ground; C recalls an out cap.
// R restarts the level, M mutes, Esc pauses / resumes. On the overworld map the arrows walk
// between stops and Enter or Jump plays one; Tab or controller Start opens the quick level select; Q quits.
// Controller Start pauses. Coins bank at the flag and buy items in the house shop (items.h).
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "gfx.h"
#include "sound.h"
#include <stdarg.h>
#include <limits.h>
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
#define MW 2560         // map size in 8 px tiles: 2560 wide (20480 px), 32 high
// Per level (main area and bonus rooms together): the most of each object.
#define MAXEN 480
#define MAXTUBE 128
#define MAXBAR 240
#define MAXHOME 240
#define MAXCR 960
#define MAXCK 64
#define MH 320          // the tallest an area can be, in tiles; each is as tall as its rows (at least MINH)
#define MINH 32
#define SOLID (0x1BF3E | WORLDSOLID | 0x38000000u)    // full tiles 1..5, slopes 8..9, tubes 10..12, crumble 13, found hidden block 15, fire bar pivot 16
// Tiles: see tiletype().
// Physics is fixed point, 1/256 px, 60 steps per second.
#define GRAV 48
#define MAXV 400
#define ACC 18
#define AACC 14
#define FRIC (MAXV/10)  // Odyssey NormalBrakeFrame: 10; scaled to this game's run speed
#define CAPSTALL (8 + clstall)       // first air throw pauses vertical motion for 8 frames
#define CAP2 256         // keep the two cap buttons distinct until press edges are read
#define ANALOG 512       // signed stick axis, biased by 256, in bits 10..19
#define ROLLSTART (MAXV*20/14)
#define ROLLMAX (MAXV*35/14)
#define ROLLBOOST (MAXV*6/14)
#define TRIPLE_V 1220
#define LJLATE 9         // Down within a running jump's first 8 airborne frames still makes it a long jump
#define CAPBOUNCE_V 768
#define CAPVAULT_V (CAPBOUNCE_V*32/26)
#define PRACTICE (1<<20)
#define BACK (1<<21)
#define START (1<<22)
#define QUIT (1<<23)
#define MENUBACK (1<<24)
#define PADCROUCH (1<<25)  // crouch held on a controller shoulder or trigger: ZL/ZR + either cap button dives, as in Odyssey
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
static int menu, menufr, menunav, menurepeat, resumable, quitting;   // menu: the map (resumable 0) or the pause screen
// Presentation only (never read by the game logic): camera, flips, squash and stretch.
static int cxf, cyf, look, camgy;          // camera position and look-ahead in 1/256 px, last standing height
static int spinlen, spind, sqv, turnt, lface, runph, vang, pgnd, pvy, dustt, rollph;
// Sounds requested by the game logic this frame. The platform layer plays them; the simulator
// and tests just ignore the queue. hop (1..256, 0 = none) is the progress of the enemies' little
// hop on the music's "bah" accents; it is purely visual, so collisions never depend on audio.
static int sndq[16], nsnd, muted, loads, hop;
// Music hooks other features set every frame: underwater (Hatrick is in water: the whole mix is
// low-passed) and bossnear (a boss fight is on screen: the danger strings come in).
static int underwater, bossnear;
typedef struct { int x, y, vx, vy, t, a, h, r, s, u, w; } E;   // r: the room it lives in
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
enum { CARD_HILLS, CARD_BRICKS, CARD_SPIKES, CARD_SKY, CARD_CASTLE, CARD_PLAYGROUND, CARD_BEACH, CARD_PEAK, CARD_WOODS, CARD_CLOCK, NCARD };
static const char *const CARDNAME[NCARD] = { "hills", "bricks", "spikes", "sky", "castle", "playground", "beach", "peak", "woods", "clock" };
#define NROOM 4
enum { T_UP, T_DOWN, T_LEFT, T_RIGHT };   // the way a tube mouth opens
typedef struct { int room, x, y, dir, id, link; } Tube;   // mouth's top-left cell; link: partner tube or -1
typedef struct { int room, x, y, len, speed, a0; } Bar;    // fire bar pivot cell; speed in 1/65536 turn per frame (+ clockwise)
typedef struct { int room, tube, spit; } Home;             // a tube dweller and the mouth it lives in
typedef struct { int w, h, cave; u8 *grid; } Room;   // grid: h rows of w map characters (0 = empty)
#define GRID(R, x, y) ((R)->grid[(y)*(R)->w + (x)])
typedef struct {
  char name[40], music[64]; int card, lab, time, nroom, ntube, nbar, nhome, nmoon, boss, mini;   // time: the level timer, s
  Room room[NROOM]; Tube tube[MAXTUBE]; Bar bar[MAXBAR]; Home home[MAXHOME];
  struct { int room, x, y; } moon[3];   // its secret moon coins, in reading order (main area, then rooms)
  struct { int star, sg, gr, gx, gy, pc, pr, px, py; char secretof[40]; } cl;   // collect.h: star time, secret exit, postcard
  int theme, avalanche; char before[40];   // worlds.h: theme=, avalanche=, before=
  int gim[8], gimroom[NROOM];              // gimmicks.h: header and room options
} Level;
#define GIM_PART 1
#include "gimmicks.h"
#undef GIM_PART
static Level *LV;
// bosses (boss.c): header options, setup on every (re)start, a frame, drawing
static int bossopt(Level *L, const char *k, const char *v, const char *file, int line);
static void bossstart(void), bosstick(void), bossdraw(void), bosscam(void);
static int NLV, NLEVEL, PLAY = -1;   // campaign levels, all levels, playground index (-1: none)
static int levelgen;                 // counts level lists loaded, so the map knows when to rebuild
#include "collect.h"   // hats, star ratings, secret exits, postcards, the house
#include "worlds.h"
static const char KNOWN[] = "#B^STo/\\|-M?C*%~:!0123456789@FKgbhnm(GP" "NJ" "HXRU" "I<>=w" "csqjf[]" "ukvLxpQYZ$&" "OAV_{}DEW";   // every map character but space

// What a map character becomes in the live map: 1 ground, 2 brick, 3 spikes, 4 stone, 5 spring,
// 6 coin, 8/9 slopes, 10/11 tube body (vertical / horizontal), 12 tube mouth, 13 crumble block,
// 14 hidden block (15 once found), 16 fire bar pivot. Objects and markers leave the cell empty.
static int tiletype(int c) {
  switch (c) {
    case '#': return 1; case 'B': return 2; case '^': return 3; case 'S': return 4; case 'T': return 5;
    case 'o': return 6; case '/': return 8; case '\\': return 9; case '|': return 10; case '-': return 11;
    case 'H': return 26; case 'X': return 27; case 'R': return 28; case 'U': return 25;   // cap.h
    case 'C': return 13; case '?': return 14; case '*': case '%': return 16;
    case 'Y': case 'Z': return 4; case '$': case '&': return 16;   // enemies.h
    case 'A': case 'E': case 'W': return 4;   // gimmicks.h: gold block, cannon, fan
    case 'I': case '<': case '>': return 1;   // ice and conveyors: ground with a surface (movement.h)
  }
  return c == 'M' || (c >= '0' && c <= '9') ? 12 : worldtile(c);   // worlds.h tiles
}

static void levelerr(const char *file, int line, const char *fmt, ...) {
  va_list ap; va_start(ap, fmt);
  worldline(&file, &line);
  fprintf(stderr, "hatrick: %s:%d: ", file, line); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
  va_end(ap);
}
// Reads a level header "= 1 hills music=overworld card=hills" or a room header "+ cellar bg=sky".
static void parseheader(Level *L, Room *R, char *h, const char *file, int line) {
  char name[40] = "", *w, *ws = 0;
  for (w = strtok_r(h, " \t", &ws); w; w = strtok_r(0, " \t", &ws)) {
    char *eq = strchr(w, '=');
    if (!eq) { if (strlen(name) + strlen(w) + 2 < sizeof name) strcat(strcat(name, *name ? " " : ""), w); continue; }
    *eq++ = 0;
    if (gim_opt(L, R, w, eq, file, line)) continue;   // gimmicks.h
    if (R) {
      if (!strcmp(w, "bg") && (!strcmp(eq, "cave") || !strcmp(eq, "sky") || !strcmp(eq, "dark"))) R->cave = !strcmp(eq, "cave") ? 1 : !strcmp(eq, "dark") ? 2 : 0;   // dark: cap.h lantern
      else levelerr(file, line, "unknown room option \"%s=%s\" (use bg=cave or bg=sky)", w, eq);
    } else if (bossopt(L, w, eq, file, line)) ;
    else if (!strcmp(w, "music")) snprintf(L->music, sizeof L->music, "%s", eq);
    else if (!strcmp(w, "bg") && !strcmp(eq, "dark")) L->room[0].cave = 2;   // cap.h: a dark main area
    else if (!strcmp(w, "time")) { L->time = atoi(eq); if (L->time < 1 || L->time > 9999) levelerr(file, line, "time needs 1 to 9999 seconds"), L->time = 500; }
    else if (!strcmp(w, "par")) levelerr(file, line, "par= is no longer used: every level has a timer (time=, 500 s by default)");
    else if (!strcmp(w, "card")) {
      int c = 0; while (c < NCARD && strcmp(eq, CARDNAME[c])) c++;
      if (c < NCARD) L->card = c; else levelerr(file, line, "unknown card \"%s\" (use hills, bricks, spikes, sky, castle, playground, beach, peak, woods or clock)", eq);
    } else if (worldopt(L, w, eq, file, line)) ;
    else if (collectopt(L, w, eq, file, line)) ;   // star=, secret=
    else levelerr(file, line, "unknown option \"%s\" (use music=, card= or time=)", w);
  }
  if (R) return;
  L->lab = !strncmp(name, "lab", 3);
  char *p = name; while ((*p >= '0' && *p <= '9') || *p == ' ') p++;   // "1 hills" -> "HILLS"
  for (int i = 0; p[i] && i < (int)sizeof L->name - 1; i++) L->name[i] = p[i] >= 'a' && p[i] <= 'z' ? p[i]-32 : p[i];
  if (!*L->name) snprintf(L->name, sizeof L->name, "LEVEL %.*s", (int)strspn(name, "0123456789"), name);   // "= 3": saved progress needs a name
}
// Fills one area's grid from its rows: as tall as its rows (at least MINH), the rows at the bottom.
// Returns 0 if unusable.
static int parserows(Level *L, int r, char **ln, const int *row, int nr, const char *file, int head) {
  Room *R = L->room + r;
  if (nr > MH) { levelerr(file, head, "%s \"%s\" is taller than %d rows, skipped", r ? "bonus room of level" : "level", L->name, MH); return 0; }
  for (int i = 0; i < nr; i++) {
    int len = strlen(ln[row[i]]);
    if (len > MW) { levelerr(file, row[i]+1, "level \"%s\" is wider than %d columns, skipped", L->name, MW); return 0; }
    if (len > R->w) R->w = len;
  }
  if (!R->w) R->w = 1;
  R->h = nr > MINH ? nr : MINH;
  R->grid = calloc(R->h * R->w, 1);
  for (int i = 0; i < nr; i++) {
    const char *l = ln[row[i]]; int y = R->h - nr + i, len = strlen(l);
    for (int x = 0; x < len; x++) {
      char c = l[x];
      if (c != ' ' && !strchr(KNOWN, c)) { levelerr(file, row[i]+1, "unknown tile '%c' in column %d, left empty", c, x+1); c = ' '; }
      if (r && (c == '@' || c == 'F')) { levelerr(file, row[i]+1, "'%c' belongs in the level's main area, not a bonus room; left empty", c); c = ' '; }
      GRID(R, x, y) = c == ' ' ? 0 : c;
    }
  }
  return 1;
}
static void freelevel(Level *L) { for (int r = 0; r < NROOM; r++) free(L->room[r].grid), L->room[r].grid = 0; }
// Tubes, fire bars and tube dwellers of one area; mistakes are reported and the object dropped.
static int isarm(int c) { return c == '~' || c == ':' || c == '!'; }
static void parseobjects(Level *L, int r, const char *file, const int *line) {
  Room *R = L->room + r;
  u8 *seen = calloc(R->w * R->h, 1);
  #define AT(x, y) ((unsigned)(x) < (unsigned)R->w && (unsigned)(y) < (unsigned)R->h ? GRID(R, x, y) : 0)
  #define SEEN(x, y) seen[(y)*R->w + (x)]
  for (int y = 0; y < R->h; y++) for (int x = 0; x < R->w; x++) {
    int c = GRID(R, x, y);
    if (!c || SEEN(x, y)) continue;
    if (c == 'M' || (c >= '0' && c <= '9')) {
      Tube t = { r, x, y, T_UP, c == 'M' ? -1 : c-'0', -1 };
      if (AT(x+1, y) == c && !SEEN(x+1, y)) {
        SEEN(x+1, y) = 1;
        t.dir = AT(x, y-1) == '|' && AT(x, y+1) != '|' ? T_DOWN : T_UP;
      } else if (AT(x, y+1) == c) {
        SEEN(x, y+1) = 1;
        t.dir = AT(x-1, y) == '-' && AT(x+1, y) != '-' ? T_RIGHT : T_LEFT;
      } else { levelerr(file, line[y], "tube mouth '%c' in column %d needs a second cell: side by side for a tube opening up or down, stacked for one opening sideways", c, x+1); continue; }
      if (L->ntube < MAXTUBE) L->tube[L->ntube++] = t; else levelerr(file, line[y], "too many tube mouths (at most %d)", MAXTUBE);
    }
    if (c == '*' || c == '%' || c == '$' || c == '&') {
      static const int DX[4] = { 1, 0, -1, 0 }, DY[4] = { 0, 1, 0, -1 };
      int best = 0, dir = 0, kind = '~';
      for (int d = 0; d < 4; d++) {
        int n = 1; while (isarm(AT(x + n*DX[d], y + n*DY[d]))) n++;
        if (n-1 > best) best = n-1, dir = d, kind = AT(x + DX[d], y + DY[d]);
      }
      int speed = kind == ':' ? 182 : kind == '!' ? 437 : 273;   // a turn in 6, 4 or 2.5 s
      Bar b = { r, x, y, best ? best+1 : 5, c == '*' ? speed : -speed, dir*64 };
      if (c == '$' || c == '&') b.speed = c == '$' ? 1 : -1;   // a beat bar (enemies.h)
      if (L->nbar < MAXBAR) L->bar[L->nbar++] = b; else levelerr(file, line[y], "too many fire bars (at most %d)", MAXBAR);
    }
  }
  for (int y = 0; y < R->h; y++) for (int x = 0; x < R->w; x++) {
    int c = GRID(R, x, y), home = -1;
    if (c != 'n' && c != 'm') continue;
    for (int i = 0; i < L->ntube; i++) {
      Tube *t = L->tube + i;
      if (t->room == r && (t->x == x || t->x+1 == x) && ((t->dir == T_UP && t->y == y+1) || (t->dir == T_DOWN && t->y == y-1))) home = i;
    }
    if (home < 0) levelerr(file, line[y], "tube dweller '%c' in column %d must sit right above a tube opening up, or right below one opening down", c, x+1);
    else if (L->nhome < MAXHOME) L->home[L->nhome++] = (Home){ r, home, c == 'm' };
    else levelerr(file, line[y], "too many tube dwellers (at most %d)", MAXHOME);
  }
  #undef AT
  #undef SEEN
  free(seen);
}
// Parses a whole levels.txt. Broken levels are reported (file name and line) and skipped;
// returns 0 if no campaign level is usable, leaving the current levels in place.
static int parselevels(const char *text, const char *file) {
  char *copy = gim_expand(text);   // gimmicks.h: copy= levels
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
    memset(L, 0, sizeof *L); strcpy(L->music, "overworld"); L->time = 500;
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
      for (int y = 0; y < MH; y++) { int i = y - (L->room[r].h - nr); lines[r][y] = ok && i >= 0 && i < nr ? row[i]+1 : s+1; }
      L->nroom++;
      s = next;
    }
    int sx = -1, fx = -1, nen = 0;
    for (int r = 0; r < L->nroom && ok; r++) for (int y = 0; y < L->room[r].h; y++) for (int x = 0; x < L->room[r].w; x++) {
      int c = GRID(L->room + r, x, y);
      if (c == '@') sx = x; else if (c == 'F') fx = x; else if (c && strchr("gbhc", c)) nen++;
    }
    if (ok && sx < 0) levelerr(file, h+1, "level \"%s\" has no start (@), skipped", L->name), ok = 0;
    if (ok && fx < 0) levelerr(file, h+1, "level \"%s\" has no flag (F), skipped", L->name), ok = 0;
    if (ok && nen > MAXEN) levelerr(file, h+1, "level \"%s\" has too many enemies (at most %d), skipped", L->name, MAXEN), ok = 0;
    if (ok) {
      for (int r = 0; r < L->nroom; r++) parseobjects(L, r, file, lines[r]);
      for (int id = 0; id < 10; id++) {   // each digit links the two mouths that carry it
        int a = -1, b = -1, more = 0;
        for (int i = 0; i < L->ntube; i++) if (L->tube[i].id == id) { if (a < 0) a = i; else if (b < 0) b = i; else more = 1; }
        if (b >= 0) L->tube[a].link = b, L->tube[b].link = a;
        else if (a >= 0) levelerr(file, h+1, "level \"%s\": tube %d has no partner (give a second tube mouth the same digit); it can't be entered", L->name, id);
        if (more) levelerr(file, h+1, "level \"%s\": more than two tube mouths are numbered %d; only the first two are linked", L->name, id);
      }
      for (int r = 0; r < L->nroom; r++) for (int y = 0; y < L->room[r].h; y++) for (int x = 0; x < L->room[r].w; x++)
        if (GRID(L->room + r, x, y) == '(') {
          if (L->nmoon < 3) L->moon[L->nmoon].room = r, L->moon[L->nmoon].x = x, L->moon[L->nmoon++].y = y;
          else levelerr(file, h+1, "level \"%s\" has more than 3 moon coins; the rest are ignored", L->name), GRID(L->room + r, x, y) = 0;
        }
      cl_parse(L, file, h+1);
      for (int r = 0; r < L->nroom; r++) for (int y = 0; y < L->room[r].h; y++) for (int x = 0; x < L->room[r].w; x++) {
        int c = GRID(L->room + r, x, y);   // a boss arena (boss.c: 15 columns left, 16 right of N or J) can't hold a checkpoint
        if (!((c == 'N' && L->boss) || (c == 'J' && L->mini))) continue;
        for (int ky = 0; ky < L->room[r].h; ky++) for (int kx = x-15 < 0 ? 0 : x-15; kx <= x+16 && kx < L->room[r].w; kx++)
          if (GRID(L->room + r, kx, ky) == 'K')
            levelerr(file, lines[r][ky], "level \"%s\": a checkpoint inside a boss arena would trap Hatrick behind its gates; removed", L->name), GRID(L->room + r, kx, ky) = 0;
      }
      int nck = 0, ncr = 0;
      for (int r = 0; r < L->nroom; r++) for (int y = 0; y < L->room[r].h; y++) for (int x = 0; x < L->room[r].w; x++)
        nck += GRID(L->room + r, x, y) == 'K', ncr += GRID(L->room + r, x, y) == 'C';
      if (nck > MAXCK) levelerr(file, h+1, "level \"%s\" has more than %d checkpoints; the rest are ignored", L->name, MAXCK);
      if (ncr > MAXCR) levelerr(file, h+1, "level \"%s\" has more than %d crumble blocks; the rest stay solid", L->name, MAXCR);
    }
    if (!ok) freelevel(L);
    else if (!L->lab) for (int j = 0; j < n; j++)   // progress is saved by name
      if (!out[j].lab && !strcmp(out[j].name, L->name)) { levelerr(file, h+1, "another level is also named \"%s\"; they share saved progress, so give one another name", L->name); break; }
    n += ok;
    h = end - 1;
  }
  free(ln); free(copy);
  int camp = 0; for (int i = 0; i < n; i++) camp += !out[i].lab;
  if (!camp) { fprintf(stderr, "hatrick: %s: no playable level\n", file); for (int i = 0; i < n; i++) freelevel(out + i); free(out); return 0; }
  Level *sorted = malloc(n * sizeof *sorted); int k = 0;   // campaign first, then labs, in file order
  for (int pass = 0; pass < 3; pass++) for (int i = 0; i < n; i++) if (out[i].lab == pass) sorted[k++] = out[i];   // campaign, labs, secret (lab 2)
  worldorder(sorted, camp);   // before= moves new worlds ahead of the finale
  free(out); for (int i = 0; i < NLEVEL && LV; i++) freelevel(LV + i); free(LV);
  LV = sorted; NLV = camp; NLEVEL = n; PLAY = n - cl_nsec(sorted, n) > camp ? n - NSEC - 1 : -1; levelgen++;
  return 1;
}
static int readlevels(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  char *text = 0; size_t n = 0, cap = 0, got;
  do { if (n + 4096 >= cap) text = realloc(text, cap = cap*2 + 8192); got = fread(text + n, 1, cap - n - 1, f); n += got; } while (got);
  fclose(f); text[n] = 0;
  text = worldjoin(text, path);   // assets/worlds.txt follows levels.txt
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
  E en[MAXEN]; int ne;
  Crumble cr[MAXCR]; int ncr;
  Dweller dw[MAXHOME];
  Shot sh[8];
  Check ck[MAXCK]; int nck;
  int found;      // bonus rooms already visited (bit per room), for the find bonus
  int moongot;    // moon coins picked up on this visit (bit per coin); kept once the flag is reached
  GimWorld gw;    // gimmicks.h
} wd, saved;
#define map (wd.rm[room])
#define en (wd.en)
#define ne (wd.ne)
static int room, lh = MINH, startx, starty, haveck, ckroom, ckx, cky;   // lh: the current area's height, tiles
static int score, lscore, savedcoins, savedscore, lstart, split;   // split: frames the finished level took
#define LIMIT (LV[lvl].time*60)   // the level's timer (time=, 500 s by default); out of time is a death
static int left, timeout;  // frames left on it; the last death was the timer running out
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

static int tile(int x, int y) { return x < 0 ? 4 : x >= lw || y < 0 || y >= lh ? 0 : map[y][x]; }
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
// What the thrown cap can hit (enemies, bosses, shots, blocks): its 8x5 sprite plus CAPREACH px on
// every side, so a near miss still counts, as in Odyssey. Bouncing on the cap uses the sprite itself.
#define CAPREACH 2
#define CAPBOX (cxp >> 8)-CAPREACH, (cyp >> 8)-CAPREACH, 8+2*CAPREACH, 5+2*CAPREACH
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
// Screen shake strength: render() shakes the view by up to shake/3 px, easing out as it counts down.
static void kick(int n) { if (n > shake) shake = n; }
static void sparkle(int x, int y, u32 c, int n) {   // a small ring of sparks, x, y in px
  for (int i = 0; i < n; i++) { int a = (i*256/n + rnd(16)) & 255; part(x << 8, y << 8, SIN[(a+64) & 255]*3/2, SIN[a]*3/2 - 120, 14+rnd(8), 6, c); }
}
static void dust(int x, int y, int dir, int n) {   // soft puffs at the feet, x, y in 1/256 px
  while (n--) part(x + (rnd(5)-2)*256, y - 256, dir*(60+rnd(160)) + rnd(60)-30, -40-rnd(110), 16+rnd(14), 3, 0xf4f1e6);
}
#define SPIN(n, d) (spin = spinlen = (n), spind = (d))   // flip animation: length, direction
#include "cap.h"   // cap posts, cap switches, hat swap and the rest of the cap ideas
#include "items.h"   // the coin bank, the shop and the items
static int dieforce;   // doom(): a kill no hat power can soak up
// Hearts, like Odyssey: a hit costs one and Hatrick blinks for a moment; the last one lost is a death.
// Touching a checkpoint fills them again, and every (re)spawn starts full.
#define MAXHP 3
static int hp = MAXHP, hpt, healt;   // hpt: frames since a heart was lost (it jumps on the HUD); healt: a refill's glow
static int hurt(void) {   // die() asks after the hat power: a spare heart takes the hit. Pits and the timer always count.
  if ((hy >> 8) > lh*8 || !left || hp <= 1) return 0;
  hp--; hpt = 0; capx_inv = 90; it_shoes = 0; if (hvy > -600) hvy = -600;
  burst((hx >> 8)+3, (hy >> 8)+2, 0xff4a5a, 10); sfx(S_BOUNCE); rumble(5); kick(6);
  return 1;
}
static void die(void) {
  if (!dieforce && (it_shield() || capx_absorb() || it_eggsave() || (st < TUBE && hurt()))) return;
  if (st < TUBE) { st = DEAD; stt = 0; hvy = -900; hvx = 0; hp = 0; hpt = 0; deaths++; sfx(S_DEATH); rumble(7); kick(9); }
}
static void doom(void) { dieforce = 1; die(); dieforce = 0; }   // lava, rising lines, avalanches, quicksand, squeezes
static void pop(int x, int y, int v) {
  Pop *p = pops; for (Pop *q = pops; q < pops+12; q++) if (q->t < p->t) p = q;
  p->x = x; p->y = y; p->v = v; p->t = 45;
}
static void addscore(int v, int x, int y) { score += v; pop(x, y, v); }
static void smash(int tx, int ty) { map[ty][tx] = 0; burst(tx*8+4, ty*8+4, 0xd0602a, 6); sfx(S_BRICK); rumble(3); kick(5); addscore(50, tx*8+4, ty*8); }
static Crumble *crumbleat(int r, int x, int y) {
  for (Crumble *c = wd.cr; c < wd.cr + wd.ncr; c++) if (c->room == r && c->x == x && c->y == y) return c;
  return 0;
}

#include "movement.h"
#define GIM_PART 2
#include "gimmicks.h"
#undef GIM_PART
#include "enemies.h"
// The level as the file describes it, before anything happened.
static int sweep(int ph) { ph = ph+1 & 127; return (ph < 64 ? ph : 128-ph)*96; }   // a sweeping flyer's x along its patrol at phase ph
static void build(void) {
  const Level *L = LV + lvl;
  memset(&wd, 0, sizeof wd);
  for (int r = 0; r < L->nroom; r++) for (int y = 0; y < L->room[r].h; y++) for (int x = 0; x < L->room[r].w; x++) {
    int c = GRID(L->room + r, x, y);
    wd.rm[r][y][x] = tiletype(c);
    if (r == 0 && c == '@') startx = x, starty = y;
    if (r == 0 && c == 'F') gx = x, gy = y;
    if ((c == 'g' || c == 'b' || c == 'h') && ne < MAXEN) { E *n = en + ne++;   // bounded: extended enemies share en[]
      n->x = x << 11; n->y = n->h = y << 11; n->vx = -100; n->vy = 0; n->t = c == 'g' ? 1 : c == 'b' ? 2 : 3; n->a = 1; n->r = r;
      if (n->t == 3) n->vx = n->x - sweep(n->h >> 9);   // a sweeper's vx holds the left end of its patrol
    }
    if (c == 'c' && ne < MAXEN) en[ne++] = (E){ x << 11, y << 11, -170, 0, E_CRAB, 1, y << 11, r };   // a crab (worlds.c)
    extspawn(r, x, y, c);   // enemies.h
    if (c == 'C' && wd.ncr < MAXCR) wd.cr[wd.ncr++] = (Crumble){ r, x, y };
    if (c == 'K' && wd.nck < MAXCK) wd.ck[wd.nck++] = (Check){ r, x, y };
  }
  for (int i = 0; i < L->nhome; i++) wd.dw[i] = (Dweller){ 0, 40 + i*53 % 100, 0, 1 };   // staggered
  room = 0; lw = L->room[0].w; lh = L->room[0].h;
  for (gb = gy*8; gb < lh*8 && !scan(gx*8+3, gb, 1, 1, SOLID); gb++);
  mv_build();
  gim_build();
}
// Puts Hatrick in area r at (x, y) (1/256 px) with a fresh state: level start, checkpoint or tube.
static void spawn(int r, int x, int y) {
  room = r; lw = LV[lvl].room[r].w; lh = LV[lvl].room[r].h;
  hx = x; hy = y;
  hvx = hvy = st = stt = jn = cst = lock = spin = skid = fr = gnd = jbuf = coy = wall = cut = capok = diveok = stall = cready = throwt = 0;
  duck = catcht = catchok = twirl = gpspin = rollbuf = cvy = ckind = 0;
  arcg = GRAV; runt = rundir = launch = boostt = capbuf = capkeys = capextend = capreflect = 0;
  ledget = climbx = climby = slopedir = poundt = 0;
  mv_reset();
  hp = MAXHP; hpt = 99; healt = 0;
  it_spawn();
  loads++;
  bossstart();
  capx_reset();
  gim_restart();
  jn = -1; face = lface = 1; landt = 99;
  cxf = hx - (W/2 - 3 << 8); cyf = hy - (H*5/8 << 8); look = 0; camgy = hy; sqv = turnt = vang = pgnd = pvy = rollph = 0;
  worldspawn();
}
static void load(void) {   // (re)start the level from the top
  build(); haveck = 0; left = LIMIT; timeout = 0;
  spawn(0, startx << 11, (starty-1) << 11);
  coins = lcoins; score = lscore;
}
static void respawn(void) {   // after a death: from the last checkpoint touched, else from the top
  if (!haveck) { load(); return; }
  wd = saved; coins = savedcoins; score = savedscore;
  if (timeout) left = LIMIT, timeout = 0;   // the time left carries over, unless it ran out
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
        room = b->room; lw = L->room[room].w; lh = L->room[room].h;
        if (room && !(wd.found >> room & 1)) wd.found |= 1 << room, addscore(2000, x+3, y-4), sfx(S_REVEAL);   // a bonus room found
      }
      hx = (x << 8) - vx*20; hy = (y << 8) - vy*20; sfx(S_TUBE);
      if (vx) face = vx > 0 ? 1 : -1;
      cxf = hx - (W/2 - 3 << 8); cyf = hy - (H*5/8 << 8); camgy = hy;   // cut to the new place
    }
    hx += vx; hy += vy;
    while (scan(hx >> 8, hy >> 8, 6, 11, 4) == 2) smash(htx, hty);   // bricks over the way out burst
  }
  if (++stt == 40) { st = NORM; coy = 99; jn = -1; landt = 99; tubelock = tubeto+1; }   // no instant way back in
}
static int tubecheck(int k, int dir, int X, int Y, int vy0) {   // enter a tube this frame?
  const Level *L = LV + lvl;
  if (st != NORM && st != SPINJ) return 0;   // a spin jump (Up held, then Jump) reaches a mouth that opens down too
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
// Hatrick slides down the pole with the flag, poses, the time left on the timer is counted into the
// score, then back to the map. A new jump press skips straight to the end of all of it.
static void tomap(void);
static int runok = 1, runnext;   // the run went level 1 onward in order (a high score counts only then); the level it needs next
static void nextlevel(void) {
  if (gim_rushnext()) return;   // gimmicks.h: the coin rush goes on
  if (lvl < NLV) runnext = lvl+1;
  if (lvl == NLV-1) done = 1;   // the final level: the end of the run, then the map
  else tomap();
}
static void win(int pr) {
  stt++;
  if (stt > 6 && pr & 16 && !done) {
    score += tally*50; tally = 0; skipclear = 1; nextlevel(); return;
  }
  if (wphase == 0) {   // slide down with the flag
    int down = !scan(hx >> 8, (hy >> 8)+1, 6, 11, SOLID) && (hy >> 8)+11 < gb;   // gb: the ground, or the bottom when there's none
    if (down) hy += 256;
    if (flagy < gb-9) flagy++;
    if (!down && flagy >= gb-9) {
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
// ---------- progress: levels cleared and moon coins brought home, by level name, in ~/.hatrick_progress ----------
// Lines "<bits> <LEVEL NAME>": bits 1, 2, 4 the level's moon coins (reading order), 8 the level cleared.
// HATRICK_PROGRESS names another file; the simulator and tests only ever use that.
static struct { char name[40]; int bits; } prog[64];
static int nprog;
static const char *savepath(const char *env, const char *file) {
  static char p[1100];
  const char *e = getenv(env), *home = getenv("HOME");
  if (e && *e) return e;
#ifdef SIM
  return 0;
#endif
  if (!home) return 0;
  snprintf(p, sizeof p, "%s/%s", home, file);
  return p;
}
static void progload(void) {
  const char *p = savepath("HATRICK_PROGRESS", ".hatrick_progress"); FILE *f = p ? fopen(p, "r") : 0;
  char line[128];
  nprog = 0;
  while (f && nprog < 64 && fgets(line, sizeof line, f)) {
    char *name; int bits = (int)strtol(line, &name, 10);
    while (*name == ' ') name++;
    name[strcspn(name, "\r\n")] = 0;
    if (*name && bits > 0) snprintf(prog[nprog].name, sizeof prog[0].name, "%s", name), prog[nprog++].bits = bits & 127;
  }
  if (f) fclose(f);
}
static int progbits(int l) {
  for (int i = 0; i < nprog; i++) if (!strcmp(prog[i].name, LV[l].name)) return prog[i].bits;
  return 0;
}
static int moonbits(int l) { return progbits(l) & 7; }   // the moon coins of level l already brought home
static int cleared(int l) { return progbits(l) >> 3 & 1; }
static void progkeep(int l, int bits) {   // adds bits to level l's record and saves the file
  if ((progbits(l) | bits) == progbits(l)) return;
  int i = 0; while (i < nprog && strcmp(prog[i].name, LV[l].name)) i++;
  if (i == nprog) { if (nprog == 64) return; nprog++; snprintf(prog[i].name, sizeof prog[0].name, "%s", LV[l].name); prog[i].bits = 0; }
  prog[i].bits |= bits;
  const char *p = savepath("HATRICK_PROGRESS", ".hatrick_progress"); FILE *f = p ? fopen(p, "w") : 0;
  if (!f) { if (p) fprintf(stderr, "hatrick: cannot save the progress to %s\n", p); return; }
  for (int j = 0; j < nprog; j++) fprintf(f, "%d %s\n", prog[j].bits, prog[j].name);
  fclose(f);
}

static int firstclear;   // the level just finished was cleared for the first time: its path opens on the map
static void touchflag(int Y) {
  int top = gy*8, h = gb - top, f = h > 0 ? (gb - (Y+11)) * 100 / h : 100;
  st = WIN; stt = 0; hvx = hvy = 0; hx = gx*8-3 << 8; sfx(S_CLEAR); rumble(8);
  split = tim - lstart; wphase = wt = 0; flagy = top+1; skipclear = 0;
  tally = left / 60;   // whole seconds left
  firstclear = lvl < NLV && !cleared(lvl);
  if (lvl < NLV) progkeep(lvl, wd.moongot | 8);   // cleared; the moon coins count once they reach a flag
  addscore(f >= 90 ? 5000 : f >= 65 ? 2000 : f >= 40 ? 800 : f >= 20 ? 400 : 100, gx*8+8, Y);
  cl_flag();
  it_clear();   // items.h: the coins go into the bank
}

static void hero(int k, int pr) {
  int axis = moveaxis(k), dir = axis > 0 ? 1 : axis < 0 ? -1 : 0;
  int target = iabs(axis)*MAXV/256, D = k >> 3 & 1, U = k >> 2 & 1, g = GRAV, X, Y;
  int takeoff = 0, rollcancel = st == ROLL && !D;
  int cappress = pr & 32, downthrow = D && (pr & CAP2) && !(prevk & 32) && !(k & PADCROUCH);
  if (st == DEAD) { hvy += GRAV; hy += hvy; if (++stt > 60) respawn(); return; }
  if (st == WIN) { win(pr); return; }
  if (st == TUBE) { tubemove(); return; }
  if ((pr = mv_pre(k, pr)) < 0) return;   // swing poles, water, wall slides (movement.h)
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
  worldbefore(&target);   // snow
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
  // Late in the window only a still-held direction counts: letting go and pressing Down is a ground pound.
  if (!gnd && launch && pr & 8 && !(k & 32) && (dir || (runt && launch >= LJLATE-5))) {
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
      if (rollcancel && cappress) { st = NORM; hvy = -870; posture(0); gnd = 0; coy = 99; cut = 1; jn = 0; launch = LJLATE; }
      else longjump();
      takeoff = 1; sfx(rollcancel && cappress ? S_JUMP : S_LONGJ);
    } else if (!D) st = NORM, posture(0);
  } else if (st == GSPIN) {
    posture(0); hvx = hvx*95/100;
    target = iabs(hvx) > MAXV*8/14 ? iabs(hvx) : MAXV*8/14;
    if (dir) { hvx += axis*AACC/256; if (iabs(hvx) > target) hvx = (hvx > 0 ? 1 : -1)*target; face = dir; }
    if (jbuf && coy < 6) { jbuf = 0; st = SPINJ; hvy = -560; cut = launch = 0; gnd = 0; coy = 99; jn = -1; takeoff = 1; sfx(S_SPIN); }
    else if (++stt >= 90 || D || !gnd) st = NORM;
  } else if (st == SLIDE && mv_slidetick(dir)) {   // the slope slide (movement.h)
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
      jbuf = 0; coy = 99; cut = 1; spin = 0; gnd = 0; posture(0); takeoff = 1; launch = LJLATE;
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
      dust(hx + (3 << 8) + wall*(3 << 8), hy + (6 << 8), -wall, 4);
    } else if (jbuf && catcht && catchok && !gnd) {
      jbuf = catcht = catchok = 0; st = NORM; spin = cut = 0;
      hvy = -320; arcg = 26; throwt = 0; twirl = 10; stall = 1; launch = 0; sfx(S_SPIN);
    } else if (pr & 16 && !gnd && !takeoff && it_airjump()) {   // items.h: the spring shoes
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
  capx_flutter(k, pr);
  it_glide(k);   // items.h: the feather cap
  if (st != GPSLAM) {
    if (hvy > 1100) hvy = 1100;   // wall slides brake in mv_move()
  }
  mv_move(k, dir, g);

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
  if (scan(X, Y+duck, 6, 11-duck, SOLID)) {   // in a wall: out to the nearest free spot close by, back the way he came first
    int s = hvx > 0 ? -1 : 1, nx = X;
    for (int d = 1; d <= 12 && nx == X; d++)
      if (!scan(X+s*d, Y+duck, 6, 11-duck, SOLID)) nx = X+s*d;
      else if (!scan(X-s*d, Y+duck, 6, 11-duck, SOLID)) nx = X-s*d;
    X = nx; hx = X << 8; hvx = 0;   // nothing free nearby: stay put and leave it to the vertical push
  }
  int was = gnd, vy0 = hvy, top0 = Y+duck; gnd = 0;
  hy += hvy; Y = hy >> 8;
  if (vy0 < 0) for (int tx = X >> 3, ty = (Y+duck) >> 3; tx <= (X+5) >> 3; tx++)   // hidden blocks: only a bump from below finds them
    if (tile(tx, ty) == 14 && top0 >= ty*8+8) {
      map[ty][tx] = 15; coins++; bumpx = tx; bumpy = ty; bumpt = 10; pop(tx*8, ty*8-8, 0);
      addscore(1100, tx*8+4, ty*8-4); sfx(S_REVEAL); sfx(S_COIN);
    }
  if (st == GPSLAM) while (scan(X, Y+duck, 6, 12-duck, 4) == 2) smash(htx, hty);
  if (st == GPSLAM) capx_heavy(X, Y+duck, 12-duck);
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
  if (!gnd && hvy >= 0 && extride()) Y = hy >> 8;   // a thwomp (enemies.h)
  if (!gnd && capx_stand(X, &Y)) gnd = 1;   // a cap stuck on a post
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
      if (st == GPSLAM && mv_slopepound()) ;   // on a slope: the slope slide (movement.h)
      else if (st == GPSLAM) {
        st = GPLAND; stt = 0; poundt = 31; kick(10); sfx(S_GPLAND); rumble(6);
        if (rollbuf) roll(gpspin ? MAXV*30/14 : ROLLSTART);
      } else if ((st == DIVE || st == LONGJ) && D) roll(iabs(hvx) > ROLLSTART ? iabs(hvx) : ROLLSTART);
      else if (st == DIVE) st = SLIDE, posture(5);
      else if (st == LONGJ || st == SPINJ) st = NORM;
      if (vy0 > 900 && st != GPLAND) rumble(2), sfx(S_LAND), kick(vy0 > 1300 ? 5 : 0);
    }
    if (scan(X, Y+11, 6, 1, 32)) {
      hvy = -1500; gnd = 0; st = NORM; arcg = GRAV; launch = 0; posture(0); coy = 99; jbuf = 0;
      SPIN(30, face); cut = 0; sfx(S_SPRING); rumble(5); sparkle(hx/256+3, hy/256+11, 0xfff0a8, 8);
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
  worldafter(was, vy0, k);   // quicksand, jelly, flowers
  if (st == DEAD) return;
  while (scan(X, Y+duck, 6, 11-duck, 64)) { map[hty][htx] = 0; coins++; sfx(S_COIN); addscore(100, htx*8+4, hty*8); sparkle(htx*8+4, hty*8+4, 0xffe066, 6); }
  if (scan(X-1, Y+duck+2, 8, 8-duck, 8) || scan(X+1, Y+duck-1, 4, 13-duck, 8) || Y > lh*8+8) die();
  if (room == 0 && X+6 > gx*8+2 && X < gx*8+6 && Y < gb && st < TUBE) touchflag(Y);
  else if (st < TUBE && !lock) tubecheck(k, dir, X, Y, vy0);
}

static void capupd(int k) {
  if (capx_tick(k)) return;
  it_captick();
  if (!cst) return;
  if (ckind == CAPSPIN && cst < 3) {
    int a = ++ct*256/24;
    int nx = hx + 256 + SIN[(a+64)&255]*14, ny = hy + (5 << 8) + SIN[a&255]*5;
    if (!scan(nx >> 8, ny >> 8, 8, 4, SOLID)) cxp = nx, cyp = ny;
    if (ct >= 24) cst = 3;
  } else if (cst == 1 && it_boomhome()) {   // items.h: the boomerang cap flying home
  } else if (cst == 1) {
    int nx = cxp + cvx, ny = cyp + cvy, slow = it_capslow();
    if (scan(nx >> 8, ny >> 8, 8, 4, SOLID)) {
      if (capreflect && cvx) { cvx = -cvx; capreflect = 0; }
      else if (!it_boomturn()) cst = 2, ct = 0;
    }
    else {
      cxp = nx; cyp = ny;
      if (cvx) cvx -= cvx > 0 ? slow : -slow;
      if (cvy) cvy -= cvy > 0 ? slow : -slow;
      if (iabs(cvx) < 100 && iabs(cvy) < 100 && !it_boomturn()) cst = 2, ct = 0;
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
  if (cst && cst < 3) while (scan(cxp >> 8, cyp >> 8, 8, 4, 64)) { map[hty][htx] = 0; capx_scoop(htx, hty); sparkle(htx*8+4, hty*8+4, 0xffe066, 6); }
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

static void kill(E *e) { e->a = 0; burst((e->x >> 8)+4, (e->y >> 8)+4, 0x9a48d0, 8); sfx(S_STOMP); rumble(4); kick(5); addscore(200, (e->x >> 8)+4, e->y >> 8); }

static void enemies(int k) {
  int X = hx >> 8, Y = hy >> 8, n0 = ne;
  static int x0[MAXEN]; capone = 0; for (int i = 0; i < ne; i++) x0[i] = en[i].x;   // for stacks (enemies.h)
  mv_slidehits();   // the slope slide runs enemies over (movement.h)
  for (E *e = en; e < en+ne; e++) {
    if (!e->a || e->r != room) continue;
    if (e->t >= T_SHY && e->t <= T_PISTON) { extenemy(e, k); continue; }
    int ex, ey;
    if (e->t == 1 || e->t == E_CRAB) {   // walker (and crab): patrols its platform
      e->vy += GRAV; e->y += e->vy; ex = e->x >> 8; ey = e->y >> 8;
      if (scan(ex, ey, 8, 8, SOLID)) { do ey--; while (scan(ex, ey, 8, 8, SOLID)); e->y = ey << 8; e->vy = 0; }
      e->x += e->vx; ex = e->x >> 8;
      if (scan(ex, ey, 8, 8, SOLID) || (!e->vy && !scan(e->vx > 0 ? ex+8 : ex-1, ey+8, 1, 1, SOLID)))
        e->x -= e->vx, e->vx = -e->vx;
      if (ey > lh*8) e->a = 0;
    } else {           // flyers: 2 bob vertically, 3 sweep horizontally
      int ph = (fr + (e->h >> 9)) & 127, o = (ph < 64 ? ph : 128-ph) - 32;
      if (e->t == 2) e->y = e->h + o*192; else e->x = e->vx + sweep(ph);   // from its patrol, so it can't drift
    }
    ex = e->x >> 8; ey = e->y >> 8;
    if (st < TUBE && ov(X, Y+duck, 6, 11-duck, ex+1, ey+1, 6, 7)) {
      if ((hvy > 0 || st == GPSLAM) && Y+11 < ey+6 && (st == GPSLAM || !worldclaws(e))) {
        kill(e); hvy = k & 16 ? -1000 : -650; st = NORM; arcg = GRAV; launch = 0; cut = 0; capok = diveok = stall = 1; spin = throwt = 0;
      } else die();
    }
    if (e->a && ecap(ex, ey, 8, 8)) capx_enemy(e), kill(e);
  }  estack(x0, n0);
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
      if (c->fy >> 8 > LV[lvl].room[c->room].h*8+16) c->state = 2, c->t = 150;
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
  if (hpt < 99) hpt++;
  if (healt) healt--;
  for (int i = 0; i < L->nmoon; i++)   // moon coins: a 10 px box around each
    if (L->moon[i].room == room && alive && !(wd.moongot >> i & 1) && ov(X - it_moonreach(), Y - it_moonreach(), 6 + 2*it_moonreach(), hh + 2*it_moonreach(), L->moon[i].x*8-1, L->moon[i].y*8-1, 10, 10)) {
      wd.moongot |= 1 << i; sfx(S_MOON); rumble(3);
      burst(L->moon[i].x*8+4, L->moon[i].y*8+4, 0xfff0a0, 10); burst(L->moon[i].x*8+4, L->moon[i].y*8+4, 0xc8d8ff, 8);
      addscore(2000, L->moon[i].x*8+4, L->moon[i].y*8-4);
    }
  for (Check *c = wd.ck; c < wd.ck + wd.nck; c++) {
    if (c->up && c->up < 20) c->up++;
    if (c->room == room && alive && ov(X, Y, 6, hh, c->x*8+1, c->y*8-8, 6, 16)) {
      if (!c->up) {
        haveck = 1; ckroom = room; ckx = (c->x*8+1) << 8; cky = (c->y*8-3) << 8;
        c->up = 20; saved = wd; savedcoins = coins; savedscore = score;   // the respawn snapshot has it raised
        c->up = 1; sfx(S_CHECK);
      }
      if (hp < HPMAX) {   // any checkpoint, new or already raised, fills the hearts
        if (c->up > 1) sfx(S_CHECK);
        hp = HPMAX; healt = 24; sparkle(X+3, Y+2, 0xff7a8a, 10);
      }
    }
  }
  for (const Bar *b = L->bar; b < L->bar + L->nbar; b++) {
    if (b->room != room || !alive) continue;
    int a = barangle(b);
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
    if (here && capon && dwellerhit(d, h, CAPBOX)) {
      if (h->spit) capx_gain(POW_SEED, cx, my);
      d->a = 0; burst(cx, up ? my-d->ofs/2 : my+d->ofs/2, h->spit ? 0xe0586a : 0x2f8f9a, 10); sfx(S_STOMP); rumble(4); kick(5);
      addscore(500, cx, up ? my-d->ofs : my);
    }
  }
  for (Shot *p = wd.sh; p < wd.sh+8; p++) {
    if (!p->a) continue;
    p->vy += 14; p->x += p->vx; p->y += p->vy;
    int sx = p->x >> 8, sy = p->y >> 8;
    if (p->room != room || sy > lh*8+16 || (SOLID >> tile(sx >> 3, sy >> 3) & 1)) { p->a = 0; continue; }
    if (alive && ov(X, Y, 6, hh, sx-2, sy-2, 4, 4)) die();
    if (capon && ov(CAPBOX, sx-2, sy-2, 4, 4)) p->a = 0, burst(sx, sy, 0x9a6a3a, 4), addscore(50, sx, sy);
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
    naming = 0; tomap(); scoreview = 1;
    sfx(S_MENUOK);
  }
}

// ---------- the overworld ----------
// An island with Hatrick's house (the high-score board), one stop per campaign level along a
// winding path, and the movement playground below the house. A level's stop opens once the
// one before it is cleared. It is laid out from the level list, so any number of levels fits.
// Positions are map px: the map is 144 high and as wide as the levels need.
enum { N_HOUSE, N_LEVEL, N_PLAY };
typedef struct { int x, y, kind, lvl; } Node;
static Node *node;
static int nnode, mapw;
static int mapgen;           // the level list the map was built for (levelgen)
static u8 *land;             // per map px: 0 sea, 1 shallows, 2 sand, 3 grass
static int mapat, mapto = -1, mapt, mapgoal = -1, unlockt, unlocknode = -1, pausesel;
static int trav, travsel, travtop, travnav, travrep;   // travel.h: the quick level select is open, the row picked, the top row shown
static int trav_tick(int k, int pr);
static void trav_open(void);
static void trav_render(void);
static int edgeoff(int a, int b) { return (a + b) & 1 ? 7 : -7; }   // how far a path between two stops bows
static void pathpt(int a, int b, int t, int *x, int *y) {   // t 0..256 from stop a to stop b
  *x = node[a].x + (node[b].x - node[a].x) * t / 256;
  *y = node[a].y + (node[b].y - node[a].y) * t / 256 + SIN[t >> 1] * edgeoff(a, b) / 256;
}
static int nodeof(int l) { return l >= 0 && l < NLV ? 1 + l : cl_issecret(l) ? 1 + NLV + l - SEC0 : l == PLAY && PLAY >= 0 ? nnode - 1 : 0; }
static int nodeopen(int n) { return node[n].kind != N_LEVEL || node[n].lvl == 0 || (node[n].lvl >= NLV ? cl_secretopen(node[n].lvl) : cleared(node[n].lvl - 1)); }
static int neighbours(int n, int *out) {   // the stops a path leads to from n
  int k = 0;
  if (node[n].kind == N_HOUSE) { if (NLV) out[k++] = 1; if (PLAY >= 0) out[k++] = nnode - 1; }
  else if (node[n].kind == N_PLAY) out[k++] = 0;
  else if (node[n].lvl >= NLV) out[k++] = cl_parentnode(node[n].lvl);   // a secret stop: back to its level
  else { out[k++] = n - 1; if (n < NLV) out[k++] = n + 1; k = cl_branches(n, out, k); }
  return k;
}
static int cmpx(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
static void mapbuild(void) {
  if (mapgen == levelgen && node) return;
  mapgen = levelgen; free(node); free(land);
  nnode = 1 + NLV + NSEC + (PLAY >= 0);
  node = calloc(nnode, sizeof *node);
  node[0] = (Node){ 28, 70, N_HOUSE, -1 };
  for (int i = 0; i < NLV; i++) node[1+i] = (Node){ 80 + i*56, 74 + SIN[(i*80 + 70) & 255] * 22 / 256, N_LEVEL, i };
  cl_mapnodes();
  if (PLAY >= 0) node[nnode-1] = (Node){ 40, 120, N_PLAY, PLAY };
  mapw = NLV ? node[NLV].x + 56 : 0; if (mapw < W) mapw = W;
  land = calloc(mapw * H, 1);
  int np = 0, maxp = nnode * 64, (*isle)[2] = malloc(sizeof *isle * maxp);   // points the island is grown around
  for (int n = 0; n < nnode; n++) {
    isle[np][0] = node[n].x; isle[np++][1] = node[n].y;
    int nb[4], k = neighbours(n, nb);
    for (int j = 0; j < k; j++) if (nb[j] > n) for (int t = 16; t < 256 && np < maxp; t += 16) pathpt(n, nb[j], t, &isle[np][0], &isle[np][1]), np++;
  }
  qsort(isle, np, sizeof *isle, cmpx);   // by x: a column only looks at the points close enough to matter
  for (int x = 0, lo = 0; x < mapw; x++) {
    while (lo < np && isle[lo][0] < x - 38) lo++;   // the coast reaches at most 37 px from a point
    for (int y = 0; y < H; y++) {
      int best = 1 << 30;
      for (int i = lo; i < np && isle[i][0] <= x + 38; i++) { int dx = x - isle[i][0], dy = (y - isle[i][1]) * 3 / 2, d = dx*dx + dy*dy; if (d < best) best = d; }
      int r = 24 + (SIN[(x*5 + y*3) & 255] + SIN[(x*2 - y*7) & 255]) / 96;   // a ragged coast
      land[y*mapw + x] = best < r*r ? 3 : best < (r+4)*(r+4) ? 2 : best < (r+8)*(r+8) ? 1 : 0;
    }
  }
  free(isle);
}
static void mapstart(void) {   // Hatrick stands at the first level not cleared yet (or the last one)
  mapbuild();
  mapat = NLV ? nodeof(NLV-1) : 0;
  for (int i = NLV-1; i >= 0; i--) if (!cleared(i)) mapat = nodeof(i);
  mapto = mapgoal = -1;
}
static void startlevel(int l) {
  if (l == 0) deaths = coins = lcoins = score = lscore = lstart = tim = 0, runok = 1;   // level 1 starts a new run
  else lcoins = coins, lscore = score, lstart = tim;
  if (l && l < NLV && l != runnext) runok = 0;   // out of order (a replay, a skip): no high score this run
  lvl = l; done = donet = shake = rumq = 0; scoreview = 0; it_level();
  for (P *p = pt; p < pt+NP; p++) p->l = 0;
  menu = 0; resumable = 1; load(); sfx(S_MENUOK);
}
static void tomap(void) {   // back on the map, at the stop of the level just played
  mapbuild();
  menu = 1; resumable = 0; done = donet = 0; menufr = 0;
  mapat = nodeof(lvl); mapto = mapgoal = -1;
  if (firstclear && lvl < NLV-1) unlocknode = mapat + 1, unlockt = 70;   // the path to the next stop opens
  firstclear = 0;
  cl_tomap();
}
static void quitlevel(void) {   // left without clearing it: what was picked up there (and playground time) doesn't count
  score = lscore; coins = lcoins;
  if (lvl == PLAY) tim = lstart;
  tomap();
}
static void finishrun(void) {   // the end-of-run screen cut short: a qualifying score still gets its initials
  menu = 0; hiload();
  if (runok && hiqualifies(score)) naming = 1, namepos = 0, menunav = 0, menufr = 0, sfx(S_BONUS);
  else tomap();
}
static void mapenter(void) {
  const Node *n = node + mapat;
  if (n->kind == N_HOUSE) cl_house();
  else startlevel(n->lvl);
}
static int mapstep(int from, int to) {   // the next stop on the way from one stop to another (the paths form a tree)
  int seen[nnode], prev[nnode], q[nnode], h = 0, t = 0, nb[4];
  for (int i = 0; i < nnode; i++) seen[i] = 0;
  q[t++] = from; seen[from] = 1;
  while (h < t) {
    int n = q[h++], k = neighbours(n, nb);
    for (int j = 0; j < k; j++) if (!seen[nb[j]] && nodeopen(nb[j])) seen[nb[j]] = 1, prev[nb[j]] = n, q[t++] = nb[j];
  }
  if (!seen[to] || to == from) return -1;
  while (prev[to] != from) to = prev[to];
  return to;
}
static void mapwalk(int to) { if (to >= 0 && to != mapat && nodeopen(to)) mapto = to, mapt = 0, sfx(S_MENUMOVE); }
static void mapclick(int n) {   // the mouse: a stop clicked on the map
  if (n < 0 || mapto >= 0) { if (n >= 0) mapgoal = n; return; }
  if (n == mapat) mapenter();
  else if (mapstep(mapat, n) >= 0) mapgoal = n, mapwalk(mapstep(mapat, n));
}
static void maptick(int k, int pr) {
  mapbuild();
  menufr++;
  if (unlockt) unlockt--;
  if (cl_maptick(k, pr)) return;   // Hatrick's house is open
  if (trav_tick(k, pr)) return;     // travel.h: the level list is open
  if (scoreview) {   // the table: any button goes back to the map
    if (pr & (16|32|START|BACK|MENUBACK)) scoreview = 0, hinew = -1, sfx(S_MENUBACK);
    return;
  }
  if (mapto >= 0) {   // walking along a path
    int dx = node[mapto].x - node[mapat].x, dy = node[mapto].y - node[mapat].y, len = iabs(dx) + iabs(dy);
    if ((mapt += len > 0 ? 400 / len + 1 : 256) >= 256) {
      mapat = mapto; mapto = -1; mapt = 0;
      if (mapgoal >= 0 && mapgoal != mapat) mapwalk(mapstep(mapat, mapgoal)); else mapgoal = -1;
    }
    return;
  }
  if (pr & BACK) { quitting = 1; return; }   // Esc on the map quits, as on any title screen
  if (pr & START) { trav_open(); return; }   // travel.h: the quick level select
  if (pr & 16) { mapenter(); return; }
  if (pr & PRACTICE && PLAY >= 0) { startlevel(PLAY); return; }
  int axis = moveaxis(k), dx = axis > 128 ? 1 : axis < -128 ? -1 : 0, dy = k & 4 ? -1 : k & 8 ? 1 : 0;
  if (dx || dy) {   // the path that leaves most nearly in the held direction
    int nb[4], n = neighbours(mapat, nb), best = -1, bestdot = 0;
    for (int j = 0; j < n; j++) {
      int vx = node[nb[j]].x - node[mapat].x, vy = node[nb[j]].y - node[mapat].y, len = iabs(vx) + iabs(vy) + 1;
      int dot = (vx*dx + vy*dy) * 256 / len;
      if (nodeopen(nb[j]) && dot > 90 && dot > bestdot) best = nb[j], bestdot = dot;
    }
    mapgoal = -1; mapwalk(best);
  }
}
// The pause screen over the frozen level: continue, or back to the map.
static void openmenu(int k) {
  menu = 1; pausesel = 0; menufr = 0; sfx(S_PAUSE);
  menunav = k & 12 ? (k & 8 ? 1 : -1) : 0;   // a direction held while pausing doesn't move the choice
  menurepeat = 18;
}
static void pausetick(int k, int pr) {
  menufr++;
  if (it_bag) { it_bagtick(k, pr); return; }   // items.h: the bag
  if (pr & (BACK|MENUBACK|32)) { menu = 0; sfx(S_MENUBACK); return; }
  if (pr & (16|START)) {
    if (pausesel == 2) done ? finishrun() : quitlevel(), sfx(S_MENUOK);
    else if (pausesel == 1) it_openbag();
    else menu = 0, sfx(S_MENUBACK);
    return;
  }
  int axis = moveaxis(k), nav = k & 12 ? (k & 8 ? 1 : -1) : axis > 128 ? 1 : axis < -128 ? -1 : 0;
  if (nav && (nav != menunav || --menurepeat <= 0)) {
    pausesel = (pausesel + nav + 3) % 3; sfx(S_MENUMOVE);
    menurepeat = nav != menunav ? 18 : 6;
  }
  menunav = nav;
}
static void menutick(int k, int pr) { if (resumable) pausetick(k, pr); else maptick(k, pr); }
static void tick(int k) {
  int pr = k & ~prevk;
  prevk = k;
  if (k & CAP2) k |= 32;
  if (pr & CAP2) pr |= 32;   // pressing the other face button is a real new action
  if (pr & 128) muted ^= 1;
  if (pr & QUIT) { quitting = 1; return; }
  if (gim_keys(k, pr)) return;   // gimmicks.h: F2 the gallery, F3 a coin rush
  if (naming) { nametick(k, pr); return; }
  if (menu) { menutick(k, pr); return; }
  if (pr & (BACK|START)) { openmenu(k); return; }
  if (pr & PRACTICE && PLAY >= 0) {   // F1: the playground and back
    if (done) finishrun(); else if (lvl == PLAY) quitlevel(); else score = lscore, coins = lcoins, startlevel(PLAY);
    return;
  }
  if (pr & 64) { if (done) finishrun(); else load(); return; }
  fr++;
  if (!done) tim++;
  else if (++donet == 150 && lvl < NLV) { hiload(); if (runok && hiqualifies(score)) naming = 1, namepos = 0, menunav = 0, menufr = 0, sfx(S_BONUS); }
  else if (donet >= 330) { tomap(); return; }   // no new high score: back to the map
  if (shake) shake--;
  oldhy = hy;
  if (gim_pre(k, pr)) return;   // gimmicks.h: platforms; a door turning the room holds the game
  objects();
  hero(k, pr);
  capupd(k);
  enemies(k);
  hazards();
  cl_tick();
  it_tick();   // items.h
  bosstick();
  worldtick();   // clock blocks, the avalanche
  gim_post(k, pr);   // gimmicks.h
  if (st < DEAD && !done && left > (st == TUBE)) {   // the level timer (it holds at 1 in a tube, where Hatrick can't die)
    if (--left == 100*60) sfx(S_HURRY);
    if (!left) timeout = 1, die();
  }
  for (Pop *p = pops; p < pops+12; p++) if (p->t) p->t--;
  if (bumpt) bumpt--;
  for (P *p = pt; p < pt+NP; p++) if (p->l) p->l--, p->x += p->vx, p->y += p->vy, p->vy += p->g;
  if (done && !(fr & 15)) {   // fireworks: a ring of sparks in one colour
    static const u32 FW[5] = { 0xff5050, 0xffd84a, 0x5af07a, 0x60d8ff, 0xff80e0 };
    int x = rnd(W-40)+20 + (cxf >> 8) << 8, y = rnd(50)+12 + (cyf >> 8) << 8;
    u32 c = FW[rnd(5)];
    for (int i = 0; i < 24; i++) part(x, y, SIN[(i*32/3+64) & 255] * (3+rnd(2)), SIN[i*32/3 & 255] * (3+rnd(2)), 40+rnd(20), 6, c);
  }
  gim_again(k);   // gimmicks.h: the speed flip runs a second step
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
// ---------- graphics: built in, or replaced by PNGs in assets/gfx/ (see MODDING.md) ----------
// Pixels are 0xAARRGGBB; an image without pixels means the built-in art. Alpha under 128 is
// transparent; opaque pure black is stored as 0x010101 because 0 means "nothing" to the drawing.
typedef struct { int w, h; u32 *px; } Img;
static Img G_TILES, G_HERO, G_WALKER, G_BUZZER, G_DWELLER, G_CAP, G_MOON, G_SKY, G_CAVE;
static Img G_SKY2;   // sky2.png: an optional nearer layer over sky.png at half speed; where it is transparent the sky shows
// Tile sheet cells (8x8 each, 8 per row of tiles.png): the order is part of the modding format.
enum { CELL_GROUND_TOP, CELL_GROUND, CELL_BRICK, CELL_SPIKES, CELL_SPIKES_DOWN, CELL_STONE, CELL_SPRING,
       CELL_SLOPE_UP, CELL_SLOPE_DOWN, CELL_FOUND, CELL_CRUMBLE, CELL_PIVOT, CELL_COIN,   /* coin: 4 frames */
       CELL_TUBE_L = CELL_COIN + 4, CELL_TUBE_R, CELL_TUBE_T, CELL_TUBE_B,
       CELL_MOUTH,   /* 8: opening up (left, right half), down (l, r), left (top, bottom), right (t, b) */
       CELL_EMBER = CELL_MOUTH + 8, CELL_SEED, NCELL };
static const u16 *const HFRAMES[] = { HSTAND, HRUN1, HRUN2, HJUMP, HTHROW, HHANG, HCROUCH, HSPIN, HPOSE, HROLL };
// Where a sprite's frame is in a loaded image: pixels of its top row, the row stride, the rows to skip.
static const u32 *spriteimg(const u16 *s, const u32 *spit, int h, int *stride) {
  const Img *g = 0; int i = 0, top = 0;
  for (int k = 0; k < 10; k++) if (s == HFRAMES[k]) g = &G_HERO, i = k, top = 12 - h + (capless && G_HERO.h >= 24 ? 12 : 0);
  if (s == GRUM1 || s == GRUM2) g = &G_WALKER, i = s == GRUM2;
  if (s == BUZZ1 || s == BUZZ2) g = &G_BUZZER, i = s == BUZZ2;
  if (s == SNAP1 || s == SNAP2) g = &G_DWELLER, i = (s == SNAP2) + (spit ? 2 : 0);
  if (s == CAP) g = &G_CAP;
  if (!g || !g->px) return 0;
  *stride = g->w;
  return g->px + top*g->w + i*8;
}
// An 8-wide sprite with its feet at (fx, fy) in 1/256 world px, rotated by ang (256 = one
// turn, clockwise, about its centre) and scaled by sx, sy (256 = 1, about its feet).
static const u32 SPITPAL[3];
static void sprx(const u16 *s, int h, int fx, int fy, int fl, int ang, int sx, int sy, const u32 *pal) {
  int co = SIN[(ang+64) & 255], si = SIN[ang & 255], ux = SC*sx, uy = SC*sy, stride = 0;
  const u32 *img = spriteimg(s, pal == SPITPAL ? pal : 0, h, &stride);
  int cx = fx*SC - (ox << 8), cy = fy*SC - (oy << 8) - h*uy/2;   // centre, 1/256 screen px
  int r = (h > 8 ? h : 8) * (ux > uy ? ux : uy) / 256 * 3 / 4 + 2;
  for (int ys = (cy >> 8) - r; ys <= (cy >> 8) + r; ys++) {
    if ((unsigned)ys >= SH) continue;
    for (int xs = (cx >> 8) - r; xs <= (cx >> 8) + r; xs++) {
      if ((unsigned)xs >= SW) continue;
      int dx = (xs << 8) + 128 - cx, dy = (ys << 8) + 128 - cy;
      int u = ((dx*co + dy*si) / ux + 4*256) >> 8, v = ((dy*co - dx*si) / uy + h*128) >> 8;
      if ((unsigned)u > 7 || (unsigned)v >= h) continue;
      if (img) { u32 p = img[v*stride + (fl ? 7-u : u)]; if (p >> 24 >= 128) big[ys][xs] = p & 0xffffff; continue; }
      int c = s[v] >> (14 - 2*(fl ? 7-u : u)) & 3;
      if (c) big[ys][xs] = capless && v >= capoff && v < capoff+3 && c == 1 ? 0x5a3018 : pal[c-1];
    }
  }
}
static u32 HPAL[3] = { 0xff7a1c, 0xffcc99, 0x1f4f5f };
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
static void hudtext(const char *s,int x,int y,u32 c) {   // bold menu type with a thick dark outline, for the HUD
  for(int dy=-3;dy<=4;dy++)for(int dx=-3;dx<=4;dx++)
    if((dx<0?-dx:dx>1?dx-1:0)+(dy<0?-dy:dy>1?dy-1:0)>=2&&(dx<0?-dx:dx>1?dx-1:0)+(dy<0?-dy:dy>1?dy-1:0)<=3)menutext(s,x+dx,y+dy,1,0x14100c);
  for(int i=0;i<4;i++)menutext(s,x+(i&1),y+(i>>1),1,c);
}
// One of the hearts under the clock (menu canvas px, cx, cy its middle). full: still there. fx > 0:
// frames since this one was lost (it jumps and flashes white); -1: just refilled (a pink glow).
static void hudheart(int cx, int cy, int full, int fx) {
  if (fx > 0) cy -= (24 - fx) * (fx < 8 ? fx : 8) / 24, cx += fx < 12 && fx & 2 ? 2 : 0;
  u32 fill = fx > 0 && fx < 10 ? 0xffffff : full ? (hp == 1 && fr & 32 ? 0xff7080 : 0xf0303e) : 0x4a3a46;
  for (int pass = 0; pass < 2; pass++) {   // a dark outline, then the fill
    int g = pass ? 0 : 3; u32 c = pass ? fill : 0x14100c;
    mellipse(cx-6, cy-4, 7+g, 7+g, c); mellipse(cx+6, cy-4, 7+g, 7+g, c);
    for (int y = -4; y <= 11+g; y++) { int w = (13+g) * (11+g - y) / (15+g); if (w > 0) mrect(cx-w, cy+y, 2*w, 1, c); }
  }
  if (full) mellipse(cx-7, cy-6, 2, 3, fx < 0 ? 0xffffff : 0xffb0b8);   // a shine
  if (fx < 0 && fr & 4) mellipse(cx, cy, 3, 3, 0xffd0d8);
}
static void mhat(int x,int y) {
  mround(x+3,y,20,10,4,0x733431);mround(x+4,y+1,18,8,3,0xf07b35);
  mround(x,y+7,30,5,2,0x733431);mround(x+1,y+7,28,3,1,0xffa449);
  mrect(x+7,y+3,9,1,0xffce77);mrect(x+5,y+6,17,2,0xa5482d);
}
static void darken(int keep) {   // dims the whole picture to keep/256, under a panel
  for (int y = 0; y < SH; y++) for (int x = 0; x < SW; x++) {
    u32 c = big[y][x];
    big[y][x] = ((c >> 16 & 255) * keep >> 8) << 16 | ((c >> 8 & 255) * keep >> 8) << 8 | (c & 255) * keep >> 8;
  }
}
static void plaque(int x, int y, int w, int h) {   // a gold-edged navy panel (menu canvas px)
  mround(x+3, y+4, w, h, 8, 0x0e1424); mround(x, y, w, h, 8, 0xffd894); mround(x+3, y+3, w-6, h-6, 6, 0xa67150);
  mround(x+5, y+5, w-10, h-10, 5, 0x2c4162);
}
// The high-score board over the map: the table, or the initials being entered.
static void menuscores(void) {
  char line[32];
  const char *title=naming?"NEW HIGH SCORE":"HIGH SCORES";
  darken(110);
  plaque(204,22,360,62); hudtext(title,(MENUW-textwidth(title,1))/2,42,0xffd894);
  plaque(150,100,468,300);
  if(naming) {
    snprintf(line,sizeof line,"%d",score);
    centered(line,150,2,0xffd894);
    for(int i=0;i<3;i++) {
      int x=384-75+i*57,y=250,active=i==namepos,bob=active?SIN[(menufr*6)&255]*3/256:0;
      char c[2]={initials[i],0};
      mround(x-6,y-8,48,58,6,active?0xffdb87:0x46607e);mround(x-4,y-6,44,54,5,active?0xeaaa5d:0x34506e);
      menutext(c,x+3,y+2+bob,2,active?0xfff3d1:0xc8d4dc);
      if(active)for(int j=0;j<6;j++){mrect(x+18-j,y-16+j,2*j+1,1,0xffd894);mrect(x+18-j,y+62-j,2*j+1,1,0xffd894);}
    }
    return;
  }
  if(!nhi){centered("NO SCORES YET",240,1,0xc8d4dc);return;}
  for(int i=0;i<nhi;i++) {
    int y=118+i*27; u32 c=i==hinew?0xffd894:0xf2efe0;
    if(i==hinew)mrect(170,y-4,428,26,0x46607e);
    snprintf(line,sizeof line,"%d.",i+1);menutext(line,200+(i<9?18:0),y,1,c);
    menutext(hi[i].ini,270,y,1,c);
    snprintf(line,sizeof line,"%d",hi[i].score);menutext(line,570-textwidth(line,1),y,1,c);
  }
}
// The pause screen: the level stays in view, dimmed, under three choices.
static void pauserender(void) {
  static const char *const OPT[3] = { "CONTINUE", "ITEMS", "EXIT TO MAP" };
  if (it_bag) { it_bagrender(); return; }   // items.h
  darken(120);
  plaque(234,70,300,290);
  hudtext("PAUSED",(MENUW-textwidth("PAUSED",1))/2,96,0xffd894);
  for (int i = 0; i < 3; i++) {
    int y = 150 + i*62, on = i == pausesel;
    mround(270, y, 228, 44, 6, on ? 0xffdb87 : 0x46607e); mround(272, y+2, 224, 40, 5, on ? 0xeaaa5d : 0x34506e);
    menutext(OPT[i], 384 - textwidth(OPT[i], 1)/2, y+12, 1, on ? 0xfff3d1 : 0xc8d4dc);
    if (on) mhat(232, y+12 + SIN[(menufr*5) & 255]*2/256);
  }
}
static int pausehit(int x, int y) {   // which choice is under the mouse (window px), or -1
  x = x * MENUW / SW; y = y * MENUH / SH;
  for (int i = 0; i < 3; i++) if (x >= 270 && x < 498 && y >= 150 + i*62 && y < 194 + i*62) return i;
  return -1;
}

// ---------- drawing the overworld (map px; the camera scrolls along it) ----------
static int mapcam;
static void mrectw(int x, int y, int w, int h, u32 c) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) wpx(x+i, y+j, c); }
static void mdisc(int cx, int cy, int r, u32 c) { for (int y = -r; y <= r; y++) for (int x = -r; x <= r; x++) if (x*x + y*y <= r*r + r) wpx(cx+x, cy+y, c); }
static void landmark(const Node *n, int open) {   // the little scene beside each stop, after its level's card
  int x = n->x + 7, y = n->y - 4, card = n->kind == N_HOUSE ? -1 : n->kind == N_PLAY ? CARD_PLAYGROUND : LV[n->lvl].card;
  u32 dim = open ? 0 : 1;
  #define C(c) (dim ? (((c) >> 1) & 0x7f7f7f) + 0x303840 : (c))
  if (card == -1) {   // Hatrick's house: a cap for a roof
    mrectw(x, y-8, 13, 9, C(0x3a2410)); mrectw(x+1, y-7, 11, 8, C(0xf4e6c8));
    for (int j = 0; j < 6; j++) mrectw(x+6-j-(j > 4), y-15+j, 1+2*j+2*(j > 4), 1, C(j ? 0xff7a1c : 0x9a3c10));   // the crown
    mrectw(x-2, y-9, 17, 2, C(0xc85a14)); mrectw(x-2, y-9, 17, 1, C(0xffa449));                                  // the brim
    mrectw(x+5, y-4, 3, 5, C(0x6a3a1a)); mrectw(x+2, y-6, 2, 2, C(0x6ab0e0)); mrectw(x+9, y-6, 2, 2, C(0x6ab0e0));
  } else if (card == CARD_HILLS) {
    for (int i = -7; i <= 7; i++) { int h = 7 - i*i/8; mrectw(x+7+i, y-h, 1, h+1, C(i < -2 ? 0x8be05a : 0x4cb83c)); }
    mrectw(x+11, y-15, 1, 10, C(0xd8dde4)); mrectw(x+12, y-15, 4, 2, C(0xe8403a)); mrectw(x+12, y-13, 2, 1, C(0xe8403a));
  } else if (card == CARD_BRICKS) {
    for (int j = 0; j < 14; j++) for (int i = 0; i < 10; i++)
      wpx(x+2+i, y-13+j, C(j % 4 == 3 || (i + (j/4 & 1)*3) % 5 == 4 ? 0x5a2410 : j % 4 == 0 ? 0xf09060 : 0xd0602a));
    for (int i = 0; i < 10; i += 3) mrectw(x+2+i, y-15, 2, 2, C(0xd0602a));
  } else if (card == CARD_SPIKES) {
    for (int k = 0; k < 3; k++) { int h = 9 - (k == 1)*3; for (int j = 0; j < h; j++) { int w = (h - j) / 2; mrectw(x+3+k*5 - w, y - j, 2*w+1, 1, C(j == 0 ? 0x586070 : 0x9aa4b0)); wpx(x+3+k*5 - w, y - j, C(0xe0e6ee)); } }
  } else if (card == CARD_SKY) {
    mdisc(x+4, y-10, 3, C(0xffffff)); mdisc(x+8, y-12, 4, C(0xffffff)); mdisc(x+12, y-10, 3, C(0xffffff)); mrectw(x+3, y-9, 11, 3, C(0xffffff));
    mrectw(x+3, y-3, 10, 3, C(0x9098a8)); mrectw(x+3, y-3, 10, 1, C(0xc8d0e0));
  } else if (card == CARD_CASTLE) {
    mrectw(x, y-14, 18, 15, C(0x586070)); mrectw(x+1, y-13, 16, 14, C(0x9098a8));
    for (int i = 0; i < 18; i += 4) mrectw(x+i, y-17, 2, 3, C(0x9098a8));
    mrectw(x+7, y-6, 4, 7, C(0x262a32)); mrectw(x+8, y-24, 1, 8, C(0xd8dde4)); mrectw(x+9, y-24, 5, 3, C(0xe8403a));
    mrectw(x+3, y-10, 2, 2, C(0x262a32)); mrectw(x+13, y-10, 2, 2, C(0x262a32));
  } else if (card == CARD_PLAYGROUND) {
    for (int i = 0; i < 12; i++) mrectw(x+i, y - i*2/3, 1, i*2/3 + 1, C(i == 11 ? 0x8be05a : 0x4cb83c));
    mrectw(x+13, y-5, 6, 6, C(0x9098a8)); mrectw(x+13, y-5, 6, 1, C(0xc8d0e0));
  } else worldlandmark(card, x, y, dim);   // the new worlds' cards
  #undef C
}
static void maprender(void) {
  mapbuild();
  int hxm, hym;   // Hatrick on the map
  if (mapto >= 0) pathpt(mapat, mapto, mapt, &hxm, &hym); else hxm = node[mapat].x, hym = node[mapat].y;
  int want = hxm - W/2; if (want > mapw - W) want = mapw - W; if (want < 0) want = 0;
  mapcam += (want - mapcam) / 6 + (want > mapcam) - (want < mapcam);
  ox = mapcam * SC; oy = 0; capless = capoff = 0;
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {   // sea, shallows, beach and grass
    int mx = x + mapcam, l = mx < mapw ? land[y*mapw + mx] : 0;
    u32 c = l == 3 ? ((mx*7 + y*13) % 29 ? 0x5cb860 : 0x4ca850) : l == 2 ? 0xf0d898 : l == 1 ? 0x6cc0e8 : 0x3a8fd8;
    if (l == 0 && ((mx + menufr/6 + (y/6)*11) % 41) < 4 && y % 6 == 0) c = 0x8fd0ff;   // waves
    if (l == 3 && y+1 < H && land[(y+1)*mapw + mx] == 2) c = 0x3e8f48;                 // a grass edge
    wpx(mx, y, c);
  }
  for (int i = 0; i < mapw / 5; i++) {   // trees, away from the paths and stops
    u32 hsh = i * 2654435761u; int x = hsh % mapw, y = 12 + (hsh >> 12) % 120;
    if (land[y*mapw + x] != 3 || land[(y+6)*mapw + x] != 3) continue;
    int clear = 1;
    for (int n = 0; n < nnode && clear; n++) {
      if (iabs(x - node[n].x) < 26 && iabs(y - node[n].y) < 22) clear = 0;
      int nb[4], k = neighbours(n, nb);
      for (int j = 0; j < k && clear; j++) for (int t = 0; t <= 256; t += 16) { int px, py; pathpt(n, nb[j], t, &px, &py); if (iabs(px - x) < 7 && iabs(py - y) < 9) clear = 0; }
    }
    if (!clear) continue;
    mrectw(x-2, y+3, 5, 1, 0x3e8f48); mrectw(x, y, 1, 3, 0x6a3a1a);
    mdisc(x, y-2, 3, 0x2e7a3c); mdisc(x-1, y-3, 2, 0x4cb83c); wpx(x-2, y-4, 0x8be05a);
  }
  for (int n = 0; n < nnode; n++) {   // paths: sandy dots; the newest one draws itself in
    int nb[4], k = neighbours(n, nb);
    for (int j = 0; j < k; j++) {
      int a = n, b = nb[j];
      if (b < a || !nodeopen(a) || !nodeopen(b)) continue;
      int upto = b == unlocknode && unlockt ? 256 - unlockt * 256 / 70 : 256;
      for (int t = 12; t <= 244 && t <= upto; t += 14) { int px, py; pathpt(a, b, t, &px, &py); mrectw(px-1, py, 2, 2, 0xb8984c); mrectw(px-1, py-1, 2, 2, 0xf8e8b0); }
      if (upto < 256) { int px, py; pathpt(a, b, upto, &px, &py); mdisc(px, py, 2, 0xfff3b0); }
    }
  }
  for (int n = 0; n < nnode; n++) if (cl_visible(n)) landmark(node + n, nodeopen(n));
  for (int n = 0; n < nnode; n++) {   // the stops: red to play, gold once cleared, grey still locked
    const Node *d = node + n;
    if (!cl_visible(n)) continue;
    int open = nodeopen(n), done_ = d->kind == N_LEVEL && cleared(d->lvl), x = d->x, y = d->y;
    mdisc(x, y+1, 5, 0x1a2a20); mdisc(x, y, 5, 0x24303c);
    mdisc(x, y, 4, d->kind != N_LEVEL ? 0x5a8ad0 : !open ? 0x7a8496 : done_ ? 0xffd84a : 0xe8403a);
    mdisc(x-1, y-1, 1, !open ? 0x9aa4b0 : 0xffffff);
    if (done_) { mrectw(x+2, y-9, 1, 7, 0xd8dde4); mrectw(x+3, y-9, 3, 2, 0x2ec85a); }
  }
  {   // Hatrick, walking the run cycle along a path or idling on a stop
    int walking = mapto >= 0, f = walking ? (menufr >> 3 & 1) : 0;
    if (walking) face = node[mapto].x > node[mapat].x ? 1 : node[mapto].x < node[mapat].x ? -1 : face;
    mrectw(hxm-3, hym+1, 7, 2, 0x24303c);
    sprx(walking ? (f ? HRUN2 : HRUN1) : HSTAND, 12, (hxm << 8) + 128, (hym + 1 - (walking && f)) << 8, face < 0, 0, 256, 256, HPAL);
  }
  // the banner: where Hatrick stands
  if (!naming && !scoreview) {
    const Node *d = node + (mapto >= 0 ? mapto : mapat);
    char t[64];
    if (d->kind == N_HOUSE) snprintf(t, sizeof t, "HOME");
    else if (d->kind == N_PLAY) snprintf(t, sizeof t, "PLAYGROUND");
    else if (d->lvl >= NLV) snprintf(t, sizeof t, "SECRET %s", LV[d->lvl].name);
    else snprintf(t, sizeof t, "%d %s", d->lvl + 1, LV[d->lvl].name);
    int w = textwidth(t, 1), nm = d->kind == N_LEVEL ? LV[d->lvl].nmoon : 0, pw = w + 60 + nm*26;
    if (pw > 720) pw = 720;
    plaque((MENUW - pw)/2, 12, pw, 52);
    hudtext(t, (MENUW - pw)/2 + 30, 27, d->kind == N_LEVEL && cleared(d->lvl) ? 0xffd894 : 0xffffff);
    for (int i = 0; i < nm; i++) {   // the level's moon coins: gold once brought home
      int got = moonbits(d->lvl) >> i & 1, mx = (MENUW - pw)/2 + 30 + w + 22 + i*26, my = 38;
      mellipse(mx, my, 10, 10, 0x0e1424); mellipse(mx, my, 8, 8, got ? 0xdbe6ff : 0x46607e);
      if (got) { mellipse(mx-2, my, 5, 6, 0xf2c440); mellipse(mx+1, my-2, 5, 5, 0xdbe6ff); }
    }
  }
  hudtext("HATRICK", 22, 392, 0xfff3d1);
  if (naming || scoreview) menuscores();
  cl_maprender();
  trav_render();
}
static int maphit(int x, int y) {   // the stop under the mouse (window px), or -1
  int mx = x / SC + mapcam, my = y / SC;
  for (int n = 0; n < nnode; n++) if (iabs(mx - node[n].x) <= 7 && iabs(my - node[n].y) <= 7) return n;
  return -1;
}

// Brass tubes: shading across a 16 px wide tube (a = 0..15), and the bands around it every 16 px.
#include "collect_ui.h"
#define ITEMS_UI
#include "items.h"   // the shop, the bag, the items in the level
#undef ITEMS_UI
#include "travel.h"   // Start on the map: the quick level select

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
// Which sheet cell a tile shows, from its neighbours (grass on top, hanging spikes, tube halves).
static int cellof(int t, int tx, int ty) {
  int up = SOLID >> tile(tx, ty-1) & 1, n = 0;
  switch (t) {
    case 1: return up ? CELL_GROUND : CELL_GROUND_TOP;
    case 2: return CELL_BRICK;
    case 3: return up && !(SOLID >> tile(tx, ty+1) & 1) ? CELL_SPIKES_DOWN : CELL_SPIKES;
    case 4: return CELL_STONE;
    case 5: return CELL_SPRING;
    case 6: return CELL_COIN + (fr >> 3 & 3);
    case 8: return CELL_SLOPE_UP;
    case 9: return CELL_SLOPE_DOWN;
    case 10: while (tile(tx-1-n, ty) == 10) n++; return n & 1 ? CELL_TUBE_R : CELL_TUBE_L;
    case 11: while (tile(tx, ty-1-n) == 11) n++; return n & 1 ? CELL_TUBE_B : CELL_TUBE_T;
    case 12: { const Tube *m = tubeat(tx, ty);
               return CELL_MOUTH + (m ? m->dir : T_UP)*2 + (m && (m->dir < T_LEFT ? tx > m->x : ty > m->y)); }
    case 13: return CELL_CRUMBLE;
    case 15: return CELL_FOUND;
    case 16: return CELL_PIVOT;
  }
  return -1;   // 14: hidden until found
}
// The built-in art of a cell; tx, ty only add texture (speckles, the bands on tubes).
static u32 cellpx(int cell, int tx, int ty, int u, int v) {
  switch (cell) {
    case CELL_SLOPE_UP: case CELL_SLOPE_DOWN: {
      int surface = cell == CELL_SLOPE_UP ? 7-u : u;
      if (v < surface) return 0;
      return v == surface ? 0x8be05a : v <= surface+2 ? 0x4cb83c : 0xa8642c;
    }
    case CELL_GROUND_TOP:
      if (v < 3 && !(v == 2 && (u*5+tx) % 3 == 0)) return v ? 0x4cb83c : 0x8be05a;
      if (v == 3) return 0x6a3a1a;
      /* fall through */
    case CELL_GROUND: return (u*7+v*3+tx*5+ty*11) % 11 ? 0xa8642c : 0x7a4420;
    case CELL_BRICK:
      if (v == 3 || v == 7 || (v < 3 ? u == 7 : u == 3)) return 0x5a2410;
      return v == 0 || v == 4 ? 0xf09060 : 0xd0602a;
    case CELL_SPIKES: case CELL_SPIKES_DOWN: {
      int vv = cell == CELL_SPIKES_DOWN ? 7-v : v, q = u & 3;
      if (vv == 7) return 0x606870;
      if (vv < (q == 0 || q == 3 ? 4 : 1)) return 0;
      return q < 2 ? 0xe0e6ee : 0x8a94a0;
    }
    case CELL_STONE: return v == 0 || u == 0 ? 0xc8d0e0 : v == 7 || u == 7 ? 0x586070 : 0x9098a8;
    case CELL_SPRING:
      if (v < 3) return v ? 0xe8403a : 0xff8a7a;
      return v & 1 ? 0x9aa4b0 : u > 1 && u < 6 ? 0x606a78 : 0;
    case CELL_COIN: case CELL_COIN+1: case CELL_COIN+2: case CELL_COIN+3: return coinpx(u, v, (cell - CELL_COIN) << 3);
    case CELL_TUBE_L: case CELL_TUBE_R: { int a = u + (cell == CELL_TUBE_R)*8; return a == 0 || a == 15 ? 0 : brass(a, ty*8+v+1); }
    case CELL_TUBE_T: case CELL_TUBE_B: { int a = v + (cell == CELL_TUBE_B)*8; return a == 0 || a == 15 ? 0 : brass(a, tx*8+u+1); }
    case CELL_CRUMBLE:   // a cracked biscuit
      if (u == 0 || v == 0) return 0xf6dca0;
      if (u == 7 || v == 7) return 0x6e4420;
      if ((v == 3 && u < 5) || (u == 4 && v > 3) || (u == 2 && v < 3) || (v == 5 && u > 4)) return 0x9a6630;
      return 0xe2b56a;
    case CELL_FOUND:   // a found hidden block with a little cap stamp
      if (u == 0 || v == 0) return 0xa6dce0;
      if (u == 7 || v == 7) return 0x2e5a66;
      if ((v == 3 || v == 4) && u > 1 && u < 6 && !(v == 4 && (u == 2 || u == 5))) return 0x3c7480;
      return 0x5e9aa6;
    case CELL_PIVOT:   // iron with an ember
      if ((u == 3 || u == 4) && (v == 3 || v == 4)) return (fr >> 2 & 1) ? 0xffe08a : 0xffb040;
      if (u > 1 && u < 6 && v > 1 && v < 6) return 0xc8501c;
      if (u == 0 || v == 0) return 0x7a8290;
      if (u == 7 || v == 7) return 0x262a32;
      return 0x4a4f5a;
    case CELL_EMBER: case CELL_SEED: {   // 4x4 in the middle of the cell
      int k = u - 4, j = v - 4, edge = (k == -2 || k == 1) + (j == -2 || j == 1);
      if (k < -2 || k > 1 || j < -2 || j > 1 || edge == 2) return 0;
      if (cell == CELL_SEED) return k+j == -2 ? 0xe0b070 : (k+j) & 1 ? 0x7a4a24 : 0x5a3418;
      return edge ? 0xff7a20 : 0xffc040;
    }
  }
  if (cell >= CELL_MOUTH && cell < CELL_MOUTH + 8) {   // a wide collar with the dark opening on its open side
    int dir = (cell - CELL_MOUTH) / 2, half = (cell - CELL_MOUTH) & 1, across, along;
    if (dir < T_LEFT) across = u + half*8, along = dir == T_UP ? v : 7-v;
    else across = v + half*8, along = dir == T_LEFT ? u : 7-u;
    if (along == 0 || along == 7) return 0x3e2a10;
    if (along < 3 && across > 1 && across < 14) return along == 1 ? 0x140c04 : 0x2a1a08;
    if (along == 5 && (across == 3 || across == 12)) return 0xfff0b0;
    return across == 0 || across == 15 ? 0x3e2a10 : brass(across, 1);
  }
  return 0;
}
static u32 sheetpx(int cell, int tx, int ty, int u, int v) {   // tiles.png if there is one, else the built-in art
  if (!G_TILES.px) return cellpx(cell, tx, ty, u, v);
  u32 p = G_TILES.px[(cell/8*8 + v)*G_TILES.w + cell%8*8 + u];
  return p >> 24 >= 128 ? p & 0xffffff : 0;
}
static u32 tilepx(int t, int tx, int ty, int u, int v) {
  if (t >= WT_SNOW) return worldtilepx(t, tx, ty, u, v);
  int cell = cellof(t, tx, ty);
  return cell < 0 ? 0 : worldtint(sheetpx(cell, tx, ty, u, v));
}
static u32 shade(u32 c, int k) {   // k/256 of the colour (above 256 brightens toward white)
  return k <= 256 ? mixcolor(0, c, k, 256) : mixcolor(c, 0xffffff, k-256, 256);
}
// Terrain gets the NSMB2 two-tone rim where it meets open air: a dark outer line and a light inner one.
static int outlined(int t) { return t == 1 || t == 2 || t == 4 || t == 13 || t == 15; }
static void drawtile(int t, int tx, int ty, int dx, int dy) {
  int rim = outlined(t);
  int up = rim && !(SOLID >> tile(tx, ty-1) & 1), dn = rim && !(SOLID >> tile(tx, ty+1) & 1);
  int lf = rim && !(SOLID >> tile(tx-1, ty) & 1), rt = rim && !(SOLID >> tile(tx+1, ty) & 1);
  for (int v = 0; v < 8; v++) for (int u = 0; u < 8; u++) {
    u32 c = tilepx(t, tx, ty, u, v);
    if (!c) continue;
    if ((up && v == 0) || (dn && v == 7) || (lf && u == 0) || (rt && u == 7)) c = shade(c, 120);
    else if ((up && v == 1) || (lf && u == 1)) c = shade(c, 320);
    wpx(tx*8+u+dx, ty*8+v+dy, c);
  }
}
static u32 moonpx(int u, int v) {   // the built-in moon coin, u, v in -6..6 from its centre (0: outside)
  int d = u*u + v*v, moon = (u+1)*(u+1) + v*v <= 16 && (u-1)*(u-1) + (v+1)*(v+1) > 10;   // crescent: one disc minus another
  return d > 38 ? 0 : d >= 30 ? 0x3a4a7a : d >= 22 ? 0x8fa8d8 : moon ? (v < -1 ? 0xfff3b0 : 0xf2c440) : 0xdbe6ff;
}
// A moon coin centred at (cx, cy) world px: a silver-blue coin with a gold crescent and a glint.
// One brought home before shows as a faint outline that can still be picked up.
static void drawmoon(int cx, int cy, int ghost) {
  if (G_MOON.px) {   // moon.png, centred; an outline-only ghost uses every other pixel, pale
    for (int v = 0; v < G_MOON.h; v++) for (int u = 0; u < G_MOON.w; u++) {
      u32 p = G_MOON.px[v*G_MOON.w + u];
      if (p >> 24 >= 128 && (!ghost || (u + v) & 1)) wpx(cx - G_MOON.w/2 + u, cy - G_MOON.h/2 + v, ghost ? 0xc8d4f0 : p & 0xffffff);
    }
    return;
  }
  for (int v = -6; v <= 6; v++) for (int u = -6; u <= 6; u++) {
    int d = u*u + v*v;
    if (d > 38) continue;
    if (ghost) { if (d >= 26 && (u + v) & 1) wpx(cx+u, cy+v, 0xc8d4f0); continue; }
    wpx(cx+u, cy+v, moonpx(u, v));
  }
  int g = fr % 90;   // a glint crossing now and then
  if (!ghost && g < 12) for (int k = -1; k <= 1; k++) wpx(cx - 4 + g*2/3 + k, cy - 4 + k, 0xffffff);
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
  bosscam();
  if (cxf > (lw*8-W) * 256) cxf = (lw*8-W) * 256;   // negative in levels narrower than the screen
  if (cxf < 0) cxf = 0;
  if (cyf > (lh*8-H) << 8) cyf = (lh*8-H) << 8;
  if (cyf < 0) cyf = 0;
  gim_camera();   // gimmicks.h: auto-scroll
}

#include "movement_draw.h"
#define GIM_PART 3
#include "gimmicks.h"
#undef GIM_PART
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
  mv_pose(&f, &fl, &ang, &sx, &fx, &fy);
  sprx(f, sh, fx, fy + bob + iabs(vang) * 512 / 64, fl, ang, sx, sy, it_pal(capx_pal(HPAL)));
}
static const u32 SNAPPAL[3] = { 0x2f8f9a, 0xf4e6c0, 0x1b2433 };
// The background behind everything: sky.png / cave.png, or the built-in sky, clouds and hills / cave.
static void drawbg(int cave, int lo) {
  static int cm[SW], fh[SW], nh[SW];
  const Img *bg = cave ? &G_CAVE : &G_SKY;
  if (!cave && !bg->px && worldbg(lo)) return;   // a new world's theme
  if (bg->px) {   // sky.png / cave.png: fixed to the screen vertically, scrolling at a quarter speed
    for (int y = 0; y < SH; y++) {
      const u32 *src = bg->px + (y / SC * bg->h / H) * bg->w;
      for (int x = 0; x < SW; x++) big[y][x] = src[((x + ox/4) / SC) % bg->w] & 0xffffff;
    }
    if (!cave && G_SKY2.px)   // sky2.png over it at half speed; transparent pixels show the sky
      for (int y = 0; y < SH; y++) {
        const u32 *src = G_SKY2.px + (y / SC * G_SKY2.h / H) * G_SKY2.w;
        for (int x = 0; x < SW; x++) { u32 p = src[((x + ox/2) / SC) % G_SKY2.w]; if (p >> 24 >= 128) big[y][x] = p & 0xffffff; }
      }
  } else if (cave) {   // bonus rooms: a dim cave with a few glinting stones
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
        // hills in crayon, Yoshi's Island style: diagonal strokes that wobble, a darker rim on top
        if (yf > fh[x]) { int u = (x + ox/4) / SC; c = (u + yf + (u*7 >> 3 & 1)) % 5 == 0 && yf > fh[x]+2 ? 0x8cc994 : yf == fh[x]+1 ? 0x82bf8b : 0x9ad6a0; }
        if (yn > nh[x]) { int u = (x + ox/2) / SC; c = yn <= nh[x]+1 ? 0x2f7a3a : yn == nh[x]+2 ? 0x3e8f48 :
                                                       (u - yn + (yn*5 >> 2 & 1)) % 4 == 0 ? 0x50a855 : (u*3 + yn*7) % 23 == 0 ? 0x6cc670 : 0x5cb860; }
        row[x] = c;
      }
    }
  }
}
static const u32 SPITPAL[3] = { 0xe0586a, 0xf4e6c0, 0x3a1424 };

#include "worlds.c"
#define CAPX_DRAW
#include "cap.h"
static void render(void) {
  const Level *L = LV + lvl;
  if ((menu && !resumable) || naming) { maprender(); if (!clhouse && !trav) gim_maphud(); return; }
  camera();
  ox = cxf * SC >> 8;
  oy = cyf * SC >> 8;
  if (shake) {   // strongest at first, easing out; mostly vertical, a little sideways
    int a = shake*shake*SC / 30;
    oy += SIN[(fr*96 + 64) & 255] * a >> 8; ox += SIN[fr*57 & 255] * a >> 9;
  }
  int lo = (((lh*8-H) << 8) - cyf) * SC >> 8;     // camera height above the bottom, screen px
  drawbg(L->room[room].cave, lo);
  gim_bg(lo);   // gimmicks.h: 8-bit and night backdrops
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
  capx_draw();
  mv_drawtiles();
  gim_draw();
  for (const Crumble *c = wd.cr; c < wd.cr + wd.ncr; c++)   // falling crumble blocks
    if (c->room == room && c->state == 1) for (int v = 0; v < 8; v++) for (int u = 0; u < 8; u++) wpx(c->x*8+u, (c->fy >> 8)+v, tilepx(13, c->x, c->y, u, v));
  for (int i = 0; i < L->nmoon; i++) if (L->moon[i].room == room && !(wd.moongot >> i & 1))   // moon coins
    drawmoon(L->moon[i].x*8+4, L->moon[i].y*8+4 + (SIN[(fr*3 + i*85) & 255] > 128) - (SIN[(fr*3 + i*85) & 255] < -128), moonbits(lvl) >> i & 1);
  for (const Check *c = wd.ck; c < wd.ck + wd.nck; c++) if (c->room == room && !capx_rack(c)) {   // checkpoints (cap.h draws hat racks): a post and a banner that rises
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
  cl_render();
  for (E *e = en; e < en+ne; e++)
    if (e->a && e->r == room) {
      if (e->t >= T_SHY && e->t <= T_PISTON) { extdraw(e); continue; }
      int lift = hop ? SIN[hop/2] * 3 : 0, stretch = hop ? SIN[hop/2] / 6 : 0;   // hop on the "bah"
      if (e->t == E_CRAB) worldcrab(e);
      else if (e->t == 1) sprx(fr & 8 ? GRUM2 : GRUM1, 8, e->x + (4 << 8), e->y + (8 << 8) - lift, e->vx > 0, 0, 256 - stretch/2, 256 + stretch, GPAL);
      else sprx(fr & 4 ? BUZZ2 : BUZZ1, 8, e->x + (4 << 8), e->y + (8 << 8) - lift*2/3, 0, 0, 256, 256 + stretch/2, BPAL);
    }
  for (const Shot *p = wd.sh; p < wd.sh+8; p++) if (p->a && p->room == room)   // spitter seeds
    for (int j = -4; j < 4; j++) for (int i = -4; i < 4; i++) {
      u32 c = G_TILES.px ? sheetpx(CELL_SEED, 0, 0, i+4, j+4) : (i < -2 || i > 1 || j < -2 || j > 1 || ((i == -2 || i == 1) && (j == -2 || j == 1))) ? 0 :
              i+j == -2 ? 0xe0b070 : (i+j+(fr >> 2)) & 1 ? 0x7a4a24 : 0x5a3418;
      if (c) wpx((p->x >> 8)+i, (p->y >> 8)+j, c);
    }
  for (const Bar *b = L->bar; b < L->bar + L->nbar; b++) if (b->room == room) {   // fire bars: a line of embers
    int a = barangle(b);
    for (int i = 0; i < b->len; i++) {
      int fx = b->x*8+4 + SIN[(a+64) & 255]*i*8/256, fy = b->y*8+4 + SIN[a]*i*8/256, fl = (fr + i) >> 1 & 1;
      if (G_TILES.px) { for (int j = -4; j < 4; j++) for (int k = -4; k < 4; k++) { u32 c = sheetpx(CELL_EMBER, 0, 0, k+4, j+4); if (c) wpx(fx+k, fy+j, c); } continue; }
      for (int j = -2; j < 2; j++) for (int k = -2; k < 2; k++) {
        int edge = (k == -2 || k == 1) + (j == -2 || j == 1);
        if (edge == 2) continue;
        wpx(fx+k, fy+j, edge ? (fl ? 0xd8401c : 0xff7a20) : (fl ? 0xfff0a0 : 0xffc040));
      }
    }
  }
  worlddraw();   // the avalanche
  capless = 0;
  if (cst && cst != 4) {   // the thrown cap spins (one on a post: cap.h): its width follows a cosine
    int w = SIN[(fr*24 + 64) & 255];
    sprx(CAP, 4, cxp + (4 << 8), cyp + (4 << 8), w < 0, 0, iabs(w) < 48 ? 48 : iabs(w), 256, HPAL);
  }
  capless = cst != 0;
  if (st != TUBE) drawhero();
  it_draw();   // items.h: the egg buddy, a planted flag
  bossdraw();
  mv_drawwater();
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
  capx_dark();
  gim_overlay();   // gimmicks.h: lava, night, 8-bit pixels, the upside-down view
  // HUD, in the menu's smooth type with a dark outline: coins left, score in the middle, time left right
  {
    char t[16];
    mellipse(40, 37, 11, 15, 0x14100c); mellipse(40, 37, 9, 13, 0xc87a10); mellipse(41, 37, 7, 12, 0xffc93a);   // the coin
    mrect(39, 28, 3, 18, 0xfff0a8);
    for (int o = -3; o <= 3; o++) { mline(59+o, 30, 71+o, 44, 0x14100c); mline(71+o, 30, 59+o, 44, 0x14100c); }
    for (int o = -1; o <= 1; o++) { mline(61+o, 32, 69+o, 42, 0xffffff); mline(69+o, 32, 61+o, 42, 0xffffff); }   // the times sign
    snprintf(t, sizeof t, "%02d", coins); hudtext(t, 82, 26, 0xffffff);
    snprintf(t, sizeof t, "%06d", score); hudtext(t, (MENUW - textwidth(t, 1)) / 2, 26, 0xffffff);
    int hurry = left < 100*60;
    u32 c = hurry ? (fr & 16 ? 0xff5a4a : 0xffb0a0) : 0xffffff;
    mellipse(670, 37, 14, 14, 0x14100c); mellipse(670, 37, 12, 12, c);                                         // the clock
    for (int o = -1; o <= 0; o++) { mline(670+o, 37, 670+o, 28, 0x14100c); mline(670, 37+o, 678, 37+o, 0x14100c); }
    snprintf(t, sizeof t, "%03d", (left + 59) / 60); hudtext(t, 692, 26, c);
    for (int i = 0; i < HPMAX; i++) hudheart(672 - (HPMAX-3)*30 + i*30, 72, i < hp, i == hp && hpt < 24 ? hpt : i < hp && healt ? -1 : 0);
    it_hud();   // items.h: what is on
  }
  if (done) {   // the run is over: its time and score, big
    int s = 2, x = 84, yy = 52;
    u32 c = 0xffd84a;
    x = num(tim/3600, 1, x, yy, s, c);
    txt(FONT[10], 3, 15, x, yy, s, c);
    x = num(tim/60 % 60, 2, x+4*s, yy, s, c);
    txt(FONT[11], 3, 15, x, yy, s, c);
    num(tim % 60 * 100 / 60, 2, x+4*s, yy, s, c);
    txt(I_STAR, 5, 25, 84, 72, 2, 0xffd84a); num(score, 6, 98, 72, 2, 0xffffff);
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
  worldhud();
  gim_hud();
  cl_hud();
  if (menu) pauserender();
}

#include "boss.c"

#ifndef SIM
// ---------- platform: X11 window, frame timing, gamepads (evdev) and the audio director ----------
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "audio.h"
#include "vendor/stb_image.h"

// Replacement art from assets/gfx/*.png (MODDING.md lists the files and their layouts). A missing
// file keeps the built-in art; a file of the wrong size is reported and skipped.
static void gfxload(const char *dir) {
  static const struct { Img *g; const char *name; int w, h, h2; } F[] = {
    { &G_TILES, "tiles", 64, 32, 0 }, { &G_HERO, "hatrick", 80, 12, 24 }, { &G_WALKER, "walker", 16, 8, 0 },
    { &G_BUZZER, "buzzer", 16, 8, 0 }, { &G_DWELLER, "dweller", 32, 16, 0 }, { &G_CAP, "cap", 8, 4, 0 },
    { &G_MOON, "moon", 13, 13, 0 }, { &G_SKY, "sky", 0, 0, 0 }, { &G_CAVE, "cave", 0, 0, 0 },
    { &G_SKY2, "sky2", 0, 0, 0 },
  };
  for (size_t i = 0; i < sizeof F / sizeof *F; i++) {
    char path[1200]; int w, h, n;
    snprintf(path, sizeof path, "%s/gfx/%s.png", dir, F[i].name);
    if (access(path, F_OK) < 0) continue;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    if (!px) { fprintf(stderr, "hatrick: cannot read %s (%s); using the built-in art\n", path, stbi_failure_reason()); continue; }
    if (F[i].w && (w != F[i].w || (h != F[i].h && h != F[i].h2))) {
      if (F[i].h2) fprintf(stderr, "hatrick: %s is %dx%d, it must be %dx%d or %dx%d; using the built-in art\n", path, w, h, F[i].w, F[i].h, F[i].w, F[i].h2);
      else fprintf(stderr, "hatrick: %s is %dx%d, it must be %dx%d; using the built-in art\n", path, w, h, F[i].w, F[i].h);
      stbi_image_free(px); continue;
    }
    u32 *out = malloc((size_t)w*h*4);
    for (int k = 0; k < w*h; k++) {
      u32 c = px[4*k] << 16 | px[4*k+1] << 8 | px[4*k+2], a = px[4*k+3];
      out[k] = a << 24 | (c ? c : 0x010101);
    }
    stbi_image_free(px);
    F[i].g->w = w; F[i].g->h = h; F[i].g->px = out;
  }
}

// ---------- gamepads: every evdev gamepad is read directly, Super Mario Odyssey layout ----------
// A/B jump, X/Y cap, LT/RT (or LB/RB) crouch / ground pound (either cap button dives from them),
// left stick or D-pad move (stick down + the west cap button throws downward),
// Start opens the menu, View mutes. Pads are rescanned every 2 s, so hotplugging works, and pads
// with force feedback get rumble effects uploaded (played from rumble()).
static const short PADB[] = { BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR, BTN_TL2, BTN_TR2, BTN_SELECT, BTN_START,
                              BTN_DPAD_LEFT, BTN_DPAD_RIGHT, BTN_DPAD_UP, BTN_DPAD_DOWN };
static const unsigned PADK[] = { 16, 16|MENUBACK, 32, CAP2, 8|PADCROUCH, 8|PADCROUCH, 8|PADCROUCH, 8|PADCROUCH, 128, START, 1, 2, 4, 8 };
static struct { int fd, num, held, x, hx, y, hy, ymid, yrange, ydz, t1, t2, mid, range, dz, tq, tq2, c1, c2, fx[8]; } pad[4];   // fd is stored +1, 0 = free slot
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
      // A trigger counts as pressed a quarter of the way in, measured from its own resting end:
      // some drivers report triggers from 0, others from a negative minimum.
      p->t1 = p->t2 = INT_MIN; p->tq = p->tq2 = INT_MAX;
      if (ioctl(fd, EVIOCGABS(p->c1), &ai) >= 0) p->t1 = ai.value, p->tq = ai.minimum + (ai.maximum-ai.minimum)/4;
      if (ioctl(fd, EVIOCGABS(p->c2), &ai) >= 0) p->t2 = ai.value, p->tq2 = ai.minimum + (ai.maximum-ai.minimum)/4;
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
    if (p->t1 > p->tq || p->t2 > p->tq2) k |= 8|PADCROUCH;
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
  const char *want = title ? LV[0].music : gim_theme(LV[lvl].music);   // gimmicks.h: chiptune in 8-bit areas
  if (skipclear) snd_stop(S_CLEAR), skipclear = 0;   // the course clear was skipped
  if (st == DEAD) dead = 1;
  if (strcmp(want, theme)) { snprintf(theme, sizeof theme, "%s", want); snd_theme(theme, 1); seen = loads; dead = 0; }
  else if (loads != seen && !menu) { snd_theme(theme, 1); seen = loads; if (dead) quiet = 80; dead = 0; }
  if (quiet) quiet--;   // after a death the theme waits for the death jingle
  // stems
  int fast = (gnd && iabs(hvx) >= 380) || st == LONGJ || st == DIVE || st == SLIDE || st == ROLL || st == SPINJ || (spin && jn == 2);
  fast |= gim_fastmusic() || it_gold;   // gimmicks.h: the speed flip; items.h: the gold cap
  if (fast && !menu) fastt = 90; else if (fastt) fastt--;
  if (arpt) arpt--;
  float g[NSTEM] = { 1, 1, 1, 1, fastt ? 1 : 0, arpt ? 1 : 0, 1 };
  int ms = 600;
  if (!title && !menu && lvl < NLEVEL) {
    const Level *L = LV + lvl;
    if (bossnear || (left < 100*60 && st != WIN)) g[STEM_DANGER] = 1;   // low strings: time short, or a boss
    for (int i = 0; i < L->nmoon; i++)   // celesta swells near a moon coin not found yet: a hint
      if (L->moon[i].room == room && !(wd.moongot >> i & 1) && !(moonbits(lvl) >> i & 1)) {
        int dx = L->moon[i].x*8+4 - (hx >> 8) - 3, dy = L->moon[i].y*8+4 - (hy >> 8) - 6, d2 = dx*dx + dy*dy;   // no libm: isqrt (enemies.h), and only when in range
        float v = d2 < 40*40 ? 1 : d2 > 140*140 ? 0 : (140 - isqrt(d2)) / 100.0f;
        if (v > g[STEM_SECRET]) g[STEM_SECRET] = v;
      }
    if (room && !gim_8bit()) {   // bonus rooms: the theme on marimba and glockenspiel alone
      for (int s = 0; s < NSTEM; s++) if (s != STEM_DANGER && s != STEM_SECRET) g[s] = 0;
      g[STEM_MALLET] = 1;
    }
  }
  snd_filter(underwater && !title ? 1 : 0);
  snd_pause(menu && resumable && !naming);   // the level's clock (fr) stops on the pause screen: so does the music, or the beat drifts
  if (title) g[STEM_LEAD] = 0, g[STEM_PERC] = 0.45f, g[STEM_BASS] = 0.8f, g[STEM_FAST] = g[STEM_ARP] = 0;
  else if (menu) { for (int s = 0; s < NSTEM; s++) g[s] *= 0.3f; ms = 200; }   // paused: duck
  if (!title && (st == DEAD || st == WIN || quiet)) { for (int s = 0; s < NSTEM; s++) g[s] = 0; ms = st == WIN ? 300 : 150; }
  for (int s = 0; s < NSTEM; s++)
    snd_stem(s, g[s] * MUSIC, s == STEM_FAST && g[s] ? 400 : s == STEM_ARP && g[s] ? 150 : s >= STEM_DANGER && !menu ? 900 :
                              (s >= STEM_FAST && !g[s] && !menu ? 1500 : ms));
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
  gfxload(dir);
  if (!snd_init(dir, silent, dump)) fprintf(stderr, "hatrick: playing without sound\n");
  Display *d = XOpenDisplay(0);
  if (!d) { fprintf(stderr, "hatrick: cannot open display\n"); return 1; }
  Window w = XCreateSimpleWindow(d, RootWindow(d, 0), 0, 0, W*SC, H*SC, 0, 0, 0);
  XStoreName(d, w, "Hatrick");
  XSelectInput(d, w, ButtonPressMask | PointerMotionMask);
  Atom wmdelete = XInternAtom(d, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(d, w, &wmdelete, 1);
  XMapWindow(d, w);
  Visual *vis = DefaultVisual(d, 0); int depth = DefaultDepth(d, 0);
  int native = depth == 24 && vis->red_mask == 0xff0000 && vis->green_mask == 0xff00 && vis->blue_mask == 0xff;   // big's own 0xRRGGBB
  XImage *im = XCreateImage(d, vis, native ? 24 : depth, ZPixmap, 0, native ? (char *)big : 0, W*SC, H*SC, 32, 0);
  if (!native) im->data = malloc(im->bytes_per_line * im->height);   // other displays (16 bit, 30 bit): converted each frame
  int sh[3], keep[3]; const unsigned long mask[3] = { vis->red_mask, vis->green_mask, vis->blue_mask };
  for (int c = 0; c < 3; c++) {   // where each 8-bit channel goes: its mask's lowest bit, and how many bits it has
    unsigned long m = mask[c]; sh[c] = keep[c] = 0;
    while (m && !(m & 1)) m >>= 1, sh[c]++;
    while (m & 1) m >>= 1, keep[c]++;
  }
  struct timespec t;
  char km[32];
  int volume = 8, volkeys = 0;
  load(); menu = 1; hiload(); progload(); mapstart();
  clock_gettime(CLOCK_MONOTONIC, &t);
  for (;;) {
    while (XPending(d)) {
      XEvent e; XNextEvent(d, &e);
      if (e.type == ClientMessage && (Atom)e.xclient.data.l[0] == wmdelete) quitting = 1;
      if (menu && !scoreview && !naming && !clhouse && !it_bag && !trav && (e.type == MotionNotify || (e.type == ButtonPress && e.xbutton.button == 1))) {
        int x = e.type == MotionNotify ? e.xmotion.x : e.xbutton.x;
        int y = e.type == MotionNotify ? e.xmotion.y : e.xbutton.y;
        if (resumable) {   // the pause screen: hover picks, click chooses
          int hit = pausehit(x, y);
          if (hit >= 0 && hit != pausesel) pausesel = hit, sfx(S_MENUMOVE);
          if (hit >= 0 && e.type == ButtonPress) { if (hit == 2) tomap(), sfx(S_MENUOK); else if (hit) it_openbag(); else menu = 0, sfx(S_MENUBACK); }
        } else if (e.type == ButtonPress) mapclick(maphit(x, y));   // the map: click a stop to walk there, again to enter
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
    tick((active ? padkeys() : 0) | K(113) | K(114) << 1 | K(111) << 2 | K(116) << 3 | (K(52) | K(29) | K(65) | K(36) | K(104)) << 4 | K(53) << 5 | K(27) << 6 | K(58) << 7 | K(54) << 8 | K(67) << 20 | K(9) << 21 | K(23) << 22 | K(24) << 23 | K(68) << 28 | K(69) << 29);
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
    if (!native) for (int y = 0; y < H*SC; y++) for (int x = 0; x < W*SC; x++) {
      unsigned long v = 0; u32 c = big[y][x];
      for (int k = 0; k < 3; k++) { unsigned long ch = c >> (16 - 8*k) & 255; v |= (keep[k] >= 8 ? ch << (keep[k]-8) : ch >> (8-keep[k])) << sh[k]; }
      XPutPixel(im, x, y, v);
    }
    XPutImage(d, w, DefaultGC(d, 0), im, 0, 0, 0, 0, W*SC, H*SC);
    if ((t.tv_nsec += 16666667) >= 1000000000) t.tv_nsec -= 1000000000, t.tv_sec++;
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, 0);   // until the next frame
  }
}
#else
// ---------- headless simulator: replays scripted input, reports deaths / goal ----------
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// Writes the built-in art in the layouts of assets/gfx/*.png, as raw RGBA files NAME.rgba in dir,
// and prints "NAME W H" for each (tools/export_gfx.py turns them into PNG templates).
static void putimg(const char *dir, const char *name, int w, int h, const u32 *px) {
  char path[1200]; snprintf(path, sizeof path, "%s/%s.rgba", dir, name);
  FILE *f = fopen(path, "wb");
  for (int i = 0; i < w*h; i++) { u32 c = px[i]; fputc(c >> 16 & 255, f); fputc(c >> 8 & 255, f); fputc(c & 255, f); fputc(c ? 255 : 0, f); }
  fclose(f);
  printf("%s %d %d\n", name, w, h);
}
static void sprimg(u32 *dst, int w, int x0, int y0, const u16 *s, int h, const u32 *pal, int capoff_) {   // a 2-bit sprite into an image
  for (int v = 0; v < h; v++) for (int u = 0; u < 8; u++) {
    int c = s[v] >> (14 - 2*u) & 3;
    dst[(y0+v)*w + x0+u] = !c ? 0 : capoff_ >= 0 && v >= capoff_ && v < capoff_+3 && c == 1 ? 0x5a3018 : pal[c-1];
  }
}
static int exportgfx(const char *dir) {
  static u32 tiles[32*64], hero[24*80], walker[8*16], buzz[8*16], dw[16*32], cap[4*8], moon[13*13], sky[H*W];
  for (int c = 0; c < NCELL; c++) for (int v = 0; v < 8; v++) for (int u = 0; u < 8; u++)
    tiles[(c/8*8 + v)*64 + c%8*8 + u] = cellpx(c, 0, 1, u, v);
  for (int k = 0; k < 10; k++) {
    int h = HFRAMES[k] == HROLL ? 8 : 12;
    sprimg(hero, 80, k*8, 12-h, HFRAMES[k], h, HPAL, -1);
    sprimg(hero, 80, k*8, 24-h, HFRAMES[k], h, HPAL, HFRAMES[k] == HCROUCH ? 5 : 0);   // second row: the cap thrown
  }
  sprimg(walker, 16, 0, 0, GRUM1, 8, GPAL, -1); sprimg(walker, 16, 8, 0, GRUM2, 8, GPAL, -1);
  sprimg(buzz, 16, 0, 0, BUZZ1, 8, BPAL, -1); sprimg(buzz, 16, 8, 0, BUZZ2, 8, BPAL, -1);
  sprimg(dw, 32, 0, 0, SNAP1, 16, SNAPPAL, -1); sprimg(dw, 32, 8, 0, SNAP2, 16, SNAPPAL, -1);
  sprimg(dw, 32, 16, 0, SNAP1, 16, SPITPAL, -1); sprimg(dw, 32, 24, 0, SNAP2, 16, SPITPAL, -1);
  sprimg(cap, 8, 0, 0, CAP, 4, HPAL, -1);
  for (int v = 0; v < 13; v++) for (int u = 0; u < 13; u++) moon[v*13 + u] = moonpx(u-6, v-6);
  putimg(dir, "tiles", 64, 32, tiles); putimg(dir, "hatrick", 80, 24, hero); putimg(dir, "walker", 16, 8, walker);
  putimg(dir, "buzzer", 16, 8, buzz); putimg(dir, "dweller", 32, 16, dw); putimg(dir, "cap", 8, 4, cap); putimg(dir, "moon", 13, 13, moon);
  for (int cave = 0; cave < 2; cave++) {   // one screen of each background, from the bottom of a level
    ox = oy = 0; lh = MINH; cyf = (lh*8-H) << 8;
    drawbg(cave, 0);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) sky[y*W + x] = big[y*SC][x*SC];
    putimg(dir, cave ? "cave" : "sky", W, H, sky);
  }
  return 0;
}

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
  if (argc > 2 && !strcmp(argv[1], "--export-gfx")) return exportgfx(argv[2]);
  if (argc < 3) { fprintf(stderr, "usage: sim LEVEL TASFILE [trace] [frame out.ppm] | sim --check | sim --export-gfx DIR\n"); return 3; }
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
        printf("%d %s x=%.1f y=%.1f vx=%d vy=%d st=%d gnd=%d cap=%d room=%d score=%d wall=%d moons=%d", frame, b, hx/256., hy/256., hvx, hvy, st, gnd, cst, room, score, wall, wd.moongot);
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
      if (st == WIN && stt == 1) { printf("GOAL frame %d (%.2fs) coins %d score %d deaths %d moons %d/%d\n", frame, frame/60., coins, score, deaths, __builtin_popcount(wd.moongot), LV[lvl].nmoon); return 0; }
    }
  }
  printf("END frame %d at x=%d y=%d (tile %d,%d) st=%d\n", frame, hx >> 8, hy >> 8, hx >> 11, hy >> 11, st);
  return 2;
}
#endif
