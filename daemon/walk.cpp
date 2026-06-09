// walk.cpp -- Feverdream Engine: drive a rigged character in the live engine.
//
// Renders a --rig scene from mdl2pov.py (a character split into body + 2 legs that
// swing about the hips) against the resident POV-Ray session, one frame per loop,
// and feeds it pose declares from the keyboard:
//   UP/DOWN  walk forward/back (advances the step phase + moves position)
//   LEFT/RIGHT turn   SPACE jump   F filter   ESC quit
// Spatial upscaling: render at win/rdiv, upscale to the window (see live.cpp).
//
//   walk rigged.pov [winW] [winH] [rdiv] [libdir]

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <cmath>
#include <SDL2/SDL.h>
#include "vfe.h"
#include "vfeplatform.h"
using namespace vfe; using namespace vfePlatform; using namespace pov_frontend;

static std::vector<unsigned char> g_fb; static int g_w=0,g_h=0;
static inline void fb_put(unsigned x,unsigned y,const Display::RGBA8&c){
    if((int)x>=g_w||(int)y>=g_h)return; unsigned char*p=&g_fb[(y*(size_t)g_w+x)*4];
    p[0]=c.red;p[1]=c.green;p[2]=c.blue;p[3]=255; }
class Cap: public vfeDisplay { public:
    Cap(unsigned w,unsigned h,GammaCurvePtr g,vfeSession*s,bool v):vfeDisplay(w,h,g,s,v){}
    void Initialise() override { g_w=GetWidth();g_h=GetHeight();
        if((int)g_fb.size()!=g_w*g_h*4)g_fb.assign((size_t)g_w*g_h*4,0); }
    void DrawPixel(unsigned x,unsigned y,const RGBA8&c) override { fb_put(x,y,c); }
    void DrawPixelBlock(unsigned x1,unsigned y1,unsigned x2,unsigned y2,const RGBA8*c) override {
        unsigned i=0; for(unsigned y=y1;y<=y2;++y)for(unsigned x=x1;x<=x2;++x)fb_put(x,y,c[i++]); } };
static vfeDisplay* mk(unsigned w,unsigned h,GammaCurvePtr g,vfeSession*s,bool v){return new Cap(w,h,g,s,v);}

static bool render(vfeUnixSession*s,const std::string&scene,const std::string&lib,int w,int h,
                   double posx,double posz,double jump,double turn,double step){
    vfeRenderOptions o; const char*tc=getenv("FD_THREADS"); o.SetThreadCount(tc?atoi(tc):8);
    if(!lib.empty())o.AddLibraryPath(lib); o.SetSourceFile(scene); char b[80];
    snprintf(b,sizeof b,"Width=%d",w);o.AddCommand(b); snprintf(b,sizeof b,"Height=%d",h);o.AddCommand(b);
    snprintf(b,sizeof b,"Declare=POSX=%.3f",posx);o.AddCommand(b);
    snprintf(b,sizeof b,"Declare=POSZ=%.3f",posz);o.AddCommand(b);
    snprintf(b,sizeof b,"Declare=JUMP=%.3f",jump);o.AddCommand(b);
    snprintf(b,sizeof b,"Declare=TURN=%.3f",turn);o.AddCommand(b);
    snprintf(b,sizeof b,"Declare=STEP=%.3f",step);o.AddCommand(b);
    o.AddCommand("Antialias=off");o.AddCommand("Output_to_File=off");o.AddCommand("Display=on");
    o.AddCommand("Verbose=off");o.AddCommand("Pause_When_Done=off");
    if(s->SetOptions(o)!=vfeNoError)return false; if(s->StartRender()!=vfeNoError)return false;
    while((s->GetStatus(true,1)&stRenderShutdown)==0); return true; }

