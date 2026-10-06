// Checks for the level objects (MODDING.md): checkpoints, tubes and bonus rooms, crumble blocks,
// hidden blocks, fire bars, tube dwellers, scoring, the course clear and the high-score table.
// gcc -O1 -w tools/test_features.c -o /tmp/hatrick-feature-tests && /tmp/hatrick-feature-tests
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
static void use(const char *text) {   // a fresh run on the first level of text
  CHECK(parse(text));
  menu = resumable = done = donet = naming = 0; lvl = 0;
  deaths = coins = lcoins = score = lscore = lstart = tim = 0;
  load(); prevk = 0;
}
static void run(int k, int n) { while (n--) tick(k); }
static void place(int x, int y) { hx = x << 8; hy = y << 8; hvx = hvy = 0; }   // px, top-left of the body
static int X(void) { return hx >> 8; }
static int Y(void) { return hy >> 8; }

int main(void) {
  // ---- checkpoints: touching one saves the level; a death comes back there, R starts over
  use("= 1 ck\n"
      "@ o  K  o   F\n"
      "#############\n");
  for (int i = 0; i < 100 && !haveck; i++) tick(2);   // run right: the first coin, then the checkpoint
  CHECK(haveck && wd.ck[0].up && coins == 1 && score == 100);
  CHECK(ckroom == 0 && ckx == (5*8+1) << 8 && cky == (30*8-3) << 8);
  for (int i = 0; i < 100 && coins < 2; i++) tick(2);
  CHECK(coins == 2 && !map[30][8]);   // a coin after it
  int t0 = tim; die(); CHECK(st == DEAD && deaths == 1);
  run(0, 62);
  CHECK(st == NORM && X() == 5*8+1 && coins == 1 && score == 100 && map[30][8] == 6 && !map[30][2]);
  CHECK(tim == t0 + 62 && wd.ck[0].up == 20);   // the run timer kept going; the banner stays up
  run(64, 1); run(0, 1);           // R: the whole level again
  CHECK(!haveck && X() == 0 && !wd.ck[0].up && coins == 0 && score == 0 && map[30][2] == 6);

  // ---- tubes: Down on top of a linked mouth, out of the partner in a bonus room, and back
  use("= 1 tube\n"
      "@   11      F\n"
      "    ||\n"
      "#############\n"
      "+ bonus\n"
      "  11\n"
      "  ||\n"
      "######\n");
  CHECK(LV[0].nroom == 2 && LV[0].ntube == 2 && LV[0].tube[0].link == 1 && LV[0].tube[1].room == 1 && LV[0].room[1].cave);
  place(4*8+5, 29*8-11); run(0, 2); CHECK(gnd && st == NORM);
  run(8, 1); CHECK(st == TUBE);
  run(8, 19); CHECK(room == 0 && Y() > 29*8-11);   // sliding in
  run(8, 21); CHECK(st == NORM && room == 1 && X() == 2*8+5 && Y() == 29*8-11 && score == 2000 && lw == 6);
  run(8, 10); CHECK(st != TUBE && room == 1);       // still holding Down: no instant way back
  run(0, 1); run(8, 1); CHECK(st == TUBE);
  run(0, 40); CHECK(st == NORM && room == 0 && X() == 4*8+5 && score == 2000);   // found once only
  // a dweller's tube and an unlinked one can't be entered
  use("= 1 shut\n"
      "@   MM      F\n"
      "    ||\n"
      "#############\n");
  place(4*8+5, 29*8-11); run(0, 2); run(8, 10); CHECK(st != TUBE && LV[0].tube[0].link < 0);

  // ---- side tubes: walk into the mouth
  use("= 1 side\n"
      "@     2-    F\n"
      "      2-\n"
      "############\n"
      "+ cellar bg=sky\n"
      "  -2\n"
      "  -2\n"
      "######\n");
  CHECK(LV[0].tube[0].dir == T_LEFT && LV[0].tube[1].dir == T_RIGHT && !LV[0].room[1].cave);
  for (int i = 0; i < 60 && st != TUBE; i++) tick(2);
  CHECK(st == TUBE && X() >= 6*8-6);
  run(2, 39); run(0, 1);
  CHECK(st == NORM && room == 1 && X() == 3*8+9 && face == 1);

  // ---- crumble blocks: shake while stood on, fall after half a second carrying Hatrick, come back
  use("= 1 crumble\n"
      "@                F\n"
      "###   CC   #######\n");
  place(6*8+1, 31*8-11); run(0, 20); place(0, 30*8-11); run(0, 1);   // stepped off early
  CHECK(map[31][6] == 13 && wd.cr[0].state == 0);
  run(0, 30); CHECK(wd.cr[0].t == 0);                                 // and it settles again
  place(6*8+1, 31*8-11); run(0, 31);
  CHECK(wd.cr[0].state == 1 && !map[31][6] && gnd && ride);           // riding it down
  int y0 = Y(); run(0, 10); CHECK(Y() > y0 + 2 && gnd);
  run(16, 1); CHECK(hvy < 0);                                         // and jumping off works
  hx = 0; hy = 29*8 << 8; run(0, 300);
  CHECK(wd.cr[0].state == 0 && map[31][6] == 13);                      // back in place

  // ---- hidden blocks: invisible and passable until bumped from below
  use("= 1 hidden\n"
      "   ?\n"
      "\n"
      "@        F\n"
      "##########\n");
  CHECK(map[28][3] == 14 && !(SOLID >> 14 & 1));
  place(3*8, 27*8-11); hvy = 200; run(0, 3); CHECK(Y() > 27*8-11);  // falls through from above
  place(3*8, 31*8-11); run(0, 2); run(16, 30);                        // jump into it from below
  CHECK(map[28][3] == 15 && coins == 1 && score == 1100 && (SOLID >> 15 & 1));
  run(0, 60); CHECK(gnd && Y() == 31*8-11);

  // ---- fire bars: the arm sets length, start angle and speed; touching any ember is lethal
  CHECK(parse("= 1 fire\n@  *~~~~  %::  F\n#################\n"));
  CHECK(LV[0].nbar == 2 && LV[0].bar[0].len == 5 && LV[0].bar[0].speed == 273 && LV[0].bar[0].a0 == 0);
  CHECK(LV[0].bar[1].len == 3 && LV[0].bar[1].speed == -182);
  use("= 1 fire\n"
      "\n"
      "@   *~~~~       F\n"
      "\n"
      "#################\n");
  fr = 0; place(4*8+4+2*8-3, 29*8+4-5); hazards(); CHECK(st == DEAD);     // on an ember of the arm
  load(); fr = 0; place(4*8+4-24, 29*8+4-5); hazards(); CHECK(st == NORM);  // behind the pivot
  fr = 240*128/273 - 1; while (((LV[0].bar[0].a0*256 + LV[0].bar[0].speed*fr) >> 8 & 255) != 128) fr++;
  hazards(); CHECK(st == DEAD);                                          // half a turn later it points there

  // ---- tube dwellers: rise on a timer, stay in while Hatrick is near, only the cap beats them
  use("= 1 dw\n"
      "@          n     F\n"
      "           MM\n"
      "           ||\n"
      "#################\n");
  CHECK(LV[0].nhome == 1 && LV[0].home[0].tube == 0);
  place(0, 31*8-11); run(0, 41); CHECK(wd.dw[0].phase == 1);
  run(0, 16); CHECK(wd.dw[0].phase == 2 && wd.dw[0].ofs == 16);
  run(0, 90); CHECK(wd.dw[0].phase == 0 && wd.dw[0].ofs == 0);
  place(11*8-6, 31*8-11); run(0, 200); CHECK(wd.dw[0].phase == 0);     // right next to the tube
  place(0, 31*8-11); while (wd.dw[0].phase != 2) tick(0);
  place(11*8+5, 29*8-11-8); hvy = 300; run(0, 3); CHECK(st == DEAD);   // landing on it doesn't stomp
  load(); place(0, 31*8-11); while (wd.dw[0].phase != 2) tick(0);
  cst = 1; ckind = CAPFORWARD; cxp = (11*8+4) << 8; cyp = (29*8-8) << 8; cvx = 0; cready = 0;
  hazards(); CHECK(!wd.dw[0].a && score == 500);
  // the spitter lobs seeds that arc down
  use("= 1 sp\n"
      "@          m     F\n"
      "           MM\n"
      "           ||\n"
      "#################\n");
  place(0, 31*8-11);
  int shots = 0;
  for (int i = 0; i < 200; i++) { tick(0); for (int j = 0; j < 8; j++) shots += wd.sh[j].a && wd.sh[j].vy < -500; }
  CHECK(shots >= 1);
  CHECK(parse("= 1 x\n@  n   F\n#######\n") && strstr(err, "tube dweller 'n' in column 4 must sit"));

  // ---- the course clear: height bonus, the flag slides, a pose, the time bonus ticks in, next level
  use("= 1 one\n"
      "             F\n"
      "\n"
      "\n"
      "@\n"
      "##############\n"
      "= 2 two\n"
      "@     F\n"
      "#######\n");
  tim = 600; lstart = 0; left = LIMIT - 601; place(13*8-3, 27*8-12); hvx = 300; run(2, 1);
  CHECK(st == WIN && score == 5000 && split == 601 && tally == 489 && left == LIMIT - 601);   // the very top; 489 s left
  int ticks = 0, f0 = flagy;
  for (int i = 0; i < 400 && lvl == 0; i++) { tick(0); ticks += nsnd && sndq[0] == S_TICK; nsnd = 0; }
  CHECK(lvl == 1 && st == NORM && score == 5000 + 489*50 && lscore == score && lstart > 600 && ticks >= 5 && f0 < gb && left == LIMIT - 1);   // the new level's timer, one frame in
  // low on the pole, and skipped with Jump
  use("= 1 one\n"
      "             F\n"
      "\n"
      "\n"
      "@\n"
      "##############\n"
      "= 2 two\n"
      "@     F\n"
      "#######\n");
  place(13*8-3, 31*8-12); hvx = 300; run(2, 1);
  CHECK(st == WIN && score == 100 && tally == 500);
  run(0, 8); run(16, 1);
  CHECK(lvl == 1 && st == NORM && score == 100 + 500*50 && skipclear);
  // the last level ends the run
  for (int i = 0; i < 200 && st != WIN; i++) tick(2);
  CHECK(st == WIN);
  for (int i = 0; i < 400 && !done; i++) tick(0);
  CHECK(done);

  // ---- the 500 s level timer: counts down in play, hurries at 100 s, a death at 0
  use("= 1 clock\n"
      "@    K       F\n"
      "##############\n");
  CHECK(left == LIMIT); run(0, 60); CHECK(left == LIMIT - 60);
  menu = 1; run(0, 30); menu = 0; CHECK(left == LIMIT - 60);          // paused: stopped
  nsnd = 0; left = 100*60 + 1; tick(0);
  { int hurry = 0; for (int i = 0; i < nsnd; i++) hurry |= sndq[i] == S_HURRY; CHECK(hurry); } nsnd = 0;
  left = 1; tick(0); CHECK(st == DEAD && timeout && deaths == 1);
  run(0, 62); CHECK(st == NORM && left >= LIMIT - 2 && !timeout);    // from the top, with a full timer
  for (int i = 0; i < 100 && !haveck; i++) tick(2);
  CHECK(haveck);
  left = 9000; die(); run(0, 62); CHECK(st == NORM && left > 9000 - 70 && left < 9000);   // a checkpoint keeps the time left
  left = 1; tick(0); CHECK(st == DEAD && timeout);
  run(0, 62); CHECK(st == NORM && X() == 5*8+1 && left >= LIMIT - 2);  // ...unless it ran out
  CHECK(parse("= 1 old par=90\n@ F\n###\n") && strstr(err, "par= is no longer used"));

  // ---- moon coins: three secret ones per level, kept once they reach the flag, saved by level name
  char mpath[] = "/tmp/hatrick-moons-XXXXXX"; close(mkstemp(mpath)); setenv("HATRICK_MOONS", mpath, 1);
  moonload(); CHECK(nmoons == 0);
  use("= 1 moony\n"
      "@ (   K  (      F\n"
      "################\n"
      "+ den\n"
      "  (\n"
      "  11\n"
      "#####\n");
  CHECK(LV[0].nmoon == 3 && LV[0].moon[2].room == 1 && LV[0].moon[0].x == 2 && LV[0].moon[1].x == 9 && !map[30][2]);
  for (int i = 0; i < 60 && !wd.moongot; i++) tick(2);
  CHECK(wd.moongot == 1 && score == 2000);
  die(); run(0, 62); CHECK(!wd.moongot && score == 0);                 // lost: no checkpoint yet
  for (int i = 0; i < 100 && !haveck; i++) tick(2);
  CHECK(haveck && wd.moongot == 1);
  for (int i = 0; i < 100 && wd.moongot != 3; i++) tick(2);
  CHECK(wd.moongot == 3);
  die(); run(0, 62); CHECK(wd.moongot == 1);                           // the checkpoint kept the first
  for (int i = 0; i < 200 && st != WIN; i++) tick(2);
  CHECK(st == WIN && moonbits(0) == 3);                                // the two from this visit count
  nmoons = 0; moonload(); CHECK(nmoons == 1 && moonbits(0) == 3 && !strcmp(moons[0].name, "MOONY"));
  load(); render(); CHECK(!wd.moongot);                                // drawn as outlines now
  for (int i = 0; i < 60 && !wd.moongot; i++) tick(2);
  CHECK(wd.moongot == 1 && score == 2000);                             // and can be picked up again
  menu = 1; render(); menu = 0;
  CHECK(parse("= 1 greedy\n@ ((((  F\n#########\n") && LV[0].nmoon == 3 && strstr(err, "more than 3 moon coins"));
  unlink(mpath);

  // ---- points: coins, stomps and bricks
  use("= 1 pts\n"
      "     B\n"
      "\n"
      "@ o    g    F\n"
      "#############\n");
  for (int i = 0; i < 60 && !coins; i++) tick(2);
  CHECK(score == 100);
  place(7*8+1, 30*8-8); hvy = 400; en[0].x = (7*8) << 8; en[0].y = (30*8) << 8; en[0].vx = 0; enemies(0); CHECK(score == 300 && !en[0].a);
  place(5*8, 31*8-11); run(16, 20); CHECK(score == 350 && !map[28][5]);

  // ---- high scores: sorted top ten, saved and read back; only better scores get in
  char path[] = "/tmp/hatrick-scores-XXXXXX"; close(mkstemp(path)); setenv("HATRICK_SCORES", path, 1);
  nhi = 0;
  for (int i = 1; i <= 12; i++) { char ini[4] = { 'A'+i, 'B', 'C', 0 }; if (hiqualifies(i*1000)) hiinsert(ini, i*1000); }
  CHECK(nhi == 10 && hi[0].score == 12000 && hi[9].score == 3000 && !hiqualifies(2500) && hiqualifies(3500));
  CHECK(hiinsert("ZZZ", 7500) == 5 && hi[9].score == 4000);
  hisave(); nhi = 0; hiload();
  CHECK(nhi == 10 && hi[5].score == 7500 && !strcmp(hi[5].ini, "ZZZ") && hi[9].score == 4000);
  // initials after the last level: Up/Down letters, Left/Right or Jump/Cap move, Jump saves
  use("= 1 end\n@ F\n###\n");
  score = 99999; done = 1; donet = 149; tick(0);
  CHECK(naming && namepos == 0);
  strcpy(initials, "AAA");
  tick(4); tick(0); CHECK(initials[0] == 'B');
  tick(8); tick(0); tick(8); tick(0); CHECK(initials[0] == 'Z');
  tick(16); tick(0); tick(16); tick(0); CHECK(namepos == 2);
  tick(32); tick(0); CHECK(namepos == 1);
  tick(2); tick(0); tick(4); tick(0); CHECK(namepos == 2 && initials[2] == 'B');
  tick(16); tick(0);
  CHECK(!naming && menu && scoreview && hinew == 0 && hi[0].score == 99999 && !strcmp(hi[0].ini, "ZAB") && !score);
  render(); tick(16); CHECK(menu && !scoreview);
  nhi = 0; hiload(); CHECK(hi[0].score == 99999);
  // the high-score card opens the table from the menu
  menusel = MENUN-1; tick(0); tick(16); CHECK(menu && scoreview); render(); tick(0); tick(32); CHECK(!scoreview);
  unlink(path);

  // ---- level file mistakes are reported and the rest still loads
  CHECK(parse("= 1 bad\n@  11   3  F\n   ||   |\n###########\n+ room bg=lava\n @ F\n###\n"));
  CHECK(!strstr(err, "tube mouth '1'") && strstr(err, "tube 1 has no partner"));
  CHECK(strstr(err, "mod.txt:2: tube mouth '3' in column 9 needs a second cell"));
  CHECK(strstr(err, "unknown room option \"bg=lava\""));
  CHECK(strstr(err, "'@' belongs in the level's main area") && strstr(err, "'F' belongs in the level's main area"));
  CHECK(parse("= 1 lonely\n@ 44   F\n  ||\n#######\n") && strstr(err, "tube 4 has no partner"));
  printf("PASS: %d feature checks\n", checks);
}
