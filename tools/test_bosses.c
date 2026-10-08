// Checks for the bosses (boss.c) on the boss levels of assets/levels.txt: gates, every boss's
// weak spot and defeat, the seesaw and cloud platforms, deaths restarting a fight.
// gcc -O1 -w tools/test_bosses.c -o /tmp/hatrick-boss-tests && /tmp/hatrick-boss-tests   (from the game folder)
#define SIM
#define SC 1
#define main replay_main
#include "../hatrick.c"
#undef main
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static void run(int k, int n) { while (n--) tick(k); }
static void place(int x, int y) { hx = x << 8; hy = y << 8; hvx = hvy = 0; st = NORM; }   // px, top-left of the body
static void go(int kind, int mini) {   // the first level with this boss (or mini-boss), from the top
  for (lvl = 0; lvl < NLEVEL; lvl++) if ((kind && LV[lvl].boss == kind) || (mini && LV[lvl].mini == mini)) break;
  CHECK(lvl < NLEVEL);
  menu = resumable = done = donet = naming = 0; deaths = coins = lcoins = score = lscore = lstart = tim = 0;
  load(); prevk = 0;
}
static Boss *find(int kind, int mini) { for (int i = 0; i < nbz; i++) if ((kind && bz[i].kind == kind) || (mini && bz[i].mini == mini)) return bz + i; return 0; }
static void enter(Boss *b) {   // drop Hatrick into the arena, near its left gate, and let the fight start
  place((b->ax0 + 4)*8, (b->my - 3)*8); if (b->kind == BOSS_CLOUDKING) place((b->ax0 + 4)*8, floorpx - 30);
  run(0, 30);
  CHECK(b->on == 1 && b->ng > 0 && wd.rm[b->room][b->my > 4 ? b->my - 4 : 0][b->ax0] == 4);   // the gates are shut
}
static void stomp(Boss *b, int dx, int dy) {   // Hatrick falls onto a head at (dx, dy) px from the boss's x, y
  while (b->hurt) run(0, 1);
  place((b->x >> 8) + dx - 3, (b->y >> 8) + dy - 13); hvy = 500; gnd = 0;
  for (int i = 0; i < 6 && hvy >= 0; i++) tick(0);
}
static void beaten(Boss *b) {
  CHECK(b->on == 2 && b->ng == 0 && wd.rm[b->room][b->my > 4 ? b->my - 4 : 0][b->ax0] == 0);   // gates open again
}

