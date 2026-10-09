// ---------- items: a coin bank, the shop in Hatrick's house, and items used from the pause screen ----------
// hatrick.c includes this file twice: the game logic right after cap.h, and the screens, the
// drawing and the save file (ITEMS_UI defined) after collect_ui.h.
//
// The coins picked up in a campaign or secret level go into the bank when its flag is reached (a
// level left from the pause screen banks nothing). Down in Hatrick's house opens the shop:
//   HEART PIE      a 4th heart until Hatrick goes down
//   FEATHER CAP    holding Jump while falling glides; the buzzer's flutter lasts twice as long
//   BOOMERANG CAP  the cap flies twice as far and comes back by itself, hitting on the way back too
//   GOLD CAP       10 s: nothing hurts Hatrick (pits, lava and the timer still do), enemies he runs
//                  into are knocked out, and the music runs fast
//   SPRING SHOES   one more jump in the air, every time he leaves the ground, until a hit costs a heart
//   MAGNET         coins and moon coins come to Hatrick from a few tiles away
//   EGG BUDDY      an egg follows Hatrick and takes one hit for him
//   FLAG           a checkpoint planted where Hatrick stands (not during a boss fight)
// Items are used from the pause screen (ITEMS). Feather, boomerang and magnet last the rest of the
// level; the others are lost when Hatrick goes down. ~/.hatrick_items keeps the bank and the
// items bought (HATRICK_ITEMS names another file; the simulator and tests only ever use that).
#ifndef ITEMS_UI
enum { IT_PIE, IT_FEATHER, IT_BOOM, IT_GOLD, IT_SHOES, IT_MAGNET, IT_EGG, IT_FLAG, NITEM };
static const struct { const char *name, *what; int price; } ITEM[NITEM] = {
  { "HEART PIE", "A 4TH HEART UNTIL YOU GO DOWN", 30 },
  { "FEATHER CAP", "HOLD JUMP TO GLIDE DOWN", 40 },
  { "BOOMERANG CAP", "FLIES FAR AND HITS TWICE", 50 },
  { "GOLD CAP", "10 SECONDS UNBEATABLE", 80 },
  { "SPRING SHOES", "ONE MORE JUMP IN THE AIR", 40 },
  { "MAGNET", "PULLS IN COINS AND MOONS", 30 },
  { "EGG BUDDY", "TAKES ONE HIT FOR YOU", 40 },
  { "FLAG", "PLANT YOUR OWN CHECKPOINT", 25 },
};
#define IT_MAX 9          // the most of one item Hatrick can carry
#define IT_GOLDT 600      // frames the gold cap lasts
#define IT_GLIDE 300      // the feather cap's fall speed
static int it_have[NITEM], it_bank, it_read;   // what is in the bag, the coins banked; it_read: the save file was read
// What is on right now. it_gold: frames left; it_ajump: the shoes' air jump is spent until the ground
static int it_pie, it_feather, it_boom, it_gold, it_shoes, it_ajump, it_magnet, it_egg;
static int it_boomback, it_pcst, it_eggx, it_eggy;   // the boomerang flying home; the egg (1/256 px)
static int it_flagr, it_flagx, it_flagy;            // the last flag planted (area, 1/256 px)
static int it_bag, it_bagsel, it_bagmsg, it_bagnav, it_bagrep;   // the pause screen's item bag
#define HPMAX (MAXHP + it_pie)
static void it_openbag(void);
static void it_bagtick(int k, int pr);
static void it_bagrender(void);
static int bossinarena(int x);   // boss.c
static void it_clear(void);
static void it_tick(void);

