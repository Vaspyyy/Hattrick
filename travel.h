// ---------- the quick level select: Start on the map (Tab on the keyboard) ----------
// A list of every stop Hatrick can reach: home, each level open so far (its secret level after it,
// once found) and the playground. Up/Down pick (Left/Right a page), Jump or Start takes Hatrick
// straight to that stop, ready to go in; the cap button or Esc closes the list.
#define TRAV_ROWS 9
static int trav_list(int *out) {   // the stops on the list, in order; returns how many
  int n = 0;
  out[n++] = 0;
  for (int i = 0; i < NLV; i++) {
    if (!nodeopen(nodeof(i))) continue;
    out[n++] = nodeof(i);
    for (int s = SEC0; s < NLEVEL; s++) if (cl_parent(s) == i && cl_visible(nodeof(s))) out[n++] = nodeof(s);
  }
  if (PLAY >= 0) out[n++] = nnode - 1;
  return n;
}
static void trav_name(int n, char *t, int size) {   // as on the map's banner
  const Node *d = node + n;
  if (d->kind == N_HOUSE) snprintf(t, size, "HOME");
  else if (d->kind == N_PLAY) snprintf(t, size, "PLAYGROUND");
  else if (d->lvl >= NLV) snprintf(t, size, "SECRET %s", LV[d->lvl].name);
  else snprintf(t, size, "%d %s", d->lvl + 1, LV[d->lvl].name);
}
static void trav_open(void) {
  int l[nnode], n = trav_list(l);
  trav = 1; travsel = travtop = 0; travnav = 0; travrep = 18;
  for (int i = 0; i < n; i++) if (l[i] == mapat) travsel = i;
  if (travsel >= TRAV_ROWS) travtop = travsel - TRAV_ROWS/2;
  sfx(S_MENUOK);
}
static int trav_tick(int k, int pr) {   // maptick() hands it the input while the list is open
  if (!trav) return 0;
  int l[nnode], n = trav_list(l);
  if (travsel >= n) travsel = n - 1;
  if (pr & (32|BACK|MENUBACK)) { trav = 0; sfx(S_MENUBACK); return 1; }
  if (pr & (16|START)) { trav = 0; mapat = l[travsel]; mapto = mapgoal = -1; sfx(S_MENUOK); return 1; }
  int axis = moveaxis(k), nav = k & 4 ? -1 : k & 8 ? 1 : axis > 128 ? TRAV_ROWS : axis < -128 ? -TRAV_ROWS : 0;
  if (nav && (nav != travnav || --travrep <= 0)) {
    int to = travsel + nav;
    if (iabs(nav) == 1) travsel = (to + n) % n;   // a row at a time wraps around; a page stops at the ends
    else travsel = to < 0 ? 0 : to >= n ? n - 1 : to;
    travrep = nav != travnav ? 18 : 5; sfx(S_MENUMOVE);
  }
  travnav = nav;
  if (travsel < travtop) travtop = travsel;
  if (travsel >= travtop + TRAV_ROWS) travtop = travsel - TRAV_ROWS + 1;
  return 1;
}
static void trav_render(void) {
  if (!trav) return;
  int l[nnode], n = trav_list(l);
  char t[64];
  darken(110);
  plaque(134, 18, 500, 400);
  hudtext("GO TO", (MENUW - textwidth("GO TO", 1))/2, 36, 0xffd894);
  for (int r = 0; r < TRAV_ROWS && travtop + r < n; r++) {
    int i = travtop + r, nd = l[i], y = 82 + r*32, on = i == travsel;
    const Node *d = node + nd;
    if (on) { mround(170, y-5, 428, 30, 6, 0xffdb87); mround(172, y-3, 424, 26, 5, 0xa67150); mhat(176, y-1 + SIN[(menufr*5) & 255]*2/256); }
    trav_name(nd, t, sizeof t);
    int lv = d->kind == N_LEVEL, done_ = lv && cleared(d->lvl);
    menutext(t, 214, y, 1, on ? 0xfff3d1 : done_ ? 0xffd894 : 0xc8d4dc);
    if (!lv) continue;
    for (int m = 0; m < LV[d->lvl].nmoon; m++) {   // its moon coins, filled once brought home
      int got = moonbits(d->lvl) >> m & 1, mx = 520 + m*20, my = y + 10;
      mellipse(mx, my, 8, 8, 0x0e1424); mellipse(mx, my, 6, 6, got ? 0xdbe6ff : 0x46607e);
      if (got) mellipse(mx-1, my, 4, 5, 0xf2c440);
    }
    if (cl_record(d->lvl) && cl_starred(d->lvl)) mstar(500, y + 10, 8, 0xffd84a);
  }
  if (travtop > 0) for (int j = 0; j < 6; j++) mrect(384 - j, 66 + j, 2*j + 1, 1, 0xffd894);   // more above, more below
  if (travtop + TRAV_ROWS < n) for (int j = 0; j < 6; j++) mrect(384 - j, 382 - j, 2*j + 1, 1, 0xffd894);
  centered("JUMP: GO   CAP: BACK", 390, 1, 0x8a94a0);
}
