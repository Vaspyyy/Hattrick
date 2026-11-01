// Finds a route that beats a boss in the C game, for testing the remake's port of boss.c: the route
// is then replayed through both (tools/difftrace.sh with SIM_BOSS=N) and must match frame for frame.
// A greedy search with random rollouts: from the state so far it tries a few random moves, plays
// a few random futures after each (fork() snapshots the whole game), keeps the move whose best
// future hurt the boss most without losing hearts, and repeats until the boss is beaten.
// Build (from the repository root, like ctrace.c):
//   gcc -O2 -w remake/tools/bossbot.c -o bossbot
// usage: bossbot LEVEL BOSS [SEED] [MAXFRAMES] > route.tas    (BOSS: 0 or 1, as SIM_BOSS)
#define kill sys_kill_   // signal.h's kill() would clash with the game's
#include <sys/wait.h>
#include <unistd.h>
#undef kill
#define SIM
#define SC 1
#define main hatrick_sim_main
#include "../../hatrick.c"
#undef main

static const char *MOVES[] = { "R", "L", "RJ", "LJ", "J", "C", "RC", "LC", "UC", "-", "D", "DJ", "RD", "LD", "V", "RV", "LV", "RU", "LU", "UJ" };
#define NMOVES (int)(sizeof MOVES / sizeof *MOVES)
static int keys(const char *b) {
  int k = 0;
  for (const char *c = b; *c; c++) k |= *c=='L' ? 1 : *c=='R' ? 2 : *c=='U' ? 4 : *c=='D' ? 8 : *c=='J' ? 16 : *c=='C' ? 32 : *c=='V' ? CAP2 : 0;
  return k;
}
static unsigned rs = 12345;
static int rr(int n) { rs = rs * 1664525u + 1013904223u; return (rs >> 8) % n; }

typedef struct { int move, len, pound; } Step;
static void play(Step s) {   // a pound: jump first, then Down in the air
  int k = keys(MOVES[s.move]);
  if (s.pound) { int j = keys(MOVES[s.move][0] == 'L' ? "LJ" : "RJ"); for (int i = 0; i < 10; i++) tick(j); }
  for (int i = 0; i < s.len; i++) tick(k);
}
static Step randstep(void) { Step s = { rr(NMOVES), 3 + rr(22), rr(5) == 0 }; return s; }

static int target;
static int botscore(void) {
  const Boss *b = bz + target;
  int dmg = (b->kind == BOSS_HABERDASHER ? (b->phase - 1) * 3 : 0) + (b->maxhp - b->hp);
  if (b->on == 2) return 1000000;
  if (st == DEAD) return -1000000 + dmg * 1000;
  int s = dmg * 10000 + hp * 3000;
  // a nudge toward the fight: near the boss (or the matriarch's open tube), and a hat to wear
  int bx = b->x >> 8, by = b->y >> 8;
  if (b->kind == BOSS_MATRIARCH && mcur >= 0) { int cx, my, up; mspot(mcur, &cx, &my, &up); bx = cx; by = my; }
  if (b->kind == BOSS_CLOUDKING) {
    int best = 1 << 30;
    for (int i = 0; i < 3; i++) if (bcl[i].a) { int d = iabs((bcl[i].x >> 8) + 12 - (hx >> 8)); if (d < best) best = d; }
    if (best < 1 << 30) bx = (hx >> 8) + best;
  }
  s -= iabs(bx - (hx >> 8)) + iabs(by - (hy >> 8)) / 2;
  if (hatst == 2) s += 2000;
  return s;
}

int main(int argc, char **argv) {
  modlevels();
  if (argc < 3) { fprintf(stderr, "usage: bossbot LEVEL BOSS [SEED] [MAXFRAMES]\n"); return 3; }
  lvl = atoi(argv[1]); target = atoi(argv[2]);
  rs = argc > 3 ? (unsigned)atoi(argv[3]) * 2654435761u + 1 : 12345;
  int maxf = argc > 4 ? atoi(argv[4]) : 6000;
  load();
  if (target >= nbz) { fprintf(stderr, "bossbot: level %d has no boss %d\n", lvl, target); return 3; }
  Boss b0 = bz[target];
  spawn(b0.room, ((b0.ax0 + 4)*8) << 8, ((b0.my - 3)*8) << 8);
  static Step route[4000]; int n = 0, frames = 0;
  while (frames < maxf && bz[target].on != 2 && st != DEAD && n < 4000) {
    enum { K = 18, R = 10 };
    Step cand[K]; int best = -2000000000, bi = 0;
    for (int c = 0; c < K; c++) {
      cand[c] = randstep();
      int fd[2]; pipe(fd);
      pid_t p = fork();
      if (!p) {
        play(cand[c]);
        int top = botscore(); long long sum = 0; int nr = 0;
        for (int r = 0; r < R && top < 1000000; r++) {
          int pfd[2]; pipe(pfd);
          pid_t q = fork();
          if (!q) {
            for (int j = 0; j < 6 && st != DEAD && bz[target].on != 2; j++) play(randstep());
            int s = botscore(); write(pfd[1], &s, sizeof s); _exit(0);
          }
          int s = -2000000000; read(pfd[0], &s, sizeof s); waitpid(q, 0, 0); close(pfd[0]); close(pfd[1]);
          if (s > top) top = s;
          sum += s; nr++;
        }
        int now = botscore();   // the best future counts, but so do the risky ones
        int v = top / 2 + (nr ? (int)(sum / nr) : now) / 4 + now / 4;
        write(fd[1], &v, sizeof v); _exit(0);
      }
      int v = -2000000000; read(fd[0], &v, sizeof v); waitpid(p, 0, 0); close(fd[0]); close(fd[1]);
      if (v > best) best = v, bi = c;
    }
    play(cand[bi]); route[n++] = cand[bi];
    frames += cand[bi].len + (cand[bi].pound ? 10 : 0);
    if (n % 20 == 0) fprintf(stderr, "bossbot: %d frames, boss hp %d phase %d, hearts %d\n", frames, bz[target].hp, bz[target].phase, hp);
  }
  for (int i = 0; i < n; i++) {
    if (route[i].pound) printf("10 %s\n", MOVES[route[i].move][0] == 'L' ? "LJ" : "RJ");
    printf("%d %s\n", route[i].len, MOVES[route[i].move]);
  }
  fprintf(stderr, "bossbot: level %d boss %d (kind %d mini %d) %s after %d frames\n", lvl, target, bz[target].kind, bz[target].mini, bz[target].on == 2 ? "beaten" : st == DEAD ? "died" : "gave up", frames);
  return bz[target].on == 2 ? 0 : 1;
}
