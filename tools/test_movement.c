// Behavioral regression checks against the actual game simulation.
// Run: gcc -O1 -w tools/test_movement.c -o /tmp/hatrick-movement-tests && /tmp/hatrick-movement-tests
// Add --routes to also replay level recordings (retiming is needed after movement changes).
#define SIM
#define SC 1
#define main replay_main
#include "../hatrick.c"
#undef main

static int checks;
#define CHECK(expr) do { checks++; if (!(expr)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); exit(1); } } while (0)
static void fresh(void) {
  lvl = NLV; load(); prevk = 0;
  for (int i=0; i<10; i++) tick(0);
  CHECK(gnd);
}
static void jump(int run) {
  fresh();
  if (run) for (int i=0; i<25; i++) tick(2);
  tick((run ? 2 : 0)|16);
}
static void airborne(int vy) {
  fresh(); hy = 100<<8; hvy = vy; gnd = 0; coy = 99; cut = 0;
}
static void platform(int capstate, int y) {
  cst = capstate; cxp = hx; cyp = y<<8; cvx = 0; ct = 0; cready = 1; prevk = 32;
}
static void no_spawn_bounce(void) {
  int timings = 0;
  for (int left=0; left<2; left++) for (int mode=0; mode<4; mode++) {
    for (int delay=0; delay<32; delay++) {
      int run=mode&1, hold=mode<2, k=run ? (left ? 1 : 2) : 0;
      jump(0); face = left ? -1 : 1; hvx = run ? face*402 : 0; hx = 160<<8;
      for (int i=0; i<delay; i++) tick(k|(hold ? 16 : 0));
      if (gnd) continue;
      int y=hy;
      tick(k|32|(hold ? 16 : 0));
      CHECK(cst==1 && capok && hvy==0 && hy==y && throwt==CAPSTALL-1);
      timings++;
    }
  }
  printf("Airborne throw timings: %d\n", timings); CHECK(timings==212);
}
static void stall_and_combos(void) {
  for (int held=0; held<2; held++) {
    airborne(800); hvx=402;
    int y=hy, x=hx;
    tick(32|(held ? 16 : 0));
    for (int i=1; i<CAPSTALL; i++) { CHECK(hy==y && hvy==0 && capok); tick(32|(held ? 16 : 0)); }
    CHECK(hy==y && hvy==0 && throwt==0 && hx>x);
    tick(0); CHECK(hvy>0 && hy>y);
    cst=0; tick(32); CHECK(throwt==0 && hvy>0);  // cannot hover forever by rethrowing
  }
  jump(1); for (int i=0;i<10;i++) tick(2|16);
  tick(2|32); CHECK(throwt>0);
  tick(2|32|8); CHECK(st==DIVE && throwt==0 && hvy<0);
  int bounced=0;
  for (int i=0;i<35;i++) { tick(2|32); if (!capok) { bounced=1; break; } }
  CHECK(bounced && hvy==-CAPBOUNCE_V && cst==3 && diveok && stall && hvx==760);
  while (cst) tick(2|32);
  tick(2); tick(2|32); CHECK(throwt==CAPSTALL-1 && !capok);
  tick(2|32|8); CHECK(st==DIVE && !diveok && throwt==0);

  airborne(0); tick(8); CHECK(st==GPWIND);
  tick(8|32); CHECK(st==DIVE && hvy==-420+GRAV);
  airborne(0); tick(32); tick(32|8); CHECK(st==DIVE);
  airborne(0); tick(CAP2); CHECK(cst==1 && throwt>0);
  tick(CAP2|8); CHECK(st==DIVE);  // either cap button works while held
  airborne(0); tick(32); tick(32|8); st=GPWIND; diveok=1;
  tick(32|CAP2|8); CHECK(st==DIVE); // second face button has an independent press edge

  airborne(-1000); hvx=1000; face=1; tick(8|32);
  CHECK(st==DIVE && hvx==1000 && hvy==-1000+GRAV);
  airborne(-1000); hvx=-1000; face=-1; tick(8|32);
  CHECK(st==DIVE && hvx==-1000 && hvy==-1000+GRAV);
  airborne(0); tick(8); for (int i=0;i<16;i++)tick(8);
  CHECK(st==GPSLAM && hvy==1400); // ground pound still works without cap held
  airborne(0); tick(32); load(); CHECK(!throwt && !cst && !cready);
}
static void bounce_contacts(void) {
  airborne(0); platform(2,105); tick(32); CHECK(capok && cst==2); // side overlap
  airborne(-200); hy=110<<8; platform(2,105); tick(32); CHECK(capok); // underside
  airborne(512); platform(2,112); tick(32);
  CHECK(!capok && hvy==-CAPBOUNCE_V && diveok && stall); // feet land on cap
  platform(2,(hy>>8)+12); hvy=256; tick(32); CHECK(hvy!=-CAPBOUNCE_V && !capok); // one air bounce
  airborne(128); platform(3,112); tick(32); CHECK(capok); // return is not a platform
  airborne(0); st=DIVE; hvx=0; platform(1,105); cready=0; tick(32); CHECK(capok); // unarmed cap
  airborne(0); st=DIVE; hvx=0; platform(1,105); tick(32); CHECK(!capok && hvy==-CAPBOUNCE_V); // intentional dive contact
  fresh(); platform(2,232); tick(2|32);
  CHECK(!gnd && hvy==-CAPVAULT_V && hvx==700 && capok && diveok && stall); // grounded vault retains air bounce
  airborne(0); platform(2,160);
  for (int i=0;i<200;i++) capupd(32);
  CHECK(cst==2); capupd(0); CHECK(cst==3); // hold indefinitely, then release
  airborne(0); platform(1,103); cxp=72<<8; cvx=1100; map[12][10]=map[13][10]=4;
  int before=cxp; capupd(32); CHECK(cst==2 && cxp==before); // cap stops outside a wall
  airborne(0); map[12][4]=map[13][4]=4; tick(32);
  CHECK(cst==2 && !scan(cxp>>8,cyp>>8,8,4,SOLID) && capok); // throw beside wall
  fresh(); hy=227<<8; hvy=512; gnd=0; capok=diveok=stall=0; map[30][3]=5;
  tick(0); CHECK(hvy==-1500 && !gnd && capok && diveok && stall && coy==99);
  tick(16); CHECK(hvy==-1500+GRAV); // jump cannot overwrite an automatic spring launch
}
static int stick(int a) { return ANALOG|((a+256)<<10); }
static void analog_control(void) {
  CHECK(stickaxis(0,0,32767,5000)==0 && stickaxis(4000,0,32767,5000)==0);
  CHECK(stickaxis(32767,0,32767,5000)==256 && stickaxis(-32767,0,32767,5000)==-256);
  CHECK(stickaxis(16000,0,32767,5000)>0 && stickaxis(16000,0,32767,5000)<256);
  CHECK(stickaxis(1,0,0,0)==0);
  CHECK(moveaxis(stick(128))==128 && moveaxis(stick(-128))==-128);
  CHECK(moveaxis(stick(-128)|2)==256 && moveaxis(stick(128)|1)==-256);
  fresh(); for (int i=0;i<40;i++)tick(stick(128)); CHECK(hvx==200);
  for (int i=0;i<40;i++)tick(stick(-128)); CHECK(hvx==-200);
  fresh(); for (int i=0;i<40;i++)tick(2); CHECK(hvx==MAXV); // full-speed input respects the configured speed limit
  for (int i=0;i<40;i++)tick(stick(64)); CHECK(hvx==100);
  airborne(0); hvx=900; tick(stick(128)); CHECK(hvx==900); // air steering preserves bonus speed
}
static void rolling(void) {
  for (int left=0; left<2; left++) {
    fresh(); hx=640<<8; face=left ? -1 : 1;
    tick(8|32); CHECK(st==ROLL && duck==5 && gnd && face*hvx==ROLLSTART);
    int start=face*hvx; tick(8); tick(8|CAP2);
    CHECK(st==ROLL && face*hvx<=start); // no instant boost spam
    for (int i=0;i<4;i++) { for(int n=0;n<15;n++)tick(8); tick(8|32); }
    CHECK(face*hvx>900 && face*hvx<=ROLLMAX);
    int speed=face*hvx; tick(8|16);
    CHECK(st==LONGJ && !gnd && !duck && face*hvx>=speed*998/1000 && hvy==-560+34);
    int landed=0;
    for(int i=0;i<60;i++) { tick(8); if(gnd) {landed=1;break;} }
    CHECK(landed && st==ROLL && face*hvx>900);
    cst=0; tick(32); CHECK(st==NORM && cst==1 && gnd && !duck); // roll cancel + throw
  }
  airborne(600); hy=227<<8; st=DIVE; hvx=900; tick(8);
  CHECK(gnd && st==ROLL && hvx==900 && duck==5);
  airborne(600); hy=227<<8; st=DIVE; hvx=900; tick(0);
  CHECK(gnd && st==SLIDE && duck==5);
  tick(8); CHECK(st==ROLL && hvx==900);
  fresh(); tick(8|32); tick(8);
  for(int i=0;i<100;i++) tick(8);
  CHECK(st==ROLL && hvx>400); // sustained crouch roll has gentle drag
}
static int peak(int kind) {
  fresh(); hx=160<<8;
  int k=16;
  if(kind==1) jn=0, landt=0; // stationary double
  if(kind==2) jn=1, landt=0, hvx=402, k|=2;
  if(kind==3) k|=8;
  if(kind==4) st=GPLAND, stt=4;
  if(kind==5) hvx=-200, skid=5, k|=2;
  int start=hy, top=hy; tick(k);
  if(kind==1) CHECK(jn==1 && hvx==0);
  for(int i=0;i<120 && !gnd;i++) { if(hy<top)top=hy;tick(k&~8); }
  return start-top;
}
static void jumps_and_spin(void) {
  int normal=peak(0), dbl=peak(1), triple=peak(2), back=peak(3), pound=peak(4), side=peak(5);
  CHECK(triple>pound && pound>back && back>side && dbl>normal);
  printf("Jump heights: triple %.1f, pound %.1f, backflip %.1f, sideflip %.1f px\n",triple/256.,pound/256.,back/256.,side/256.);
  fresh(); jn=-1; landt=0; tick(16); CHECK(jn==0); // special-move landing cannot start a double
  fresh(); jn=1; landt=0; tick(16); CHECK(jn==0); // stationary third returns to normal jump
  // Perform a real stationary first/second jump chain, including landing edges.
  fresh(); hx=160<<8; tick(16); CHECK(jn==0);
  for(int i=0;i<90 && !gnd;i++)tick(16);
  CHECK(gnd); tick(0); tick(16); CHECK(jn==1 && hvx==0);
  fresh(); tick(4|16); CHECK(st==SPINJ && !gnd);
  for(int i=0;i<50 && hvy<0;i++)tick(0);
  CHECK(st==SPINJ && hvy>=0); int vy=hvy; tick(0); CHECK(hvy==vy+13);
  tick(8); CHECK(st==GPWIND && gpspin);
  // Buffer the roll during the last few frames before a spinning impact.
  st=GPSLAM; hy=224<<8; hvy=1400; tick(8|32);
  CHECK(gnd && st==ROLL && hvx==MAXV*30/14 && duck==5);
  airborne(0); st=GPSLAM; hy=224<<8; tick(8|32);
  CHECK(gnd && st==ROLL && hvx==ROLLSTART);
  fresh(); st=GPLAND; gpspin=1; tick(8|32); CHECK(st==ROLL && hvx==MAXV*30/14);
}
static void catches_and_throws(void) {
  fresh(); cst=3; cxp=hx+256; cyp=hy+768; tick(0);
  CHECK(!cst && catcht==10); tick(16); CHECK(!gnd && hvy==-900+42);
  airborne(400); cst=3; cxp=hx+256; cyp=hy+768; tick(0);
  CHECK(!cst && catcht==10); int y=hy; tick(16);
  CHECK(twirl && !throwt && !catchok && hy<y && hvy<0 && stall);
  for(int i=0;i<13;i++)tick(0);
  CHECK(!throwt && !twirl && hvy>0);
  tick(32); CHECK(throwt==CAPSTALL-1 && cst==1); // catch recharges the throw stall
  cst=3; cxp=hx+256; cyp=hy+768; tick(0); tick(16);
  CHECK(!catchok && !twirl); // cannot repeat the catch jump endlessly
  airborne(0); hx=160<<8; tick(4|32);
  CHECK(ckind==CAPUP && !cvx && cvy<0 && cyp<hy && capok);
  airborne(0); hx=160<<8; tick(8|CAP2);
  CHECK(ckind==CAPDOWN && !cvx && cvy>0 && cyp>hy+(11<<8) && st==NORM && diveok);
  airborne(0); tick(8|32); CHECK(st==DIVE); // the primary button still dives
  airborne(0); hx=160<<8; map[11][20]=4; tick(4|32);
  CHECK(!scan(cxp>>8,cyp>>8,8,4,SOLID));
  while(cst==1)capupd(32);
  CHECK(cst==2 && !scan(cxp>>8,cyp>>8,8,4,SOLID)); // vertical cap stops outside ceiling
  airborne(0); hx=160<<8; map[15][20]=4; tick(8|CAP2);
  while(cst==1)capupd(32);
  CHECK(cst==2 && !scan(cxp>>8,cyp>>8,8,4,SOLID));
  airborne(0); hx=160<<8; map[11][20]=6; int count=coins; tick(4|32);
  for(int i=0;i<10;i++)capupd(32);
  CHECK(coins>count && !map[11][20]); // directional throw collects a coin
  fresh(); st=GPLAND; posture(4); tick(32);
  CHECK(ckind==CAPDOWN && cvx>0 && !cvy && cyp==hy+(7<<8)); // ground-pound landing throw
  fresh(); tick(4|16); tick(32); CHECK(ckind==CAPSPIN && st==SPINJ);
  capok=1; for(int i=0;i<24;i++)capupd(32); CHECK(cst==3 && capok);
  // This wall-jump refresh is an intentional feature, including the cap bounce.
  airborne(0); wall=1; capok=diveok=stall=catchok=0; tick(16);
  CHECK(st==NORM && hvy==-900+GRAV && capok && diveok && stall && catchok);
}
static void crouch_collision(void) {
  fresh(); hx=32<<8;
  for(int x=6;x<16;x++)map[28][x]=4; // 8 px tunnel between the roof and floor
  for(int i=0;i<90;i++)tick(2|8);
  CHECK(hx>60<<8 && hvx==128 && duck==4 && gnd && st==NORM);
  CHECK(!scan(hx>>8,(hy>>8)+duck,6,11-duck,SOLID));
  CHECK(scan(hx>>8,hy>>8,6,11,SOLID));
  tick(0); CHECK(duck==4 && gnd); // cannot stand inside the ceiling
  for(int i=0;i<80;i++)tick(2);
  CHECK(hx>128<<8 && !duck && st==NORM);
  fresh(); hx=32<<8; for(int x=6;x<16;x++)map[28][x]=4;
  tick(2|8|32); for(int i=0;i<10;i++)tick(8);
  CHECK(st==ROLL && duck==5 && hx>48<<8 && !scan(hx>>8,(hy>>8)+duck,6,11-duck,SOLID));
  tick(8|16); CHECK(st==LONGJ && duck==5); // no expansion into the roof during takeoff
}
static void braking_control(void) {
  for(int left=0;left<2;left++) {
    int direction=left?-1:1, key=left?1:2, opposite=left?2:1;
    fresh(); hx=320<<8;
    for(int i=0;i<30;i++)tick(key);
    CHECK(hvx==direction*MAXV);
    int x=hx;
    for(int i=0;i<10;i++)tick(0);
    CHECK(hvx==0 && iabs(hx-x)<=8<<8 && gnd); // stop within one tile
    tick(0); CHECK(hvx==0); // never reverse when friction reaches zero
    fresh(); hx=320<<8; hvx=direction*MAXV; face=direction;
    for(int i=0;i<10;i++)tick(opposite);
    CHECK(direction*hvx<=0 && skid); // turn braking is as strong as release braking
    fresh(); hx=320<<8; hvx=direction*1040; face=direction;
    for(int i=0;i<26;i++)tick(0);
    CHECK(hvx==0); // retained trick speed still brakes promptly
    fresh(); hx=320<<8; st=SLIDE; posture(5); hvx=direction*760; face=direction;
    x=hx; for(int i=0;i<30 && hvx;i++)tick(0);
    CHECK(st==NORM && !hvx && iabs(hx-x)<28<<8);
    fresh(); hx=320<<8; st=SLIDE; posture(5); hvx=direction*760; face=direction;
    tick(opposite); CHECK(st==NORM && gnd);
    for(int i=0;i<20;i++)tick(opposite);
    CHECK(direction*hvx<0); // dive landing no longer locks out counter-steering
    airborne(0); hvx=direction*MAXV; face=direction;
    for(int i=0;i<15;i++)tick(opposite);
    CHECK(direction*hvx<=0); // normal air counter-steering brakes twice as strongly
    airborne(-500); hvx=direction*900; face=direction; tick(key);
    CHECK(hvx==direction*900); // forward airborne trick momentum survives
    airborne(-500); st=LONGJ; hvx=direction*700; face=direction;
    tick(opposite); CHECK(st==LONGJ && hvx==direction*(700-AACC));
    airborne(-500); st=DIVE; hvx=direction*760; face=direction;
    tick(opposite); CHECK(st==DIVE && hvx==direction*(760-AACC));
    hvx=direction*72; tick(opposite); CHECK(hvx==direction*(MAXV*5/28));
    fresh(); hx=320<<8; st=ROLL; posture(5); hvx=direction*1040; face=direction;
    tick(0); CHECK(st==NORM && hvx==direction*1000);
    tick(0); CHECK(hvx==direction*960); // release does not coast through an extra frame
  }
}
static void longjump_inputs(void) {
  int cases=0;
  for(int left=0;left<2;left++) {
    int key=left?1:2, sign=left?-1:1;
    // Crouching for a fraction of a second or much longer must not change move selection.
    for(int delay=0;delay<=60;delay++) {
      fresh(); hx=640<<8;
      for(int i=0;i<25;i++)tick(key);
      for(int i=0;i<delay;i++)tick(key|8);
      tick(key|8|16);
      CHECK(st==LONGJ && !gnd && hvy<0 && sign*hvx>=700); cases++;
    }
    // Also accept the jump-first ordering, only during the first five airborne frames.
    for(int delay=1;delay<=5;delay++) {
      fresh(); hx=640<<8;for(int i=0;i<25;i++)tick(key);
      tick(key|16); for(int i=1;i<delay;i++)tick(key|16);
      tick(key|16|8); CHECK(st==LONGJ && hvy<0 && sign*hvx>=700); cases++;
    }
    fresh(); hx=640<<8;for(int i=0;i<25;i++)tick(key);
    for(int i=0;i<8;i++)tick(8);tick(8|16);CHECK(st==LONGJ); // remembered running intent
    fresh(); hx=640<<8; tick(key|8|16); CHECK(st==LONGJ); // no run-up is required
    fresh(); hx=640<<8;tick(stick(sign*64)|8|16);CHECK(st==LONGJ); // even a partial stick
    fresh(); hx=640<<8;face=sign;tick(8|16);
    CHECK(st==NORM && hvy<0 && sign*hvx<0 && spin && arcg==32); // stationary backflip stays available
    fresh(); hx=640<<8;for(int i=0;i<25;i++)tick(key);
    tick(key|16); for(int i=0;i<8;i++)tick(key|16);
    tick(key|8);CHECK(st==GPWIND); // late Down remains a ground pound
    fresh(); hx=640<<8;st=ROLL;posture(5);hvx=sign*900;face=sign;
    int y=hy;tick(key|16|32);
    CHECK(st==NORM && !gnd && hy<y && hvy<0 && cst==1 && !throwt && !duck);
    for(int i=0;i<5;i++)tick(key|16);CHECK(!gnd && hy<y); // the launch actually persists
  }
  printf("Long-jump timing combinations: %d\n",cases);
}
static void remaining_combos(void) {
  airborne(1000);st=GPSLAM;tick(8|32);
  CHECK(st==DIVE && hvy<0 && hvx>=760 && !diveok); // cancel the descent, not just the windup
  fresh();hy=224<<8;gnd=0;st=GPSLAM;tick(8);CHECK(st==GPLAND && gnd);
  tick(16);CHECK(gnd && st==GPLAND); // early jump waits until the allowed recovery frame
  for(int i=0;i<4;i++)tick(16);
  CHECK(!gnd && hvy<0 && st==NORM && spin);
  fresh();st=GPLAND;stt=19;poundt=12;tick(16);CHECK(hvy==-GPJUMP_V+GRAV);
  fresh();jn=0;landt=9;tick(16);CHECK(jn==1);
  fresh();jn=0;landt=11;tick(16);CHECK(jn==0);
  airborne(-100);st=SPINJ;tick(0);CHECK(hvy==-87);hvy=100;tick(0);CHECK(hvy==113);
  fresh();tick(4);CHECK(st==GSPIN && gnd);
  for(int i=0;i<25;i++)tick(2);
  CHECK(st==GSPIN && hvx<=MAXV*8/14);
  tick(32);CHECK(ckind==CAPSPIN && gnd && st==GSPIN);
  tick(16);CHECK(st==SPINJ && !gnd && hvy<0);
  fresh();tick(4);for(int i=0;i<90;i++)tick(0);CHECK(st==NORM);
  // A press made just before the cap comes back survives the catch, including direction.
  airborne(0);hx=320<<8;cst=3;cxp=hx+(10<<8);cyp=hy+768;
  tick(4|32);CHECK(capbuf && cst==3);
  for(int i=0;i<8 && ckind!=CAPUP;i++)tick(0);
  CHECK(ckind==CAPUP && cst && cvy<0 && !capbuf);
  // Explicit recall uses the second button. Primary requests one append/homing throw.
  airborne(0);hx=320<<8;tick(32);for(int i=0;i<16;i++)tick(32);
  CHECK(cst==2);tick(0);tick(CAP2);CHECK(cst==3 && !capbuf);
  airborne(0);hx=320<<8;tick(32);for(int i=0;i<16;i++)tick(32);
  int x=cxp;tick(0);tick(32);CHECK(capextend && cst==1 && cxp>x && cvx>0);
  tick(0);int v=cvx;tick(32);CHECK(cvx<v && capextend); // cannot extend forever
  airborne(0);hx=320<<8;tick(32);for(int i=0;i<16;i++)tick(32);
  ne=1;en[0]=(E){cxp+(20<<8),cyp-(15<<8),0,0,1,1,cyp-(15<<8)};
  tick(0);tick(32);CHECK(capextend && cvx>0 && cvy<0); // homing targets an actual enemy
  fresh();hx=320<<8;tick(8|32);tick(8);
  tick(32);CHECK(capreflect && cst);
  cxp=400<<8;cyp=232<<8;cvx=1100;map[29][51]=4;
  capupd(32);CHECK(cvx<0 && !capreflect); // rolling throw reflects once
  // Press frequency cannot bypass the roll restart interval.
  fresh();hx=640<<8;tick(8|32);int start=hvx;
  for(int i=0;i<6;i++){tick(8);tick(8|32);}CHECK(hvx<=start);
  for(int i=0;i<15;i++)tick(8);start=hvx;tick(8|32);CHECK(hvx>start);
  lvl=0;load();prevk=0;tick(PRACTICE);CHECK(lvl==NLV+1 && !done && map[29][28]==8 && map[22][43]==9);
  tick(0);tick(PRACTICE);CHECK(lvl==0 && !done);
}
static void terrain_moves(void) {
  for(int left=0;left<2;left++) {
    fresh(); int key=left?1:2;
    for(int x=20;x<28;x++)for(int y=25;y<30;y++)map[y][x]=4;
    hx=(left?224:154)<<8;hy=196<<8;gnd=0;hvy=100;hvx=left?-200:200;coy=99;
    for(int i=0;i<8 && st!=HANG;i++)tick(key);
    CHECK(st==HANG && !gnd && !hvy && !hvx);
    tick(4);CHECK(st==CLIMB);
    for(int i=0;i<12 && st==CLIMB;i++)tick(0);
    CHECK(st==NORM && gnd && hy==189<<8 && !scan(hx>>8,hy>>8,6,11,SOLID));
    // Hang again, then drop without immediately catching the same edge.
    hx=(left?224:154)<<8;hy=196<<8;gnd=0;hvy=100;hvx=left?-200:200;
    for(int i=0;i<8 && st!=HANG;i++)tick(key);
    CHECK(st==HANG);tick(8);CHECK(st==NORM && ledget>0);
  }
  lvl=NLV+1;load();prevk=0;for(int i=0;i<10;i++)tick(0);
  int high=hy, airborne_frames=0;
  for(int i=0;i<300 && hx<430<<8;i++) {
    tick(2);if(hy<high)high=hy;if(!gnd)airborne_frames++;
    CHECK(!scan(hx>>8,(hy>>8)+duck,6,11-duck,SOLID));
  }
  CHECK(hx>420<<8 && high<180<<8 && airborne_frames<5); // walk the full ramp up and down
  // On the descending ramp, gravity adds speed to an intentional roll.
  hx=360<<8;hy=181<<8;hvx=500;gnd=1;st=ROLL;posture(5);slopedir=1;boostt=0;
  int start=hvx;tick(8);CHECK(hvx>start && gnd && slopedir==1);
  tick(0);CHECK(st==NORM && hvx<start); // release still brakes on slopes
  hx=360<<8;hy=181<<8;hvx=0;gnd=1;st=NORM;duck=0;slopedir=1;
  tick(8);CHECK(st==ROLL && hvx>0 && gnd && duck==5); // crouch starts a downhill roll
  hx=360<<8;hy=181<<8;hvx=-500;face=-1;gnd=1;st=ROLL;slopedir=1;
  tick(8);CHECK(hvx>-500 && hvx<0 && gnd); // the same ramp slows an uphill roll
  lvl=NLV+1;load();prevk=0;hx=420<<8;hy=229<<8;gnd=1;
  high=hy;airborne_frames=0;
  for(int i=0;i<300 && hx>200<<8;i++) {
    tick(1);if(hy<high)high=hy;if(!gnd)airborne_frames++;
    CHECK(!scan(hx>>8,(hy>>8)+duck,6,11-duck,SOLID));
  }
  CHECK(hx<210<<8 && high<180<<8 && airborne_frames<5); // slopes also traverse in reverse
  fresh();map[29][20]=8;
  CHECK(!scan(160,232,1,1,SOLID) && scan(167,232,1,1,SOLID)); // triangle is not a full block
  // Floor and ceiling collision use exactly the same ramp shape for a thrown cap.
  airborne(0);hx=160<<8;map[12][22]=9;tick(32);
  for(int i=0;i<12 && cst==1;i++)capupd(32);
  CHECK(!scan(cxp>>8,cyp>>8,8,4,SOLID));
}

int main(int argc, char **argv) {
  no_spawn_bounce(); stall_and_combos(); bounce_contacts(); analog_control(); rolling(); jumps_and_spin(); catches_and_throws(); crouch_collision(); braking_control(); longjump_inputs(); remaining_combos(); terrain_moves();
  if (argc > 1 && !strcmp(argv[1], "--routes")) for (int i=0; i<NLV; i++) {
    char level[2] = { '0'+i, 0 }, path[] = "tas/1.tas";
    path[4] += i;
    char *args[] = { "sim", level, path };
    prevk = 0;
    CHECK(replay_main(3,args)==0);
  }
  printf("PASS: %d movement checks, including airborne throw timings\n",checks);
}
