// Checks for the collectibles (collect.h, collect_ui.h): postcards, the secret exit and its level
// on the map, star ratings, the caps on the house rack and the house screen.
// gcc -O1 -w tools/test_collect.c -o /tmp/hatrick-collect-tests && /tmp/hatrick-collect-tests
#define SIM
#define SC 1
#define main replay_main
#include "../hatrick.c"
#undef main
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static char err[8192];
static int parse(const char *text) {   // parselevels with its reports captured in err
  fflush(stderr); int saved = dup(2); FILE *t = tmpfile(); dup2(fileno(t), 2);
  int ok = parselevels(text, "mod.txt");
  fflush(stderr); dup2(saved, 2); close(saved);
  rewind(t); size_t n = fread(err, 1, sizeof err - 1, t); err[n] = 0; fclose(t);
  return ok;
}
static void use(const char *text, int l) {   // a fresh run on level l of text, nothing saved yet
  CHECK(parse(text));
  nprog = 0; clhouse = 0; cl_wear(0);
  menu = resumable = done = donet = naming = 0; lvl = l;
  deaths = coins = lcoins = score = lscore = lstart = tim = 0;
  load(); prevk = 0;
}
static void run(int k, int n) { while (n--) tick(k); }
static int find(const char *name) { for (int i = 0; i < NLEVEL; i++) if (!strcmp(LV[i].name, name)) return i; return -1; }

static const char *TWO =
  "= 1 meadow star=30\n"
  "@   P     G        F\n"
  "####################\n"
  "= 2 hill\n"
  "@  F\n"
  "####\n"
  "= cave shop secret=1\n"
  "@ (  F\n"
  "######\n"
  "= lab playground\n"
  "@ F\n"
  "###\n";

