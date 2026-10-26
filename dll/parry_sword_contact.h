#pragma once
#include <cmath>
#include <cstring>

// Pure geometry only. No process access, frame/attack barrier or parry Decision.
// All points/radii use the same UHD world/BIN units; no implicit unit conversion.
namespace link_runtime::parry::sword {
struct Vec3 {double x,y,z;};
struct Mat34 {double v[3][4];}; // Row-major affine, column-vector action.
inline Vec3 Sub(Vec3 a,Vec3 b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline double Dot(Vec3 a,Vec3 b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
inline double LengthSquared(Vec3 a) {return Dot(a,a);}
inline bool Finite(Vec3 a) {return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
inline Vec3 Transform(const Mat34& m,Vec3 p) {
    return {m.v[0][0]*p.x+m.v[0][1]*p.y+m.v[0][2]*p.z+m.v[0][3],
            m.v[1][0]*p.x+m.v[1][1]*p.y+m.v[1][2]*p.z+m.v[1][3],
            m.v[2][0]*p.x+m.v[2][1]*p.y+m.v[2][2]*p.z+m.v[2][3]};
}
inline Mat34 Multiply(const Mat34& a,const Mat34& b) {
    Mat34 out{};
    for(unsigned i=0;i<3;++i) for(unsigned j=0;j<4;++j) {
        out.v[i][j]=(j==3?a.v[i][3]:0);
        for(unsigned k=0;k<3;++k) out.v[i][j]+=a.v[i][k]*b.v[k][j];
    }
    return out;
}
inline double Determinant(const Mat34& a) {
    return a.v[0][0]*(a.v[1][1]*a.v[2][2]-a.v[1][2]*a.v[2][1])
         - a.v[0][1]*(a.v[1][0]*a.v[2][2]-a.v[1][2]*a.v[2][0])
         + a.v[0][2]*(a.v[1][0]*a.v[2][1]-a.v[1][1]*a.v[2][0]);
}
inline bool Rigid(const Mat34& a) {
    // Numerical acceptance tolerance, not evidence of a native pose epoch.
    constexpr double tolerance=1e-4;
    for(const auto& row:a.v) for(double x:row) if(!std::isfinite(x)) return false;
    for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) {
        double dot=0;for(unsigned k=0;k<3;++k) dot+=a.v[i][k]*a.v[j][k];
        if(!std::isfinite(dot)||std::abs(dot-(i==j?1.:0.))>tolerance) return false;
    }
    return std::abs(Determinant(a)-1.)<=tolerance;
}
inline bool InverseRigid(const Mat34& a,Mat34& out) {
    out={};if(!Rigid(a)) return false;
    // Actual inverse, not transpose: tolerate float32 rotation roundoff without
    // treating it as exact orthogonality at large world translations.
    Mat34 inv{};const double det=Determinant(a);
    for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) {
        unsigned r1=(j+1)%3,r2=(j+2)%3,c1=(i+1)%3,c2=(i+2)%3;
        inv.v[i][j]=(a.v[r1][c1]*a.v[r2][c2]-a.v[r1][c2]*a.v[r2][c1])/det;
    }
    for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) inv.v[i][3]-=inv.v[i][j]*a.v[j][3];
    if(!Rigid(inv)) return false;
    out=inv;return true;
}
struct Pose {Mat34 body0,info,bone10,inverse_bind10;};
inline bool WorldTransform(const Pose& pose,Mat34& out) {
    out={};Mat34 inverse{};
    if(!InverseRigid(pose.body0,inverse)||!Rigid(pose.info)||!Rigid(pose.bone10)||!Rigid(pose.inverse_bind10)) return false;
    Mat34 result=Multiply(Multiply(Multiply(Multiply(pose.body0,pose.info),inverse),pose.bone10),pose.inverse_bind10);
    if(!Rigid(result)) return false;
    out=result;return true;
}
enum class Profile {Unknown,Ordon,MasterSword,CampaignPl00};
inline constexpr char kOrdonSha[]="c6961db4a69de3bb8b64cd62b0335ca75639ad674ac54d10f23c7da09f15eb49";
inline constexpr char kMasterSha[]="47b137283099881fc36d2abcf69506d4fe3adde47f63404167dc1672b0d7424f";
inline constexpr char kCampaignPl00Sha[]="8ccdf6ae45a84de1ecf1c01231ebf784aa9a90ef379b7c65e4dafa4087ab7823";
inline Profile ProfileForBinSha(const char* sha) {
    if(sha&&std::strcmp(sha,kOrdonSha)==0) return Profile::Ordon;
    if(sha&&std::strcmp(sha,kMasterSha)==0) return Profile::MasterSword;
    if(sha&&std::strcmp(sha,kCampaignPl00Sha)==0) return Profile::CampaignPl00;
    return Profile::Unknown;
}
// Caller must establish these from the same identified model resource. A costume
// number or historical digest alone does not establish current asset identity.
struct Binding {
    unsigned parts,palette_records,influences,bone,weight_percent;
    bool all_indices_zero;
    unsigned model_flags,info_flags;
};
struct Blade {Vec3 base{},tip{};};
inline bool BuildBlade(Profile profile,const Pose& pose,const Binding& binding,Blade& out) {
    out={};
    if(binding.parts!=119||binding.palette_records!=1||binding.influences!=1||binding.bone!=10||
       binding.weight_percent!=100||!binding.all_indices_zero||(binding.model_flags&0x4000)||
       (binding.info_flags&2)||!(binding.info_flags&8)) return false;
    Vec3 base{},tip{};
    // Installed pl08/pl09 DAT9 group0, blade material. Base is the mean of
    // unique vertices within 0.03 BIN of minimum Z; tip is a distal vertex.
    if(profile==Profile::Ordon) {
        base={-816.8260269165039,1468.2916717529297,38.710275650024414};
        tip={-816.7288818359375,1468.291748046875,1053.02392578125};
    } else if(profile==Profile::MasterSword) {
        base={-816.8222045898438,1472.2435302734375,82.41160583496094};
        tip={-816.7085571289062,1472.3883056640625,1269.4913330078125};
    } else if(profile==Profile::CampaignPl00) {
        // Campaign pl00 DAT9: collar vertices at z=266.028, distal vertex.
        // Diagnostic axis only; defense uses native frontal timing, not mesh overlap.
        base={-816.8046569824219,1472.3881225585938,266.0279846191406};
        tip={-798.5018920898438,1472.3900146484375,1048.789794921875};
    } else return false;
    Mat34 world{};if(!WorldTransform(pose,world)) return false;
    Blade candidate{Transform(world,base),Transform(world,tip)};
    const double length2=LengthSquared(Sub(candidate.tip,candidate.base));
    if(!Finite(candidate.base)||!Finite(candidate.tip)||!std::isfinite(length2)||length2<=1e-12) return false;
    out=candidate;return true;
}
struct Contact {bool valid=false,intersects=false;double t=0,distance_squared=0;Vec3 closest{};};
// blade_radius is explicit policy (zero gives a segment). This is geometric
// overlap only, not exact mesh collision, temporal sweeping or parry acceptance.
inline Contact SphereSegment(Vec3 center,double radius,Vec3 base,Vec3 tip,double blade_radius) {
    if(!Finite(center)||!Finite(base)||!Finite(tip)||!std::isfinite(radius)||radius<0||
       !std::isfinite(blade_radius)||blade_radius<0) return {};
    const Vec3 axis=Sub(tip,base),offset=Sub(center,base);
    const double length2=LengthSquared(axis),projection=Dot(offset,axis);
    if(!std::isfinite(length2)||length2<=1e-12||!std::isfinite(projection)) return {};
    double t=projection/length2;if(t<0)t=0;if(t>1)t=1;
    const Vec3 closest={base.x+t*axis.x,base.y+t*axis.y,base.z+t*axis.z};
    const double distance2=LengthSquared(Sub(center,closest)),sum=radius+blade_radius,limit=sum*sum;
    if(!Finite(closest)||!std::isfinite(distance2)||!std::isfinite(limit)) return {};
    return {true,distance2<=limit,t,distance2,closest};
}
struct SweepContact {
    bool valid=false,intersects=false;
    double attack_t=0,blade_t=0,distance_squared=0;
    Vec3 attack_point{},blade_point{};
};
inline Vec3 Cross(Vec3 a,Vec3 b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline double ClampUnit(double value) {return value<0?0:(value>1?1:value);}
// Sweep of a sphere center against ONE qualified blade pose. This does not
// reconstruct a moving blade, native collision time, facing or input eligibility.
inline SweepContact SweptSphereBlade(Vec3 previous,Vec3 current,double radius,Vec3 base,Vec3 tip,double blade_radius) {
    if(!Finite(previous)||!Finite(current)||!Finite(base)||!Finite(tip)||
       !std::isfinite(radius)||radius<0||!std::isfinite(blade_radius)||blade_radius<0)return {};
    const Vec3 u=Sub(current,previous),v=Sub(tip,base),gap=Sub(base,previous);
    const double uu=LengthSquared(u),vv=LengthSquared(v),sum=radius+blade_radius,limit=sum*sum;
    if(!std::isfinite(uu)||!std::isfinite(vv)||vv<=1e-12||!std::isfinite(limit)||!Finite(gap))return {};
    SweepContact best{};bool failed=false;
    auto consider=[&](double s,double t) {
        if(!std::isfinite(s)||!std::isfinite(t)){failed=true;return;}
        s=ClampUnit(s);t=ClampUnit(t);
        const Vec3 p={previous.x+s*u.x,previous.y+s*u.y,previous.z+s*u.z};
        const Vec3 q={base.x+t*v.x,base.y+t*v.y,base.z+t*v.z};
        const double d=LengthSquared(Sub(p,q));
        if(!Finite(p)||!Finite(q)||!std::isfinite(d)){failed=true;return;}
        if(!best.valid||d<best.distance_squared)best={true,d<=limit,s,t,d,p,q};
    };
    // Every boundary of the constrained quadratic has an endpoint projection.
    consider(0,Dot(Sub(previous,base),v)/vv);
    consider(1,Dot(Sub(current,base),v)/vv);
    if(uu>0) {
        consider(Dot(gap,u)/uu,0);
        consider(Dot(Sub(tip,previous),u)/uu,1);
        // Cross products avoid subtracting near-equal uu*vv and dot(u,v)^2
        // for nearly parallel segments. Parallel overlap is covered above.
        const Vec3 normal=Cross(u,v);const double nn=LengthSquared(normal);
        if(!std::isfinite(nn))return {};
        if(nn>0) {
            const double s=Dot(Cross(gap,v),normal)/nn,t=Dot(Cross(gap,u),normal)/nn;
            if(!std::isfinite(s)||!std::isfinite(t))return {};
            if(s>=0&&s<=1&&t>=0&&t<=1)consider(s,t);
        }
    }
    return failed?SweepContact{}:best;
}
} // namespace link_runtime::parry::sword
