#include "fd_retro.h"
#include <cstdio>
#include <set>
#include <vector>
#include <cstring>
static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ printf("FAIL: %s\n", msg); fails++; } }while(0)

int main(){
    const int W=64,H=64;
    auto grad=[&](std::vector<uint8_t>&b){ b.resize((size_t)W*H*4);
        for(int y=0;y<H;y++)for(int x=0;x<W;x++){uint8_t*p=&b[((size_t)y*W+x)*4];
            p[0]=x*255/(W-1); p[1]=y*255/(H-1); p[2]=128; p[3]=255; } };

    // 1. posterize reduces distinct R values
    std::vector<uint8_t> a; grad(a);
    std::set<int> before; for(int x=0;x<W;x++) before.insert(a[(x)*4]);
    FdRetro pc; pc.palette=0; pc.levels=4; pc.scanline=0;
    fd_retro_apply(a.data(),W,H,pc);
    std::set<int> after; for(size_t i=0;i<a.size();i+=4) after.insert(a[i]);
    CHECK(after.size() <= 8, "posterize should collapse to few R levels");
    printf("posterize levels=4 -> %zu distinct R (was %zu)\n", after.size(), before.size());

    // 2. determinism: same input+cfg -> identical output
    std::vector<uint8_t> b1,b2; grad(b1); grad(b2);
    fd_retro_apply(b1.data(),W,H,pc); fd_retro_apply(b2.data(),W,H,pc);
    CHECK(memcmp(b1.data(),b2.data(),b1.size())==0, "transform must be deterministic");

    // 3. ega16: every output pixel must be one of the 16 palette colours
    std::vector<uint8_t> e; grad(e);
    FdRetro ec; ec.palette=2; ec.scanline=0;
    fd_retro_apply(e.data(),W,H,ec);
    bool all_pal=true;
    for(size_t i=0;i<e.size();i+=4){ bool hit=false;
        for(int k=0;k<16;k++) if(e[i]==FD_EGA16[k][0]&&e[i+1]==FD_EGA16[k][1]&&e[i+2]==FD_EGA16[k][2]){hit=true;break;}
        if(!hit){all_pal=false;break;} }
    CHECK(all_pal, "ega16 output must only contain palette colours");
    printf("ega16 snap: all pixels in 16-colour set = %s\n", all_pal?"yes":"NO");

    // 4. scanlines darken odd rows vs even rows (flat mid-grey input)
    std::vector<uint8_t> s((size_t)W*H*4, 120); for(size_t i=3;i<s.size();i+=4)s[i]=255;
    FdRetro sc; sc.palette=0; sc.levels=64; sc.scanline=50;
    fd_retro_apply(s.data(),W,H,sc);
    int even=s[(0*W+10)*4], odd=s[(1*W+10)*4];
    CHECK(odd < even, "odd scanline row should be darker");
    printf("scanline: even row R=%d, odd row R=%d\n", even, odd);

    // 5. alpha preserved
    CHECK(a[3]==255 && e[3]==255, "alpha must be untouched");

    // 6. disabled = no-op
    std::vector<uint8_t> n1,n2; grad(n1); grad(n2);
    FdRetro off; off.enabled=false; fd_retro_apply(n1.data(),W,H,off);
    CHECK(memcmp(n1.data(),n2.data(),n1.size())==0, "disabled must be a no-op");

    printf(fails? "\n%d CHECK(S) FAILED\n":"\nALL RETRO CHECKS PASSED\n", fails);
    return fails?1:0;
}
