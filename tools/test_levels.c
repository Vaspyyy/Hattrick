// levels.txt loading checks (MODDING.md): header options, error reports, menu and playground.
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
static void fresh(void) { menu = 1; menusel = menufr = menunav = menurepeat = prevk = resumable = quitting = 0; lvl = 0; load(); }

int main(void) {
  // the built-in copy matches the stock campaign
  CHECK(NLV == 5 && NLEVEL == 7 && PLAY == 6 && MENUN == 7);   // + the high-score card
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
  CHECK(!*err && NLV == 2 && NLEVEL == 3 && PLAY == 2 && MENUN == 4);
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
  CHECK(NLV == 2 && PLAY == -1 && MENUN == 3 && !strcmp(LV[1].name, "ODD") && LV[1].card == CARD_HILLS);
  CHECK(LV[0].card == CARD_CASTLE);

  // too tall / too wide / too many enemies
  char big[20000] = "= 1 tall\n@ F\n"; for (int i = 0; i < 32; i++) strcat(big, "#\n");
  strcat(big, "= 2 wide\n@ F\n"); for (int i = 0; i < 257; i++) strcat(big, "#"); strcat(big, "\n");
  strcat(big, "= 3 crowd\n@ F\n"); for (int i = 0; i < 49; i++) strcat(big, "g"); strcat(big, "\n#\n");
  strcat(big, "= 4 fine\n@ F\n###\n");
  CHECK(parse(big) && NLV == 1 && !strcmp(LV[0].name, "FINE"));
  CHECK(strstr(err, "level \"TALL\" is taller than 32 rows") && strstr(err, "level \"WIDE\" is wider than 256 columns") &&
        strstr(err, "level \"CROWD\" has too many enemies (at most 48)"));

  // nothing playable: reported, and the previous levels stay
  CHECK(!parse("= lab only\n@ F\n#\n") && strstr(err, "no playable level") && NLV == 1 && !strcmp(LV[0].name, "FINE"));
  CHECK(!parse("") && NLV == 1);

  // without a lab there is no playground: F1 and the menu ignore it
  fresh(); tick(PRACTICE); CHECK(menu && lvl == 0);
  menu = 0; resumable = 1; tick(0); tick(PRACTICE); CHECK(lvl == 0);

  // many levels: the menu pages, every card is enterable, the last level ends the run
  char many[20000] = "";
  for (int i = 0; i < 13; i++) sprintf(many + strlen(many), "= %d level %d\n@ F\n###\n", i+1, i+1);
  strcat(many, "= lab play card=playground\n@  F\n####\n");
  CHECK(parse(many) && NLV == 13 && PLAY == 13 && MENUN == 15);
  for (int i = 0; i < MENUN-1; i++) {
    fresh(); menusel = i; render(); tick(16);
    CHECK(!menu && lvl == (i == NLV ? PLAY : i));
  }
  fresh(); tick(1); CHECK(menusel == 14); render();
  fresh(); menusel = 14; tick(16); CHECK(menu && scoreview); render(); tick(0); tick(16); CHECK(menu && !scoreview);
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