static void it_level(void) {   // startlevel(): a new level starts with nothing on
  it_pie = it_feather = it_boom = it_gold = it_shoes = it_ajump = it_magnet = it_egg = it_boomback = 0;
  it_flagr = it_flagx = it_flagy = -1;
}
static void it_spawn(void) {   // spawn(): going down loses the pie, the gold cap, the shoes and the egg
  it_pie = it_gold = it_shoes = it_ajump = it_egg = it_boomback = it_pcst = 0;
  it_eggx = hx; it_eggy = hy;
}
// die() asks first: the gold cap shrugs off anything a fall or the timer doesn't settle.
static int it_shield(void) { return it_gold && st < TUBE && (hy >> 8) <= lh*8 && left; }
// die() asks after the hat power: the egg takes the hit and breaks.
static int it_eggsave(void) {
  if (!it_egg || st >= TUBE || (hy >> 8) > lh*8 || !left) return 0;
  it_egg = 0; capx_inv = 90; if (hvy > -600) hvy = -600;
  burst((it_eggx >> 8)+3, (it_eggy >> 8)+3, 0xfff8e8, 10); burst((it_eggx >> 8)+3, (it_eggy >> 8)+3, 0x5ac85a, 6);
  sfx(S_BRICK); rumble(4); kick(4);
  return 1;
}
// hero(), right after the flutter: the feather cap's glide, the shoes ready again on the ground.
static void it_glide(int k) {
  if (gnd) { it_ajump = 0; if (it_feather) capx_fuel = 96; return; }
  if (!it_feather || st != NORM || !(k & 16) || mv_swim || capx_flut || hvy <= IT_GLIDE) return;
  hvy = IT_GLIDE;
  if (!(fr & 7)) part(hx + (3 << 8) + (rnd(7)-3)*256, hy + (11 << 8), rnd(100)-50, 60, 18, 0, 0xf2efe0);
}
// hero(), a new Jump press in the air: the spring shoes' extra jump.
static int it_airjump(void) {
  if (!it_shoes || it_ajump || !freemove() || mv_swim) return 0;
  it_ajump = 1; jbuf = 0; throwt = twirl = 0;   // a jump ends an air-throw stall
  st = NORM; hvy = -960; arcg = GRAV; cut = 1; jn = -1; launch = 0; posture(0);
  SPIN(30, face); sfx(S_SPRING); rumble(1);
  for (int i = 0; i < 6; i++) part(hx + (3 << 8), hy + (11 << 8), (i-3)*90, 140, 14, 0, 0xffe066);
  return 1;
}
// capupd(): the boomerang cap. A new throw goes out again; at the far end (or a wall) the cap turns
// around and flies straight home, still a thrown cap that knocks out what it meets.
static int it_pext;   // capextend last frame
static void it_captick(void) {   // a new throw, or an extended one (cap pressed again), flies out again first
  if ((cst == 1 && !it_pcst) || (capextend && !it_pext)) it_boomback = 0;
  it_pcst = cst; it_pext = capextend;
}
static int it_boomturn(void) { if (!it_boom || it_boomback) return 0; it_boomback = 1; return 1; }
static int it_boomhome(void) {
  if (!it_boomback) return 0;
  int dx = hx + 256 - cxp, dy = hy + (duck+3)*256 - cyp, m = iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy);
  if (m < 1024) { cst = 0; catcht = 10; it_boomback = 0; sfx(S_CATCH); return 1; }
  cxp += dx * 900 / m; cyp += dy * 900 / m;
  return 1;
}
static int it_capslow(void) { return it_boom ? 36 : 72; }   // how fast a thrown cap slows down
// The magnet's reach for moon coins (hazards()): px added on every side of Hatrick.
static int it_moonreach(void) { return it_magnet ? 20 : 0; }

#else   // ---------- the save file, the shop, the bag, the effects that need the whole game, drawing ----------

static void it_load(void) {
  const char *p = savepath("HATRICK_ITEMS", ".hatrick_items"); FILE *f = p ? fopen(p, "r") : 0;
  char line[80];
  it_read = 1; it_bank = 0; memset(it_have, 0, sizeof it_have);
  while (f && fgets(line, sizeof line, f)) {
    char *name; int n = (int)strtol(line, &name, 10);
    while (*name == ' ') name++;
    name[strcspn(name, "\r\n")] = 0;
    if (n <= 0) continue;
    if (!strcmp(name, "BANK")) it_bank = n > 99999 ? 99999 : n;
    else for (int i = 0; i < NITEM; i++) if (!strcmp(name, ITEM[i].name)) it_have[i] = n > IT_MAX ? IT_MAX : n;
  }
  if (f) fclose(f);
}
static void it_save(void) {
  const char *p = savepath("HATRICK_ITEMS", ".hatrick_items"); FILE *f = p ? fopen(p, "w") : 0;
  if (!f) { if (p) fprintf(stderr, "hatrick: cannot save the items to %s\n", p); return; }
  fprintf(f, "%d BANK\n", it_bank);
  for (int i = 0; i < NITEM; i++) if (it_have[i]) fprintf(f, "%d %s\n", it_have[i], ITEM[i].name);
  fclose(f);
}
static void it_need(void) { if (!it_read) it_load(); }
// touchflag(): the coins of this visit go into the bank.
static void it_clear(void) {
  int got = coins + capx_carry - lcoins;   // coins still riding home on the cap count too
  if (!cl_record(lvl) || got <= 0) return;
  it_need(); it_bank += got; if (it_bank > 99999) it_bank = 99999;
  it_save();
}

