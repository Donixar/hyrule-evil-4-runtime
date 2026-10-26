#pragma once
#include "ztarget.h"
#include <atomic>
#include <string>
#include <cwchar>
#include <cstdlib>
namespace link_runtime::ztarget::pad {
// [ZTarget] ZTargetPadButton (18-oct): R3 (default; Donald plays RE4's Type II = XInput profile B, where LT aims,
// RT fires, LB is the knife and RB Ashley, so the free R3 locks), LT, LB or L3.
// The XInput source of each and the input.ini token the native layer turns it into (docs/dll8-ztarget-reticula-
// mando-los-2026-10-16.md: LT byte > 30 -> GC_B8, RT -> GC_B5, RS 0x80 -> GC_B15; LB 0x100 is GC_B7 (KEY_LB),
// LS 0x40 GC_B16). Nothing here consumes the native input: a button RE4 also binds keeps its native action.
enum class HoldButton:uint8_t {LT,R3,LB,L3};
inline const char* HoldName(HoldButton b){static const char* const n[]={"LT","R3","LB","L3"};return n[unsigned(b)];}
inline const char* HoldToken(HoldButton b){static const char* const t[]={"QKEY_GC_B8","QKEY_GC_B15","QKEY_GC_B7","QKEY_GC_B16"};return t[unsigned(b)];}
// "" or missing -> R3 (the default); LT/ZL, R3/RS, LB/L, L3/LS (any case); anything else -> false (keep the default).
inline bool ParseHold(const wchar_t* v,HoldButton& out){
    out=HoldButton::R3;if(!v||!*v)return true;
    char s[8]{};for(int i=0;i<7&&v[i];++i){if(v[i]>0x7E)return false;s[i]=char(v[i]>='a'&&v[i]<='z'?v[i]-32:v[i]);}
    if(v[0]&&std::wcslen(v)>6)return false;
    const std::string t(s);
    if(t=="LT"||t=="ZL")out=HoldButton::LT;else if(t=="R3"||t=="RS")out=HoldButton::R3;
    else if(t=="LB"||t=="L")out=HoldButton::LB;else if(t=="L3"||t=="LS")out=HoldButton::L3;else return false;
    return true;
}
// XInput buttons word and left trigger byte -> the hold button is down (LT past the native 30 threshold).
inline bool HoldPressed(HoldButton b,uint16_t buttons,uint8_t lt){
    switch(b){case HoldButton::LT:return lt>30;case HoldButton::R3:return (buttons&0x80)!=0;
        case HoldButton::LB:return (buttons&0x100)!=0;case HoldButton::L3:return (buttons&0x40)!=0;}
    return false;
}
// [ZTarget] ZTargetPadMode (18-oct, Donald): toggle (default) = a TAP of the pad button locks and the next tap
// releases; hold = held to lock (16-oct). The keyboard key is always held.
enum class PadMode:uint8_t {Toggle,Hold};
inline const char* PadModeName(PadMode m){return m==PadMode::Toggle?"toggle":"hold";}
inline bool ParseMode(const wchar_t* v,PadMode& out){
    out=PadMode::Toggle;if(!v||!*v)return true;
    std::string t;for(int i=0;v[i]&&i<8;++i){if(v[i]>0x7E)return false;t+=char(v[i]>='A'&&v[i]<='Z'?v[i]+32:v[i]);}
    if(t=="toggle")out=PadMode::Toggle;else if(t=="hold")out=PadMode::Hold;else return false;
    return true;
}
// Toggle latch, game thread, once per Z-target frame. pressed: the gate's hold (already edge-armed and stale-safe).
// A press flips the latch. While latched: the lock it took ending (target lost, out of sight or range: Recenter)
// releases it, as does a hard native release (aiming, firing, a native event: `release`); the knife (Shield Bash,
// Back Slice, slash), a hit taken and the action key (kick) keep it (bug #4, 24-oct): the runtime passes a soft-suspended lock as still `locked`, so the lock comes back after
// the swing. A latch that found nothing to lock for kSearchFrames releases itself (no surprise lock later).
// End(): why the last latch ended, for the zt log (19-oct; was folded into input_or_target_loss).
enum class ToggleEnd:uint8_t {None,TapOff,AutoRelease,LockEnded,Native};
class Toggle {
    bool latched_=false,prev_=false,locked_once_=false;float search_=0;ToggleEnd end_=ToggleEnd::None;
    void End(ToggleEnd why){latched_=false;locked_once_=false;search_=0;end_=why;}
public:
    static constexpr float kSearchFrames=45.f;
    bool Latched() const{return latched_;}
    ToggleEnd LastEnd() const{return end_;}
    void Reset(){latched_=false;locked_once_=false;search_=0;}
    // locked: the controller holds a lock now; release: a hard native release (or focus loss); frames: 60 Hz step.
    bool Step(bool pressed,bool locked,bool release,float frames){
        const bool edge=pressed&&!prev_;prev_=pressed;
        if(edge){if(latched_)End(ToggleEnd::TapOff);else{latched_=true;locked_once_=false;search_=0;end_=ToggleEnd::None;}}
        if(!latched_)return false;
        if(release){End(ToggleEnd::Native);return false;}
        if(locked){locked_once_=true;search_=0;return true;}
        if(locked_once_){End(ToggleEnd::LockEnded);return false;}
        search_+=frames>0.f?frames:0.f;
        if(search_>kSearchFrames)End(ToggleEnd::AutoRelease);
        return latched_;
    }
};
// The lock input of one Z-target frame (19-oct). The keyboard key and the pad are independent sources, OR'ed: no pad
// state (toggle off, its auto-release, hold released, disconnected, stale) can drop a lock the key holds, and the key
// never flips the toggle. toggle_latched: Toggle::Step's result this frame; pad_hold: the Gate's hold (Hold mode).
inline bool PadSource(bool pad_enabled,PadMode mode,bool toggle_latched,bool pad_hold){
    return pad_enabled&&(mode==PadMode::Toggle?toggle_latched:pad_hold);
}
inline bool LockHeld(bool key_down,bool pad_enabled,PadMode mode,bool toggle_latched,bool pad_hold){
    return key_down||PadSource(pad_enabled,mode,toggle_latched,pad_hold);
}
// The zt reason when the lock input goes from held to not held: the source that held it last (the key wins when both
// let go together). nullptr: the input did not change (another cause ended the lock).
inline const char* ReleaseReason(bool key_was,bool key_now,bool pad_was,bool pad_now,PadMode mode,ToggleEnd end){
    if((key_was||pad_was)&&(key_now||pad_now))return nullptr;
    if(key_was&&!key_now)return "key_released";
    if(!pad_was||pad_now)return nullptr;
    if(mode==PadMode::Hold)return "pad_released";
    switch(end){case ToggleEnd::TapOff:return "pad_toggle_off";case ToggleEnd::AutoRelease:return "pad_autorelease";
        case ToggleEnd::LockEnded:return "pad_lock_ended";case ToggleEnd::Native:return "pad_native_release";default:return "pad_lost";}
}
// The button names of the XInput gameplay tokens (docs/dll8-ztarget-reticula-mando-los-2026-10-16.md, input.ini).
inline const char* PadTokenName(const std::string& token){
    static const char* const n[]={"?","DL","DR","DD","DU","RT","RB","LB","LT","A","B","X","Y","Back","Start","R3","L3"};
    if(token.rfind("QKEY_GC_B",0)!=0)return "?";
    const int i=std::atoi(token.c_str()+9);
    return i>=1&&i<=16?n[i]:i>=17&&i<=20?"LS":i>=21&&i<=24?"RS":"?";
}
// "A=LT B=LB C=LB": the first button each XInput gameplay profile binds `action` to (KEY_KAMAE_KNIFE, KEY_KAMAE...).
inline std::string ProfileButtons(const std::string& text,const char* action){
    std::string out;bool gameplay=false;int profile=-1;bool found[3]={};std::string name[3];
    size_t at=0;
    while(at<text.size()){
        const size_t end=text.find('\n',at);std::string line=text.substr(at,end==std::string::npos?end:end-at);
        at=end==std::string::npos?text.size():end+1;
        std::string t;for(char c:line)if(c!=' '&&c!='\t'&&c!='\r')t+=char(c>='a'&&c<='z'?c-32:c);
        if(t.rfind("###",0)==0){
            if(t=="###GAMEPLAY")gameplay=true;else if(t=="###MENUS")gameplay=false;
            profile=gameplay&&t=="###XINPUT_CONTROLLER_A"?0:gameplay&&t=="###XINPUT_CONTROLLER_B"?1:gameplay&&t=="###XINPUT_CONTROLLER_C"?2:-1;
            continue;
        }
        if(profile<0||t.empty()||t[0]==';')continue;
        const auto eq=t.find('=');if(eq==std::string::npos||found[profile])continue;
        if(t.substr(0,eq)==action){found[profile]=true;name[profile]=PadTokenName(t.substr(eq+1));}
    }
    for(int i=0;i<3;++i){if(i)out+=' ';out+=char('A'+i);out+='=';out+=found[i]?name[i]:"-";}
    return out;
}
// Gameplay actions of the three XInput profiles bound to `token` (bit 0 A, 1 B, 2 C) and their names (first two).
struct Uses {unsigned profiles=0;bool readable=false;std::string actions;};
inline Uses BindingUses(const std::string& text,const char* token){
    Uses u;bool gameplay=false;int profile=-1;unsigned seen=0;
    const auto trim=[](std::string s){const auto a=s.find_first_not_of(" \t\r");
        return a==std::string::npos?std::string{}:s.substr(a,s.find_last_not_of(" \t\r")-a+1);};
    size_t at=0;
    while(at<text.size()){
        const size_t end=text.find('\n',at);std::string line=trim(text.substr(at,end==std::string::npos?end:end-at));
        at=end==std::string::npos?text.size():end+1;
        for(char& c:line)if(c>='a'&&c<='z')c=char(c-32);
        if(line.rfind("###",0)==0){
            if(line=="###GAMEPLAY"){gameplay=true;profile=-1;}
            else if(line=="###MENUS"){gameplay=false;profile=-1;}
            else if(gameplay&&line=="###XINPUT_CONTROLLER_A"){profile=0;seen|=1;}
            else if(gameplay&&line=="###XINPUT_CONTROLLER_B"){profile=1;seen|=2;}
            else if(gameplay&&line=="###XINPUT_CONTROLLER_C"){profile=2;seen|=4;}
            else profile=-1;
            continue;
        }
        if(profile<0||line.empty()||line[0]==';'||line[0]=='#')continue;
        const auto comment=line.find_first_of(";#");if(comment!=std::string::npos)line.resize(comment);
        const auto eq=line.find('=');if(eq==std::string::npos)continue;
        if(trim(line.substr(eq+1))!=token)continue;
        u.profiles|=1u<<profile;
        const std::string action=trim(line.substr(0,eq))+"("+char('A'+profile)+")";
        if(u.actions.find(action)==std::string::npos){if(!u.actions.empty())u.actions+=",";u.actions+=action;}
    }
    u.readable=seen==7;return u;
}
inline bool BindingsSafe(const std::string& text){
    bool gameplay=false,pad=false;unsigned profiles=0;size_t at=0;
    const auto trim=[](std::string s){const auto a=s.find_first_not_of(" \t\r");
        return a==std::string::npos?std::string{}:s.substr(a,s.find_last_not_of(" \t\r")-a+1);};
    while(at<text.size()){
        const size_t end=text.find('\n',at);std::string line=trim(text.substr(at,end==std::string::npos?end:end-at));
        at=end==std::string::npos?text.size():end+1;
        for(char& c:line)if(c>='a'&&c<='z')c=char(c-32);
        if(line.rfind("###",0)==0){
            if(line=="###GAMEPLAY"){gameplay=true;pad=false;}
            else if(line=="###MENUS"){gameplay=false;pad=false;}
            else{
                pad=gameplay&&line.rfind("###XINPUT_CONTROLLER_",0)==0;
                if(pad){if(line=="###XINPUT_CONTROLLER_A")profiles|=1;
                    else if(line=="###XINPUT_CONTROLLER_B")profiles|=2;
                    else if(line=="###XINPUT_CONTROLLER_C")profiles|=4;else return false;}
            }continue;
        }
        if(!pad||line.empty()||line[0]==';'||line[0]=='#')continue;
        const auto comment=line.find_first_of(";#");if(comment!=std::string::npos)line.resize(comment);
        const auto eq=line.find('=');if(eq==std::string::npos)continue;
        if(trim(line.substr(eq+1))=="QKEY_GC_B15")return false;
    }
    return profiles==7;
}
struct State {bool hold=false;int stick=0;};
class Channel {
    // Publish has one owner: the existing Ocarina XInput worker. Consumers only load.
    std::atomic<uint64_t> packet_{0};bool armed_=false;
public:
    void Publish(bool connected,bool focused,uint16_t buttons,int16_t rx,uint32_t now){PublishHold(connected,focused,(buttons&0x80)!=0,rx,now);}
    // pressed: the configured hold button (HoldPressed), already decided by the worker.
    void PublishHold(bool connected,bool focused,bool pressed,int16_t rx,uint32_t now){
        const bool available=connected&&focused;
        if(!available)armed_=false;else if(!pressed)armed_=true;
        const uint32_t bits=(available?1U:0U)|((available&&armed_&&pressed)?2U:0U)|(uint32_t(uint16_t(rx))<<16);
        packet_.store((uint64_t(now)<<32)|bits,std::memory_order_release);
    }
    uint64_t Load()const{return packet_.load(std::memory_order_acquire);}
};
class Gate {
    bool known_=false,armed_=false;uint32_t last_=0;
public:
    // Game-thread state. A stopped worker/focus loss cannot leave RS held indefinitely.
    State Read(uint64_t packet,uint32_t now,bool focused){
        if(!known_||uint32_t(now-last_)>100)armed_=false;
        known_=true;last_=now;
        if(!focused||!(packet&1)||uint32_t(now-uint32_t(packet>>32))>100){armed_=false;return {};}
        if(!(packet&2)){armed_=true;return {};}
        if(!armed_)return {};
        return {true,int(int16_t(uint16_t(packet>>16)))};
    }
};
}
