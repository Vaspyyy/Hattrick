// Drawing for movement.h: ice, conveyor belts and swing poles over the tiles, water over
// everything in it, and Hatrick's swing, swim and flutter poses. Included before drawhero().
static void mv_blend(int x, int y, int w, int h, u32 c, int a) {   // a/256 of colour c over screen px
  if (x < 0) w += x, x = 0;
  if (y < 0) h += y, y = 0;
  if (x+w > SW) w = SW-x;
  if (y+h > SH) h = SH-y;
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
    u32 b = big[y+j][x+i];
    big[y+j][x+i] = ((b >> 16 & 255)*(256-a) + (c >> 16 & 255)*a) >> 8 << 16 | ((b >> 8 & 255)*(256-a) + (c >> 8 & 255)*a) >> 8 << 8 | ((b & 255)*(256-a) + (c & 255)*a) >> 8;
  }
}
static u32 mv_tilepx(int s, int tx, int ty, int u, int v) {
  switch (s) {
    case MV_ICE: {
      int top = !(SOLID >> tile(tx, ty-1) & 1);
      if (top && v == 0) return 0xffffff;
      if (u == 7 || v == 7) return 0x5a9ec8;
      if ((u + v + tx*3) % 7 == 0 && v < 5) return 0xf4fcff;   // glints
      return v < 2 && top ? 0xd6f2ff : 0xa8dcf4;
    }
    case MV_CONVL: case MV_CONVR: {
      int dir = s == MV_CONVR ? 1 : -1, ph = (tx*8 + u - dir*(fr >> 1)) & 7;
      if (v == 0 || v == 7) return 0x24262c;                   // the belt
      if (v < 3) return (ph == 2 || ph == 3) ? 0xffcc3a : 0x3a3e48;   // moving chevrons
      if (v == 3) return 0x24262c;
      int r = (u - 3)*(u - 3) + (v - 5)*(v - 5) * 2;           // a roller
      return r < 6 ? ((fr >> 2) + u & 1 ? 0xa8b0bc : 0x707884) : 0x50545e;
    }
    case MV_POLE: {
      int l = mvsurf(tx-1, ty) == MV_POLE, r = mvsurf(tx+1, ty) == MV_POLE;
      if ((!l && u == 0) || (!r && u == 7)) return v >= 2 && v <= 5 ? 0x586070 : 0;   // end caps
      return v == 3 ? 0xf4f6fa : v == 4 ? 0x9aa4b4 : 0;
    }
  }
  return 0;
}
static void mv_drawtiles(void) {
  for (int ty = fdiv(oy, 8*SC); ty <= (oy+SH) / (8*SC); ty++)
    for (int tx = fdiv(ox, 8*SC); tx <= (ox+SW) / (8*SC); tx++) {
      int s = mvsurf(tx, ty);
      if (s && s != MV_WATER) for (int v = 0; v < 8; v++) for (int u = 0; u < 8; u++) {
        u32 c = mv_tilepx(s, tx, ty, u, v);
        if (c) wpx(tx*8+u, ty*8+v, c);
      }
    }
}
static void mv_drawwater(void) {
  for (int ty = fdiv(oy, 8*SC); ty <= (oy+SH) / (8*SC); ty++)
    for (int tx = fdiv(ox, 8*SC); tx <= (ox+SW) / (8*SC); tx++) {
      if (mvsurf(tx, ty) != MV_WATER) continue;
      int sx = tx*8*SC - ox, sy = ty*8*SC - oy;
      if (mvsurf(tx, ty-1) != MV_WATER) {   // the surface: a light, gently moving line
        for (int u = 0; u < 8; u++) {
          int wave = SIN[(tx*8 + u)*8 + fr*3 & 255] > 96;
          mv_blend(sx + u*SC, sy + wave*SC, SC, 8*SC - wave*SC, 0x2a78d0, 110);
          blk(sx + u*SC, sy + wave*SC, SC, SC, 0xc8ecff);
        }
      } else mv_blend(sx, sy, 8*SC, 8*SC, 0x2a78d0, 110);
    }
}
// Swinging, swimming and fluttering poses; called by drawhero() just before it draws.
static void mv_pose(const u16 **f, int *fl, int *ang, int *sx, int *fx, int *fy) {
  if (st >= TUBE) return;
  if (mv_swing) {
    int a = (face > 0 ? -(mv_ang >> 8) : mv_ang >> 8) & 255;
    *f = HHANG; *fl = face < 0; *ang = a; *sx = 256;
    *fx = hx + (3 << 8); *fy = hy + (11 << 8);   // the sprite turns about its centre, the body's
  } else if (mv_swim && !gnd) {
    *f = mv_stroke > 6 ? HTHROW : HJUMP;
    *ang = hvx / 24 + (hvy > 0 ? 6 : -6)*face;
  } else if (mv_slide && st == SLIDE) {   // the slope slide: sitting, tipped back along the slope
    *f = HCROUCH; *ang = gnd ? -slopedir*24 : 0;
    if (!(fr & 1)) dust(hx + (3 << 8) - face*(2 << 8), hy + (11 << 8), -face, 1);
  } else if (mv_flut || capx_flut) {   // either flutter: the legs paddle
    *f = fr >> 1 & 1 ? HRUN1 : HRUN2;
  }
}