// ---------- using an item ----------
static const char *it_use(int i) {   // 0 when it worked, else why not
  if (!it_have[i]) return "NONE LEFT - THE SHOP IS AT HOME";
  if (st >= TUBE || done) return "NOT NOW";
  switch (i) {
    case IT_PIE: if (it_pie) return "ALREADY HAVE A 4TH HEART"; it_pie = 1; hp++; healt = 24; break;
    case IT_FEATHER: if (it_feather) return "ALREADY WEARING IT"; it_feather = 1; break;
    case IT_BOOM: if (it_boom) return "ALREADY WEARING IT"; it_boom = 1; break;
    case IT_GOLD: it_gold = IT_GOLDT; break;
    case IT_SHOES: if (it_shoes) return "ALREADY WEARING THEM"; it_shoes = 1; it_ajump = 0; break;
    case IT_MAGNET: if (it_magnet) return "ALREADY ON"; it_magnet = 1; break;
    case IT_EGG: if (it_egg) return "THE EGG IS ALREADY HERE"; it_egg = 1; it_eggx = hx - face*(12 << 8); it_eggy = hy; break;
    case IT_FLAG:
      if (bossnear || bossinarena(hx >> 8)) return "NOT IN A BOSS ARENA";
      if (!gnd || st != NORM) return "STAND ON THE GROUND FIRST";
      haveck = 1; ckroom = room; ckx = hx; cky = hy; saved = wd; savedcoins = coins; savedscore = score;
      it_flagr = room; it_flagx = hx; it_flagy = hy; sfx(S_CHECK);
      break;
  }
  it_have[i]--; it_save();
  sparkle((hx >> 8)+3, (hy >> 8)+4, 0xfff0a0, 12);
  cl_say(ITEM[i].name, ITEM[i].what, 0); clmsgt = 120;
  return 0;
}
static void it_openbag(void) {
  it_need(); it_bag = 1; it_bagmsg = 0; it_bagnav = 0; it_bagrep = 18; sfx(S_MENUOK);
  if (!it_have[it_bagsel]) for (int i = 0; i < NITEM; i++) if (it_have[i]) { it_bagsel = i; break; }
}
static const char *it_why;
static void it_bagtick(int k, int pr) {   // the pause screen's ITEMS: Left/Right pick, Jump uses, Cap goes back
  if (it_bagmsg) it_bagmsg--;
  if (pr & (BACK|MENUBACK|32)) { it_bag = 0; sfx(S_MENUBACK); return; }
  if (pr & (16|START)) {
    it_why = it_use(it_bagsel);
    if (it_why) it_bagmsg = 120, sfx(S_MENUBACK);
    else it_bag = 0, menu = 0, sfx(S_MENUOK);   // straight back into the level
    return;
  }
  int axis = moveaxis(k), nav = axis > 128 ? 1 : axis < -128 ? -1 : k & 4 ? -4 : k & 8 ? 4 : 0;
  if (nav && (nav != it_bagnav || --it_bagrep <= 0)) {
    it_bagsel = (it_bagsel + nav + NITEM) % NITEM; it_bagmsg = 0; sfx(S_MENUMOVE);
    it_bagrep = nav != it_bagnav ? 18 : 7;
  }
  it_bagnav = nav;
}

