// The new worlds: Tidepool Bay, Frostfall Peak, Crayon Woods and Clocktower (assets/worlds.txt).
// Declarations only; worlds.c, included just before render(), has the code. What they add:
//   theme=beach|frost|crayon|clock|desert   recolours the built-in background and ground
//   avalanche=<px/s>                         a snow wall that chases Hatrick from the left
//   before=<level name>                      places the level before another one in the campaign
//   map: c crab, s snow (slow going), q quicksand, j jelly block (bouncy), f flower,
//        [ ] clock blocks (solid on alternate ticks)
enum { TH_NONE, TH_BEACH, TH_FROST, TH_CRAYON, TH_CLOCK, TH_DESERT, NTHEME };
#define WT_SNOW 17      // live tile types: 17..22 and 30..31 (cap.h has 24..29)
#define WT_SAND 18      // quicksand: not solid, Hatrick sinks slowly
#define WT_JELLY 19
#define WT_FLOWER 20
#define WT_TICKA 21     // '[' solid; 22 is its ghost
#define WT_TICKB 30     // ']' solid; 31 is its ghost
#define WORLDTILE(t) (((t) >= WT_SNOW && (t) <= WT_TICKA+1) || (t) >= WT_TICKB)
#define WORLDSOLID (1u<<WT_SNOW | 1u<<WT_JELLY | 1u<<WT_TICKA | 1u<<WT_TICKB)
#define E_CRAB 40      // enemy type of the crab
static int worldopt(Level *L, const char *w, const char *eq, const char *file, int line);
static void worldline(const char **file, int *line);
static char *worldjoin(char *text, const char *path);
static void worldorder(Level *lv, int camp);
static int worldtile(int c);
static void worldbefore(int *target);
static void worldafter(int was, int vy0, int k);
static void worldtick(void);
static void worldspawn(void);
static int worldclaws(const E *e);
static u32 worldtilepx(int t, int tx, int ty, int u, int v);
static u32 worldtint(u32 c);
static int worldbg(int lo);
static void worlddraw(void);
static void worldhud(void);
static void worldlandmark(int card, int x, int y, int dim);
static void worldcrab(const E *e);