int main(int argc,char**argv){
    std::string scene=(argc>1)?argv[1]:"thom_rig.pov";
    int winW=(argc>2)?atoi(argv[2]):960, winH=(argc>3)?atoi(argv[3]):540, rdiv=(argc>4)?atoi(argv[4]):3;
    std::string lib=(argc>5)?argv[5]:"/usr/share/povray-3.7/include"; if(rdiv<1)rdiv=1;

    if(SDL_Init(SDL_INIT_VIDEO)!=0){fprintf(stderr,"SDL:%s\n",SDL_GetError());return 1;}
    SDL_Window*win=SDL_CreateWindow("Feverdream - walk (raytraced, live rig)",
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,winW,winH,SDL_WINDOW_SHOWN);
    SDL_Renderer*ren=SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    int rW=winW/rdiv,rH=winH/rdiv,filt=0;
    SDL_Texture*tex=SDL_CreateTexture(ren,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,rW,rH);
    SDL_SetTextureScaleMode(tex,SDL_ScaleModeNearest);

    vfeUnixSession*s=new vfeUnixSession();
    if(s->Initialize(NULL,NULL)!=vfeNoError){fprintf(stderr,"init:%s\n",s->GetErrorString());return 1;}
    s->SetDisplayCreator(mk);

    // character state. The rig swings legs about hips in the model's Z; "forward"
    // is the facing direction (turn about Y). Units are model-space (Thom ~tall).
    double posx=0,posz=0,turn=0,step=0;     // step = walk-cycle phase
    double jumpY=0,jumpV=0; bool grounded=true;
    const double SPEED=2.2, STEP_RATE=9.0, TURN_RATE=2.5, G=-90.0, JV=34.0;
    bool running=true; Uint32 prev=SDL_GetTicks(),fpsT=prev; int fpsN=0;
    long maxf=getenv("FD_MAXFRAMES")?atol(getenv("FD_MAXFRAMES")):0, done=0;

    printf("walk: UP/DOWN move, LEFT/RIGHT turn, SPACE jump, F filter, ESC quit\n");
    while(running){
        Uint32 now=SDL_GetTicks(); double dt=(now-prev)/1000.0; if(dt>0.1)dt=0.1; prev=now;
        const Uint8*k=SDL_GetKeyboardState(NULL);
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type==SDL_QUIT)running=false;
            else if(e.type==SDL_KEYDOWN){
                if(e.key.keysym.sym==SDLK_ESCAPE)running=false;
                else if(e.key.keysym.sym==SDLK_SPACE && grounded){jumpV=JV;grounded=false;}
                else if(e.key.keysym.sym==SDLK_f){filt=(filt+1)%3;SDL_SetTextureScaleMode(tex,(SDL_ScaleMode)filt);}
            }
        }
        double move=0;
        if(k[SDL_SCANCODE_UP])move+=1; if(k[SDL_SCANCODE_DOWN])move-=1;
        if(k[SDL_SCANCODE_LEFT])turn-=TURN_RATE*dt; if(k[SDL_SCANCODE_RIGHT])turn+=TURN_RATE*dt;
        if(move!=0){
            step+=STEP_RATE*dt*(move>0?1:-1);          // animate legs while moving
            double fwd=move*SPEED;                       // facing dir from turn (about Y)
            posx+=sin(turn)*fwd; posz+=cos(turn)*fwd;
        }
        // jump arc
        if(!grounded){ jumpV+=G*dt; jumpY+=jumpV*dt; if(jumpY<=0){jumpY=0;jumpV=0;grounded=true;} }

        if(!render(s,scene,lib,rW,rH,posx,posz,jumpY,turn*57.2958,step)){
            fprintf(stderr,"render:%s\n",s->GetErrorString()); break; }
        if(g_w==rW&&g_h==rH&&!g_fb.empty()){
            SDL_UpdateTexture(tex,NULL,g_fb.data(),rW*4);
            SDL_RenderClear(ren); SDL_RenderCopy(ren,tex,NULL,NULL); SDL_RenderPresent(ren); }
        if(maxf&&++done>=maxf){ FILE*f=fopen("walk_selftest.ppm","wb");
            if(f){fprintf(f,"P6\n%d %d\n255\n",g_w,g_h);
                for(size_t i=0;i<(size_t)g_w*g_h;++i)fwrite(&g_fb[i*4],1,3,f);fclose(f);}
            printf("self-test: %ld frames\n",done); running=false; }
        if(++fpsN>=10){ double fps=fpsN*1000.0/(now-fpsT); char t[120];
            snprintf(t,sizeof t,"Feverdream walk - %dx%d->%dx%d  %.0f fps",rW,rH,winW,winH,fps);
            SDL_SetWindowTitle(win,t); fpsT=now; fpsN=0; }
    }
    s->Shutdown(); delete s;
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