// ---------- every frame in a level, after cl_tick() ----------
static void it_tick(void) {
  int X = hx >> 8, Y = hy >> 8, alive = st < TUBE;
  if (it_gold && st < DEAD) {
    if (--it_gold == 120) sfx(S_HURRY);
    if (!(fr & 3)) part(hx + (rnd(9)-1)*256, hy + rnd(12)*256, 0, -120, 16, 0, (fr >> 2) & 1 ? 0xffd84a : 0xffffff);
    if (alive) for (E *e = en; e < en+ne; e++)   // running into an enemy knocks it out
      if (ekillable(e) && ov(X-1, Y+duck-1, 8, 13-duck, (e->x >> 8)+1, (e->y >> 8)+1, 6, 7)) ekill(e, 0xffd84a, 200);
  }
  if (it_magnet && alive) {   // coins within three tiles fly in
    int cx = X+3, cy = Y+5;
    for (int ty = (cy-24) >> 3; ty <= (cy+24) >> 3; ty++) for (int tx = (cx-24) >> 3; tx <= (cx+24) >> 3; tx++)
      if (tile(tx, ty) == 6) {
        map[ty][tx] = 0; coins++; sfx(S_COIN); addscore(100, tx*8+4, ty*8);
        part((tx*8+4) << 8, (ty*8+4) << 8, (cx - tx*8-4)*32, (cy - ty*8-4)*32, 8, 0, 0xffd84a);
      }
  }
  if (it_egg) {   // the egg waddles along behind Hatrick
    int tx = hx - face*(11 << 8), ty = hy + (4 << 8);
    it_eggx += (tx - it_eggx) / 6; it_eggy += (ty - it_eggy) / 6;
  }
}

// ---------- drawing ----------
static void it_heart(int cx, int cy, int r, u32 c) {   // a small heart, menu px
  mellipse(cx - r/2, cy - r/3, r/2 + 1, r/2 + 1, c); mellipse(cx + r/2, cy - r/3, r/2 + 1, r/2 + 1, c);
  for (int y = 0; y <= r; y++) { int w = r * (r - y) / r + 1; mrect(cx - w, cy - r/3 + y, 2*w, 1, c); }
}
// Item i's picture, about 40 x 40 menu px from (x, y). dim: not in the bag.
static void it_icon(int i, int x, int y, int dim) {
  #define D(c) (dim ? mixcolor((c), 0x24303e, 7, 10) : (c))
  int cx = x + 20, cy = y + 20;
  switch (i) {
    case IT_PIE:   // a pie with a heart on it
      mellipse(cx, cy+8, 18, 9, D(0x6a3a1a)); mellipse(cx, cy+6, 17, 8, D(0xe0a050)); mellipse(cx, cy+4, 14, 5, D(0xf4c878));
      it_heart(cx, cy-4, 8, D(0x14100c)); it_heart(cx, cy-4, 6, D(0xf0303e));
      break;
    case IT_FEATHER:   // a white cap with a feather in its band
      hatdraw(x+2, y+16, 4, D(0xf2efe0), 0);
      for (int j = 0; j < 16; j++) mrect(cx+6 + j/3, y+14 - j, 6 - (j > 11 ? j-11 : 0), 1, D(j & 1 ? 0x8fd0ff : 0xffffff));
      break;
    case IT_BOOM:   // a cap with a curved arrow around it
      hatdraw(x+4, y+18, 4, D(0x3a8fd8), 0);
      for (int a = 0; a < 100; a += 4) { int px_ = cx + SIN[(a+64+140) & 255]*18/256, py = cy + SIN[(a+140) & 255]*14/256; mrect(px_-1, py-1, 3, 3, D(0xffd894)); }
      mrect(cx-20, cy-2, 7, 3, D(0xffd894)); mrect(cx-18, cy-6, 3, 9, D(0xffd894));
      break;
    case IT_GOLD:   // a gold cap that shines
      hatdraw(x+2, y+16, 4, D(0xffd84a), 0);
      mstar(x+33, y+8, 6, D(0xffffff)); mstar(x+7, y+10, 4, D(0xfff0a0));
      break;
    case IT_SHOES:   // a red shoe on a spring
      for (int j = 0; j < 4; j++) mrect(cx-8, cy+6 + j*4, 16, 2, D(0xc8d0e0));
      mround(cx-14, cy-8, 24, 14, 5, D(0x701818)); mround(cx-12, cy-6, 20, 10, 4, D(0xe8403a));
      mround(cx-2, cy-2, 18, 8, 3, D(0x701818)); mround(cx-1, cy-1, 16, 6, 2, D(0xe8403a)); mrect(cx-12, cy+4, 26, 3, D(0xffffff));
      break;
    case IT_MAGNET:   // a red horseshoe magnet with silver tips
      mellipse(cx, cy-4, 16, 14, D(0xd83a3a)); mellipse(cx, cy-4, 8, 7, 0x2c4162);
      mrect(cx-16, cy-4, 8, 18, D(0xd83a3a)); mrect(cx+8, cy-4, 9, 18, D(0xd83a3a)); mrect(cx-8, cy-4, 16, 20, 0x2c4162);
      mrect(cx-16, cy+10, 8, 6, D(0xe8f4ff)); mrect(cx+8, cy+10, 9, 6, D(0xe8f4ff));
      break;
    case IT_EGG:   // a spotted egg
      mellipse(cx, cy+2, 14, 18, D(0x14100c)); mellipse(cx, cy+2, 12, 16, D(0xfff8e8));
      mellipse(cx-5, cy-4, 4, 4, D(0x5ac85a)); mellipse(cx+5, cy+6, 5, 4, D(0x5ac85a)); mellipse(cx-4, cy+11, 3, 3, D(0x5ac85a));
      break;
    case IT_FLAG:   // a pole with a green pennant
      mrect(cx-10, y+2, 4, 36, D(0xd8dde4)); mrect(cx-14, y+36, 12, 4, D(0x6a3a1a));
      for (int j = 0; j < 14; j++) mrect(cx-6, y+4 + j, 22 - iabs(j-7)*3, 1, D(0x2ec85a));
      break;
  }
  #undef D
}
static void it_coin(int x, int y) {   // the bank's coin, menu px
  mellipse(x, y, 11, 14, 0x14100c); mellipse(x, y, 9, 12, 0xc87a10); mellipse(x+1, y, 7, 11, 0xffc93a); mrect(x-1, y-8, 3, 16, 0xfff0a8);
}

