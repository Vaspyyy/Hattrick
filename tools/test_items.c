// Checks for items.h (the coin bank, the shop, the pause screen's bag and the eight items) and
// travel.h (the quick level select on the map).
// gcc -O1 -w tools/test_items.c -o /tmp/hatrick-item-tests && /tmp/hatrick-item-tests   (from the game folder)
#define SIM
#define SC 1
#define main replay_main
#include "../hatrick.c"
#undef main
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static void use(const char *text) {   // a fresh run on the first level of text
  CHECK(parselevels(text, "items.txt"));
  menu = resumable = done = donet = naming = 0; lvl = 0;
  deaths = coins = lcoins = score = lscore = lstart = tim = 0;
  it_level(); load(); prevk = 0; resumable = 1;
}
static void run(int k, int n) { while (n--) tick(k); }
static void tap(int k) { tick(k); tick(0); }
static void place(int x, int y) { hx = x << 8; hy = y << 8; hvx = hvy = 0; }   // px, top-left of the body
static int X(void) { return hx >> 8; }
static int Y(void) { return hy >> 8; }
static void settle(void) { for (int i = 0; i < 40 && !gnd; i++) tick(0); run(0, 2); }
static void hit(void) { capx_inv = 0; die(); }   // a hit, past the blink of the last one
static void give(int i) { it_read = 1; it_have[i] = 1; }
static void pauseuse(int i) {   // pause, ITEMS, pick item i, use it
  tap(BACK); CHECK(menu && resumable); tap(8); CHECK(pausesel == 1); tap(16); CHECK(it_bag);
  it_bagsel = i; tap(16);
}

static const char *FLAT =
  "= 1 flat\n"
  "\n"
  "\n"
  "\n"
  "\n"
  "@            o o                                                          F\n"
  "###########################################################################\n";

