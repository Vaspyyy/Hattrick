// Checks for the movement additions in movement.h: ice, conveyors, water, swing poles, wall
// slides and the flutter, against the real game simulation on the flat lab level.
// Run: gcc -O1 -w tools/test_moves.c -o /tmp/hatrick-moves-tests && /tmp/hatrick-moves-tests
#define SIM
#define SC 1
#define main replay_main
#include "../hatrick.c"
#undef main

static int checks;
#define CHECK(expr) do { checks++; if (!(expr)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); exit(1); } } while (0)
static int G;   // the lab's ground row
static void fresh(void) {
  lvl = NLV; load(); prevk = 0;
  for (int i = 0; i < 10; i++) tick(0);
  CHECK(gnd);
  G = (hy >> 8) + 12 >> 3;
}
static void surface(int s, int x0, int x1, int y0, int y1) {
  for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) mvs[0][y][x] = s;
}
static int stopframes(void) {   // frames to stop after letting go at full speed
  for (int i = 0; i < 80; i++) tick(2);
  CHECK(hvx == MAXV);
  int n = 0; while (hvx && n < 500) tick(0), n++;
  return n;
}

static void ice(void) {
  fresh(); int plain = stopframes();
  fresh(); surface(MV_ICE, 0, 150, G, G);
  int slid = stopframes();
  printf("Stopping from a run: ground %d frames, ice %d frames\n", plain, slid);
  CHECK(plain == 10 && slid == MAXV/MV_ICEBRAKE);
  fresh(); for (int i = 0; i < 6; i++) tick(2); int dry = hvx;
  fresh(); surface(MV_ICE, 0, 150, G, G); for (int i = 0; i < 6; i++) tick(2);
  CHECK(hvx > 0 && hvx < dry);   // slower to get going too
  fresh(); surface(MV_ICE, 0, 150, G, G);
  for (int i = 0; i < 80; i++) tick(2);
  tick(1); CHECK(hvx == MAXV - MV_ICEBRAKE);   // turning round skates on
  fresh(); surface(MV_ICE, 0, 150, G, G);
  for (int i = 0; i < 80; i++) tick(2);
  tick(2|16); CHECK(!gnd && hvx == MAXV);      // jumps leave at full speed
}

static void conveyors(void) {
  fresh(); surface(MV_CONVR, 0, 150, G, G);
  int x = hx; for (int i = 0; i < 30; i++) tick(0);
  CHECK(hx - x == 30*MV_CONVEYOR && hvx == 0);
  tick(16); tick(0); CHECK(!gnd && hvx == MV_CONVEYOR);   // jumping off keeps the push
  fresh(); surface(MV_CONVL, 0, 150, G, G);
  x = hx; for (int i = 0; i < 30; i++) tick(0);
  CHECK(x - hx == 30*MV_CONVEYOR);
  for (int i = 0; i < 40; i++) tick(2);   // walking against it
  int x2 = hx; tick(2); CHECK(hx - x2 == MAXV - MV_CONVEYOR);
  fresh(); surface(MV_CONVR, 0, 150, G, G);
  tick(8|32); CHECK(st == ROLL);
  x = hx; tick(8); CHECK(hx - x == hvx + MV_CONVEYOR);    // adds to a roll
}

static void water(void) {
  fresh(); surface(MV_WATER, 0, 150, 2, G-1);
  hy -= 150 << 8; gnd = 0; hvy = 900; coy = 99;
  tick(0); CHECK(mv_swim && hvy <= 300);
  for (int i = 0; i < 40; i++) tick(0);
  CHECK(!gnd && hvy > 0 && hvy <= 180);              // sinks slowly
  tick(8); for (int i = 0; i < 20; i++) tick(8);
  CHECK(st == NORM && hvy > 180 && hvy <= 460);      // Down sinks faster, no ground pound
  int y = hy; tick(16); CHECK(hvy < 0 && mv_stroke);  // a stroke
  for (int i = 0; i < 4; i++) tick(0);
  CHECK(hy < y);
  for (int i = 0; i < 60; i++) tick(2);
  CHECK(hvx == MV_SWIMMAX);
  tick(2|8|32); CHECK(st != DIVE);                    // no dives under water
  while (!gnd) tick(0);
  tick(16); CHECK(!gnd && hvy < 0 && hvy > -600);     // a stroke off the floor, not a jump
  // near the surface a stroke leaps out
  fresh(); surface(MV_WATER, 0, 150, G-3, G-1);
  hy = ((G-3)*8 - 2) << 8; gnd = 0; hvy = 0; coy = 99; tick(0); CHECK(mv_swim);
  tick(16); for (int i = 0; i < 12; i++) tick(0);
  CHECK(!mv_swim && (hy >> 8) + 11 < (G-3)*8);
  // the cap flies further under water
  fresh(); for (int i = 0; i < 2; i++) tick(0);
  tick(32); int far = 0; while (cst == 1) tick(32), far++;
  int dry = cxp - hx;
  fresh(); surface(MV_WATER, 0, 150, 2, G-1); for (int i = 0; i < 2; i++) tick(0);
  tick(32); while (cst == 1) tick(32);
  CHECK(cxp - hx > dry + (8 << 8));
  // the parser: a pool needs only its surface row
  const char *txt = "= 1 pool\n\n   @   ww F\n#####    ##\n#####    ##\n###########\n";
  CHECK(parselevels(txt, "test"));
  lvl = 0; build();
  int y0 = lh - 4;
  CHECK(mvs[0][y0][7] == MV_WATER && mvs[0][y0+1][7] == MV_WATER && mvs[0][y0+2][7] == MV_WATER && mvs[0][y0+3][7] == 0 && mvs[0][y0][6] == 0);
  builtinlevels();
}