// ---------- the shop: Down in Hatrick's house ----------
static int it_shopsel, it_shopmsg, it_shopnav, it_shoprep;
static const char *it_shopwhy;
static void it_openshop(void) { it_need(); it_shop = 1; it_shopmsg = 0; it_shopnav = 4; it_shoprep = 18; sfx(S_MENUOK); }
static int it_shoptick(int k, int pr) {   // cl_maptick() hands it the input while the shop is open
  if (it_shopmsg) it_shopmsg--;
  if (pr & (32|BACK|MENUBACK)) { it_shop = 0; sfx(S_MENUBACK); return 1; }
  if (pr & (16|START)) {
    int i = it_shopsel;
    if (it_have[i] >= IT_MAX) it_shopwhy = "THE BAG IS FULL OF THOSE", it_shopmsg = 120, sfx(S_MENUBACK);
    else if (it_bank < ITEM[i].price) it_shopwhy = "NOT ENOUGH COINS", it_shopmsg = 120, sfx(S_MENUBACK);
    else { it_bank -= ITEM[i].price; it_have[i]++; it_save(); it_shopwhy = "THANK YOU"; it_shopmsg = 60; sfx(S_COIN); sfx(S_MENUOK); }
    return 1;
  }
  int axis = moveaxis(k), nav = axis > 128 ? 1 : axis < -128 ? -1 : k & 4 ? -4 : k & 8 ? 4 : 0;
  if (nav && (nav != it_shopnav || --it_shoprep <= 0)) {
    it_shopsel = (it_shopsel + nav + NITEM) % NITEM; it_shopmsg = 0; sfx(S_MENUMOVE);
    it_shoprep = nav != it_shopnav ? 18 : 7;
  }
  it_shopnav = nav;
  return 1;
}
static void it_shoprender(void) {
  char t[64];
  ox = oy = 0;
  darken(110);
  plaque(24, 10, 720, 412);
  hudtext("SHOP", (MENUW - textwidth("SHOP", 1))/2, 24, 0xffd894);
  it_coin(560, 37); snprintf(t, sizeof t, "%d", it_bank); hudtext(t, 580, 26, 0xffffff);
  for (int i = 0; i < NITEM; i++) {   // two shelves of four
    int x = 84 + i%4*156, y = 72 + i/4*126, on = i == it_shopsel, bob = on ? SIN[(menufr*6) & 255]*3/256 : 0;
    mround(x, y, 132, 108, 8, on ? 0xffdb87 : 0x46607e); mround(x+2, y+2, 128, 104, 7, on ? 0xa67150 : 0x34506e);
    it_icon(i, x+46, y+10 - (on ? 4 : 0) + bob, 0);
    it_coin(x+40, y+80); snprintf(t, sizeof t, "%d", ITEM[i].price);
    menutext(t, x+56, y+70, 1, it_bank >= ITEM[i].price ? 0xfff3d1 : 0xe08070);
    if (it_have[i]) { snprintf(t, sizeof t, "X%d", it_have[i]); menutext(t, x+124 - textwidth(t, 1), y+8, 1, 0xc8d4dc); }
  }
  centered(ITEM[it_shopsel].name, 330, 1, 0xffd894);
  centered(it_shopmsg ? it_shopwhy : ITEM[it_shopsel].what, 360, 1, it_shopmsg ? 0xffffff : 0xc8d4dc);
  centered("JUMP: BUY   CAP: BACK", 392, 1, 0x8a94a0);
}

