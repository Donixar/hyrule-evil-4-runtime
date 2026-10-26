#pragma once
// Pure pose math. Matrices use UHD's row-major 3x4 convention, distances in mm.
// Evidence and integration: docs/zelda-cast-pose-2026-10-23.md.
#include <cmath>
#include <cstdint>
namespace link_runtime::zelda_cast {
constexpr double kDurationMs = 600;
constexpr unsigned kChain = 11;
inline constexpr int kParents[kChain] = {-1,0,1,2,3,2,5,5,7,8,9};
inline double Weight(double age_ms) {
    if (!std::isfinite(age_ms) || age_ms <= 0 || age_ms >= 600) return 0;
    if (age_ms < 120) { const double t = 1 - age_ms / 120; return 1 - t*t*t; }
    if (age_ms <= 450) return 1;
    const double t = (age_ms - 450) / 150;
    return 1 - t*t*(3 - 2*t);
}
class Timeline {
    double start_ = 0;
    bool armed_ = false;
public:
    bool Active(double now) const {
        return armed_ && std::isfinite(now) && now >= start_ && now-start_ < kDurationMs;
    }
    bool Begin(double now) {
        if (!std::isfinite(now) || now < 0 || Active(now)) return false;
        start_ = now; armed_ = true; return true;
    }
    double Age(double now) const { return now-start_; }
    void Cancel() { armed_ = false; }
};
// Live probe reply 01: only idle/walking, never turn/run/action/damage/event.
inline bool CanCast(unsigned character, bool in_play, unsigned r0, unsigned r1, int fixed_part) {
    return character == 1 && in_play && r0 == 0 && r1 <= 1 && fixed_part == 0;
}
struct Vec { float x=0, y=0, z=0; };
struct Mat { float v[12]{}; };
inline Mat Identity() { return {{1,0,0,0, 0,1,0,0, 0,0,1,0}}; }
inline Vec Position(const Mat& a) { return {a.v[3],a.v[7],a.v[11]}; }
inline Vec Direction(const Mat& a, Vec p) {
    return {a.v[0]*p.x+a.v[1]*p.y+a.v[2]*p.z,
            a.v[4]*p.x+a.v[5]*p.y+a.v[6]*p.z,
            a.v[8]*p.x+a.v[9]*p.y+a.v[10]*p.z};
}
inline Mat Multiply(const Mat& a, const Mat& b) {
    Mat c;
    for (unsigned r=0;r<3;++r) for (unsigned k=0;k<4;++k) {
        double v = k==3 ? a.v[r*4+3] : 0;
        for (unsigned j=0;j<3;++j) v += double(a.v[r*4+j])*b.v[j*4+k];
        c.v[r*4+k]=float(v);
    }
    return c;
}
// Native RotMatrix is Rz * Ry * Rx. Target arm -X maps to model +Z.
inline Mat Euler(double x, double y, double z, Vec p={}) {
    const double cx=std::cos(x),sx=std::sin(x),cy=std::cos(y),sy=std::sin(y),cz=std::cos(z),sz=std::sin(z);
    return {{float(cz*cy),float(cz*sy*sx-sz*cx),float(cz*sy*cx+sz*sx),p.x,
             float(sz*cy),float(sz*sy*sx+cz*cx),float(sz*sy*cx-cz*sx),p.y,
             float(-sy),float(cy*sx),float(cy*cx),p.z}};
}
struct RigidMetrics { bool finite=true; double dot_error=0,det=1; };
inline bool Rigid(const Mat& a,RigidMetrics* metrics=nullptr) {
    RigidMetrics result;
    for (float v:a.v) if (!std::isfinite(v)) result.finite=false;
    for (unsigned i=0;i<3;++i) for (unsigned j=0;j<3;++j) {
        double dot=0;
        for (unsigned k=0;k<3;++k) dot+=double(a.v[4*k+i])*a.v[4*k+j];
        const double error=std::abs(dot-(i==j?1.:0.));
        if(error>result.dot_error) result.dot_error=error;
    }
    result.det=a.v[0]*(double(a.v[5])*a.v[10]-double(a.v[6])*a.v[9])-
        a.v[1]*(double(a.v[4])*a.v[10]-double(a.v[6])*a.v[8])+
        a.v[2]*(double(a.v[4])*a.v[9]-double(a.v[5])*a.v[8]);
    if(metrics) *metrics=result;
    return result.finite && result.dot_error<=.01 && result.det>.99 && result.det<1.01;
}
inline Mat InverseRotation(const Mat& a) {
    Mat b=Identity();
    for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) b.v[4*i+j]=a.v[4*j+i];
    return b;
}
struct Quat { double x=0,y=0,z=0,w=1; };
inline Quat Normalize(Quat q) {
    const double n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    return {q.x/n,q.y/n,q.z/n,q.w/n};
}
inline Quat Rotation(const Mat& m) {
    const auto& a=m.v; Quat q;
    const double trace=double(a[0])+a[5]+a[10];
    if(trace>0) {
        const double s=2*std::sqrt(trace+1);
        q={(a[9]-a[6])/s,(a[2]-a[8])/s,(a[4]-a[1])/s,s/4};
    } else if(a[0]>a[5] && a[0]>a[10]) {
        const double s=2*std::sqrt(1.+a[0]-a[5]-a[10]);
        q={s/4,(a[1]+a[4])/s,(a[2]+a[8])/s,(a[9]-a[6])/s};
    } else if(a[5]>a[10]) {
        const double s=2*std::sqrt(1.+a[5]-a[0]-a[10]);
        q={(a[1]+a[4])/s,s/4,(a[6]+a[9])/s,(a[2]-a[8])/s};
    } else {
        const double s=2*std::sqrt(1.+a[10]-a[0]-a[5]);
        q={(a[2]+a[8])/s,(a[6]+a[9])/s,s/4,(a[4]-a[1])/s};
    }
    return Normalize(q);
}
inline Quat Slerp(Quat a, Quat b, double t) {
    double dot=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if(dot<0) { b={-b.x,-b.y,-b.z,-b.w}; dot=-dot; }
    double u=1-t,v=t;
    if(dot<.9995) {
        const double angle=std::acos(dot),s=std::sin(angle);
        u=std::sin((1-t)*angle)/s; v=std::sin(t*angle)/s;
    }
    return Normalize({u*a.x+v*b.x,u*a.y+v*b.y,u*a.z+v*b.z,u*a.w+v*b.w});
}
inline Mat Matrix(Quat q, Vec p) {
    const double x=q.x,y=q.y,z=q.z,w=q.w;
    return {{float(1-2*(y*y+z*z)),float(2*(x*y-z*w)),float(2*(x*z+y*w)),p.x,
             float(2*(x*y+z*w)),float(1-2*(x*x+z*z)),float(2*(y*z-x*w)),p.y,
             float(2*(x*z-y*w)),float(2*(y*z+x*w)),float(1-2*(x*x+y*y)),p.z}};
}
inline bool Driven(unsigned bone) { return bone==5 || bone==7 || bone==8 || bone==9 || bone==10; }
// Model-space targets compensate the animated torso. Forearm includes double joint 9.
inline Mat Target(unsigned bone) {
    constexpr double half_pi=1.57079632679489661923;
    if(bone==5) return Identity();
    const Mat arm=Euler(0,half_pi,0);
    return bone==10 ? Multiply(arm,Euler(0,0,-half_pi)) : arm;
}
// Pending native addRot is consumed by partsWorldCalc after animation: world * Rx *
// Rz, then Ry * world, with the pre-correction translation restored. Include it in
// parent prediction and undo it in driven targets; the native call still owns it.
struct PoseDiagnostic { const char* detail="arguments"; int bone=-1; RigidMetrics matrix; };
inline bool BuildPose(const Mat& model, const Mat* native_local, double weight, Mat* local, Mat* world,
                      const Vec* add_rot=nullptr,PoseDiagnostic* diagnostic=nullptr) {
    PoseDiagnostic problem;
    auto fail=[&](const char* detail,int bone) {
        problem.detail=detail; problem.bone=bone;
        if(diagnostic) *diagnostic=problem;
        return false;
    };
    if(!native_local || !local || !world) return fail("arguments",-1);
    if(!std::isfinite(weight) || weight<0 || weight>1) return fail("weight",-1);
    if(!Rigid(model,&problem.matrix)) return fail("model",-1);
    for(unsigned i=0;i<kChain;++i) if(!Rigid(native_local[i],&problem.matrix)) return fail("local",int(i));
    if(add_rot) for(unsigned i=0;i<kChain;++i) {
        if(!std::isfinite(add_rot[i].x) || !std::isfinite(add_rot[i].y) || !std::isfinite(add_rot[i].z)) {
            problem.matrix.finite=false; return fail("addrot",int(i));
        }
    }
    for(unsigned i=0;i<kChain;++i) {
        const Mat& parent=kParents[i]<0 ? model : world[kParents[i]];
        const Vec rot=add_rot ? add_rot[i] : Vec{};
        const Mat pre=Euler(0,rot.y,0),post=Multiply(Euler(rot.x,0,0),Euler(0,0,rot.z));
        local[i]=native_local[i];
        if(weight>0 && Driven(i)) {
            const Mat desired=Multiply(Multiply(InverseRotation(Multiply(pre,parent)),Multiply(model,Target(i))),InverseRotation(post));
            local[i]=Matrix(Slerp(Rotation(local[i]),Rotation(desired),weight),Position(local[i]));
        }
        world[i]=Multiply(parent,local[i]);
        if(rot.x!=0 || rot.y!=0 || rot.z!=0) {
            const Vec pos=Position(world[i]);
            world[i]=Multiply(pre,Multiply(world[i],post));
            world[i].v[3]=pos.x; world[i].v[7]=pos.y; world[i].v[11]=pos.z;
        }
    }
    return true;
}
}
