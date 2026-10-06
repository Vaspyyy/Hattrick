// Focused ledge clearance checks against the actual movement code.
// gcc -O1 -w tools/test_ledges.c -o /tmp/hatrick-ledge-tests && /tmp/hatrick-ledge-tests
#define SIM
#define SC 1
#define main replay_main
#include "../hatrick.c"
#undef main
static int checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"FAIL line %d: %s (x=%d y=%d st=%d)\n",__LINE__,#x,hx>>8,hy>>8,st);exit(1);}} while(0)
static void setup(int left,int obstruction,int type) {
  lvl=NLV;menu=done=prevk=0;load();
  // Mirror a wall so both grab directions get the same geometry.
  int col=left?20:21,air=left?21:20;
  for(int y=28;y<30;y++)map[y][col]=4;
  if(obstruction>=0)map[28+obstruction][air]=type;
  hx=(left?168:162)*256;hy=220*256;hvx=hvy=gnd=duck=0;coy=99;st=NORM;
}
int main(void) {
  for(int left=0;left<2;left++) {
    int k=left?1:2;
    // A one-tile stair used to snap the hanging body's feet into the lower step.
    setup(left,1,4);tick(k);
    CHECK(st!=HANG && !scan(hx>>8,hy>>8,6,11,SOLID));
    for(int i=0;i<16;i++){tick(k);CHECK(st!=HANG && !scan(hx>>8,hy>>8,6,11,SOLID));}
    // Reject every solid tile kind underneath a prospective hanging position.
    for(int type=1;type<=5;type++){setup(left,1,type);tick(k);CHECK(st!=HANG);}
    setup(left,1,left?9:8);tick(k);CHECK(st!=HANG); // a ramp is also occupied
    // Two full air tiles above the floor leave enough room for a real ledge.
    setup(left,-1,0);tick(k);
    CHECK(st==HANG && !scan(hx>>8,hy>>8,6,11,SOLID));
    tick(4);CHECK(st==CLIMB);
    for(int i=0;i<12 && st==CLIMB;i++)tick(0);
    CHECK(st==NORM && gnd && (hy>>8)==213 && !scan(hx>>8,hy>>8,6,11,SOLID));
    setup(left,-1,0);tick(k);CHECK(st==HANG);tick(8);CHECK(st==NORM && ledget>0);
    // Ordinary coins in the air space shouldn't make a valid ledge ungrabbable.
    setup(left,1,6);tick(k);CHECK(st==HANG && !scan(hx>>8,hy>>8,6,11,SOLID));
  }
  printf("PASS: %d ledge checks (stairs, ramps, valid ledges, climb/drop, both directions)\n",checks);
}
