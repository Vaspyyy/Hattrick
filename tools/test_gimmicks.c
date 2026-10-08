// Level gimmicks (gimmicks.h, MODDING.md "Level gimmicks"): options, lava, platforms, the gold
// block, hat flips, turning doors, auto-scroll, night, the coin rush and the gallery.
// From the hatrick folder: gcc -O1 -w tools/test_gimmicks.c -o /tmp/hatrick-gimmick-tests && /tmp/hatrick-gimmick-tests
#define SIM
#define SC 1
#define main replay_main
#include "../hatrick.c"
#undef main
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static int find(const char *name) { for (int i = 0; i < NLEVEL; i++) if (!strcmp(LV[i].name, name)) return i; return -1; }
static void run(int k, int n) { while (n--) tick(k); }
static void put(int r, int x, int y) { spawn(r, x << 8, y << 8); }   // Hatrick's top-left, px
static GimObj *obj(int kind, int r) { for (GimObj *o = GW.ob; o < GW.ob + GW.nob; o++) if (o->kind == kind && o->room == r) return o; return 0; }
static const Tube *tubeof(int r) { for (int i = 0; i < LV[lvl].ntube; i++) if (LV[lvl].tube[i].room == r) return LV[lvl].tube + i; return 0; }

int main(void) {
  CHECK(readlevels("assets/levels.txt"));
  int tower = find("LAB GIMMICKS LAVA TOWER"), toy = find("LAB GIMMICKS TOYBOX"), air = find("LAB GIMMICKS AIRSHIP"),
      night = find("LAB GIMMICKS HILLS AT NIGHT"), hills = find("HILLS");
  CHECK(tower >= 0 && toy >= 0 && air >= 0 && night >= 0 && hills >= 0);

  // options and copy=
  CHECK(LV[tower].gim[GO_RISE] == 1 && LV[tower].gim[GO_RISEV] == 7 && LV[tower].gim[GO_FLIP] == GF_SWAP);
  CHECK(LV[toy].gim[GO_FLIP] == GF_GRAVITY && LV[toy].gimroom[3] == GR_8BIT && LV[toy].nroom == 4);
  CHECK(LV[air].gim[GO_SCROLL] == 26);
  CHECK(LV[night].gim[GO_NIGHT] && LV[night].gimroom[0] == GR_NIGHT);
  CHECK(LV[night].nroom == LV[hills].nroom && LV[night].room[0].w == LV[hills].room[0].w && LV[night].nmoon == LV[hills].nmoon);

  // night: the walkers fly, the bobbing buzzers walk
  int walk = 0, bob = 0;
  lvl = hills; load(); for (E *e = en; e < en+ne; e++) walk += e->t == 1, bob += e->t == 2;
  lvl = night; load(); { int w2 = 0, b2 = 0; for (E *e = en; e < en+ne; e++) w2 += e->t == 1, b2 += e->t == 2; CHECK(w2 == bob && b2 == walk); }

  // rising lava: standing still is a death, and the lava came up to Hatrick
  startlevel(tower);
  int r0 = GW.rise, f = 0;
  while (st != DEAD && f < 6000) tick(0), f++;
  CHECK(st == DEAD && GW.rise < r0 && f > 120);

  // the swap flower: bricks become coins and come back after 30 s
  startlevel(tower);
  { int bricks = 0, coins0 = 0;
    for (int y = 0; y < lh; y++) for (int x = 0; x < lw; x++) bricks += map[y][x] == 2, coins0 += map[y][x] == 6;
    GimObj *o = obj('O', 0); CHECK(o);
    put(0, o->x*8+1, o->y*8-3); tick(0);
    CHECK(GW.flipt && GW.flipk == GF_SWAP && o->t);
    int b2 = 0; for (int y = 0; y < lh; y++) for (int x = 0; x < lw; x++) b2 += map[y][x] == 2;
    CHECK(b2 == coins0);
    GW.rise = 1 << 30; GW.flipt = 2; run(0, 3);
    int b3 = 0; for (int y = 0; y < lh; y++) for (int x = 0; x < lw; x++) b3 += map[y][x] == 2;
    CHECK(!GW.flipt && b3 >= bricks - 2);
  }

  // seesaw: landing on it, leaning toward Hatrick's side, carrying him
  startlevel(toy);
  { GimPlat *p = 0; for (int i = 0; i < GW.npl; i++) if (GW.pl[i].kind == 2 && GW.pl[i].room == 0) { p = GW.pl + i; break; }
    CHECK(p);
    put(0, (p->x >> 8) + 10, (p->y >> 8) - 30); run(0, 40);
    CHECK(GW.ride == p - GW.pl + 1 && gnd && st == NORM);
    CHECK(p->tilt > 20);   // leans down on his (right) side
    int sy; gim_plattop(p, hx >> 8, &sy); CHECK(iabs(hy + (11 << 8) - sy) < 256);
    run(16, 1); run(0, 4); CHECK(GW.ride == 0 && hvy < 0);   // jumps off
  }
  // scale lifts: the one stood on sinks, its partner rises
  { GimPlat *p = 0; for (int i = 0; i < GW.npl; i++) if (GW.pl[i].kind == 1 && GW.pl[i].room == 0 && GW.pl[i].link > i) { p = GW.pl + i; break; }
    CHECK(p);
    GimPlat *q = GW.pl + p->link; int pa = p->y, qa = q->y;
    put(0, (p->x >> 8) + 4, (p->y >> 8) - 20); run(0, 60);
    CHECK(GW.ride == p - GW.pl + 1 && p->y > pa && q->y < qa && p->y + q->y == pa + qa);
    CHECK(iabs(hy + (11 << 8) - p->y) < 512);
  }
  // gold block: a bump from below puts it on his head; running spills coins
  startlevel(toy);
  { GimObj *o = obj('A', 0); CHECK(o);
    put(0, o->x*8+1, o->y*8+24); run(0, 20); CHECK(gnd);
    int c0 = coins; run(16, 12); run(0, 40);
    CHECK(o->t && GW.gold > 0 && map[o->y][o->x] == 15);
    run(2, 120); CHECK(coins > c0 + 10 && GW.goldn == coins - c0);
  }
  // speed flower in a main area: two steps a frame, the clocks still run at one
  startlevel(toy);
  { GimObj *o = obj('O', 0); CHECK(o);
    put(0, o->x*8+1, o->y*8-3); tick(0);
    CHECK(GW.flipt && GW.flipk == GF_SPEED);
    int f0 = fr, t0 = tim, l0 = left; tick(0);
    CHECK(fr == f0 + 1 && tim == t0 + 1 && left == l0 - 1);   // fr too: beat hazards keep time with the music
  }
  // gravity flower in a bonus room: the room turns upside down and back after 30 s
  startlevel(toy);
  { GimObj *o = obj('O', 1); CHECK(o);
    const Tube *t = tubeof(1); int ty = t->y, tdir = t->dir, oy = o->y;
    put(1, o->x*8+1, o->y*8-3); tick(0);
    CHECK(GW.flip && GW.xroom == 1 && GW.flipk == GF_GRAVITY);
    CHECK(t->dir == T_DOWN && t->y == lh-1-ty && o->y == lh-1-oy);
    CHECK(wd.rm[1][1][9] == 4 && wd.rm[1][lh-2][9] == 0 && wd.rm[1][2][5] == 12);   // the two-row floor is on top now
    run(0, 120); CHECK(gnd && st < DEAD);                       // he fell onto the old ceiling
    GW.flipt = 1; tick(0);
    CHECK(!GW.flip && t->dir == tdir && t->y == ty && o->y == oy);
  }
  // turning door: a quarter turn clockwise per press; leaving the room puts it back
  startlevel(toy);
  { const Tube *t = tubeof(2); int tx = t->x, ty = t->y, w0 = LV[lvl].room[2].w, h0 = LV[lvl].room[2].h;
    for (int turn = 1; turn <= 4; turn++) {
      GimObj *d = 0;
      for (GimObj *o = GW.ob; o < GW.ob + GW.nob; o++) if (o->kind == 'D' && o->room == 2 && (!d || o->y > d->y || (o->y == d->y && o->x < d->x))) d = o;   // a bottom corner
      CHECK(d);
      if (turn == 1) put(2, d->x*8+1, d->y*8-3); else hx = (d->x*8+1) << 8, hy = (d->y*8-3) << 8;
      run(0, 10); CHECK(gnd);
      tick(4); CHECK(GW.rott && GW.rot == turn % 4 && lw == (turn & 1 ? h0 : w0));
      run(0, 60); CHECK(gnd && st < DEAD);
      if (turn == 1) CHECK(t->dir == T_RIGHT && t->x == h0-1-ty && t->y == tx);
    }
    CHECK(GW.rot == 0 && t->x == tx && t->y == ty && t->dir == T_UP);
    GimObj *d = 0; for (GimObj *o = GW.ob; o < GW.ob + GW.nob; o++) if (o->kind == 'D' && o->room == 2 && (!d || o->y > d->y)) d = o;
    hx = (d->x*8+1) << 8; hy = (d->y*8-3) << 8; run(0, 10); tick(4); run(0, 30); CHECK(GW.rot == 1);
    put(0, 40, 80); tick(0);   // back in the main area
    CHECK(GW.rot == 0 && t->x == tx && t->y == ty && wd.rm[2][ty+1][tx] == 10);
  }
  // auto-scroll: the screen moves on and pushes Hatrick along
  startlevel(air);
  { for (GimObj *o = GW.ob; o < GW.ob + GW.nob; o++) if (o->kind == 'E') o->t = -100000;   // no cannonballs here
    int s0 = GW.sx; run(0, 200);
    CHECK(GW.sx > s0 + (40 << 8) && (hx >> 8) == GW.sx >> 8 && st < DEAD);   // pushed along
    run(0, 100); CHECK(st == DEAD);                                          // and squeezed against the cannon block
  }
  // coin rush: three levels from the map, a death ends it
  menu = 1; resumable = 0; nprog = 0; lvl = 0; prevk = 0;
  tick(GIM_RUSH);
  CHECK(gim_rush == 1 && !menu && lvl == gim_rushlv[0] && left == GIM_RUSHTIME);
  coins += 7; nextlevel();
  CHECK(gim_rush == 2 && lvl == gim_rushlv[1] && gim_rushcoins == 7 && left == GIM_RUSHTIME);
  doom(); run(0, 70);
  CHECK(!gim_rush && menu && !resumable && gim_rushfail == 1);
  // the gallery: F2 from the map starts the first lab level that isn't the playground
  prevk = 0; tick(GIM_GALLERY);
  CHECK(!menu && lvl >= NLV && lvl != PLAY);
  printf("gimmicks: %d checks passed\n", checks);
  return 0;
}
