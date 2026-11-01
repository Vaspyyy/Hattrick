// The C game's simulator with a full trace: everything the remake's GDScript port (src/sim/)
// must reproduce frame for frame. tools/xtrace.gd prints the same lines from the port, so
//   tools/difftrace.sh LEVEL FILE.tas
// diffs the two. Build (from the repository root; the binary looks for assets/ beside itself):
//   gcc -O1 -w remake/tools/ctrace.c -o ctrace
// usage: ctrace LEVEL FILE.tas       (ctrace list: the levels in game order)
//   SIM_RESPAWN=1  keep going after a death (checkpoints), as ./sim does
//   SIM_BOSS=N     after the start, put Hatrick at the entrance of the level's boss arena N (0, 1)
#define SIM
#define SC 1
#define main hatrick_sim_main
#include "../../hatrick.c"
#undef main

static unsigned mapsum(void) {   // the current area's live tiles
  unsigned s = 0;
  for (int y = 0; y < lh; y++) for (int x = 0; x < lw; x++) s = s*31 + map[y][x];
  return s;
}

static void line(int frame, const char *b) {
  printf("%d %s x=%.1f y=%.1f vx=%d vy=%d st=%d gnd=%d cap=%d room=%d score=%d wall=%d moons=%d", frame, b, hx/256., hy/256., hvx, hvy, st, gnd, cst, room, score, wall, wd.moongot);
  printf(" c=%d hp=%d left=%d cx=%d,%d pow=%d carry=%d map=%u", coins, hp, left, cxp >> 8, cyp >> 8, capx_pow, capx_carry, mapsum());
  for (int i = 0; i < ne; i++) { const E *e = en + i; if (e->a && e->r == room) printf(" e%d=%d:%d,%d,%d,%d,%d", i, e->t, e->x >> 8, e->y >> 8, e->s, e->u, e->w); }
  const Level *L = LV + lvl;
  for (int i = 0; i < L->nhome; i++) if (L->home[i].room == room) printf(" d%d=%d,%d,%d", i, wd.dw[i].phase, wd.dw[i].ofs, wd.dw[i].a);
  for (int i = 0; i < 8; i++) if (wd.sh[i].a) printf(" sh=%d,%d", wd.sh[i].x >> 8, wd.sh[i].y >> 8);
  for (int i = 0; i < wd.gw.npl; i++) if (wd.gw.pl[i].room == room) printf(" p%d=%d,%d", i, wd.gw.pl[i].y, wd.gw.pl[i].tilt);
  for (int i = 0; i < GIM_NB; i++) if (wd.gw.ball[i].a) printf(" gb=%d,%d", wd.gw.ball[i].x >> 8, wd.gw.ball[i].y >> 8);
  if (L->gim[GO_RISE]) printf(" rise=%d", wd.gw.rise);
  if (wd.gw.flipt || wd.gw.rot || wd.gw.flip) printf(" flip=%d,%d,%d", wd.gw.flipt, wd.gw.rot, wd.gw.flip);
  if (wd.gw.gold) printf(" gold=%d,%d", wd.gw.gold, wd.gw.goldn);
  if (L->avalanche) printf(" av=%d", avx);
  for (int i = 0; i < nbz; i++) { const Boss *z = bz + i; printf(" B%d=%d,%d,%d,%d,%d,%d,%d,%d", i, z->on, z->x >> 8, z->y >> 8, z->hp, z->s, z->t, z->dead, z->hurt); }
  if (nbz) {
    for (int i = 0; i < 24; i++) if (bsh[i].a) printf(" S=%d:%d,%d", bsh[i].kind, bsh[i].x >> 8, bsh[i].y >> 8);
    for (int i = 0; i < 3; i++) if (bcl[i].a) printf(" C=%d,%d,%d,%d", bcl[i].x >> 8, bcl[i].y >> 8, bcl[i].charge, bcl[i].strike);
    printf(" saw=%d,%d hat=%d,%d,%d m=%d,%d,%d,%d", sawt, sawslam, hatst, hatx, haty, mcur, mofs, dcur, dofs);
  }
  printf("\n");
}

