#pragma once
// TP sword particles (sword-particles-01, 21-oct; docs/particulas-tp-diseno-2026-10-21.md). Pure: the INI keys, the JPA
// particle simulation on the real TP values (sword_particles_data.h, generated from docs/data/tp-particles/), the
// Ganado hit watch, the blade/wall contact search and the log lines; no game access. The glue (spin_attack_runtime.h)
// feeds the trail's blade samples, the Ganado pool and the EatMgr nearest-hit test; sword_particles_draw.h draws.
//
// INI, [LinkRuntime]; an invalid value keeps its default and is counted in `problems`:
//   SwordParticles         missing or 1 = on, 0 = fully inert (no reads, no registration). Needs SwordTrail=1: the
//                          contact search reads the trail's blade samples (a move not in SwordTrailMoves has none)
//   SwordParticlesHit      1 (default) / 0: TP's hit marks where the blade hits a Ganado (HITOK on a native knife slash,
//                          HITCR on our clips: spin, Great Spin, Back Slice, Ending Blow) and the hit flash
//   SwordParticlesWall     1 / 0: COLHIT_HIBANA sparks (and the flash) where the blade hits a wall, KIKUZU chips for the
//                          wall effect types listed in SwordParticlesWoodTypes
//   SwordParticlesSpin     1 / 0: at the spin's strike, KAITENGIRIL_D sparkles in the trail colour, DOWNSMOKE_A dust
//                          at Link's feet and KUSA01_KEN leaves at 6 points of the tip's ring
//   SwordParticlesGain     0.25..4 (default 1.0): brightness of the additive particles (alpha first, then colour)
//   SwordParticlesAmount   0.1..1 (default 0.35): fraction of TP's particle counts each emitter throws (at least one);
//                          Donald found TP's full counts too many flying bits (21-oct)
//   SwordParticlesDust     RRGGBB (default 8C7A5A): the dust colour (TP gives DOWNSMOKE the floor's colour)
//   SwordParticlesWoodTypes comma list of wall effect types 0..7 (EatGetEffectType of the polygon's attribute word, the
//                          `type=` of the `sword_particles wall` lines; up to 8 items, default none): KIKUZU wood chips
//   SwordParticlesWoodAttr the sword-particles-01 key (hex attribute values): still accepted, ignored (a log line says
//                          so); the nearest-hit test gives the full word, whose effect type is what tells the material
//
// Simulation (RESUMEN.md "Cómo leer los números", JPABaseParticle::calc_p): one tick per TP frame (30 Hz) on game time,
// whatever the frame rate; per tick vel_base *= air, vel_field += gravity x fade (never damped), pos += (vel_base +
// vel_field + this tick's pushes) x moment; air also on the first tick. A particle emitted mid-tick is born on the
// latest tick and gets its first step at once; drawing interpolates between the last two ticks. No collision (TP has
// none). Random ranges as JPARandom (r[0,1), r[-1,1), r[-.5,.5)) from a seeded LCG: the same calls give the same state.
// TP cm x 10 = RE4 mm.
#include "sword_particles_data.h"
#include "sword_trail_feed.h"      // Digits, Hex6 (the trail's INI parsing)
#include "mortal_draw_target.h"    // the Ganado layout (hit watch)
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace link_runtime::sword_particles {
constexpr double kPi=3.14159265358979323846;
constexpr double kTickMs=1000.0/30.0;    // one TP frame
constexpr double kCm=10.0;               // TP cm -> RE4 mm
constexpr unsigned kPool=1024,kEmitters=64;
constexpr unsigned kMaxCatchUp=30;       // ticks; further behind (a long hitch or a pause with time running) = stale: cleared
constexpr double kBackwardsMs=50.0;

// ---- INI ------------------------------------------------------------------------------------------------------------
constexpr unsigned kMaxWood=8;     // items in SwordParticlesWoodTypes
constexpr unsigned kEffectTypes=8; // EatGetEffectType: 0..7
constexpr double kMinGain=0.25,kMaxGain=4.0,kMinAmount=0.1,kMaxAmount=1.0,kDefaultAmount=0.35;
struct Config {
    bool on=true,hit=true,wall=true,spin=true;double gain=1.0,amount=kDefaultAmount;uint32_t dust=0x8C7A5A;
    uint8_t wood_types=0;          // bit t: effect type t throws KIKUZU wood chips
    bool wood_attr_ignored=false;  // the old SwordParticlesWoodAttr key was set (accepted, ignored, logged)
    unsigned problems=0;
    bool Wood(int type) const {return type>=0&&type<int(kEffectTypes)&&((wood_types>>type)&1)!=0;}
};
// "2, 5": effect types 0..7 (one digit each), comma separated, up to kMaxWood items (a repeat is allowed). false: empty,
// a bad item or too many; `out` is then untouched.
inline bool ParseWoodTypes(const wchar_t* v,uint8_t& out){
    uint8_t mask=0;unsigned n=0;const wchar_t* p=v;
    if(!p)return false;
    while(true){
        while(*p==L' '||*p==L'\t')++p;
        if(!(*p>=L'0'&&*p<=L'7')||n==kMaxWood)return false;
        mask=uint8_t(mask|1u<<unsigned(*p-L'0'));++n;++p;
        while(*p==L' '||*p==L'\t')++p;
        if(!*p)break;
        if(*p!=L',')return false;
        ++p;
    }
    out=mask;return true;
}
// get(key, out, count) fills `out` with the raw INI value ("" when missing).
template<class Get> Config ReadConfig(Get&& get){
    Config c;wchar_t v[96]{};
    auto read=[&](const wchar_t* key){v[0]=0;get(key,v,sizeof(v)/sizeof(v[0]));return v[0]!=0;};
    auto flag=[&](const wchar_t* key,bool& out){
        if(!read(key))return;
        if(v[0]==L'0'&&!v[1])out=false;else if(v[0]==L'1'&&!v[1])out=true;else ++c.problems;
    };
    flag(L"SwordParticles",c.on);flag(L"SwordParticlesHit",c.hit);flag(L"SwordParticlesWall",c.wall);
    flag(L"SwordParticlesSpin",c.spin);
    if(read(L"SwordParticlesGain")){
        wchar_t* end=nullptr;const double g=std::wcstod(v,&end);
        if(end!=v&&!*end&&std::isfinite(g)&&g>=kMinGain&&g<=kMaxGain)c.gain=g;else ++c.problems;
    }
    if(read(L"SwordParticlesAmount")){
        wchar_t* end=nullptr;const double a=std::wcstod(v,&end);
        if(end!=v&&!*end&&std::isfinite(a)&&a>=kMinAmount&&a<=kMaxAmount)c.amount=a;else ++c.problems;
    }
    if(read(L"SwordParticlesDust")&&!link_runtime::sword_trail::feed::Hex6(v,c.dust))++c.problems;
    if(read(L"SwordParticlesWoodTypes")&&!ParseWoodTypes(v,c.wood_types))++c.problems;
    if(read(L"SwordParticlesWoodAttr"))c.wood_attr_ignored=true;   // any value: not read any more, not a problem
    return c;
}

// ---- vectors --------------------------------------------------------------------------------------------------------
struct V3 {double x=0,y=0,z=0;};
inline V3 Add(const V3& a,const V3& b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline V3 Sub(const V3& a,const V3& b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline V3 Mul(const V3& a,double k){return {a.x*k,a.y*k,a.z*k};}
inline double Dot(const V3& a,const V3& b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline V3 Cross(const V3& a,const V3& b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline double Len(const V3& a){return std::sqrt(Dot(a,a));}
inline bool Finite(const V3& a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
// Unit vector, or `fallback` when degenerate or not finite.
inline V3 Unit(const V3& a,const V3& fallback={0,0,0}){
    const double l=Len(a);return std::isfinite(l)&&l>1e-9?Mul(a,1.0/l):fallback;
}
inline V3 Lerp(const V3& a,const V3& b,double u){return {a.x+(b.x-a.x)*u,a.y+(b.y-a.y)*u,a.z+(b.z-a.z)*u};}
// Any unit vector perpendicular to unit `n`.
inline V3 Perpendicular(const V3& n){return Unit(std::fabs(n.y)<0.9?Cross(n,{0,1,0}):Cross(n,{1,0,0}),{1,0,0});}
// Rotation about unit `axis` by `angle` radians (Rodrigues).
inline V3 Rotate(const V3& v,const V3& axis,double angle){
    const double c=std::cos(angle),s=std::sin(angle);
    return Add(Add(Mul(v,c),Mul(Cross(axis,v),s)),Mul(axis,Dot(axis,v)*(1-c)));
}
// 3x3, world = M * local; m[row][col]. No default value (M3{} is zero), so the large static arrays holding one stay
// zero-initialised (.bss, not in the DLL file); Identity() for a fresh frame.
struct M3 {
    double m[3][3];
    static M3 Identity(){M3 r{};r.m[0][0]=r.m[1][1]=r.m[2][2]=1;return r;}
};
inline V3 Apply(const M3& a,const V3& v){
    return {a.m[0][0]*v.x+a.m[0][1]*v.y+a.m[0][2]*v.z,a.m[1][0]*v.x+a.m[1][1]*v.y+a.m[1][2]*v.z,
            a.m[2][0]*v.x+a.m[2][1]*v.y+a.m[2][2]*v.z};
}
inline M3 Times(const M3& a,const M3& b){
    M3 r{};for(int i=0;i<3;++i)for(int j=0;j<3;++j)r.m[i][j]=a.m[i][0]*b.m[0][j]+a.m[i][1]*b.m[1][j]+a.m[i][2]*b.m[2][j];
    return r;
}
inline M3 Columns(const V3& x,const V3& y,const V3& z){
    M3 r{};r.m[0][0]=x.x;r.m[1][0]=x.y;r.m[2][0]=x.z;r.m[0][1]=y.x;r.m[1][1]=y.y;r.m[2][1]=y.z;r.m[0][2]=z.x;r.m[1][2]=z.y;r.m[2][2]=z.z;
    return r;
}
// The resource's local rotation (emitter_rotation_deg): about x, then y, then z.
inline M3 Euler(const float deg[3]){
    const double k=kPi/180.0,x=deg[0]*k,y=deg[1]*k,z=deg[2]*k;
    M3 rx=M3::Identity(),ry=M3::Identity(),rz=M3::Identity();
    rx.m[1][1]=std::cos(x);rx.m[1][2]=-std::sin(x);rx.m[2][1]=std::sin(x);rx.m[2][2]=std::cos(x);
    ry.m[0][0]=std::cos(y);ry.m[0][2]=std::sin(y);ry.m[2][0]=-std::sin(y);ry.m[2][2]=std::cos(y);
    rz.m[0][0]=std::cos(z);rz.m[0][1]=-std::sin(z);rz.m[1][0]=std::sin(z);rz.m[1][1]=std::cos(z);
    return Times(rz,Times(ry,rx));
}
// Link's facing (native yaw at player+0xA4: forward = (sin yaw, 0, cos yaw)) as an emitter frame: local z forward, y up.
inline M3 YawFrame(double yaw){
    const double s=std::sin(yaw),c=std::cos(yaw);
    return Columns({c,0,-s},{0,1,0},{s,0,c});
}
// A wall emitter, as TP sets COLHIT_* (d_a_alink_cut.inc changeCutReverseProc): the rotation
// csXyz(atan2(n.y, |n.xz|), atan2(-n.x, -n.z), 0) applied by JPAGetXYZRotateMtx (Rz * Ry * Rx, here Ry * Rx). Local -z
// is then the normal (out of the wall) and local y the world up made perpendicular to it, so the resources' direction
// (0, 0.287, -0.958) leaves the wall 17 deg upward. A degenerate normal is taken as +z; a vertical one has yaw 0
// (cM_atan2s(0, 0)).
inline M3 WallFrame(const V3& normal){
    const V3 n=Unit(normal,{0,0,1});
    const double xz=std::sqrt(n.x*n.x+n.z*n.z),pitch=std::atan2(n.y,xz),yaw=xz>1e-9?std::atan2(-n.x,-n.z):0.0;
    M3 rx=M3::Identity(),ry=M3::Identity();
    rx.m[1][1]=std::cos(pitch);rx.m[1][2]=-std::sin(pitch);rx.m[2][1]=std::sin(pitch);rx.m[2][2]=std::cos(pitch);
    ry.m[0][0]=std::cos(yaw);ry.m[0][2]=std::sin(yaw);ry.m[2][0]=-std::sin(yaw);ry.m[2][2]=std::cos(yaw);
    return Times(ry,rx);
}

// ---- JPARandom ------------------------------------------------------------------------------------------------------
// JMath::TRandom_fast_ (JPA's): x = x * 0x19660D + 0x3C6EF35F; floats from the top 23 bits.
class Rng {
    uint32_t v_;
public:
    explicit Rng(uint32_t seed=0):v_(seed){}
    uint32_t U(){v_=v_*0x19660Du+0x3C6EF35Fu;return v_;}
    double F(){return double(U()>>9)/8388608.0;}     // r[0,1)
    double ZP(){return F()*2.0-1.0;}                 // r[-1,1)
    double ZH(){return F()-0.5;}                     // r[-.5,.5)
    int16_t SS(){return int16_t(U()>>16);}           // a random binary angle
    uint32_t State() const {return v_;}
};
inline double Binary(double a){return a*2.0*kPi/65536.0;}   // JPA binary angle (65536 = 360 deg) to radians

// ---- the hit flash (TP's dKy_SordFlush: a light flash at the hit) ---------------------------------------------------
// Not a JPA resource (TP flashes the room light): a small additive soft disc, 40 cm, white to warm, 4 TP frames,
// alpha 1 -> 0 over its life, grown from half its size in the first quarter.
inline constexpr Def kFlashDef{"SORD_FLUSH",0,"-","-",
    {Volume::Point,0.f,1.f,1.f,1,false,false,false,false,{1.f,1.f,1.f},{0.f,0.f,0.f},{0.f,1.f,0.f},{0.f,0.f,0.f},
     0.f,0.f,0.f,0.f,0.f,0.f,1.f,0.f,1,1,0,4.f,0.f,1.f,0.f},
    {Shape::Billboard,DirType::Velocity,false,{40.f,40.f},true,Formula::EnvPrmLerp,0xFFFFFF,0xFFE6C8,255,255,
     ColourAnim::Normal,0,0,0,0,0,{},{}},
    {true,false,0.25f,1.f,0.5f,1.f,0.5f,1.f,0.f},{true,0.f,0.f,0.f,1.f,0.f},{false,0.f,0.f,0.f,0.f,0.f},0,{}};
constexpr Effect kFlash=Effect::Count;       // the flash's index, after the generated ones
constexpr unsigned kEffects=unsigned(Effect::Count)+1;
inline const Def& DefOf(unsigned e){return e<unsigned(Effect::Count)?kDefs[e]:kFlashDef;}
inline const char* EffectName(unsigned e){return e<kEffects?DefOf(e).name:"?";}

// ---- colour ---------------------------------------------------------------------------------------------------------
inline uint32_t Lighten(uint32_t rgb,double f){
    auto c=[&](unsigned s){const double v=double((rgb>>s)&0xFF);return uint32_t(std::lround(v+(255.0-v)*f))<<s;};
    return c(16)|c(8)|c(0);
}
inline uint32_t Modulate(uint32_t a,uint32_t b){   // channel-wise a x b / 255
    auto c=[&](unsigned s){return uint32_t(std::lround(double((a>>s)&0xFF)*double((b>>s)&0xFF)/255.0))<<s;};
    return c(16)|c(8)|c(0);
}
inline uint32_t Scale(uint32_t a,double k){
    auto c=[&](unsigned s){double v=double((a>>s)&0xFF)*k;v=v<0?0:v>255?255:v;return uint32_t(std::lround(v))<<s;};
    return c(16)|c(8)|c(0);
}
inline uint32_t Mix(uint32_t a,uint32_t b,double u){
    auto c=[&](unsigned s){const double x=double((a>>s)&0xFF),y=double((b>>s)&0xFF);return uint32_t(std::lround(x+(y-x)*u))<<s;};
    return c(16)|c(8)|c(0);
}
// A placement's colour change:
//   Multiply (JPA's global prm/env colour; TP's setPolyColor for the dust): prm and env x rgb.
//   Toward   (the spin sparkles in the trail colour): prm = rgb, env x rgb.
//   Fixed    (a texture's own colour, the leaves' olive): centre and edge as given.
enum class Tint:uint8_t {None,Multiply,Toward,Fixed};
struct Paint {Tint mode=Tint::None;uint32_t rgb=0,edge=0;};
constexpr double kPrmTexEdge=0.5;   // prm*tex: the texture's darker rim, toward the edge of the shape
// A colour table entry (JPA builds a per-frame table: linear between keys, held before the first and after the last).
inline void KeyAt(const Key* k,unsigned n,unsigned frame,uint32_t& rgb,uint8_t& a){
    if(!n)return;
    if(frame<=k[0].frame){rgb=k[0].rgb;a=k[0].a;return;}
    for(unsigned i=0;i+1<n;++i){
        if(frame<=k[i+1].frame){
            const double span=double(k[i+1].frame)-double(k[i].frame),u=span>0?(double(frame)-double(k[i].frame))/span:1.0;
            rgb=Mix(k[i].rgb,k[i+1].rgb,u);a=uint8_t(std::lround(double(k[i].a)+(double(k[i+1].a)-double(k[i].a))*u));return;
        }
    }
    rgb=k[n-1].rgb;a=k[n-1].a;
}
// The colour table index for a particle at whole frame `age` and normalised life `t` (tp_jpa.py key_frame_semantics).
inline unsigned ColourIndex(const Look& l,unsigned age,double t,unsigned offset){
    const unsigned max=l.anm_max_frame;
    switch(l.anim){
    case ColourAnim::Repeat:return (age+offset)%(max+1);
    case ColourAnim::Reverse:{if(!max)return 0;const unsigned m=(age+offset)%(2*max);return m<=max?m:2*max-m;}
    case ColourAnim::Merge:{const double f=t<0?0:t;return (unsigned(f*(max+1))+offset)%(max+1);}
    case ColourAnim::Random:return offset%(max+1);
    default:return age<max?age:max;
    }
}

// ---- curves over the normalised life --------------------------------------------------------------------------------
inline double Curve(double t,double in_t,double out_t,double in_v,double base,double out_v){
    if(t<in_t&&in_t>0)return in_v+(base-in_v)*(t/in_t);
    if(t>out_t&&out_t<1)return base+(out_v-base)*((t-out_t)/(1-out_t));
    return base;
}
inline void ScaleAt(const ScaleAnim& s,double t,double& x,double& y){
    x=y=1.0;
    if(!s.on)return;
    x=Curve(t,s.in_timing,s.out_timing,s.in_x,1.0,s.out_x);
    y=s.xy_differ?Curve(t,s.in_timing,s.out_timing,s.in_y,1.0,s.out_y):x;
}
inline double AlphaAt(const AlphaAnim& a,double t){
    return a.on?Curve(t,a.in_timing,a.out_timing,a.in_value,a.base,a.out_value):1.0;
}
// JPAFieldBase::calcFadeAffect, t = normalised life.
inline double FadeAffect(const Field& f,double t){
    if((f.enable&&t<f.en_time)||(f.disable&&t>=f.dis_time))return 0.0;
    if(f.fade_out&&t>=f.fade_out_time){const double d=f.dis_time-f.fade_out_time;return d>0?(f.dis_time-t)/d:0.0;}
    if(f.fade_in&&t<f.fade_in_time){const double d=f.fade_in_time-f.en_time;return d>0?(t-f.en_time)/d:1.0;}
    return 1.0;
}

// ---- the simulation -------------------------------------------------------------------------------------------------
struct F3 {float x=0,y=0,z=0;};
inline F3 ToF(const V3& v){return {float(v.x),float(v.y),float(v.z)};}
inline V3 ToD(const F3& v){return {v.x,v.y,v.z};}
// World mm; local = position from the emitter's origin (dir type "position", the spin field); v = this tick's velocity.
// All-zero defaults (the pool is a large static: .bss); Spawn sets every field.
struct Particle {
    F3 origin,local,prev,vb,vf,v,field[kMaxFields];
    float life=0,moment=0,scale=0,rot=0,prev_rot=0,rot_speed=0;
    int16_t age=0;uint8_t effect=0,offset=0;bool alive=false;Paint paint;
};
// Where an effect goes: the origin (world mm), its frame (world = frame * local; the resource's own rotation and
// translation are applied inside it) and the colour change.
struct Placement {V3 origin;M3 frame=M3::Identity();Paint paint;double size=1.0;};   // size: x the drawn size
// JPABaseParticle::calc_p: one TP frame of one particle (age, fields with their fades, air, moment, position, spin).
// false: it died at this tick (age reached its life).
inline bool StepParticle(Particle& p){
    if(!p.alive)return false;
    if(++p.age>=p.life){p.alive=false;return false;}
    const Def& d=DefOf(p.effect);
    const double t=double(p.age)/double(p.life);
    p.prev=p.local;p.prev_rot=p.rot;
    V3 vb=ToD(p.vb),vf=ToD(p.vf),push;const V3 local=ToD(p.local);
    for(unsigned i=0;i<d.fields&&i<unsigned(kMaxFields);++i){
        const Field& f=d.field[i];const double k=FadeAffect(f,t);
        V3 a=f.type==FieldType::Gravity?ToD(p.field[i]):Sub(Rotate(local,ToD(p.field[i]),f.mag),local);
        a=Mul(a,k);
        if(f.add_type==0)vf=Add(vf,a);else if(f.add_type==1)vb=Add(vb,a);else push=Add(push,a);
    }
    vb=Mul(vb,d.dyn.air);
    const V3 v=Mul(Add(Add(vb,vf),push),p.moment);
    p.vb=ToF(vb);p.vf=ToF(vf);p.v=ToF(v);p.local=ToF(Add(local,v));
    p.rot+=p.rot_speed;
    return true;
}
enum class Step:uint8_t {Idle,Ok,Room,Player,Backwards,Stale};
inline const char* StepName(Step s){
    static const char* const n[]={"idle","ok","room","player","backwards","stale"};
    return unsigned(s)<6?n[unsigned(s)]:"?";
}
// Game thread only. Counters are atomics so the worker can log them.
class System {
    struct Emitter {bool alive=false;uint8_t effect=0;uint16_t tick=0;bool emitted=false;double acc=0;V3 origin;M3 frame{};Paint paint;double size=1.0;};
    Particle p_[kPool];Emitter e_[kEmitters];Rng rng_;
    bool clock_=false;double anchor_=0;uint64_t ticks_=0;   // tick n at anchor_ + n * kTickMs
    bool scene_=false;uint32_t room_=0,player_=0;unsigned alive_=0,emitters_=0;
    std::atomic<unsigned> emitted_{0},overflow_{0},resets_{0},refused_{0};
    double amount_=1.0;
    double TickAt() const {return anchor_+double(ticks_)*kTickMs;}
    void Clear(){
        for(auto& p:p_)p.alive=false;
        for(auto& e:e_)e.alive=false;
        alive_=emitters_=0;clock_=false;ticks_=0;
    }
    void Spawn(const Emitter& e,const Def& d,unsigned index,unsigned count){
        const Dynamics& y=d.dyn;
        unsigned slot=kPool;
        for(unsigned i=0;i<kPool;++i)if(!p_[i].alive){slot=i;break;}
        if(slot==kPool){overflow_.fetch_add(1);return;}
        // JPAVolume*: the volume position (cm, resource-local) and the omni/axis directions.
        V3 vol,omni,axis;
        const double sweep=y.sweep,minr=y.min_radius;
        switch(y.volume){
        case Volume::Sphere:{
            const double x=Binary(double(rng_.SS()>>1)),yaw=Binary(double(rng_.SS()))*sweep;
            double r=rng_.F();if(y.fixed_density)r=1.0-r*r*r;
            const double rad=y.volume_cm*(minr+r*(1.0-minr));
            vol={rad*std::cos(x)*std::sin(yaw),-rad*std::sin(x),rad*std::cos(x)*std::cos(yaw)};omni=vol;axis={vol.x,0,vol.z};break;}
        case Volume::Cylinder:{
            const double th=Binary(double(rng_.SS()))*sweep;
            double r=rng_.F();if(y.fixed_density)r=1.0-r*r;
            const double rad=y.volume_cm*(minr+r*(1.0-minr));
            vol={rad*std::sin(th),y.volume_cm*rng_.ZP(),rad*std::cos(th)};omni=vol;axis={vol.x,0,vol.z};break;}
        case Volume::Circle:{
            const double th=(y.fixed_interval&&count?Binary(double(int16_t(uint16_t((index*0x10000u)/count)))):
                Binary(double(rng_.SS())))*sweep;
            double r=rng_.F();if(y.fixed_density)r=1.0-r*r;
            const double rad=y.volume_cm*(minr+r*(1.0-minr));
            vol={rad*std::sin(th),0,rad*std::cos(th)};omni=vol;axis={vol.x,0,vol.z};break;}
        default:   // Point
            omni={rng_.ZH(),rng_.ZH(),rng_.ZH()};axis={omni.x,0,omni.z};break;
        }
        // JPABaseParticle::init_p: the velocity terms (cm per frame, resource-local), then the emitter's rotation.
        V3 v;
        if(y.omni)v=Add(v,Mul(Unit(omni),y.omni));
        if(y.axis)v=Add(v,Mul(Unit(axis),y.axis));
        if(y.dir_speed){
            const V3 d0=Unit({y.direction[0],y.direction[1],y.direction[2]},{0,1,0});
            const double tilt=rng_.ZP()*y.spread*kPi,roll=Binary(double(rng_.SS()));
            const V3 u=Perpendicular(d0),w=Cross(d0,u);
            v=Add(v,Mul(Add(Mul(d0,std::cos(tilt)),Mul(Add(Mul(u,std::cos(roll)),Mul(w,std::sin(roll))),std::sin(tilt))),y.dir_speed));
        }
        if(y.random)v=Add(v,Mul(V3{rng_.ZH(),rng_.ZH(),rng_.ZH()},y.random));
        v=Mul(v,1.0+y.ratio_random*rng_.ZP());
        const V3 s{y.scale[0],y.scale[1],y.scale[2]};
        if(y.inherit_scale)v={v.x*s.x,v.y*s.y,v.z*s.z};
        const M3 w=Times(e.frame,Euler(y.rotation_deg));
        Particle& p=p_[slot];p=Particle{};
        p.origin=ToF(e.origin);p.local=p.prev=ToF(Mul(Apply(w,{vol.x*s.x,vol.y*s.y,vol.z*s.z}),kCm));
        p.vb=ToF(Mul(Apply(w,v),kCm));
        p.life=float(y.life*(1.0-y.life_random*rng_.F()));
        p.moment=float(1.0-y.moment_random*rng_.F());
        p.scale=float((1.0+d.scale.random*rng_.ZP())*(std::isfinite(e.size)&&e.size>0?e.size:1.0));
        if(d.rotate.on){
            p.rot=p.prev_rot=float((d.rotate.init_deg+d.rotate.random_deg*rng_.ZH())*kPi/180.0);
            double speed=d.rotate.speed_deg*(1.0+d.rotate.speed_random*rng_.ZP())*kPi/180.0;
            if(!(rng_.ZP()<d.rotate.direction))speed=-speed;
            p.rot_speed=float(speed);
        }
        p.offset=d.look.loop_mask?uint8_t(unsigned(rng_.F()*(d.look.loop_offset+1))&d.look.loop_mask):0;
        for(unsigned i=0;i<d.fields&&i<unsigned(kMaxFields);++i){
            const Field& f=d.field[i];const V3 dir{f.dir[0],f.dir[1],f.dir[2]};
            const V3 world=f.world?dir:Apply(w,dir);
            // Gravity: the acceleration (mm per tick^2); spin: its unit axis.
            p.field[i]=ToF(f.type==FieldType::Gravity?Mul(world,f.mag*kCm):Unit(world,{0,1,0}));
        }
        p.effect=e.effect;p.paint=e.paint;p.age=-1;p.alive=true;++alive_;emitted_.fetch_add(1);
    }
    // One emitter tick (JPADynamicsBlock::create): waits start_frame ticks, then emits every emit_every ticks for
    // max_frame ticks (0 is treated as 1: no endless emitter). Fixed interval: div_number particles on the ring.
    void Run(Emitter& e){
        const Def& d=DefOf(e.effect);const Dynamics& y=d.dyn;
        if(e.tick<y.start_frame)return;
        const unsigned k=unsigned(e.tick-y.start_frame),frames=y.max_frame?y.max_frame:1u,every=y.emit_every?y.emit_every:1u;
        if(k>=frames){e.alive=false;--emitters_;return;}
        if(k%every==0){
            unsigned n;
            if(y.fixed_interval)n=y.div_number;
            else{
                const double add=double(y.rate)*(1.0+double(y.rate_random)*rng_.ZP());
                e.acc+=add>0?add:0;n=unsigned(e.acc);e.acc-=n;
                if(!e.emitted&&!n&&add>0)n=1;   // the first emission rounds 0 < n < 1 up to 1
            }
            e.emitted=true;
            if(n&&amount_<1.0){const long m=std::lround(double(n)*amount_);n=m<1?1u:unsigned(m);}   // SwordParticlesAmount
            for(unsigned i=0;i<n;++i)Spawn(e,d,i,n);
        }
        if(k+1>=frames){e.alive=false;--emitters_;}
    }
    void Calc(Particle& p){if(!StepParticle(p))--alive_;}
    void Tick(){
        for(auto& e:e_)if(e.alive){++e.tick;Run(e);}
        for(auto& p:p_)if(p.alive)Calc(p);
        ++ticks_;
    }
public:
    explicit System(uint32_t seed=0):rng_(seed){}
    void Amount(double a){amount_=std::isfinite(a)&&a>0&&a<1?a:1.0;}   // the fraction of each emission thrown   // seed 0 by default: an all-zero object (a static goes to .bss)
    void Reset(){Clear();resets_.fetch_add(1);}
    void Reseed(uint32_t seed){rng_=Rng(seed);}   // the random sequence restarts (tests)
    unsigned Alive() const {return alive_;}
    unsigned Emitters() const {return emitters_;}
    bool Busy() const {return alive_||emitters_;}
    unsigned Emitted() const {return emitted_.load();}
    unsigned Overflow() const {return overflow_.load();}
    unsigned Resets() const {return resets_.load();}
    unsigned Refused() const {return refused_.load();}
    uint32_t Seed() const {return rng_.State();}
    const Particle& At(unsigned i) const {return p_[i];}
    // Up to `now` (game ms): every whole tick since the last one. Idle (nothing alive) re-anchors the tick grid at now,
    // so an effect starts on its emission time. A room or player change, time running back more than kBackwardsMs or
    // more than kMaxCatchUp ticks behind clear everything (the step says which).
    Step Advance(double now,uint32_t room,uint32_t player){
        if(!std::isfinite(now)){if(Busy())Reset();else Clear();return Step::Backwards;}
        Step reset=Step::Ok;
        if(scene_&&room!=room_)reset=Step::Room;else if(scene_&&player!=player_)reset=Step::Player;
        scene_=true;room_=room;player_=player;
        if(reset!=Step::Ok){Reset();clock_=true;anchor_=now;return reset;}
        if(!Busy()){clock_=true;anchor_=now;ticks_=0;return Step::Idle;}
        if(!clock_){clock_=true;anchor_=now;ticks_=0;return Step::Ok;}
        if(now<TickAt()-kBackwardsMs){Reset();clock_=true;anchor_=now;return Step::Backwards;}
        if(now-TickAt()>=double(kMaxCatchUp+1)*kTickMs){Reset();clock_=true;anchor_=now;return Step::Stale;}
        while(Busy()&&now-TickAt()>=kTickMs)Tick();
        if(!Busy()){anchor_=now;ticks_=0;}
        return Step::Ok;
    }
    // Between the latest tick (0) and the next (1).
    double Fraction(double now) const {
        if(!clock_||!std::isfinite(now))return 0.0;
        const double f=(now-TickAt())/kTickMs;return f<0?0:f>1?1:f;
    }
    // An effect at the latest tick (Advance to now first). Its first emission and their first step happen here; a later
    // start frame waits for the ticks. false: an unknown effect, a non-finite placement or no free emitter (refused).
    bool Emit(unsigned effect,const Placement& at){
        if(effect>=kEffects||!Finite(at.origin)){refused_.fetch_add(1);return false;}
        for(const auto& row:at.frame.m)for(double c:row)if(!std::isfinite(c)){refused_.fetch_add(1);return false;}
        Emitter* e=nullptr;
        for(auto& x:e_)if(!x.alive){e=&x;break;}
        if(!e){refused_.fetch_add(1);return false;}
        const Def& d=DefOf(effect);
        *e=Emitter{};e->alive=true;e->effect=uint8_t(effect);e->frame=at.frame;e->paint=at.paint;e->size=at.size;
        e->origin=Add(at.origin,Apply(at.frame,Mul(V3{d.dyn.translation_cm[0],d.dyn.translation_cm[1],d.dyn.translation_cm[2]},kCm)));
        ++emitters_;
        Run(*e);
        for(auto& p:p_)if(p.alive&&p.age<0)Calc(p);
        return true;
    }
    bool Emit(Effect effect,const Placement& at){return Emit(unsigned(effect),at);}
};

// What a particle looks like at `fraction` (0..1) past the latest tick: interpolated position and spin, the size (mm),
// alpha (0..1) and the colours at the centre and the edge of its shape (TEV approximated at the texture's centre,
// tex = 1, and its edge, tex = 0).
struct Visual {V3 pos,dir,local;double width=0,height=0,alpha=0,rot=0;uint32_t centre=0,edge=0;bool additive=false;
    Shape shape=Shape::Billboard;DirType dir_type=DirType::Velocity;bool anchor_bottom=false;Formula formula=Formula::EnvPrmLerp;};
inline bool VisualOf(const Particle& p,double fraction,Visual& out){
    if(!p.alive||p.life<=0)return false;
    const Def& d=DefOf(p.effect);const Look& l=d.look;
    const double f=fraction<0?0:fraction>1?1:fraction;
    const V3 local=Lerp(ToD(p.prev),ToD(p.local),f);
    out.pos=Add(ToD(p.origin),local);out.local=local;
    out.rot=double(p.prev_rot)+(double(p.rot)-double(p.prev_rot))*f;
    double age=double(p.age)-1.0+f;if(age<0)age=0;
    double t=age/double(p.life);if(t>0.999999)t=0.999999;
    double sx=1,sy=1;ScaleAt(d.scale,t,sx,sy);
    out.width=double(l.size_cm[0])*kCm*double(p.scale)*sx;out.height=double(l.size_cm[1])*kCm*double(p.scale)*sy;
    const unsigned idx=ColourIndex(l,unsigned(age),t,p.offset);
    uint32_t prm=l.prm,env=l.env;uint8_t prm_a=l.prm_a,env_a=l.env_a;
    KeyAt(l.prm_key,l.prm_keys,idx,prm,prm_a);KeyAt(l.env_key,l.env_keys,idx,env,env_a);
    out.alpha=double(prm_a)/255.0*AlphaAt(d.alpha,t);
    switch(p.paint.mode){
    case Tint::Multiply:prm=Modulate(prm,p.paint.rgb);env=Modulate(env,p.paint.rgb);break;
    case Tint::Toward:env=Modulate(env,p.paint.rgb);prm=p.paint.rgb;break;
    default:break;
    }
    out.centre=prm&0xFFFFFF;out.edge=(l.formula==Formula::EnvPrmLerp?env:Scale(prm,kPrmTexEdge))&0xFFFFFF;
    if(p.paint.mode==Tint::Fixed){out.centre=p.paint.rgb&0xFFFFFF;out.edge=p.paint.edge&0xFFFFFF;}
    out.dir=ToD(p.v);
    out.additive=l.additive;out.shape=l.shape;out.dir_type=l.dir_type;out.anchor_bottom=l.anchor_bottom;out.formula=l.formula;
    return Finite(out.pos)&&std::isfinite(out.width)&&std::isfinite(out.height)&&std::isfinite(out.alpha);
}

// ---- Ganado hits ----------------------------------------------------------------------------------------------------
constexpr double kHitRangeMm=3000.0;   // the least hit search radius: a Ganado this far from Link (horizontal) whose HP dropped
constexpr double kHitMarginMm=500.0;   // past the farthest move's reach: the Ganado's step between the publish and the drop
constexpr double kHitGraceMs=100.0;    // after the move's end, its hits still count
// The hit search radius (horizontal from Link) for `reach`, the farthest configured move reach in mm (the glue's
// ParticlesReach: the spin's radius, GreatSpinRadius up to 6 m, the Back Slice's strike reach 3.6 m, the Ending Blow's
// strike range), plus kHitMarginMm; never under kHitRangeMm (also for a reach not finite).
inline double HitRange(double reach){
    const double r=reach+kHitMarginMm;
    return std::isfinite(r)&&r>kHitRangeMm?r:kHitRangeMm;
}
constexpr double kBodyMm=1700.0;       // the body axis: feet .. feet + 1700
constexpr unsigned kMaxBursts=32;
// The weapon byte of the actor's last cDmgInfo (actor+0x328, published by 0x1AEEF0; mortal_draw_damage.h layout): the
// knife 0x10 is what Link's sword leaves, from our clips (the Mortal Draw adapter publishes the knife record) and from
// native slashes. Guns, kicks, grenades and fire leave others (>= 0x31 is stored as 2).
constexpr uint32_t kDmgWeapon=0x32E;
constexpr uint8_t kSwordWeapon=0x10;
struct Body {uint32_t pointer=0;uint8_t id=0,type=0;int16_t hp=0,hp_max=0;V3 pos;uint8_t weapon=0;};
// One read of the actor's first 0x328 bytes (cEm10 layout, mortal_draw_target.h): a live slot of the em10 vtable, of
// the Ganado family and not the Merchant. HP may be 0 or below (the killing hit). false: unreadable or not one.
template<class Reader> bool ReadBody(Reader& m,uint32_t pointer,uint32_t image_base,Body& out){
    namespace ly=mortal_draw::target::layout;
    static unsigned char b[kDmgWeapon+1];
    if(!pointer||!m.Read(pointer,b,sizeof(b)))return false;
    uint32_t vtable=0,be=0;float pos[3]{};int16_t hp=0,hp_max=0;
    std::memcpy(&vtable,b,4);std::memcpy(&be,b+ly::be_flag,4);std::memcpy(pos,b+ly::pos,12);
    std::memcpy(&hp,b+ly::hp,2);std::memcpy(&hp_max,b+ly::hp_max,2);
    mortal_draw::target::Raw r;r.id=b[ly::id];r.type=b[ly::type];r.r0=b[ly::routine];r.r1=b[ly::routine+1];
    if((be&0x201)!=1||vtable!=image_base+ly::em10_vtable_rva||!mortal_draw::target::GanadoFamily(r.id)||
       mortal_draw::target::Merchant(r)||hp_max<=0)return false;
    out={pointer,r.id,r.type,hp,hp_max,{pos[0],pos[1],pos[2]},b[kDmgWeapon]};
    return Finite(out.pos);
}
// Per pool slot: the last HP seen for the same actor (pointer, id, type, max HP). A drop between two consecutive
// observations with the sword's damage record (kSwordWeapon) is a hit; a drop by anything else, a new actor in the
// slot, or a slot not seen on the last scan, only sets the baseline.
class HitWatch {
    struct Slot {uint32_t pointer=0;uint8_t id=0,type=0;int16_t hp=0,hp_max=0;bool valid=false;};
    Slot s_[256]{};uint32_t burst_[kMaxBursts]{};unsigned bursts_=0;uint64_t move_=0;
public:
    void Reset(){for(auto& s:s_)s.valid=false;}
    // The move whose hits these are: a new one starts with an empty burst list.
    void Move(uint64_t lease){if(lease!=move_){move_=lease;bursts_=0;}}
    uint64_t CurrentMove() const {return move_;}
    void Forget(unsigned index){if(index<256)s_[index].valid=false;}
    bool Observe(unsigned index,const Body& b){
        if(index>=256)return false;
        Slot& s=s_[index];
        const bool same=s.valid&&s.pointer==b.pointer&&s.id==b.id&&s.type==b.type&&s.hp_max==b.hp_max;
        const bool dropped=same&&b.hp<s.hp&&b.weapon==kSwordWeapon;
        s={b.pointer,b.id,b.type,b.hp,b.hp_max,true};
        return dropped;
    }
    // true the first time this actor bursts in the current move (then recorded).
    bool Burst(uint32_t pointer){
        for(unsigned i=0;i<bursts_;++i)if(burst_[i]==pointer)return false;
        if(bursts_<kMaxBursts)burst_[bursts_++]=pointer;
        return true;
    }
};
// The point of segment [a,b] nearest to segment [c,d] (Ericson, Real-Time Collision Detection 5.1.9).
inline V3 ClosestOnSegment(const V3& a,const V3& b,const V3& c,const V3& d){
    const V3 d1=Sub(b,a),d2=Sub(d,c),r=Sub(a,c);
    const double aa=Dot(d1,d1),ee=Dot(d2,d2),ff=Dot(d2,r);
    double s=0,t=0;
    auto clamp=[](double x){return x<0?0:x>1?1:x;};
    if(aa<=1e-9&&ee<=1e-9)return a;
    if(aa<=1e-9){return a;}
    const double cc=Dot(d1,r);
    if(ee<=1e-9){s=clamp(-cc/aa);}
    else{
        const double bb=Dot(d1,d2),den=aa*ee-bb*bb;
        s=den>1e-9?clamp((bb*ff-cc*ee)/den):0.0;
        t=(bb*s+ff)/ee;
        if(t<0){t=0;s=clamp(-cc/aa);}else if(t>1){t=1;s=clamp((bb-cc)/aa);}
    }
    return Add(a,Mul(d1,s));
}
// Where the blade met the Ganado: the blade point nearest to its body axis.
// The burst sits on the blade point nearest the body axis; a blade that ends short of the body (a far Great Spin or
// Back Slice target) puts it on the body instead: the axis point at that height, kBodyRadiusMm toward the blade.
constexpr double kBodyRadiusMm=300.0,kShortMm=600.0;
inline V3 HitPoint(const V3& base,const V3& tip,const V3& feet){
    const V3 p=ClosestOnSegment(base,tip,feet,Add(feet,{0,kBodyMm,0}));
    const double y=p.y<feet.y?feet.y:p.y>feet.y+kBodyMm?feet.y+kBodyMm:p.y;
    const double dx=p.x-feet.x,dz=p.z-feet.z,d=std::sqrt(dx*dx+dz*dz);
    if(!(d>kShortMm))return p;
    return {feet.x+dx/d*kBodyRadiusMm,y,feet.z+dz/d*kBodyRadiusMm};
}

// ---- wall contact ---------------------------------------------------------------------------------------------------
// One native nearest-hit test (EatMgr.hitCheck with cross and normal, ztarget_los.h Native::Nearest) per probe segment:
// the contact point, the polygon's world normal and its full attribute word, whose effect type (EatGetEffectType,
// at_sub.cpp) tells the material.
enum class Seg:uint8_t {Clear,Hit,Fault};
constexpr double kWallCooldownMs=120.0;    // one burst per move every 120 ms
constexpr double kGroundNormalY=0.5;       // TP's cBgW_CheckBGround: a polygon whose normal.y >= 0.5 is ground
struct NearHit {V3 cross,normal;uint32_t attr=0;};   // a native nearest hit, as returned
struct Contact {bool hit=false,fault=false;V3 point,normal;uint32_t attr=0;unsigned calls=0;};
// EatGetEffectType (at_sub.cpp): bit 0x800000 -> 1, 0x8000 -> 2, 0x80 -> 4; the room's AtEffInfo index (1 = the core
// metal sparks, 0 = the default dirt / puddle, the others registered per room).
inline int EffectType(uint32_t attr){return ((attr&0x800000u)?1:0)+((attr&0x8000u)?2:0)+((attr&0x80u)?4:0);}
// test(from, to, hit) -> Seg: the native nearest-hit test. Base -> tip first, then the previous tip -> tip (the sweep):
// the first that hits is the contact, one call each. The normal is the polygon's, unit, turned toward that segment's
// start: RE4's line test meets both faces of a polygon, TP's (cBgS_LinChk, front faces only) only the one facing the
// blade, so the normal TP orients its sparks with always faces the blade. A degenerate normal: toward the start.
// TP skips the sparks when the line met the ground (cBgW_CheckBGround(normal.y)): normal = the contact's, facing the blade.
inline bool Ground(const V3& normal){return normal.y>=kGroundNormalY;}
// A ground hit on the first segment does not end the search: the sweep may still meet a wall (a blade dipping into the
// floor beside one). The ground contact is returned only when no segment met anything else.
template<class Test> Contact FindContact(Test&& test,const V3& base,const V3& tip,const V3* prev_tip){
    Contact c,ground;
    if(!Finite(base)||!Finite(tip)||(prev_tip&&!Finite(*prev_tip)))return c;
    const V3* from[2]={&base,prev_tip};
    for(const V3* a:from){
        if(!a)continue;
        NearHit h;++c.calls;
        const Seg s=test(*a,tip,h);
        if(s==Seg::Fault){c.fault=true;return c;}
        if(s!=Seg::Hit||!h.attr)continue;
        if(!Finite(h.cross)){c.fault=true;return c;}
        const V3 back=Unit(Sub(*a,h.cross),Unit(Sub(base,tip),{0,1,0}));
        V3 n=Finite(h.normal)?Unit(h.normal,back):back;
        if(Dot(n,Sub(*a,h.cross))<0)n=Mul(n,-1.0);
        if(Ground(n)){if(!ground.hit){ground.hit=true;ground.attr=h.attr;ground.point=h.cross;ground.normal=n;}continue;}
        c.hit=true;c.attr=h.attr;c.point=h.cross;c.normal=n;
        return c;
    }
    if(ground.hit){const unsigned made=c.calls;c=ground;c.calls=made;}
    return c;
}

// ---- the spin -------------------------------------------------------------------------------------------------------
constexpr double kSpinHeightMm=900.0;   // the sparkle ring's centre above Link's feet (as GreatSparkle's ring)
constexpr unsigned kLeafPoints=4;   // 6 in the first build: with the leaves' 10 each, too many bits (21-oct)
constexpr double kLeafRadiusMm=1100.0;  // the tip's ring when no blade sample is known
// TP's leaf texture fills only part of its 20x125 cm quad; drawn solid at that size the leaves read as huge green
// blades (live 21-oct), so they are drawn at this fraction.
constexpr double kLeafSize=0.35;
// The same for the hit set (live 21-oct): TP's glowLine and hitStar textures are thin bright marks inside their quads;
// 15 solid spindles of the full quad size (HITCR_C: 9 m long) whited out half the screen. Rays and star at this
// fraction, the rock fragments (dark discs, read as black blobs) smaller.
constexpr double kHitMarkSize=0.4,kDebrisSize=0.6;
constexpr uint32_t kLeafCentre=0x8A9A4A,kLeafEdge=0x6B7A3A;   // zl2_kusakiri's olive (the leaf texture's own colour)
inline unsigned RingPoints(const V3& centre,double radius,unsigned n,double phase,V3* out){
    if(!out||!n||!Finite(centre)||!std::isfinite(radius)||!std::isfinite(phase))return 0;
    for(unsigned i=0;i<n;++i){const double a=phase+2*kPi*i/n;out[i]={centre.x+radius*std::sin(a),centre.y,centre.z+radius*std::cos(a)};}
    return n;
}

// ---- log lines (each returns the snprintf length; <= 0 or >= size: not written whole) -------------------------------
inline int FormatWood(char* out,size_t size,const Config& c){
    if(!c.wood_types)return std::snprintf(out,size,"none");
    int n=0;bool first=true;
    for(unsigned t=0;t<kEffectTypes;++t){
        if(!((c.wood_types>>t)&1))continue;
        const int k=std::snprintf(size_t(n)<size?out+n:nullptr,size_t(n)<size?size-size_t(n):0,first?"%u":",%u",t);
        if(k<0)return k;
        n+=k;first=false;
    }
    return n;
}
inline int FormatReady(char* out,size_t size,bool ready,const Config& c,bool wall_pins){
    char wood[32];if(FormatWood(wood,sizeof(wood),c)<0)wood[0]=0;
    return std::snprintf(out,size,"sword_particles ready=%d hit=%d wall=%d spin=%d gain=%.2f amount=%.2f dust=%06X wood_types=%s los=%d "
        "pool=%u tick_hz=30",ready?1:0,c.hit?1:0,c.wall&&wall_pins?1:0,c.spin?1:0,c.gain,c.amount,unsigned(c.dust&0xFFFFFF),wood,
        wall_pins?1:0,kPool);
}
inline int FormatHit(char* out,size_t size,bool critical,uint32_t actor,const V3& at){
    return std::snprintf(out,size,"sword_particles hit kind=%s actor=0x%08X at=%.0f,%.0f,%.0f",critical?"cr":"ok",actor,at.x,at.y,at.z);
}
// A wall contact that threw particles: the polygon's full attribute word, its effect type, the effect, the contact and
// the normal (facing the blade), and the native calls (1: base -> tip, 2: the sweep).
inline int FormatWall(char* out,size_t size,uint32_t attr,bool wood,const V3& at,const V3& normal,unsigned calls){
    return std::snprintf(out,size,"sword_particles wall attr=0x%08X type=%d effect=%s at=%.0f,%.0f,%.0f normal=%.2f,%.2f,%.2f "
        "calls=%u",attr,EffectType(attr),wood?"kikuzu":"hibana",at.x,at.y,at.z,normal.x,normal.y,normal.z,calls);
}
// The first ground contact of a move (no particles, as TP): once per move.
inline int FormatGround(char* out,size_t size,uint32_t attr,const V3& at,const V3& normal){
    return std::snprintf(out,size,"sword_particles ground attr=0x%08X type=%d at=%.0f,%.0f,%.0f normal_y=%.2f",attr,
        EffectType(attr),at.x,at.y,at.z,normal.y);
}
inline int FormatSpin(char* out,size_t size,bool great,unsigned emitted){
    return std::snprintf(out,size,"sword_particles spin great=%d emitted=%u",great?1:0,emitted);
}
inline int FormatStats(char* out,size_t size,unsigned hits,unsigned walls,unsigned spins,unsigned alive,unsigned quads,
                       unsigned reg_fails,unsigned draw_fails){
    return std::snprintf(out,size,"sword_particles hits=%u walls=%u spins=%u alive=%u quads=%u reg_fails=%u draw_fails=%u",
        hits,walls,spins,alive,quads,reg_fails,draw_fails);
}
} // namespace link_runtime::sword_particles
