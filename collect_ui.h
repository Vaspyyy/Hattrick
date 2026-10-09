// ---------- collectibles and progression: the game side, the map and Hatrick's house ----------
// (collect.h has the level options and the progress bits.)

// The caps on the house rack. Cap n (1, 2, ...) hangs there once every moon coin of campaign
// level n has been brought home; two of them carry a small perk.
enum { PERK_NONE, PERK_FEATHER, PERK_MAGNET };
static const struct { const char *name; u32 c; int perk; } HATS[] = {
  { "CLASSIC", 0xff7a1c, PERK_NONE }, { "MEADOW", 0x4cb83c, PERK_NONE }, { "FEATHER", 0xf2efe0, PERK_FEATHER },
  { "DESERT", 0xe8c060, PERK_NONE }, { "MAGNET", 0xe8403a, PERK_MAGNET }, { "ROYAL", 0x9a48d0, PERK_NONE },
  { "OCEAN", 0x3a8fd8, PERK_NONE }, { "FROST", 0x9fe8ff, PERK_NONE }, { "CRAYON", 0xff80c0, PERK_NONE },
  { "CLOCKWORK", 0xc8902e, PERK_NONE }, { "MIDNIGHT", 0x2a2a48, PERK_NONE },
};
#define NHATS ((int)(sizeof HATS / sizeof HATS[0]))
static int clpre;   // the level's progress bits before this clear
static int clhouse, clhat, clsel, clhatread, clsecretnow, clunlock, clmsgt, clnav, clrep;
static const char *clmsg[3];   // lines shown over the course clear (or a find, while clmsgt runs)

static int cl_fullb(int l, int b) { int nm = LV[l].nmoon; return (b & 7) == (1 << nm) - 1 && (nm || b & 8); }
static int cl_full(int l) { return cl_fullb(l, progbits(l)); }   // every moon coin of level l home (or cleared, if it has none)
static int cl_starred(int l) { return cl_full(l) && progbits(l) & PB_STAR; }
static int cl_nhats(void) { return 1 + (NLV < NHATS-1 ? NLV : NHATS-1); }
static int cl_hatopen(int i) { return i == 0 || (i <= NLV && cl_full(i-1)); }
static int cl_record(int l) { return (l >= 0 && l < NLV) || cl_issecret(l); }   // campaign and secret levels keep progress

static void cl_wear(int i) {
  clhat = i < NHATS ? i : 0;
  HPAL[0] = HATS[clhat].c;
  clstall = HATS[clhat].perk == PERK_FEATHER ? 6 : 0;
}
// The cap worn is kept as the line "<n> *HAT" in the progress file.
static void cl_hatsave(void) {
  int i = 0; while (i < nprog && strcmp(prog[i].name, "*HAT")) i++;
  if (i == nprog) { if (nprog == 64) return; nprog++; snprintf(prog[i].name, sizeof prog[0].name, "*HAT"); }
  prog[i].bits = clhat + 1;
  const char *p = savepath("HATRICK_PROGRESS", ".hatrick_progress"); FILE *f = p ? fopen(p, "w") : 0;
  if (!f) return;
  for (int j = 0; j < nprog; j++) fprintf(f, "%d %s\n", prog[j].bits, prog[j].name);
  fclose(f);
}
static void cl_hatload(void) {
  int h = 0;
  for (int i = 0; i < nprog; i++) if (!strcmp(prog[i].name, "*HAT")) h = prog[i].bits - 1;
  cl_wear(h > 0 && h < cl_nhats() && cl_hatopen(h) ? h : 0);
}

