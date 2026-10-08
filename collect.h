// ---------- collectibles and progression: the parts the level reader needs ----------
// Hats on the house rack, star ratings, secret exits, postcards and the house that grows.
// The map, the house screen and the drawing are in collect_ui.h, included further down.
//
// Progress bits beyond the moon coins (1, 2, 4) and the clear (8): 16 the star time beaten,
// 32 the secret exit taken, 64 the postcard found. A line "<n> *HAT" keeps the cap worn (n-1).
// Map characters: 'G' a secret exit (a second goal pole, in any area), 'P' the level's postcard.
// Header options: star=<seconds> the time to beat for the star (a default comes from the width),
// secret=<level number or name> makes the level a secret one, hanging off that level's secret exit.
#define PB_STAR 16
#define PB_SECRET 32
#define PB_CARD 64
static int NSEC;      // secret levels: the last ones in LV, after the labs
#define SEC0 (NLEVEL - NSEC)   // the first secret level
static int cl_issecret(int l) { return l >= SEC0 && l < NLEVEL; }
static int clstall;   // extra frames of air-throw stall, from the hat being worn
static void levelerr(const char *file, int line, const char *fmt, ...);
static int collectopt(Level *L, const char *w, const char *eq, const char *file, int line) {
  if (!strcmp(w, "star")) {
    L->cl.star = atoi(eq);
    if (L->cl.star < 1 || L->cl.star > 9999) levelerr(file, line, "star needs 1 to 9999 seconds"), L->cl.star = 0;
    return 1;
  }
  if (!strcmp(w, "secret")) { snprintf(L->cl.secretof, sizeof L->cl.secretof, "%s", eq); return 1; }
  return 0;
}
// After a level's areas are read: its secret exit and postcard, and whether it is a secret level.
static void cl_parse(Level *L, const char *file, int line) {
  for (int r = 0; r < L->nroom; r++) for (int y = 0; y < L->room[r].h; y++) for (int x = 0; x < L->room[r].w; x++) {
    int c = GRID(L->room + r, x, y);
    if (c == 'G') {
      if (L->cl.sg) levelerr(file, line, "level \"%s\" has more than one secret exit; the rest are ignored", L->name), GRID(L->room + r, x, y) = 0;
      else L->cl.sg = 1, L->cl.gr = r, L->cl.gx = x, L->cl.gy = y;
    }
    if (c == 'P') {
      if (L->cl.pc) levelerr(file, line, "level \"%s\" has more than one postcard; the rest are ignored", L->name), GRID(L->room + r, x, y) = 0;
      else L->cl.pc = 1, L->cl.pr = r, L->cl.px = x, L->cl.py = y;
    }
  }
  if (*L->cl.secretof) L->lab = 2;   // off the main path: sorted last, after the labs
  if (!L->cl.star) L->cl.star = L->room[0].w*8/55 + 15;   // a brisk run, when the level names no time
}
static int cl_nsec(const Level *s, int n) { NSEC = 0; for (int i = 0; i < n; i++) NSEC += s[i].lab == 2; return NSEC; }
// Defined in collect_ui.h, used by the game and the map before it.
static void cl_tick(void);
static void cl_flag(void);
static void cl_tomap(void);
static int cl_maptick(int k, int pr);
static int cl_secretopen(int l);
static int cl_parentnode(int l);
static int cl_branches(int n, int *out, int k);
static void cl_mapnodes(void);
static int cl_visible(int n);
static void cl_house(void);
static void cl_maprender(void);
static void cl_render(void);
static void cl_hud(void);
