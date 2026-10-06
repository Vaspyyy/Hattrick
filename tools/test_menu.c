// Focused overworld map and pause screen checks against the actual game.
#define SIM
#define SC 1
#define main replay_main
#include "../hatrick.c"
#undef main
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static void fresh(void) {   // the map at the start of a game, nothing cleared yet
  nprog=0;lvl=0;load();menu=1;resumable=0;menufr=menunav=menurepeat=prevk=quitting=scoreview=naming=0;
  coins=lcoins=deaths=tim=done=shake=0;unlockt=0;unlocknode=-1;mapstart();
}
static void tap(int k) {tick(k);tick(0);}
static void walk(int k) {tick(k);for(int i=0;i<300 && mapto>=0;i++)tick(0);}   // press a direction, let the walk finish
static void clearall(void) {for(int i=0;i<NLV;i++)progkeep(i,8);}
int main(void) {
  fresh(); int x=hx,y=hy,enemy=en[0].x;
  CHECK(nnode==NLV+2 && node[0].kind==N_HOUSE && node[1].lvl==0 && node[nnode-1].lvl==PLAY);
  CHECK(mapat==1);   // Hatrick starts at the first level
  for(int i=0;i<120;i++)tick(0);
  CHECK(menu && !resumable && menufr==120 && !tim && hx==x && hy==y && en[0].x==enemy);
  // Paths: the next level is locked until this one is cleared; home and the playground are always open.
  walk(2);CHECK(mapat==1 && mapto<0);
  walk(1);CHECK(mapat==0);walk(8);CHECK(mapat==nnode-1);walk(4);CHECK(mapat==0);walk(2);CHECK(mapat==1);
  progkeep(0,8);walk(2);CHECK(mapat==2);walk(2);CHECK(mapat==2);
  fresh();tick(ANALOG|((256-200)<<10));CHECK(mapto==0);   // the stick walks too
  for(int i=0;i<300 && mapto>=0;i++)tick(ANALOG|((256+64)<<10));
  CHECK(mapat==0);tick(ANALOG|((256+64)<<10));CHECK(mapto<0);   // and its dead zone doesn't
  // Holding a direction keeps walking from stop to stop.
  fresh();clearall();mapat=0;for(int i=0;i<2000 && mapat<3;i++)tick(2);CHECK(mapat>=3);
  // Every open stop can be entered; holding confirm can't jump on entry.
  fresh();clearall();
  for(int n=1;n<nnode;n++) {
    menu=1;resumable=0;prevk=0;mapat=n;mapto=-1;tick(16);
    CHECK(!menu && resumable && lvl==node[n].lvl && !jbuf);
    tick(16);CHECK(!jbuf && hvy>=0);
    tick(0);for(int k=0;k<10;k++)tick(0);tick(16);CHECK(hvy<0); // a fresh jump works normally
  }
  fresh();walk(1);tick(16);CHECK(menu && scoreview);tick(0);tick(16);CHECK(menu && !scoreview);   // home: the high scores
  // A new run starts at level 1; later levels keep the run's score and time.
  fresh();clearall();score=900;tim=50;mapat=3;tick(16);CHECK(lvl==2 && score==900 && tim>=50);
  fresh();score=900;tick(16);CHECK(lvl==0 && !score && !tim);
  // The pause screen: Start, Esc, controller B and the cap button resume.
  fresh();clearall();mapat=3;tick(START);CHECK(!menu && lvl==2);
  tick(0);tick(START);CHECK(menu && resumable && pausesel==0);tick(START);CHECK(menu); // held Start doesn't bounce between modes
  tick(0);tick(START);CHECK(!menu && lvl==2);
  tick(0);tick(BACK);CHECK(menu);tick(0);tick(MENUBACK);CHECK(!menu);
  tick(0);tick(BACK);CHECK(menu);tick(0);tick(32);CHECK(!menu);
  // A controller B press is a jump in play and a back action in the menu.
  // Its raw bits stay the same between screens, avoiding a synthetic jump on resume.
  tick(0);tick(BACK);tick(0);tick(16|MENUBACK);CHECK(!menu);
  tick(16|MENUBACK);CHECK(!jbuf);tick(0);
  tick(16|MENUBACK|START);CHECK(menu);tick(16|MENUBACK);CHECK(menu);
  tick(0);tick(16|MENUBACK);CHECK(!menu);
  // Every gameplay subsystem pauses; resume doesn't reload the level.
  tick(0);tick(16);tick(0);tick(32);tick(0);
  x=hx;y=hy;int capx=cxp,capy=cyp,time=tim,frame=fr,state=st,left0=left;
  coins=7;deaths=3;tick(BACK);CHECK(menu);
  for(int i=0;i<30;i++)tick(0);
  CHECK(hx==x && hy==y && cxp==capx && cyp==capy && tim==time && fr==frame && st==state && coins==7 && deaths==3 && left==left0);
  tick(32);CHECK(!menu && hx==x && hy==y && tim==time && coins==7);
  // Down picks "exit to map": back on the map at this level's stop.
  tick(0);tick(BACK);tick(0);tap(8);CHECK(pausesel==1);tap(8);CHECK(pausesel==0);tap(4);CHECK(pausesel==1);
  tick(16);CHECK(menu && !resumable && mapat==3);
  // Clearing a level for the first time comes back to the map with the next path opening.
  fresh();tick(16);CHECK(lvl==0);
  hx=(gx*8-3)<<8;hy=(gy*8-12)<<8;hvx=300;tick(2);CHECK(st==WIN && cleared(0));
  for(int i=0;i<8;i++)tick(0);tick(16);
  CHECK(menu && !resumable && mapat==1 && unlocknode==2 && unlockt>0);
  walk(2);CHECK(mapat==2);
  // F1: the playground from the map, and back to the map from it.
  fresh();tick(PRACTICE);CHECK(!menu && lvl==PLAY);tick(0);tick(PRACTICE);CHECK(menu && !resumable && mapat==nnode-1);
  fresh();tick(MENUBACK);CHECK(menu && !quitting);tick(0);tick(BACK);CHECK(quitting);
  fresh();tick(QUIT);CHECK(quitting);
  // The mouse: the stop under the pointer; a click walks there, a click on Hatrick's stop enters.
  fresh();render();
  for(int n=0;n<nnode;n++) {
    int sx=(node[n].x-mapcam)*SC,sy=node[n].y*SC;
    if(sx<0||sx>=SW) continue;
    CHECK(maphit(sx,sy)==n && maphit(sx+5*SC,sy-5*SC)==n && maphit(sx+12*SC,sy)!=n);
  }
  mapclick(0);for(int i=0;i<300 && mapto>=0;i++)tick(0);CHECK(mapat==0);
  mapclick(0);CHECK(scoreview);tick(16);CHECK(!scoreview);
  mapclick(2);for(int i=0;i<300;i++)tick(0);CHECK(mapat==0);   // a locked stop: no walk
  fresh();muted=0;tick(128);CHECK(muted==1);
  tick(128);CHECK(muted==1);tick(0);tick(128);CHECK(!muted);
  x=hx;y=hy;time=tim;render();CHECK(menu && hx==x && hy==y && tim==time); // drawing does not touch the gameplay position
  menu=1;resumable=1;render();   // the pause screen draws over the level
  printf("PASS: %d map and pause checks\n",checks);
}