// ---------- in the level ----------
static int cl_base(int x, int y) {   // where a pole at column x, top row y meets the ground (px)
  int b = y*8; while (b < lh*8 && !scan(x*8+3, b, 1, 1, SOLID)) b++;
  return b;
}
static void cl_say(const char *a, const char *b, const char *c) { clmsg[0] = a; clmsg[1] = b; clmsg[2] = c; }
// Every frame, after hazards(): the secret exit, the postcard and the magnet cap.
static void cl_tick(void) {
  const Level *L = LV + lvl;
  int X = hx >> 8, Y = hy >> 8;
  if (st != WIN) clsecretnow = 0, clpre = cl_record(lvl) ? progbits(lvl) : 0;
  if (clmsgt) clmsgt--;
  if (!clhatread) clhatread = 1, cl_hatload();
  if (st < TUBE && room == L->cl.gr && L->cl.sg && X+6 > L->cl.gx*8+2 && X < L->cl.gx*8+6) {   // the secret exit
    int b = cl_base(L->cl.gx, L->cl.gy);
    if (Y < b) { gx = L->cl.gx; gy = L->cl.gy; gb = b; clsecretnow = 1; touchflag(Y); }
  }
  if (L->cl.pc && L->cl.pr == room && st < TUBE && cl_record(lvl) && !(progbits(lvl) & PB_CARD)
      && ov(X, Y+duck, 6, 11-duck, L->cl.px*8-1, L->cl.py*8-1, 10, 10)) {   // the postcard
    progkeep(lvl, PB_CARD); sfx(S_REVEAL); rumble(3);
    burst(L->cl.px*8+4, L->cl.py*8+4, 0xfff3d1, 10); burst(L->cl.px*8+4, L->cl.py*8+4, 0xe8403a, 6);
    addscore(1000, L->cl.px*8+4, L->cl.py*8-4);
    cl_say("POSTCARD FOUND", "IT HANGS AT HOME", 0); clmsgt = 150;
  }
  if (HATS[clhat].perk == PERK_MAGNET && cst && cst < 3) {   // the magnet cap pulls in coins near it
    int cx = (cxp >> 8) + 4, cy = (cyp >> 8) + 2;
    for (int ty = (cy-14) >> 3; ty <= (cy+14) >> 3; ty++) for (int tx = (cx-14) >> 3; tx <= (cx+14) >> 3; tx++)
      if (tile(tx, ty) == 6) { map[ty][tx] = 0; coins++; sfx(S_COIN); addscore(100, tx*8+4, ty*8); part((tx*8+4) << 8, (ty*8+4) << 8, 0, -200, 12, 0, 0xffd84a); }
  }
}
// Called by touchflag(): the record for this clear, and what to say about it.
static void cl_flag(void) {
  if (!cl_record(lvl)) return;
  int hadhat = lvl < NLV && cl_fullb(lvl, clpre), hadstar = cl_fullb(lvl, clpre) && clpre & PB_STAR, bits = 8 | wd.moongot;
  if (split <= LV[lvl].cl.star*60) bits |= PB_STAR;
  if (clsecretnow) { if (!(clpre & PB_SECRET)) clunlock = 1; bits |= PB_SECRET; }
  progkeep(lvl, bits);
  const char *m[3] = { 0, 0, 0 }; int n = 0;
  if (clunlock) m[n++] = "SECRET EXIT FOUND";
  if (lvl < NLV && !hadhat && cl_full(lvl) && lvl+1 < NHATS) m[n++] = "NEW CAP AT HOME";
  if (!hadstar && cl_starred(lvl)) m[n++] = "STAR EARNED";
  cl_say(m[0], m[1], m[2]); clmsgt = 0;
}
// Back on the map: a secret exit just taken draws the path to its level.
static void cl_tomap(void) {
  if (!clunlock) return;
  clunlock = 0;
  for (int s = SEC0; s < NLEVEL; s++) if (cl_parentnode(s) == nodeof(lvl)) unlocknode = nodeof(s), unlockt = 70;
}
// Drawn by render(), after the goal: the secret pole, the postcard and the messages.
static void cl_render(void) {
  const Level *L = LV + lvl;
  if (L->cl.sg && room == L->cl.gr && !(st == WIN && clsecretnow && room == 0)) {   // a red pennant on a dark pole
    int x = L->cl.gx*8, y = L->cl.gy*8, b = cl_base(L->cl.gx, L->cl.gy);
    for (int j = y; j < b && j < y + 256; j++) wpx(x+3, j, 0x8a94a0), wpx(x+4, j, 0x586070);
    for (int i = 0; i < 9; i++) wpx(x+2+i%3, y-3+i/3, 0xe8403a);
    for (int j = 0; j < 8; j++) for (int i = 0; i < 8-j; i++) wpx(x+2-i, y+1+j/2+(j > 3 ? j-3 : 0)/2+((fr >> 3)+i/3 & 1), j == 3 && i == 2 ? 0xffffff : 0xe8403a);
  }
  if (L->cl.pc && L->cl.pr == room) {   // the postcard: white card, red stamp, two lines of writing
    int got = cl_record(lvl) && progbits(lvl) & PB_CARD, x = L->cl.px*8, y = L->cl.py*8 + (SIN[(fr*3) & 255] > 0 ? 0 : 1) - 1;
    for (int v = 0; v < 7; v++) for (int u = 0; u < 9; u++) {
      u32 c = u == 0 || u == 8 || v == 0 || v == 6 ? 0x3a2410 : u >= 5 && v <= 3 ? (u == 5 || v == 3 ? 0xffffff : 0xe8403a) : (v == 3 || v == 5) && u < 5 && u > 0 ? 0x6a7a90 : 0xfff3d1;
      if (got) c = ((c >> 1) & 0x7f7f7f) + 0x404858;
      if (!got || (u + v + fr/4) % 3) wpx(x+u-1, y+v, c);
    }
  }
}
// Drawn by render() over everything but the pause screen: what was just found.
static void cl_hud(void) {
  int n = 0; for (int i = 0; i < 3; i++) n += clmsg[i] != 0;
  if (n && (clmsgt || (st == WIN && wphase))) for (int i = 0; i < 3; i++) if (clmsg[i])
    hudtext(clmsg[i], (MENUW - textwidth(clmsg[i], 1)) / 2, (clmsgt ? 80 : 300) + i*30, i == 0 ? 0xffd894 : 0xffffff);
  if (!clmsgt && st != WIN) clmsg[0] = clmsg[1] = clmsg[2] = 0;
}


