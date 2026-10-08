// Checks for the enemies in enemies.h (ideas.md section 3): shy-walker, shell walker, cap thief,
// cloud rider, chomp, puffer buzzer, thwomp, and the beat platforms, pistons and fire bars.
// gcc -O1 -w tools/test_enemies.c -o /tmp/hatrick-enemy-tests && /tmp/hatrick-enemy-tests
#define SIM
#define SC 1
#define main replay_main
#include "../hatrick.c"
#undef main
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static void use(const char *text) {
  fflush(stderr); int saved = dup(2); FILE *t = tmpfile(); dup2(fileno(t), 2);
  int ok = parselevels(text, "mod.txt");
  fflush(stderr); dup2(saved, 2); close(saved); fclose(t);
  CHECK(ok);
  menu = resumable = done = donet = naming = 0; lvl = 0;
  deaths = coins = lcoins = score = lscore = lstart = tim = 0;
  load(); prevk = 0;
}
static void run(int k, int n) { while (n--) tick(k); }
static void place(int x, int y) { hx = x << 8; hy = y << 8; hvx = hvy = 0; }   // px, top-left of the body
static int Y(void) { return hy >> 8; }
static E *find(int t) { for (E *e = en; e < en+ne; e++) if (e->t == t) return e; return 0; }
static void settle(void) { run(0, 30); }   // past the opening iris and onto the ground

