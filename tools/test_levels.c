// levels.txt loading checks (MODDING.md): header options, error reports, the map and the playground.
// gcc -O1 -w tools/test_levels.c -o /tmp/hatrick-level-tests && /tmp/hatrick-level-tests
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
static void walk(int k) { tick(k); for (int i = 0; i < 300 && mapto >= 0; i++) tick(0); }
static void fresh(void) { menu = 1; menufr = menunav = menurepeat = prevk = resumable = quitting = scoreview = 0; lvl = 0; load(); nprog = 0; mapstart(); }

int main(void) {
  // the built-in copy matches the stock campaign
  mapbuild(); CHECK(NLV == 5 && NLEVEL == 7 && PLAY == 6 && nnode == 7);   // home, five stops, the playground
  CHECK(!strcmp(LV[0].name, "HILLS") && !strcmp(LV[4].name, "HATRICK") && !strcmp(LV[6].name, "LAB MOVEMENT PLAYGROUND"));
  CHECK(!strcmp(LV[0].music, "overworld") && !strcmp(LV[1].music, "underground") && !strcmp(LV[4].music, "finale") && !strcmp(LV[6].music, "athletic"));
  CHECK(LV[0].card == CARD_HILLS && LV[1].card == CARD_BRICKS && LV[3].card == CARD_SKY && LV[6].card == CARD_PLAYGROUND);

  // options, names, defaults, campaign before labs, rows at the bottom, empty rows kept
  CHECK(parse("; comment before any level\n"
              "= lab test room\n@  F\n####\n"
              "= 1 first   music=my_song card=sky\n"
              "@        F\n"
              "\n"
              "##  g   ###\n"
              "= 2 Second Try\n; a comment row is not a map row\n@ o F\n#####\n"));
  mapbuild(); CHECK(!*err && NLV == 2 && NLEVEL == 3 && PLAY == 2 && nnode == 4);
  CHECK(!strcmp(LV[0].name, "FIRST") && !strcmp(LV[0].music, "my_song") && LV[0].card == CARD_SKY);
  CHECK(!strcmp(LV[1].name, "SECOND TRY") && !strcmp(LV[1].music, "overworld") && LV[1].card == CARD_HILLS);
  CHECK(!strcmp(LV[2].name, "LAB TEST ROOM") && LV[2].lab);
  lvl = 0; load();
  CHECK(lw == 11 && hx == 0 && hy == 28 << 11 && gx == 9 && gy == 29 && ne == 1 && en[0].t == 1 && en[0].y == 31 << 11);
  CHECK(map[31][0] == 1 && map[31][2] == 0 && map[30][0] == 0 && map[31][8] == 1);   // the empty row stays a row
  lvl = 1; load(); CHECK(map[31][2] == 1 && map[30][2] == 6 && lw == 5);

  // broken levels are reported with their line and skipped; the rest still load
  CHECK(parse("= 1 ok card=castle\n@ F\n###\n"
              "= 2 no flag\n@\n###\n"
              "= 3 no start\nF\n###\n"
              "= 4 odd   music=x   colour=red  card=lava\n@ F  Z\n###\n"));
  CHECK(strstr(err, "mod.txt:4: level \"NO FLAG\" has no flag (F), skipped"));
  CHECK(strstr(err, "mod.txt:7: level \"NO START\" has no start (@), skipped"));
  CHECK(strstr(err, "mod.txt:10: unknown option \"colour\""));
  CHECK(strstr(err, "mod.txt:10: unknown card \"lava\""));
  CHECK(strstr(err, "mod.txt:11: unknown tile 'Z' in column 6, left empty"));
  mapbuild(); CHECK(NLV == 2 && PLAY == -1 && nnode == 3 && !strcmp(LV[1].name, "ODD") && LV[1].card == CARD_HILLS);
  CHECK(LV[0].card == CARD_CASTLE);

  // too tall / too wide / too many enemies
  static char big[20000] = "= 1 tall\n@ F\n"; for (int i = 0; i < MH; i++) strcat(big, "#\n");
  strcat(big, "= 2 wide\n@ F\n"); for (int i = 0; i < MW+1; i++) strcat(big, "#"); strcat(big, "\n");
  strcat(big, "= 3 crowd\n@ F\n"); for (int i = 0; i < MAXEN+1; i++) strcat(big, "g"); strcat(big, "\n#\n");
  strcat(big, "= 4 fine\n@ F\n###\n");
  CHECK(parse(big) && NLV == 1 && !strcmp(LV[0].name, "FINE"));
  char wide[80], crowd[80];
  snprintf(wide, sizeof wide, "level \"WIDE\" is wider than %d columns", MW); snprintf(crowd, sizeof crowd, "level \"CROWD\" has too many enemies (at most %d)", MAXEN);
  CHECK(strstr(err, "level \"TALL\" is taller than 320 rows") && strstr(err, wide) && strstr(err, crowd));
  // a tall level: 200 rows, the start at the bottom, the flag on a ledge at the top
  static char tall[8000] = "= 1 tower\n\n\n                    F\n                 ######\n";
  for (int i = 0; i < 194; i++) strcat(tall, "\n");
  strcat(tall, " @\n########################\n");
  CHECK(parse(tall) && !*err && LV[0].room[0].h == 200);
  fresh(); lvl = 0; load(); menu = 0; resumable = 1;
  CHECK(lh == 200 && gy == 2 && starty == 198 && map[199][0] == 1 && map[3][17] == 1);
  for (int i = 0; i < 60; i++) tick(0), render();
  CHECK(st == NORM && gnd && hy >> 8 == 199*8-11 && cyf >> 8 == lh*8 - H);   // standing at the bottom, the camera there too
  hx = (18*8) << 8; hy = (3*8-11) << 8; hvy = 0;                          // on the ledge beside the flag at the top
  for (int i = 0; i < 120 && st != WIN; i++) tick(2), render();
  CHECK(st == WIN);
  load(); hx = (23*8+4) << 8; hy = (190*8) << 8;                          // off the right end: a long fall to the bottom
  for (int i = 0; i < 200 && st != DEAD; i++) tick(2);
  CHECK(st == DEAD && hy >> 8 > 200*8);
  // a long level: 2500 columns, played and drawn at its far end
  static char longl[12000] = "= 1 long\n";
  { char *p = longl + strlen(longl); p[0] = '@'; for (int i = 1; i < 2490; i++) p[i] = ' '; strcpy(p + 2490, "F\n");
    p += strlen(p); for (int i = 0; i < 2500; i++) p[i] = '#'; strcpy(p + 2500, "\n"); }
  CHECK(parse(longl) && !*err && LV[0].room[0].w == 2500);
  fresh(); lvl = 0; load(); menu = 0; resumable = 1;
  CHECK(lw == 2500 && gx == 2490);
  hx = (2470*8) << 8; hy = (30*8-11) << 8; hvx = 0;
  for (int i = 0; i < 300 && st != WIN; i++) tick(2), render();
  CHECK(st == WIN && cxf >> 8 == lw*8 - W);   // the camera reaches the very end
  CHECK(parse(big) && NLV == 1);   // back to the list the next checks use

  // nothing playable: reported, and the previous levels stay
  CHECK(!parse("= lab only\n@ F\n#\n") && strstr(err, "no playable level") && NLV == 1 && !strcmp(LV[0].name, "FINE"));
  CHECK(!parse("") && NLV == 1);

  // without a lab there is no playground: F1 and the map ignore it
  fresh(); tick(PRACTICE); CHECK(menu && !resumable && lvl == 0); walk(8); CHECK(mapat == 0 || mapat == 1);
  menu = 0; resumable = 1; tick(0); tick(PRACTICE); CHECK(lvl == 0);

  // many levels: the map grows to fit them, every stop is enterable, the last level ends the run
  char many[20000] = "";
  for (int i = 0; i < 13; i++) sprintf(many + strlen(many), "= %d level %d\n@ F\n###\n", i+1, i+1);
  strcat(many, "= lab play card=playground\n@  F\n####\n");
  CHECK(parse(many) && NLV == 13 && PLAY == 13);
  fresh(); CHECK(nnode == 15 && mapw > 13*56 && mapat == 1);
  for (int i = 0; i < NLV; i++) progkeep(i, 8);
  for (int n = 1; n < nnode; n++) {
    fresh(); for (int i = 0; i < NLV; i++) progkeep(i, 8);
    mapat = n; render(); tick(16);
    CHECK(!menu && lvl == node[n].lvl);
  }
  fresh(); walk(1); CHECK(mapat == 0); render(); tick(16); CHECK(menu && scoreview); render(); tick(0); tick(16); CHECK(menu && !scoreview);
  lvl = 12; load(); menu = 0;
  for (int i = 0; i < 1500 && !done; i++) tick(i % 2 ? 2 : 0);
  CHECK(done && lvl == 12);

  // the shipped file parses cleanly and matches the built-in copy
  FILE *f = fopen("assets/levels.txt", "rb");
  if (f) {
    fclose(f);
    CHECK(readlevels("assets/levels.txt") && NLV == 5 && PLAY == 6);
    CHECK(parse(LEVELS_TXT) && !*err);
  }
  printf("PASS: %d level file checks\n", checks);
}