// ---------- the bag on the pause screen ----------
static void it_bagrender(void) {
  char t[32];
  darken(120);
  plaque(84, 60, 600, 320);
  hudtext("ITEMS", (MENUW - textwidth("ITEMS", 1))/2, 84, 0xffd894);
  for (int i = 0; i < NITEM; i++) {
    int x = 120 + i%4*136, y = 128 + i/4*92, on = i == it_bagsel;
    mround(x, y, 120, 80, 6, on ? 0xffdb87 : 0x46607e); mround(x+2, y+2, 116, 76, 5, on ? 0xa67150 : 0x34506e);
    it_icon(i, x+40, y+16, !it_have[i]);
    snprintf(t, sizeof t, "%d", it_have[i]); menutext(t, x+112 - textwidth(t, 1), y+8, 1, it_have[i] ? 0xfff3d1 : 0x6a7a90);
  }
  centered(it_bagmsg ? it_why : ITEM[it_bagsel].name, 318, 1, it_bagmsg ? 0xffffff : 0xffd894);
  centered("JUMP: USE   CAP: BACK", 346, 1, 0x8a94a0);
}

// ---------- in the level ----------
// render(), after Hatrick: the egg buddy and a planted flag.
static void it_draw(void) {
  if (haveck && ckroom == room && ckx == it_flagx && cky == it_flagy && it_flagr == room) {   // the planted flag
    int x = (it_flagx >> 8) + 1, base = (it_flagy >> 8) + 11;
    for (int y = base-16; y < base; y++) wpx(x, y, 0xd8dde4), wpx(x+1, y, 0x8a94a0);
    for (int j = 0; j < 7; j++) for (int i = 0; i < 7 - iabs(j-3); i++) wpx(x+2+i + ((fr >> 3) + j & 1 && i > 3), base-16+j, j == 3 && i == 1 ? 0xffffff : 0x2ec85a);
  }
  if (it_egg && st != TUBE) {   // the egg: white with green spots, bobbing as it hops along
    int x = it_eggx >> 8, y = (it_eggy >> 8) - iabs(SIN[(fr*8) & 255]) / 128;
    for (int v = 0; v < 8; v++) for (int u = 0; u < 6; u++) {
      int dx = 2*u - 5, dy = 2*v - (v < 4 ? 8 : 6), r = v < 4 ? 30 : 34;
      if (dx*dx + dy*dy*(v < 4 ? 1 : 2) > r + 6) continue;
      u32 c = dx*dx + dy*dy*(v < 4 ? 1 : 2) > r - 8 ? 0x5a5040 : (u == 1 && v == 3) || (u == 4 && v == 5) || (u == 2 && v == 6) ? 0x5ac85a : u <= 1 && v <= 2 ? 0xffffff : 0xf4ecd8;
      wpx(x+u, y+v, c);
    }
  }
}
// drawhero(): the gold cap makes Hatrick flash gold.
static const u32 *it_pal(const u32 *p) {
  static u32 q[3];
  if (!it_gold || (it_gold < 120 && fr & 4)) return p;
  static const u32 G[4] = { 0xffd84a, 0xfff0a0, 0xffffff, 0xffb020 };
  q[0] = G[(fr >> 2) & 3]; q[1] = p[1]; q[2] = p[2];
  return q;
}
// The HUD, under the coin count: what is on right now (spent shoes dimmed until the ground).
static void it_hud(void) {
  int on[NITEM] = { 0, it_feather, it_boom, it_gold, it_shoes, it_magnet, it_egg, 0 }, x = 24;
  for (int i = 0; i < NITEM; i++) if (on[i]) {
    mround(x - 2, 62, 44, 44, 6, 0x14100c); mround(x, 64, 40, 40, 5, 0x2c4162); it_icon(i, x, 64, i == IT_SHOES && it_ajump);
    if (i == IT_GOLD) mrect(x+2, 100, it_gold * 36 / IT_GOLDT, 3, 0xffd84a);
    x += 48;
  }
}
#endif