int main(void) {
  // ---- parsing: every character becomes one enemy entry (a run of Y is one platform)
  use("= 1 zoo\n"
      "                         L\n"
      "\n"
      "         Q\n"
      "\n"
      "\n"
      "                   p   v\n"
      "@  u  k   x   YYY     ZZ  $~~ F\n"
      "##############   #####################\n");
  for (int t = T_SHY; t <= T_PISTON; t++) CHECK(find(t) && find(t)->a);
  CHECK(find(T_BEATPLAT)->w == 3 && find(T_BEATPLAT)->vx == 1);
  CHECK(map[30][14] == 4 && map[30][22] == 4 && map[30][26] == 16);   // Y and Z are stone, $ a pivot
  CHECK(LV[0].nbar == 1 && LV[0].bar[0].speed == 1);
  int nz = 0; for (E *e = en; e < en+ne; e++) nz += e->t == T_PISTON;
  CHECK(nz == 2 && find(T_PISTON)->vy == -1);   // both shoot upward

  const char *FLAT = "= 1 flat\n"
                     "@                                      F\n"
                     "########################################\n"
                     "########################################\n";
  #define ADD(T, X, Y) (en[ne] = (E){ (X) << 8, (Y) << 8, 0, 0, T, 1, (Y) << 8, 0 }, en + ne++)

  // ---- shy-walker: the mask blocks a cap thrown at its face, a cap from behind knocks it over
  use(FLAT); settle(); place(40, 229);
  E *s = ADD(T_SHY, 76, 232); s->vx = -100;
  run(32, 1); run(0, 12);
  CHECK(s->a && cst == 3 && score == 0);   // bounced off: the cap flies home
  use(FLAT); settle(); place(40, 229); face = 1;
  s = ADD(T_SHY, 56, 232); s->vx = 100;
  run(32, 1); run(0, 6);
  CHECK(!s->a && score == 300);
  use(FLAT); settle(); place(40, 229);
  s = ADD(T_SHY, 47, 232); s->vx = 100;   // walking away: bump its back
  for (int i = 0; i < 60 && s->a && st == NORM; i++) run(2, 1);
  CHECK(!s->a && st == NORM);
  use(FLAT); settle(); place(40, 229);
  s = ADD(T_SHY, 52, 232); s->vx = -100;  // walking at Hatrick: that hurts
  run(0, 20); CHECK(st == DEAD && s->a);

  // ---- shell walker: stomp it, kick the shell, and the shell knocks a walker over
  use(FLAT); settle();
  E *k = ADD(T_SHELL, 100, 232); k->vx = 0;
  E *w = ADD(1, 200, 232); w->vx = 0;
  place(101, 200); hvy = 400;
  for (int i = 0; i < 20 && k->s == 0; i++) run(0, 1);
  CHECK(k->s == 1 && k->vx == 0 && st != DEAD && hvy < 0);
  place(80, 229); run(0, 40);
  for (int i = 0; i < 60 && k->s == 1; i++) run(2, 1);   // walk into it from the left
  CHECK(k->s == 2 && k->vx > 0 && st != DEAD);
  for (int i = 0; i < 120 && w->a; i++) run(0, 1);
  CHECK(!w->a && k->a && k->s == 2);
  // it breaks bricks it slides into, and comes back the other way
  use("= 1 bricks\n"
      "@                         B   F\n"
      "################################\n");
  settle(); k = ADD(T_SHELL, 150, 240); k->s = 2; k->vx = 640; k->u = 0;
  place(20, 229);
  for (int i = 0; i < 60 && map[30][26] == 2; i++) run(0, 1);
  CHECK(map[30][26] == 0 && k->vx < 0);

  // ---- cap thief: grabs a thrown cap and keeps it until Hatrick touches it
  use(FLAT); settle(); place(40, 229); face = 1;
  E *v = ADD(T_THIEF, 80, 222); v->w = v->x;
  run(32, 1);
  for (int i = 0; i < 60 && v->s != 2; i++) { run(0, 1); }
  run(0, 1);
  CHECK(v->s == 2 && cst == 2 && capstolen());
  int cx0 = cxp; run(32, 1); run(0, 1); run(32, 1); run(0, 20);
  CHECK(cst == 2 && capstolen() && cxp != cx0);   // no throwing, the bird has it and moves
  place((v->x >> 8) - 2, (v->y >> 8) - 2); run(0, 1);
  CHECK(cst == 3 && !capstolen() && v->s == 3 && st != DEAD);
  for (int i = 0; i < 60 && cst; i++) run(0, 1);
  CHECK(cst == 0);   // the cap is back on Hatrick's head

  // ---- cloud rider: follows Hatrick and drops walkers, at most three of its own
  use(FLAT); settle(); place(100, 229);
  E *l = ADD(T_RIDER, 160, 150);
  int n0 = ne; run(0, 160);
  CHECK(ne == n0 + 1 && en[n0].t == 1);
  place(20, 229); run(0, 1000);   // far below, out of harm's way? no: walkers walk; keep it alive
  int mine = 0; for (E *e = en; e < en+ne; e++) mine += e->a && e->t == 1 && e->s == 1000 + (int)(l - en) + 1;
  CHECK(mine <= 3);
  CHECK(iabs((l->x >> 8) - (hx >> 8)) < 120 || st == DEAD);

  // ---- chomp: lunges at Hatrick nearby; three ground pounds on the stake set it loose
  use("= 1 chomp\n"
      "@            x       BB    F\n"
      "#############################\n"
      "#############################\n");
  settle(); E *c = find(T_CHOMP); CHECK(c);
  place(13*8+1, 29*8-11-30);
  for (int p = 0; p < 3; p++) {
    hvy = 0; st = GPSLAM; hy = (29*8-11-14) << 8; hx = (13*8+1) << 8; face = 1;
    for (int i = 0; i < 20 && st == GPSLAM; i++) run(0, 1);
    CHECK(c->w == p+1);
  }
  CHECK(c->s == 10 && c->vx > 0);
  for (int i = 0; i < 200 && c->a; i++) run(0, 1);
  CHECK(map[29][21] == 0 && map[29][22] == 0);   // the bricks are gone
  use("= 1 chomp\n"
      "@            x            F\n"
      "#############################\n"
      "#############################\n");
  settle(); place(13*8-30, 229);
  for (int i = 0; i < 90 && st != DEAD; i++) run(0, 1);
  CHECK(st == DEAD);   // it got Hatrick

  // ---- puffer: the cap puffs it up, then it's a trampoline
  use(FLAT); settle(); place(40, 229); face = 1;
  E *pf = ADD(T_PUFF, 60, 228);
  for (int i = 0; i < 400 && pf->s == 0; i++) run(!cst && !(i & 1) ? 32 : 0, 1);   // throw until it catches the bobbing puffer
  CHECK(pf->s == 1 && pf->a);
  place((pf->x >> 8), (pf->y >> 8) - 30); hvy = 200;
  int bounced = 0; for (int i = 0; i < 30 && !bounced; i++) { run(0, 1); bounced = hvy <= -1000; }
  CHECK(bounced && st != DEAD);
  run(0, 300); CHECK(pf->s == 0);

  // ---- thwomp: drops on Hatrick below, then rises and carries a rider back up
  use(FLAT); settle(); place(100, 229);
  E *q = ADD(T_THWOMP, 96, 150);
  run(0, 1); CHECK(q->s == 1);
  for (int i = 0; i < 60 && st != DEAD; i++) run(0, 1);
  CHECK(st == DEAD);
  use(FLAT); settle(); place(130, 229);
  q = ADD(T_THWOMP, 96, 150); q->y = 224 << 8; q->s = 2; q->u = 5;   // sitting on the ground
  place(100, 200); run(0, 20);
  CHECK(gnd && Y() + 11 == (q->y >> 8));
  int y0 = Y(); run(0, 60);
  CHECK(Y() < y0 - 20 && Y() + 11 == (q->y >> 8) && st != DEAD);   // rode it up

  // ---- the beat: platforms shift on every fourth beat, pistons stab on beats 2 and 4, bars snap
  use("= 1 beat music=overworld\n"
      "\n"
      "@     YY          ZZ   $~~~   F\n"
      "##########    ##################\n"
      "################################\n");
  settle();
  E *bp = find(T_BEATPLAT); CHECK(bp && bp->vx == 1);
  int gone = 0, moved = 0;
  for (int i = 0; i < 60*10; i++) {
    run(0, 1);
    int b = beatpos() >> 8;
    if ((b >> 2 & 1) && map[29][8] == 4 && map[29][9] == 4 && !map[29][6]) moved = 1;
    if (!(b >> 2 & 1) && (b & 3) == 2 && map[29][6] == 4 && map[29][7] == 4 && !map[29][8]) gone = 1;
  }
  CHECK(moved && gone);
  E *pz = find(T_PISTON); int out = 0, in = 0;
  for (int i = 0; i < 120; i++) { run(0, 1); if (pz->s) out++; else in++; CHECK(pz->s == ((beatpos() >> 8 & 1) && (beatpos() & 255) < 170)); }
  CHECK(out > 10 && in > 10);
  place(18*8+1, 28*8-11+3);
  for (int i = 0; i < 120 && st != DEAD; i++) { hy = (28*8-11+3) << 8; hvy = 0; run(0, 1); }
  CHECK(st == DEAD);
  const Bar *bar = LV[0].bar; int a0 = barangle(bar), changes = 0, last = a0;
  for (int i = 0; i < 300; i++) { fr++; int a = barangle(bar); if (a != last) changes++; last = a; }
  CHECK(changes > 0 && changes < 300);   // holds still between beats

  printf("enemies: %d checks passed\n", checks);
  return 0;
}