int main(int argc, char **argv) {
  modlevels();
  if (argc == 2 && !strcmp(argv[1], "list")) {   // the levels in game order
    for (int i = 0; i < NLEVEL; i++) printf("%d %s rooms=%d boss=%d mini=%d lab=%d\n", i, LV[i].name, LV[i].nroom, LV[i].boss, LV[i].mini, LV[i].lab);
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "dump")) {   // everything the level reader makes (tools/levels_dump.gd prints the same)
    printf("play=%d nsec=%d ncamp=%d\n", PLAY, NSEC, NLV);
    for (int i = 0; i < NLEVEL; i++) {
      const Level *L = LV + i;
      printf("%d %s music=%s time=%d card=%d lab=%d boss=%d mini=%d theme=%d av=%d before=%s gim=%d,%d,%d,%d,%d gr=%d,%d,%d,%d\n", i, L->name, L->music, L->time, L->card, L->lab, L->boss, L->mini, L->theme, L->avalanche, L->before,
        L->gim[0], L->gim[1], L->gim[2], L->gim[3], L->gim[4], L->gimroom[0], L->gimroom[1], L->gimroom[2], L->gimroom[3]);
      printf(" cl=%d,%d,%d,%d,%d,%d,%d,%d,%d,%s\n", L->cl.star, L->cl.sg, L->cl.gr, L->cl.gx, L->cl.gy, L->cl.pc, L->cl.pr, L->cl.px, L->cl.py, L->cl.secretof);
      for (int r = 0; r < L->nroom; r++) {
        const Room *R = L->room + r; unsigned s = 0;
        for (int k = 0; k < R->w*R->h; k++) s = s*31 + R->grid[k];
        printf(" room%d=%d,%d,%d,%u\n", r, R->w, R->h, R->cave, s);
      }
      for (int k = 0; k < L->ntube; k++) printf(" t%d=%d,%d,%d,%d,%d,%d\n", k, L->tube[k].room, L->tube[k].x, L->tube[k].y, L->tube[k].dir, L->tube[k].id, L->tube[k].link);
      for (int k = 0; k < L->nbar; k++) printf(" b%d=%d,%d,%d,%d,%d,%d\n", k, L->bar[k].room, L->bar[k].x, L->bar[k].y, L->bar[k].len, L->bar[k].speed, L->bar[k].a0);
      for (int k = 0; k < L->nhome; k++) printf(" h%d=%d,%d,%d\n", k, L->home[k].room, L->home[k].tube, L->home[k].spit);
      for (int k = 0; k < L->nmoon; k++) printf(" m%d=%d,%d,%d\n", k, L->moon[k].room, L->moon[k].x, L->moon[k].y);
    }
    return 0;
  }
  if (argc < 3) { fprintf(stderr, "usage: ctrace LEVEL FILE.tas | ctrace list\n"); return 3; }
  lvl = atoi(argv[1]);
  if (lvl < 0 || lvl >= NLEVEL) { fprintf(stderr, "ctrace: no level %d (there are %d)\n", lvl, NLEVEL); return 3; }
  load();
  const char *bs = getenv("SIM_BOSS");
  if (bs && *bs) {
    int n = atoi(bs);
    if (n >= nbz) { fprintf(stderr, "ctrace: level %d has no boss %d\n", lvl, n); return 3; }
    Boss b = bz[n];
    spawn(b.room, ((b.ax0 + 4)*8) << 8, ((b.my - 3)*8) << 8);
  }
  FILE *f = fopen(argv[2], "r");
  if (!f) { fprintf(stderr, "ctrace: cannot read %s\n", argv[2]); return 3; }
  int respawn = getenv("SIM_RESPAWN") && *getenv("SIM_RESPAWN"), n, frame = 0;
  char b[32];
  while (fscanf(f, "%d %31s", &n, b) == 2) {
    int k = 0;
    for (char *c = b; *c; c++) k |= *c=='L' ? 1 : *c=='R' ? 2 : *c=='U' ? 4 : *c=='D' ? 8 : *c=='J' ? 16 : *c=='C' ? 32 : *c=='V' ? CAP2 : 0;
    while (n--) {
      tick(k); frame++; rumq = 0;
      line(frame, b);
      if (st == DEAD && stt == 0 && !respawn) { printf("DIED frame %d\n", frame); return 1; }
      if (st == WIN && stt == 1) { printf("GOAL frame %d\n", frame); return 0; }
    }
  }
  printf("END frame %d\n", frame);
  return 2;
}
