#pragma once
// bughunt-01 (22-oct): where a mod node enters the world table 17 (trail, charge glow, particles, Z-target arrow).
// AddOtWorldPos (UHD 0x2EDBD0, whole body in kRuntimePins; GC decomp game/trans_ot.cpp) returns 0xFFFF in exactly two
// cases: GetPrimBuff (0x2E7440) has no frame buffer (pG+0x4F18 null) or no room for its 0x14-byte entry, or the node's
// depth z = dot(-cam.Look, pos - cam.Campos) is below zlimit or NaN. cam = pG+0x74: Campos at +0xA4 (read by
// AddOtWorldPos itself), Look at +0xD0 (read by CameraGetLookVecInverse, thunk 0xA326 -> 0x19F9E0, which negates it;
// re4-decomp camera.h: Look = normalised campos - target, so -Look is the view direction). Nothing else refuses: no
// frustum test, no game-state test, a depth past the last bucket clamps to bucket 0x3FF.
// The drawers registered at their geometry's centroid with zlimit 200 (the native world ESP limit, 20 cm). A ribbon
// or a particle cloud whose centroid fell within 20 cm of the eye plane or behind it lost the whole node for that frame
// even while part of it was on screen (a spin's arc swept behind Link: its ends at the sides in view, its centroid
// between Link and the lens). In the 21/22-oct logs every trail refusal burst came from a spin whose sword hit a wall
// (the camera pushed in close): 7 of 19 wall spins, 0 of 26 others, up to 91 of ~290 frames of one Great Spin.
// Place picks the point from the native's own camera fields, read on the game thread just before the call:
// - Centroid: the centroid's depth is at least zlimit + kMargin (as before);
// - Clamped: the centroid is nearer but some vertex reaches zlimit: the centroid moved along the view direction to
//   depth zlimit + kMargin (bucket 10: drawn after the farther world nodes, the nearest place a native world ESP takes);
// - Behind (every vertex at or behind the eye plane) or Near (every vertex nearer than zlimit, the 20 cm the native
//   world ESPs keep clear): nothing is registered and it is not a failure.
// Without the camera (its pins or memory) the centroid is used as before and AddOtWorldPos decides.
#include "native_pins.h"
#include "sword_trail_ot.h"
#include <cmath>
#include <cstdint>

namespace link_runtime::sword_trail::ot::place {
constexpr uint32_t kCameraCampos=0x74+0xA4,kCameraLook=0x74+0xD0;   // pG offsets (pG+0x118, pG+0x144)
constexpr double kMargin=4.0;   // depth units (mm) past zlimit; float rounding of the point stays far below it
// CameraGetLookVecInverse and its thunk (bytes from kEvidencePins: camera_look_inverse, thunk_a326). AddOtWorldPos's
// call into the thunk and its Campos reads are covered by kRuntimePins.
inline constexpr native_pins::Pin kPins[] = {
    {0x19F9E0, "558bec8b4508d980d00000008b4d0cd9e0d919d980d4000000d9e0d95904d980d8000000d9e0d959085dc3"},
    {0xA326, "e9b5561900"},
};
template<class Mem> bool Matches(Mem& m,uint32_t base){return native_pins::All(m,base,kPins);}

// The camera as AddOtWorldPos sees it: the eye (Campos) and the view direction (-Look).
struct Eye {double px=0,py=0,pz=0,fx=0,fy=0,fz=0,f2=0;};
inline bool FiniteF(const float* v,unsigned n){for(unsigned i=0;i<n;++i)if(!std::isfinite(v[i]))return false;return true;}
inline bool EyeFrom(const float campos[3],const float look[3],Eye& out){
    if(!FiniteF(campos,3)||!FiniteF(look,3))return false;
    Eye e;e.px=campos[0];e.py=campos[1];e.pz=campos[2];e.fx=-double(look[0]);e.fy=-double(look[1]);e.fz=-double(look[2]);
    e.f2=e.fx*e.fx+e.fy*e.fy+e.fz*e.fz;
    if(!(e.f2>1e-6)||!std::isfinite(e.f2))return false;
    out=e;return true;
}
// global: pG (the value of the global slot). false: unreadable or degenerate (no Look yet).
template<class Mem> bool ReadEye(Mem& m,uint32_t global,Eye& out){
    float campos[3]{},look[3]{};
    return global&&m.Read(global+kCameraCampos,campos,sizeof(campos))&&m.Read(global+kCameraLook,look,sizeof(look))&&
        EyeFrom(campos,look,out);
}
inline double Depth(const Eye& e,double x,double y,double z){return e.fx*(x-e.px)+e.fy*(y-e.py)+e.fz*(z-e.pz);}
inline double Depth(const Eye& e,const Vec3& p){return Depth(e,p.x,p.y,p.z);}

enum class Where:uint8_t {Centroid,Clamped,Behind,Near};
inline const char* WhereName(Where w){
    return w==Where::Centroid?"centroid":w==Where::Clamped?"clamped":w==Where::Behind?"behind":"near";
}
// `vertices` points (v[i].p) whose centroid is `centre`; `at` gets the registration point (Centroid, Clamped). The
// clamped point is checked again after its float rounding; should it still fall short of zlimit, the centroid goes
// (AddOtWorldPos decides and a refusal stays a failure).
template<class V> Where Place(const Eye& e,const V* v,int vertices,const Vec3& centre,float zlimit,Vec3& at){
    const double target=double(zlimit)+kMargin;
    const double zc=Depth(e,centre);
    at=centre;
    if(zc>=target)return Where::Centroid;
    double far_depth=-HUGE_VAL;
    for(int i=0;i<vertices;++i){const double z=Depth(e,v[i].p);if(z>far_depth)far_depth=z;}
    if(!(far_depth>0))return Where::Behind;
    if(far_depth<double(zlimit))return Where::Near;
    const double k=(target-zc)/e.f2;
    const Vec3 moved{float(centre.x+e.fx*k),float(centre.y+e.fy*k),float(centre.z+e.fz*k)};
    if(!(Depth(e,moved)>=double(zlimit)))return Where::Centroid;
    at=moved;return Where::Clamped;
}
} // namespace link_runtime::sword_trail::ot::place