int main(void) {
  // ---- the bank: a clear banks the coins of the visit; the shop sells for them
  use(FLAT); it_read = 1; it_bank = 0; memset(it_have, 0, sizeof it_have);
  place(8, 31*8-11); settle();
  coins = 12; lcoins = 2; hx = (gx*8-3) << 8; hy = (gy*8-12) << 8; hvx = 300; tick(2);
  CHECK(st == WIN && it_bank == 10);
  // the shop: Down in the house; Jump buys what the bank can pay for
  menu = 1; resumable = 0; clhouse = 1; it_shop = 0; prevk = 0;
  tap(8); CHECK(it_shop);
  it_shopsel = IT_PIE; tap(16); CHECK(it_have[IT_PIE] == 0 && it_bank == 10);   // 30 coins: too dear
  it_bank = 100; tap(16); CHECK(it_have[IT_PIE] == 1 && it_bank == 70);
  tap(2); CHECK(it_shopsel == IT_FEATHER); tap(8); CHECK(it_shopsel == IT_MAGNET); tap(4); CHECK(it_shopsel == IT_FEATHER);
  tap(1); tap(1); CHECK(it_shopsel == IT_FLAG);
  tap(32); CHECK(!it_shop && clhouse); tap(32); CHECK(!clhouse);
  it_have[IT_FLAG] = IT_MAX; it_shop = 1; it_shopsel = IT_FLAG; it_bank = 100; clhouse = 1; tap(16);
  CHECK(it_have[IT_FLAG] == IT_MAX && it_bank == 100);   // a full bag
  clhouse = it_shop = 0;

  // ---- the bag: ITEMS on the pause screen; using one goes straight back into the level
  use(FLAT); place(8, 31*8-11); settle(); memset(it_have, 0, sizeof it_have); give(IT_PIE);
  pauseuse(IT_PIE); CHECK(!menu && !it_bag && it_pie && hp == 4 && !it_have[IT_PIE]);
  give(IT_PIE); pauseuse(IT_PIE); CHECK(menu && it_bag && it_have[IT_PIE] == 1 && it_bagmsg);   // one pie at a time
  tap(32); CHECK(menu && !it_bag); tap(32); CHECK(!menu);
  pauseuse(IT_MAGNET); CHECK(menu && it_bag && !it_magnet);   // none in the bag
  tap(32); tap(32); CHECK(!menu);
  // ---- heart pie: a 4th heart, refilled by checkpoints, gone after going down
  hit(); CHECK(st != DEAD && hp == 3); hit(); hit(); CHECK(hp == 1);
  hit(); CHECK(st == DEAD);
  for (int i = 0; i < 70 && st == DEAD; i++) tick(0);
  CHECK(st != DEAD && hp == 3 && !it_pie);

  // ---- feather cap: holding Jump while falling glides
  use(FLAT); give(IT_FEATHER); CHECK(!it_use(IT_FEATHER) && it_feather);
  place(40, 100); gnd = 0; st = NORM; hvy = 0; int y0 = Y();
  run(16, 60); CHECK(!gnd && hvy == IT_GLIDE);
  int glide = Y() - y0;
  use(FLAT); place(40, 100); gnd = 0; st = NORM; hvy = 0; y0 = Y(); it_feather = 0;
  for (int i = 0; i < 60 && !gnd; i++) tick(16);
  CHECK(gnd || Y() - y0 > glide + 40);   // without it the same minute falls much farther

  // ---- boomerang cap: twice as far, then straight home, hitting on the way back
  use(FLAT); place(8, 31*8-11); settle();
  tap(32); int far = 0; for (int i = 0; i < 60 && cst; i++) { tick(0); if (cst == 1 && (cxp >> 8) - X() > far) far = (cxp >> 8) - X(); }
  CHECK(!cst);
  give(IT_BOOM); CHECK(!it_use(IT_BOOM) && it_boom);
  int farb = 0, saw2 = 0;
  tap(32); for (int i = 0; i < 90 && cst; i++) { tick(0); saw2 |= cst == 2; if ((cxp >> 8) - X() > farb) farb = (cxp >> 8) - X(); }
  CHECK(!cst && !saw2 && farb > far * 3/2);
  // on the way back it knocks out a walker it passes
  use(FLAT); it_boom = 1; place(8, 31*8-11); settle();
  tap(32); for (int i = 0; i < 90 && !it_boomback; i++) tick(0);
  CHECK(it_boomback && cst == 1);
  E *w = en + ne++; *w = (E){ ((cxp >> 8) - 30) << 8, 31*8-8 << 8, 0, 0, 1, 1, 31*8-8 << 8, 0 };
  for (int i = 0; i < 60 && w->a; i++) tick(0);
  CHECK(!w->a);

  // ---- gold cap: nothing hurts, enemies run into are knocked out, then it wears off
  use(FLAT); place(8, 31*8-11); settle(); give(IT_GOLD); CHECK(!it_use(IT_GOLD) && it_gold == IT_GOLDT);
  w = en + ne++; *w = (E){ (X()+12) << 8, 31*8-8 << 8, -100, 0, 1, 1, 31*8-8 << 8, 0 };
  for (int i = 0; i < 30 && w->a; i++) tick(0);
  CHECK(!w->a && hp == 3 && st != DEAD);
  hit(); CHECK(hp == 3 && st != DEAD);
  doom(); CHECK(st == DEAD);   // lava still counts
  use(FLAT); it_gold = 3; run(0, 3); CHECK(!it_gold); hit(); CHECK(hp == 2);

  // ---- spring shoes: one more jump in the air, back on landing, lost to a hit
  use(FLAT); place(8, 31*8-11); settle(); give(IT_SHOES); CHECK(!it_use(IT_SHOES) && it_shoes);
  run(16, 6); run(0, 2); CHECK(!gnd);
  tick(16); CHECK(it_ajump && hvy < -800); tick(0);
  int vy = hvy; tick(16); CHECK(hvy >= vy);   // only one
  settle(); CHECK(gnd && !it_ajump);
  hit(); CHECK(hp == 2 && !it_shoes);

  // ---- magnet: coins within three tiles come in
  use(FLAT); place(8, 31*8-11); settle(); CHECK(map[30][13] == 6);
  give(IT_MAGNET); CHECK(!it_use(IT_MAGNET));
  place(13*8 - 22, 31*8-11); settle(); CHECK(map[30][13] == 0 && map[30][15] == 6);
  // and moon coins
  use("= 1 moon\n\n@      (          F\n###################\n");
  CHECK(LV[0].nmoon == 1);
  place(8, 31*8-11); settle(); it_magnet = 1; place(LV[0].moon[0].x*8 - 22, 31*8-11); tick(0);
  CHECK(wd.moongot == 1);

  // ---- egg buddy: follows Hatrick and takes one hit
  use(FLAT); place(8, 31*8-11); settle(); give(IT_EGG); CHECK(!it_use(IT_EGG) && it_egg);
  place(200, 31*8-11); run(2, 60);
  CHECK(iabs((it_eggx >> 8) - X() + 11) < 12);
  hit(); CHECK(!it_egg && hp == 3 && capx_inv);

  // ---- flag: a checkpoint where Hatrick stands
  use(FLAT); place(8, 31*8-11); settle(); place(300, 31*8-11); settle(); give(IT_FLAG);
  int fx = hx, fy = hy;
  CHECK(!it_use(IT_FLAG) && haveck && ckx == fx && cky == fy && !it_have[IT_FLAG]);
  place(400, 31*8-11); settle(); doom();
  for (int i = 0; i < 70 && st == DEAD; i++) tick(0);
  CHECK(st != DEAD && hx == fx && hy == fy);
  hvy = -300; gnd = 0; give(IT_FLAG); CHECK(it_use(IT_FLAG) && it_have[IT_FLAG]);   // in the air: no

  // ---- the quick level select: Start on the map opens the list; Jump goes there
  builtinlevels(); nprog = 0; for (int i = 0; i < 4; i++) progkeep(i, 8);
  lvl = 0; load(); menu = 1; resumable = 0; scoreview = naming = clhouse = 0; prevk = 0; mapstart();
  int at = mapat; tap(START); CHECK(trav && mapat == at);
  int l[64], n = trav_list(l);
  CHECK(l[0] == 0 && l[n-1] == nnode-1 && n >= 7);   // home, the open levels, the playground
  travsel = 0; tap(8); tap(8); CHECK(travsel == 2);
  tap(16); CHECK(!trav && mapat == l[2] && mapto < 0);
  tap(START); CHECK(trav && travsel == 2); tap(4); CHECK(travsel == 1); tap(4); tap(4); CHECK(travsel == n-1);   // wraps around
  tap(32); CHECK(!trav && menu && !quitting);
  tap(16); CHECK(!menu && lvl == node[l[2]].lvl);   // Jump on the stop plays it

  printf("items: %d checks passed\n", checks);
  return 0;
}