static void poles(void) {
  int speed[2];
  for (int run = 0; run < 2; run++) {
    fresh(); int px = (hx >> 8) + 3 >> 3;
    surface(MV_POLE, px-2, px+2, G-6, G-6);
    tick(2|16);
    int n = 0; while (!mv_swing && n < 60) tick(2|16), n++;
    CHECK(mv_swing && hvx == 0 && hvy == 0 && st == NORM);
    int turns = run ? 150 : 20;
    for (int i = 0; i < turns; i++) tick(2);
    CHECK(mv_swing && !gnd);
    tick(2|16); CHECK(!mv_swing && hvx > 0 && hvy < 0);
    speed[run] = hvx;
    tick(2); CHECK(!mv_swing);   // no instant regrab
  }
  printf("Swing exit speed: short %d, long %d\n", speed[0], speed[1]);
  CHECK(speed[1] > speed[0]);
  fresh(); int px = (hx >> 8) + 3 >> 3;
  surface(MV_POLE, px-2, px+2, G-6, G-6);
  tick(16); while (!mv_swing) tick(16);
  tick(8); CHECK(!mv_swing && hvx == 0);    // Down drops off
}

static void wallslide(void) {
  fresh(); int wx = ((hx >> 8) >> 3) + 3;
  for (int y = 0; y < G; y++) map[y][wx] = 4;
  hy -= 120 << 8; gnd = 0; coy = 99; hvy = 0;
  for (int i = 0; i < 60 && !mv_wslide; i++) tick(2);
  CHECK(wall == 1 && mv_wslide == 1);
  for (int i = 0; i < 30; i++) tick(0);
  CHECK(mv_wslide == 1 && hvy <= 200 && !gnd);       // stays on without holding toward the wall
  tick(1); tick(1); CHECK(!mv_wslide && wall == 0 && mv_wcoy);
  tick(1|16); CHECK(hvx == -440 && hvy < 0);          // a late kick still works
  fresh(); for (int y = 0; y < G; y++) map[y][wx] = 4;
  hy -= 120 << 8; gnd = 0; coy = 99; hvy = 0;
  for (int i = 0; i < 60 && !mv_wslide; i++) tick(2);
  CHECK(mv_wslide);
  tick(8); CHECK(!mv_wslide && st == NORM);            // Down lets go instead of pounding
  for (int i = 0; i < 4; i++) tick(0);
  CHECK(!mv_wslide && hvy > 200);
}

static void flutter(void) {
  fresh(); hy -= 40 << 8; gnd = 0; coy = 99; hvy = -200;
  catcht = 10; catchok = 1;
  tick(16); CHECK(twirl == 10 && mv_flutok);
  while (hvy <= 0) tick(0);
  tick(16); CHECK(mv_flut);
  int lowest = 0; for (int i = 0; i < 20; i++) { tick(16); if (hvy < lowest) lowest = hvy; }
  CHECK(lowest < 0);                                     // a little lift
  int y = hy; tick(0); CHECK(!mv_flut);                  // letting go ends it
  tick(16); CHECK(!mv_flut);                             // one per twirl
  fresh(); hy -= 40 << 8; gnd = 0; coy = 99; hvy = 300;
  tick(16); CHECK(!mv_flut);                             // no twirl, no flutter
}

int main(int argc, char **argv) {
  ice(); conveyors(); water(); poles(); wallslide(); flutter();
  printf("PASS: %d checks for ice, conveyors, water, swing poles, wall slides and the flutter\n", checks);
  return 0;
}
