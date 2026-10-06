// Focused menu state and input checks against the actual game.
#define SIM
#define SC 1
#define main replay_main
#include "../hatrick.c"
#undef main
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static void fresh(void) {
  lvl=0;load();menu=1;menusel=menufr=menunav=menurepeat=prevk=resumable=quitting=0;
  coins=lcoins=deaths=tim=done=shake=0;
}
static void tap(int k) {tick(k);tick(0);}
int main(void) {
  fresh(); int x=hx,y=hy,enemy=en[0].x;
  for(int i=0;i<120;i++)tick(0);
  CHECK(menu && !resumable && menufr==120 && !tim && hx==x && hy==y && en[0].x==enemy);
  tap(2);CHECK(menusel==1);tap(8);CHECK(menusel==4);tap(1);CHECK(menusel==3);
  tap(4);CHECK(menusel==0);tap(1);CHECK(menusel==MENUN-1);tap(2);CHECK(menusel==0);
  tick(2);CHECK(menusel==1);for(int i=0;i<17;i++)tick(2);CHECK(menusel==1);
  tick(2);CHECK(menusel==2);for(int i=0;i<6;i++)tick(2);CHECK(menusel==3);
  fresh();tick(ANALOG|((256+160)<<10));CHECK(menusel==1);
  tick(ANALOG|((256+200)<<10));CHECK(menusel==1); // stick noise doesn't create new key presses
  tick(ANALOG|((256+64)<<10));CHECK(menusel==1);
  tick(ANALOG|((256-200)<<10));CHECK(menusel==0);
  for(int i=0;i<MENUN;i++) {
    fresh();menusel=i;tick(16);
    CHECK(!menu && resumable && lvl==(i==NLV?NLV+1:i) && !tim && !jbuf);
    tick(16);CHECK(!jbuf && hvy>=0); // holding confirm cannot jump on entry
    tick(0);for(int n=0;n<10;n++)tick(0);tick(16);CHECK(hvy<0); // a fresh jump works normally
  }
  fresh();menusel=2;tick(START);CHECK(!menu && lvl==2);
  tick(0);tick(START);CHECK(menu && menusel==2);tick(START);CHECK(menu); // held Start doesn't bounce between modes
  tick(0);tick(BACK);CHECK(!menu && lvl==2);
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
  x=hx;y=hy;int capx=cxp,capy=cyp,time=tim,frame=fr,state=st;
  coins=7;deaths=3;tick(BACK);CHECK(menu);
  for(int i=0;i<30;i++)tick(0);
  CHECK(hx==x && hy==y && cxp==capx && cyp==capy && tim==time && fr==frame && st==state && coins==7 && deaths==3);
  tick(32);CHECK(!menu && hx==x && hy==y && tim==time && coins==7);
  tick(0);tick(BACK);menusel=NLV;tick(0);tick(16);
  CHECK(lvl==NLV+1 && !coins && !deaths && !tim);
  fresh();tick(PRACTICE);CHECK(!menu && lvl==NLV+1);
  fresh();tick(MENUBACK);CHECK(menu && !quitting);tick(0);tick(BACK);CHECK(quitting);
  fresh();tick(QUIT);CHECK(quitting);
  // Mouse hit regions match the renderer, including card edges and gaps.
  for(int i=0;i<MENUN;i++) {
    fresh();int cx=22+i%3*73,cy=62+i/3*32;
    CHECK(menuhit(cx,cy)==i && menuhit(cx+65,cy+26)==i);
    CHECK(menuhit(cx+66,cy+10)==-1 && menuhit(cx+10,cy+27)==-1);
  }
  fresh();muted=0;tick(128);CHECK(muted==1);
  tick(128);CHECK(muted==1);tick(0);tick(128);CHECK(!muted);
  x=hx;y=hy;time=tim;render();CHECK(menu && hx==x && hy==y && tim==time); // drawing does not touch the gameplay position
  printf("PASS: %d menu checks\n",checks);
}