int main(void) {
  // ---- reading: options, the secret exit and postcard, secret levels sorted after the campaign
  CHECK(parse(TWO) && !*err);
  CHECK(NLV == 2 && NSEC == 1 && NLEVEL == 4 && PLAY == 2 && SEC0 == 3);
  int shop = find("CAVE SHOP");
  CHECK(shop == 3 && LV[shop].lab == 2 && !strcmp(LV[shop].cl.secretof, "1"));
  CHECK(LV[0].cl.star == 30 && LV[1].cl.star == 4*8/55 + 15);   // named, or from the width
  CHECK(LV[0].cl.sg && LV[0].cl.gr == 0 && LV[0].cl.gx == 10 && LV[0].cl.pc && LV[0].cl.px == 4);
  CHECK(parse("= 1 a star=0\n@ G G P P F\n###########\n"));
  CHECK(strstr(err, "star needs 1 to 9999") && strstr(err, "more than one secret exit") && strstr(err, "more than one postcard"));
  CHECK(parse("= 1 a\n@ F\n###\n= b secret=1\n@ F\n###\n") && NLV == 1 && NSEC == 1 && PLAY == -1);   // no labs: no playground

  // ---- the postcard: picked up once, kept, worth 1000
  use(TWO, 0);
  for (int i = 0; i < 60 && !(progbits(0) & PB_CARD); i++) tick(2);
  CHECK(progbits(0) & PB_CARD && score >= 1000 && clmsgt);
  int s0 = score; run(0, 2); CHECK(score == s0);

  // ---- the secret exit: a clear that opens the secret stop on the map, with its path drawn in
  for (int i = 0; i < 120 && st != WIN; i++) tick(2);
  CHECK(st == WIN && clsecretnow && gx == 10);
  CHECK(cleared(0) && progbits(0) & PB_SECRET && cl_secretopen(shop));
  CHECK(progbits(0) & PB_STAR && cl_starred(0));   // no moon coins here, and well inside 30 s
  run(0, 10); run(16, 1);   // jump skips the clear: back on the map
  CHECK(menu && !resumable);
  mapbuild();
  CHECK(nnode == 1 + 2 + 1 + 1 && nodeof(shop) == 3 && nodeof(PLAY) == 4 && node[nodeof(shop)].lvl == shop && cl_visible(nodeof(shop)) && nodeopen(nodeof(shop)));
  CHECK(unlocknode == nodeof(shop) && unlockt);
  int nb[4], k = neighbours(nodeof(0), nb), has = 0;
  for (int i = 0; i < k; i++) has |= nb[i] == nodeof(shop);
  CHECK(has && neighbours(nodeof(shop), nb) == 1 && nb[0] == nodeof(0));
  CHECK(mapstep(nodeof(0), nodeof(shop)) == nodeof(shop));
  // a secret level keeps its own record
  use(TWO, shop);
  for (int i = 0; i < 60 && st != WIN; i++) tick(2);
  CHECK(st == WIN && cleared(shop) && !(progbits(shop) & PB_SECRET));

  // ---- the plain flag: no secret, and a slow clear earns no star
  use(TWO, 1);
  lstart = tim - 99999;   // as if the level had taken half an hour
  for (int i = 0; i < 60 && st != WIN; i++) tick(2);
  CHECK(st == WIN && !clsecretnow && cleared(1) && !(progbits(1) & PB_SECRET) && !(progbits(1) & PB_STAR));
  // the hidden secret stop stays hidden until its exit is found
  nprog = 0; mapgen = -1; mapbuild();
  CHECK(!cl_visible(nodeof(shop)) && !nodeopen(nodeof(shop)));

  // ---- caps: one per level with every moon coin home; Feather stalls longer, Magnet pulls coins
  use("= 1 a\n@  (  F\n#######\n= 2 b\n@ F\n###\n", 0);
  CHECK(cl_nhats() == 3 && cl_hatopen(0) && !cl_hatopen(1));
  for (int i = 0; i < 60 && st != WIN; i++) tick(2);
  CHECK(st == WIN && cl_full(0) && cl_hatopen(1) && !cl_hatopen(2));
  CHECK(clmsg[0] && !strcmp(clmsg[0], "NEW CAP AT HOME"));
  cl_wear(2); CHECK(clstall == 6 && CAPSTALL == 14 && HPAL[0] == HATS[2].c);
  cl_wear(0); CHECK(clstall == 0 && CAPSTALL == 8 && HPAL[0] == 0xff7a1c);
  use("= 1 a\n    o\n\n@                 F\n###################\n", 0);
  cl_wear(4); run(0, 2);
  int c0 = coins; run(32, 1); run(0, 30);   // a throw along the ground passes under the coin
  CHECK(coins == c0 + 1);
  use("= 1 a\n    o\n\n@                 F\n###################\n", 0);
  c0 = coins; run(32, 1); run(0, 30);       // the classic cap leaves it
  CHECK(coins == c0);

  // ---- the house: left and right pick a cap, jump wears it if it is on the rack, up shows scores
  use("= 1 a\n@  (  F\n#######\n= 2 b\n@ F\n###\n", 0);
  for (int i = 0; i < 60 && st != WIN; i++) tick(2);
  tomap(); mapat = 0; mapenter();
  CHECK(clhouse && menu);
  run(2, 1); run(0, 1); CHECK(clsel == 1);
  run(16, 1); run(0, 1); CHECK(clhat == 1 && HPAL[0] == HATS[1].c && clhouse);
  run(2, 1); run(0, 1); CHECK(clsel == 2);
  run(16, 1); run(0, 1); CHECK(clhat == 1);   // still locked
  render();   // the house draws without trouble
  run(4, 1); run(0, 1); CHECK(!clhouse && scoreview);
  run(16, 1); run(0, 1); CHECK(!scoreview);
  mapenter(); run(32, 1); run(0, 1); CHECK(!clhouse && !quitting);
  render();   // the map with its stars and the grown house

  printf("collectibles: %d checks passed\n", checks);
  return 0;
}