// ---------- the map: secret levels branch off, stars, a house that grows ----------
static int cl_parent(int l) {   // the level whose secret exit leads to secret level l, or -1
  const char *p = LV[l].cl.secretof;
  if (*p >= '1' && *p <= '9') { int i = atoi(p) - 1; return i < NLV ? i : -1; }
  for (int i = 0; i < NLV; i++) {
    const char *a = LV[i].name, *b = p; while (*a && (*a == *b || *a == *b - 32)) a++, b++;
    if (!*a && !*b) return i;
  }
  return -1;
}
static int cl_parentnode(int l) { int p = cl_parent(l); return p < 0 ? 0 : nodeof(p); }
static int cl_secretopen(int l) { int p = cl_parent(l); return p >= 0 && progbits(p) & PB_SECRET; }
static int cl_visible(int n) { return node[n].kind != N_LEVEL || node[n].lvl < NLV || cl_secretopen(node[n].lvl); }
static int cl_branches(int n, int *out, int k) {   // the secret paths leaving level stop n
  for (int s = SEC0; s < NLEVEL && k < 4; s++) if (cl_parent(s) >= 0 && nodeof(cl_parent(s)) == n) out[k++] = nodeof(s);
  return k;
}
static void cl_mapnodes(void) {   // each secret stop sits off its level's stop, above or below the main path
  for (int s = SEC0; s < NLEVEL; s++) {
    int p = cl_parent(s), sib = 0;
    if (p < 0) { fprintf(stderr, "hatrick: secret level \"%s\": no campaign level \"%s\" to hang off\n", LV[s].name, LV[s].cl.secretof); p = 0; }
    for (int o = SEC0; o < s; o++) sib += cl_parent(o) == p;
    const Node *a = node + 1 + p;
    int y = a->y > 74 ? a->y - 38 : a->y + 38;
    node[nodeof(s)] = (Node){ a->x + 12 + sib*26, y, N_LEVEL, s };
  }
}
static void mstar(int x, int y, int r, u32 c) {   // a five-pointed star, menu px
  for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) {
    int a = 0, d = i*i + j*j; if (d > r*r) continue;
    for (int k = 0; k < 5; k++) {   // inside one of the five spikes, or the core
      int sx = SIN[(k*256/5 + 192) & 255], sy = SIN[(k*256/5 + 128) & 255];
      int along = (i*sx + j*sy) / 256, across = iabs(i*sy - j*sx) / 256;
      if (along > 0 && across * r <= (r - along) * r * 2 / 5) a = 1;
    }
    if (a || d * 9 <= r*r * 2) mrect(x+i, y+j, 1, 1, c);
  }
}
static void cl_houserender(void);
static int it_shop;
static int it_shoptick(int k, int pr);
static void it_openshop(void);
static void it_shoprender(void);
static void cl_maprender(void) {
  if (clhouse) { cl_houserender(); return; }
  if (naming || scoreview) return;
  int clears = 0; for (int i = 0; i < NLV; i++) clears += cleared(i);
  for (int i = 0; i < (clears >= 5 ? 3 : clears >= 3 ? 2 : clears >= 1); i++) {   // the house grows annexes
    int x = node[0].x + 21 + i*8, y = node[0].y - 4;
    mrectw(x, y-6, 8, 7, 0x3a2410); mrectw(x+1, y-5, 6, 6, i & 1 ? 0xe8dcc0 : 0xf4e6c8);
    mrectw(x, y-8, 8, 2, 0xc85a14); mrectw(x, y-8, 8, 1, 0xffa449); mrectw(x+3, y-3, 2, 2, 0x6ab0e0);
  }
  for (int n = 0; n < nnode; n++) {   // a star by each starred stop
    const Node *d = node + n;
    if (d->kind != N_LEVEL || !cl_visible(n) || !cl_record(d->lvl) || !cl_starred(d->lvl)) continue;
    int x = (d->x - 9 - mapcam) * 3, y = (d->y - 8) * 3;
    mstar(x, y, 13, 0x14100c); mstar(x, y, 10, (menufr >> 4) & 1 ? 0xffd84a : 0xfff0a0);
  }
  if (mapto >= 0) return;
  const Node *d = node + mapat;
  if (d->kind != N_LEVEL || !cl_record(d->lvl)) return;
  int l = d->lvl, w = 230, x = (MENUW - w)/2;   // under the banner: the star time, the postcard
  char t[24]; snprintf(t, sizeof t, "%d:%02d", LV[l].cl.star / 60, LV[l].cl.star % 60);
  plaque(x, 70, w, 42);
  mstar(x+28, 91, 10, 0x0e1424); mstar(x+28, 91, 8, cl_starred(l) ? 0xffd84a : 0x46607e);
  menutext(t, x+48, 81, 1, cl_starred(l) ? 0xffd894 : 0xc8d4dc);
  if (LV[l].cl.pc) {   // the postcard: white once found
    int px = x + w - 52, got = progbits(l) & PB_CARD;
    mrect(px, 80, 30, 22, 0x0e1424); mrect(px+2, 82, 26, 18, got ? 0xfff3d1 : 0x46607e);
    if (got) mrect(px+18, 84, 8, 7, 0xe8403a), mrect(px+5, 90, 10, 2, 0x6a7a90), mrect(px+5, 95, 10, 2, 0x6a7a90);
  }
}

