// Checks for the cap mechanics in cap.h: cap posts, cap switches with red / blue blocks, the hat
// swap powers, coins the cap carries home, bent throws, the lantern and hat-rack checkpoints.
// gcc -O1 -w tools/test_cap.c -o /tmp/hatrick-cap-tests && /tmp/hatrick-cap-tests
#define SIM
#define SC 1
#define main replay_main
#include "../hatrick.c"
#undef main
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static void use(const char *text) {   // a fresh run on the first level of text
  CHECK(parselevels(text, "cap.txt"));
  menu = resumable = done = donet = naming = 0; lvl = 0;
  deaths = coins = lcoins = score = lscore = lstart = tim = 0;
  load(); prevk = 0;
}
static void run(int k, int n) { while (n--) tick(k); }
static void place(int x, int y) { hx = x << 8; hy = y << 8; hvx = hvy = 0; }   // px, top-left of the body
static int X(void) { return hx >> 8; }
static int Y(void) { return hy >> 8; }
static void settle(void) { for (int i = 0; i < 40 && !gnd; i++) tick(0); run(0, 2); }

int main(void) {
  // ---- cap posts: a throw sticks for 2 s, Hatrick can stand on it, a cap button pulls him over
  use("= 1 post\n"
      "\n"
      "@    H        F\n"
      "###############\n");
  CHECK(map[30][5] == TC_POST && !(SOLID >> TC_POST & 1));
  place(8, 30*8-3); settle(); CHECK(gnd);
  run(32, 1); run(0, 1);
  for (int i = 0; i < 20 && cst != 4; i++) tick(0);
  CHECK(cst == 4 && cxp >> 8 == 5*8 && cyp >> 8 == 30*8+1);
  run(0, 60); CHECK(cst == 4);
  run(0, 70); CHECK(cst != 4);                     // lets go after about 2 s
  for (int i = 0; i < 40 && cst; i++) tick(0);
  CHECK(!cst);
  // standing on it
  place(8, 31*8-11); settle(); run(32, 1); run(0, 1);
  for (int i = 0; i < 20 && cst != 4; i++) tick(0);
  CHECK(cst == 4);
  place(5*8+1, 30*8+1-20); hvy = 0; gnd = 0; st = NORM;
  for (int i = 0; i < 30 && !gnd; i++) tick(0);
  CHECK(gnd && Y() == 30*8+1-11);                   // feet on the cap's top
  run(0, 10); CHECK(gnd && Y() == 30*8+1-11);
  // the grapple: a cap press pulls Hatrick to it
  load(); place(8, 31*8-11); settle(); run(32, 1); run(0, 1);
  for (int i = 0; i < 20 && cst != 4; i++) tick(0);
  CHECK(cst == 4);
  int x0 = X(); run(32, 1); run(0, 1);
  CHECK(capx_pull && hvx > 0 && hvy < 0);
  for (int i = 0; i < 40 && capx_pull; i++) tick(0);
  CHECK(!capx_pull && X() > x0 + 16 && cst != 4);

  // ---- cap switches: only the cap toggles them; red and blue blocks swap
  use("= 1 switch\n"
      "\n"
      "@   X   R U   F\n"
      "###############\n");
  CHECK(map[30][4] == TC_SWITCH && map[30][8] == TC_RED && map[30][10] == TC_BLUEOFF && !capx_blue);
  CHECK(SOLID >> TC_RED & 1 && !(SOLID >> TC_BLUEOFF & 1) && SOLID >> TC_SWITCH & 1);
  place(8, 31*8-11); settle(); run(32, 1); run(0, 1);
  for (int i = 0; i < 30 && cst == 1; i++) tick(0);
  CHECK(capx_blue && map[30][8] == TC_REDOFF && map[30][10] == TC_BLUE);
  for (int i = 0; i < 60 && cst; i++) tick(0);
  CHECK(!cst && capx_blue);                         // one throw, one toggle
  place(3*8+1, 31*8-11); run(16, 1); run(0, 30);    // a head bump does nothing
  CHECK(capx_blue);
  // a checkpoint keeps the switch state; a restart puts it back
  use("= 1 switch ck\n"
      "\n"
      "@X    K R      F\n"
      "################\n");
  place(3*8, 31*8-11); settle(); face = -1; run(32, 1); run(0, 1);
  for (int i = 0; i < 40 && cst; i++) tick(0);
  CHECK(capx_blue);
  for (int i = 0; i < 120 && !haveck; i++) tick(2);
  CHECK(haveck);
  die(); run(0, 62); CHECK(st == NORM && capx_blue && map[30][8] == TC_REDOFF);
  load(); CHECK(!capx_blue && map[30][8] == TC_RED);

  // ---- hat swap: a walker knocked out by the cap gives the heavy pound, which breaks stone
  use("= 1 heavy\n"
      "\n"
      "@    g           F\n"
      "##########SS######\n"
      "##########SS######\n");
  place(8, 30*8-3); settle();
  for (int i = 0; i < 60 && en[0].a; i++) { tick(i % 30 == 0 ? 32 : 0); }
  CHECK(!en[0].a && capx_pow == POW_HEAVY);
  CHECK(capx_pal(HPAL)[0] == CAPX_COL[POW_HEAVY]);
  place(10*8+1, 26*8); gnd = 0; st = NORM; run(0, 1); run(8, 1); run(0, 40);
  CHECK(map[30][10] == 4 && map[31][10] == 4 && st != DEAD);   // stone never breaks, even under the heavy pound
  // nor without the power
  use("= 1 plain\n\n@               F\n##########SS######\n##########SS######\n");
  place(10*8+1, 26*8); gnd = 0; st = NORM; run(0, 1); run(8, 1); run(0, 40);
  CHECK(map[30][10] == 4 && gnd);

  // ---- a power takes a hit instead of a life; pits always count
  use("= 1 hit\n\n@     ^       F\n###############\n");
  place(8, 31*8-11); settle(); capx_pow = POW_FLUTTER;
  int d0 = deaths; die();
  CHECK(st != DEAD && deaths == d0 && !capx_pow && capx_inv);
  die(); CHECK(st != DEAD);                         // still blinking
  run(0, 95); CHECK(!capx_inv);
  die(); CHECK(st == DEAD && deaths == d0+1);
  load(); capx_pow = POW_HEAVY; hy = (lh*8+12) << 8; die(); CHECK(st == DEAD);

  // ---- flutter: a buzzer's power; a new Jump press while falling hovers
  use("= 1 flutter\n"
      "\n"
      "@     b       F\n"
      "###############\n");
  CHECK(en[0].t == 2);
  capx_gain(POW_FLUTTER, 0, 0); CHECK(capx_pow == POW_FLUTTER);
  place(20, 10*8); gnd = 0; st = NORM; capx_fuel = 48; run(0, 12);
  int fall = hvy; run(16, 1); run(16, 20);
  CHECK(hvy < fall && hvy <= 0);                    // hovering
  run(16, 40); CHECK(!capx_fuel && hvy > 0);   // runs out
  capx_pow = 0; place(20, 10*8); hvy = 300; gnd = 0; capx_fuel = 48; run(16, 1); run(16, 10); CHECK(hvy > 300);   // no power, no hover
  // the cap knocks the buzzer out: flutter
  use("= 1 buzz\n"
      "\n"
      "     h\n"
      "\n"
      "@             F\n"
      "###############\n");
  place(5*8, 31*8-11); settle();
  for (int i = 0; i < 120 && en[0].a; i++) tick(i % 30 == 0 ? 4|32 : 0);   // upward throws
  CHECK(!en[0].a && capx_pow == POW_FLUTTER);

  // ---- seed power: a spitter beaten by the cap; every throw then lobs a seed that beats walkers
  use("= 1 seeds\n"
      "\n"
      "@         g     F\n"
      "#################\n");
  place(8, 31*8-11); settle(); capx_gain(POW_SEED, 0, 0);
  run(32, 1); CHECK(capx_seed[0].a || capx_seed[1].a);
  for (int i = 0; i < 200 && en[0].a; i++) { if (i % 12 == 0) capx_lob(); tick(0); }
  CHECK(!en[0].a && capx_pow == POW_SEED && !capx_inv);   // the seeds got it before it reached Hatrick

  // ---- coins ride home on the cap
  use("= 1 coins\n\n@  oo         F\n###############\n");
  place(8, 31*8-11); settle(); capthrow(0, 0, 0, 0);
  for (int i = 0; i < 8; i++) capupd(32);
  CHECK(!map[30][3] && !map[30][4] && capx_carry == 2 && coins == 0);
  for (int i = 0; i < 80 && cst; i++) tick(0);
  run(0, 1); CHECK(coins == 2 && !capx_carry && score == 200);
  // lost with a death
  use("= 1 coins2\n\n@  oo         F\n###############\n");
  place(8, 31*8-11); settle(); capthrow(0, 0, 0, 0);
  for (int i = 0; i < 8; i++) capupd(32);
  CHECK(capx_carry == 2); die(); run(0, 62); CHECK(coins == 0 && !capx_carry);

  // ---- Up / Down bend a forward throw
  use("= 1 bend\n\n@              F\n###############\n");
  place(8, 20*8); gnd = 0; cst = 0; capupd(0); capthrow(0, 0, 0, 0); CHECK(ckind == CAPFORWARD && cvx > 0 && !cvy);
  for (int i = 0; i < 6; i++) capupd(8);
  CHECK(cvy > 0);
  place(8, 20*8); cst = 0; capupd(0); capthrow(0, 0, 0, 0); for (int i = 0; i < 6; i++) capupd(4);
  CHECK(cvy < 0);
  face = -1; place(80, 20*8); cst = 0; capupd(0); capthrow(0, 0, 0, 0); for (int i = 0; i < 6; i++) capupd(8);
  CHECK(cvx < 0 && cvy > 0);                        // down is down both ways
  face = 1; place(8, 20*8); cst = 0; capupd(0); capthrow(0, 0, 0, 0); for (int i = 0; i < 6; i++) capupd(0);
  CHECK(!cvy);                                      // no bend without Up or Down

  // ---- the lantern: bg=dark areas; the cap leaves spots of light
  CHECK(parselevels("= 1 dark bg=dark\n@  F\n####\n+ den bg=dark\n####\n+ lit\n####\n", "cap.txt"));
  CHECK(LV[0].room[0].cave == 2 && LV[0].room[1].cave == 2 && LV[0].room[2].cave == 1);
  use("= 1 dark bg=dark\n\n@              F\n###############\n");
  place(8, 31*8-11); settle(); run(32, 1); run(0, 12);
  int lit = 0; for (int i = 0; i < CAPX_NL; i++) lit += capx_light[i].t > 0;
  CHECK(lit >= 2);
  run(0, CAPX_GLOW + 40); lit = 0; for (int i = 0; i < CAPX_NL; i++) lit += capx_light[i].t > 0;
  CHECK(!lit);
  render();                                          // darkness draws without trouble
  CHECK(big[0][0] != 0 && (big[0][0] & 0xff) < 0x40);
  use("= 1 light\n\n@              F\n###############\n");
  run(32, 1); run(0, 12); CHECK(!capx_light[0].t);

  // ---- hat racks: the checkpoint draws as a rack with a cap once touched
  use("= 1 rack\n\n@ K            F\n###############\n");
  render();
  for (int i = 0; i < 60 && !haveck; i++) tick(2);
  run(0, 30); render();
  CHECK(haveck && wd.ck[0].up == 20);

  printf("PASS: %d cap checks\n", checks);
  return 0;
}