int main(void) {
  CHECK(readlevels("assets/levels.txt"));
  // ---- options: unknown bosses are reported
  {
    fflush(stderr); int saved = dup(2); FILE *t = tmpfile(); dup2(fileno(t), 2);
    int ok = parselevels("= x boss=dragon mini=walker\n@ J F\n#####\n", "mod.txt");
    fflush(stderr); dup2(saved, 2); close(saved);
    char err[512]; rewind(t); size_t n = fread(err, 1, sizeof err - 1, t); err[n] = 0; fclose(t);
    CHECK(ok && LV[0].boss == 0 && LV[0].mini == MINI_WALKER && strstr(err, "unknown boss \"dragon\""));
    CHECK(readlevels("assets/levels.txt"));
  }
  // ---- Chief Walker: stomps hurt it, the cap only stuns it; three stomps open the gates
  go(0, MINI_WALKER); Boss *b = find(0, MINI_WALKER); CHECK(b && b->hp == 3);
  enter(b);
  run(0, 60);
  { int cx = (b->x >> 8) + 8, by = b->y >> 8;   // the cap: a stun, no harm
    cst = 1; cxp = (cx - 4) << 8; cyp = (by + 6) << 8; cvx = cvy = 0; ckind = CAPFORWARD; tick(0);
    CHECK(b->hp == 3 && b->s == 1); cst = 0; }
  for (int i = 0; i < 3; i++) { stomp(b, 8, 2); CHECK(st != DEAD && b->hp == 2 - i); }
  beaten(b);
  // ---- a death restarts a fight; a checkpoint past the arena keeps the boss beaten
  go(0, MINI_WALKER); b = find(0, MINI_WALKER); enter(b);
  stomp(b, 8, 2); CHECK(b->hp == 2);
  die(); run(0, 70); b = find(0, MINI_WALKER); CHECK(b->on == 0 && b->hp == 3 && wd.rm[b->room][b->my - 4][b->ax0] == 0);
  haveck = 1; ckroom = 0; ckx = (b->ax1 + 4) << 11; cky = (b->my*8 - 3) << 8; saved = wd;
  die(); run(0, 70); b = find(0, MINI_WALKER); CHECK(b->on == 2);
  haveck = 0;
  // ---- Buzz Boss: the cap or a stomp; its stingers are deadly
  go(0, MINI_BUZZER); b = find(0, MINI_BUZZER); CHECK(b);
  enter(b); run(0, 60);
  for (int i = 0; i < 3; i++) {
    while (b->hurt) run(0, 1);
    cst = 1; ckind = CAPFORWARD; cvx = cvy = 0; cxp = b->x + (4 << 8); cyp = b->y + (5 << 8); hx = (b->ax0 + 3)*8 << 8;
    tick(0); CHECK(b->hp == 2 - i);
  }
  beaten(b);
  // ---- the Snapper Matriarch: five cap hits while she is out of a tube
  go(BOSS_MATRIARCH, 0); b = find(BOSS_MATRIARCH, 0); CHECK(b && nmt == 6 && b->hp == 5);
  enter(b);
  for (int i = 0; i < 5; i++) {
    int n = 0; while ((b->s != 2 || b->hurt) && n++ < 1000) run(0, 1);
    CHECK(b->s == 2 && mofs >= 20 && st != DEAD);
    int cx, my, up; mspot(mcur, &cx, &my, &up);
    hx = (b->ax0 + 3)*8 << 8; hy = (b->my*8 - 3) << 8;
    cst = 1; ckind = CAPFORWARD; cvx = cvy = 0; cxp = (cx - 4) << 8; cyp = (up ? my - 14 : my + 10) << 8;
    tick(0); CHECK(b->hp == 4 - i);
    for (BShot *s = bsh; s < bsh+24; s++) s->a = 0;   // keep the test clear of her seeds
    cst = 0; dcur = -1; dofs = 0; dph = 0; dt = 9999;
  }
  beaten(b);
  // ---- Big Walker: Hatrick stands on the seesaw; a ground pound on the walker's side tips him into the lava
  go(BOSS_BIGWALKER, 0); b = find(BOSS_BIGWALKER, 0); CHECK(b);
  enter(b);
  place(sawx - SAWL + 8, sawy - 30); run(0, 40);
  CHECK(bostand && gnd && (hy >> 8) + 11 == sawsurf((hx >> 8) + 3) && st == NORM);
  for (int i = 0; i < 3; i++) {
    int n = 0; while (!(b->s == 0 && (b->x >> 8) + 12 > sawx + 20 && !b->hurt) && n++ < 3000) run(0, 1);
    CHECK(b->s == 0);
    place(sawx + 8, sawsurf(sawx + 11) - 40); hvy = 0; tick(8 | 0); tick(0); tick(8);   // a ground pound
    n = 0; while (!bopound && n++ < 60) run(0, 1);
    CHECK(bopound && st == GPLAND);
    n = 0; while (b->s != 4 && b->on == 1 && n++ < 300) run(0, 1);
    CHECK(b->hp == 2 - i);
    place(sawx - SAWL + 8, sawy - 30);   // out of his way
  }
  run(0, 10); beaten(b);
  // ---- the Cloud King: the cap in one of his clouds zaps him
  go(BOSS_CLOUDKING, 0); b = find(BOSS_CLOUDKING, 0); CHECK(b);
  enter(b);
  for (int i = 0; i < 3; i++) {
    int n = 0; Cloud *c = 0;
    while (n++ < 2000) { run(0, 1); for (int j = 0; j < 3; j++) if (bcl[j].a && (bcl[j].y >> 8) >= bcl[j].lane && !bcl[j].strike && !bcl[j].charge) c = bcl + j; if (c && !b->hurt) break; c = 0; }
    CHECK(c);
    place((b->ax0 + 2)*8, floorpx - 11);
    cst = 1; ckind = CAPFORWARD; cvx = cvy = 0; cxp = c->x + (8 << 8); cyp = c->y; tick(0);
    CHECK(cst == 3);   // the cap went into a cloud (maybe one passing in front of it)
    n = 0; while (b->hp == 2 - i + 1 && n++ < 300) run(0, 1);
    CHECK(b->hp == 2 - i && st != DEAD);
  }
  beaten(b);
  // Hatrick can ride a cloud
  go(BOSS_CLOUDKING, 0); b = find(BOSS_CLOUDKING, 0); enter(b);
  { int n = 0; Cloud *c = 0; while (!c && n++ < 2000) { run(0, 1); if (bcl[0].a && (bcl[0].y >> 8) >= bcl[0].lane && !bcl[0].charge && !bcl[0].strike) c = bcl; }
    CHECK(c); place((c->x >> 8) + 9, (c->y >> 8) - 20); run(0, 12); CHECK(bostand && gnd && (hy >> 8) + 11 == (c->y >> 8) - 4); }
  // ---- the Haberdasher: phase 1, hit him while he pants; phase 2, wear his hat to stomp him
  go(BOSS_HABERDASHER, 0); b = find(BOSS_HABERDASHER, 0); CHECK(b && b->phase == 1);
  enter(b);
  { int n = 0, thrown = 0; while (b->s != 3 && n++ < 3000) { run(0, 1); for (BShot *s = bsh; s < bsh+24; s++) thrown |= s->a && s->kind == BS_SCISSORS; if (st == DEAD) place((b->ax0 + 2)*8, b->my*8 - 3); }
    CHECK(thrown && b->s == 3); }
  stomp(b, 5, -7); CHECK(b->hp == 2);
  for (int i = 0, tries = 0; i < 2 && tries < 6; tries++) {   // a stomp can glance off while he hops: try again on his next pant
    int n = 0, hp = b->hp; while (b->s != 3 && n++ < 3000) { place((b->ax0 + 2)*8, b->my*8 - 60); run(0, 1); for (BShot *s = bsh; s < bsh+24; s++) s->a = 0; }
    CHECK(b->s == 3);
    stomp(b, 5, -7);
    if (b->hp != hp || b->phase == 2) i++;
    else { n = 0; while (b->s == 3 && n++ < 300) { place((b->ax0 + 2)*8, b->my*8 - 60); run(0, 1); } }
  }
  CHECK(b->phase == 2 && b->hp == 3 && hatst == 1);
  for (int i = 0; i < 3; i++) {
    while (b->hurt) { run(0, 1); for (BShot *s = bsh; s < bsh+24; s++) s->a = 0; }
    place((b->ax0 + 3)*8, b->my*8 - 3);
    cst = 1; ckind = CAPFORWARD; cvx = cvy = 0; cxp = (hatx - 4) << 8; cyp = (haty - 2) << 8; tick(0);
    CHECK(hatst == 2);
    for (BShot *s = bsh; s < bsh+24; s++) s->a = 0;
    if (i == 0) {   // wearing it, a touch only knocks it off
      place((b->x >> 8) + 2, (b->y >> 8) + 6); tick(0); CHECK(st != DEAD && hatst == 1);
      cst = 1; cvx = cvy = 0; cxp = (hatx - 4) << 8; cyp = (haty - 2) << 8; tick(0); CHECK(hatst == 2);
    }
    { int n = 0; while (!b->gnd && n++ < 200) run(0, 1); }
    stomp(b, 5, 0);
    CHECK(b->hp == 2 - i && st != DEAD);
  }
  beaten(b);
  printf("boss tests: %d checks passed\n", checks);
  return 0;
}