// ---------- Hatrick's house: the cap rack, a room per world cleared, postcards and trophies ----------
static void cl_house(void) { clhouse = 1; clsel = clhat; clnav = 0; clrep = 18; menufr = 0; sfx(S_MENUOK); }
static int cl_maptick(int k, int pr) {   // the house screen takes the input while it is open
  if (!clhatread) clhatread = 1, cl_hatload();
  if (!clhouse) return 0;
  if (it_shop) return it_shoptick(k, pr);   // items.h: the shop
  if (pr & (32|BACK|MENUBACK)) { clhouse = 0; sfx(S_MENUBACK); return 1; }
  if (pr & 8) { it_openshop(); return 1; }   // down: the shop
  if (pr & 4) { clhouse = 0; scoreview = 1; hinew = -1; hiload(); sfx(S_MENUOK); return 1; }   // up: the high scores
  if (pr & (16|START)) {
    if (cl_hatopen(clsel)) { cl_wear(clsel); cl_hatsave(); sfx(S_CATCH); } else sfx(S_MENUBACK);
    return 1;
  }
  int axis = moveaxis(k), nav = axis > 128 ? 1 : axis < -128 ? -1 : 0;
  if (nav && (nav != clnav || --clrep <= 0)) {
    clsel = (clsel + nav + cl_nhats()) % cl_nhats(); sfx(S_MENUMOVE);
    clrep = nav != clnav ? 18 : 7;
  }
  clnav = nav;
  return 1;
}
static void hatdraw(int x, int y, int z, u32 c, int locked) {   // a cap, menu px, z: size in quarter steps
  u32 dark = locked ? 0x1a2430 : mixcolor(c, 0x14100c, 6, 10), lite = locked ? 0x2c3a4c : mixcolor(c, 0xffffff, 4, 10);
  if (locked) c = 0x24303e;
  mround(x + 3*z/4, y, 20*z/4, 10*z/4, z, dark); mround(x + z, y + z/4, 18*z/4, 8*z/4, 3*z/4, c);
  mround(x, y + 7*z/4, 30*z/4, 5*z/4, z/2, dark); mround(x + z/4, y + 7*z/4, 28*z/4, 3*z/4, z/4, lite);
  if (!locked) mrect(x + 7*z/4, y + 3*z/4, 9*z/4, z/4 + 1, mixcolor(c, 0xffffff, 6, 10));
}
static void cl_postcard(int x, int y, int l) {   // a painted scene of level l in a frame (menu px)
  int card = LV[l].card;
  static const u32 SKY[6] = { 0x8fd0ff, 0x5a3a2a, 0xf0c890, 0x6cc0e8, 0x2a2a48, 0x8fd0ff };
  mrect(x-4, y-4, 72, 62, 0x6a3a1a); mrect(x-2, y-2, 68, 58, 0xc8902e);
  for (int j = 0; j < 54; j++) mrect(x, y+j, 64, 1, mixcolor(SKY[card < 6 ? card : 0], 0xffffff, j, 108));
  mrect(x, y+42, 64, 12, card == CARD_SKY ? 0xffffff : card == CARD_SPIKES ? 0xe0c070 : 0x4cb83c);
  int sox = ox, soy = oy;
  Node fake = { (x + 14) / 3 - 7, (y + 44) / 3 + 4, card == CARD_PLAYGROUND ? N_PLAY : N_LEVEL, l };
  ox = oy = 0; landmark(&fake, 1); ox = sox; oy = soy;
}
static void cl_portrait(int x, int y) {   // the Haberdasher, framed: the boss of a castle level
  mrect(x-4, y-4, 52, 62, 0x3a2410); mrect(x-2, y-2, 48, 58, 0xffd894); mrect(x, y, 44, 54, 0x3a2a48);
  mellipse(x+22, y+38, 14, 14, 0xe8b890);                                  // face
  mrect(x+10, y+6, 24, 22, 0x14100c); mrect(x+4, y+26, 36, 5, 0x14100c);    // top hat
  mrect(x+10, y+22, 24, 3, 0xe8403a);
  mrect(x+15, y+35, 4, 3, 0x14100c); mrect(x+26, y+35, 4, 3, 0x14100c);     // eyes, moustache
  mrect(x+14, y+44, 16, 3, 0x6a3a1a);
}
static void cl_houserender(void) {
  if (it_shop) { it_shoprender(); return; }   // items.h
  static const u32 PAPER[] = { 0xf4e6c8, 0xdcefc8, 0xf0d0b0, 0xf8e8b0, 0xd0e4f8, 0xd8d0e8, 0xc8e8e0, 0xe8f4ff, 0xffe0ec, 0xf0e0c0 };
  char t[64];
  ox = oy = 0;
  darken(110);
  plaque(24, 10, 720, 412);
  hudtext("HOME", (MENUW - textwidth("HOME", 1))/2, 24, 0xffd894);
  int rooms = 1; for (int i = 0; i < NLV; i++) rooms += cleared(i);
  if (rooms > 10) rooms = 10;
  int rw = 660 / rooms > 130 ? 130 : 660 / rooms, hx0 = (MENUW - rw*rooms)/2, top = 70, bot = 236;
  for (int i = 0, rl = rw*rooms + 20; i*24 < rl; i++)   // the roof: a long cap brim over every room
    mrect(hx0 - 10 + i*24, top - 14, rl - i*24 < 24 ? rl - i*24 : 24, 12, i & 1 ? 0xc85a14 : 0xff7a1c);
  mrect(hx0 - 10, top - 4, rw*rooms + 20, 4, 0xffa449);
  int shown = 0;
  for (int r = 0; r < rooms; r++) {
    int x = hx0 + r*rw, l = -1;
    if (r) { int c = 0; for (int i = 0; i < NLV; i++) if (cleared(i) && ++c == r) { l = i; break; } }
    mrect(x, top, rw, bot - top, 0x3a2410);
    mrect(x + 3, top + 3, rw - 6, bot - top - 6, PAPER[r % 10]);
    mrect(x + 3, bot - 22, rw - 6, 19, 0xa86a3a); mrect(x + 3, bot - 22, rw - 6, 3, 0xc88a5a);   // floor
    if (r == 0) {   // the rack room: a coat stand with the cap being worn
      int cx = x + rw/2;
      mrect(cx - 2, top + 50, 5, bot - top - 72, 0x6a3a1a); mrect(cx - 18, bot - 26, 37, 5, 0x6a3a1a);
      mrect(cx - 16, top + 50, 33, 4, 0x6a3a1a);
      hatdraw(cx - 15, top + 30 + SIN[(menufr*3) & 255]/128, 4, HATS[clhat].c, 0);
      continue;
    }
    if (l < 0) continue;
    shown++;
    int tx = x + rw - 34, ty = bot - 48;   // the trophy: a cup with the level's number, a star if starred
    mellipse(tx + 12, ty + 6, 12, 10, 0x8a5c1c); mellipse(tx + 12, ty + 5, 10, 8, 0xffd84a);
    mrect(tx + 9, ty + 14, 7, 6, 0xc8902e); mrect(tx + 4, ty + 20, 17, 6, 0x8a5c1c);
    snprintf(t, sizeof t, "%d", l + 1); menutext(t, tx + 5, ty - 4, 1, 0x8a5c1c);
    if (cl_starred(l)) mstar(tx + 24, ty - 8, 7, 0xffd84a);
    int px = x + 10, py = top + 16;
    if (LV[l].card == CARD_CASTLE) cl_portrait(px + 4, py), px += 56;
    if (progbits(l) & PB_CARD && rw >= 90 && px + 64 < x + rw) cl_postcard(px + 4, py, l);
    else if (progbits(l) & PB_CARD) mrect(px + 4, py, 22, 16, 0xfff3d1), mrect(px + 18, py + 2, 6, 5, 0xe8403a);
  }
  if (!shown) centered("CLEAR A LEVEL TO ADD A ROOM", 246, 1, 0x8a94a0);
  // the rack: every cap, the one picked raised
  int nh = cl_nhats(), sw = 600 / nh > 66 ? 66 : 600 / nh, sx = (MENUW - sw*nh)/2;
  mrect(sx - 10, 336, sw*nh + 20, 6, 0x6a3a1a);
  for (int i = 0; i < nh; i++) {
    int x = sx + i*sw + (sw - 40)/2, y = 300 - (i == clsel ? 8 + SIN[(menufr*6) & 255]*3/256 : 0), open = cl_hatopen(i);
    if (i == clsel) mround(sx + i*sw + 2, 284, sw - 4, 58, 6, 0x46607e);
    hatdraw(x, y, 5, HATS[i].c, !open);
    if (i == clhat) mrect(x + 14, 344, 12, 4, 0xffd894);
  }
  const char *perk = HATS[clsel].perk == PERK_FEATHER ? "LONGER STALL" : HATS[clsel].perk == PERK_MAGNET ? "PULLS IN COINS" : "";
  if (cl_hatopen(clsel)) snprintf(t, sizeof t, "%s%s%s", HATS[clsel].name, *perk ? " - " : "", perk);
  else snprintf(t, sizeof t, "ALL MOONS IN %d %s", clsel, LV[clsel-1].name);
  centered(t, 356, 1, cl_hatopen(clsel) ? 0xffd894 : 0xc8d4dc);
  centered("JUMP: WEAR   UP: SCORES   DOWN: SHOP", 390, 1, 0x8a94a0);
}
