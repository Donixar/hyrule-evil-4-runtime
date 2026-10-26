#pragma once
#include "shield_runtime.h"
#include "runtime_paths.h"
#include <cwchar>

namespace link_runtime::rear_shield {
// PCM from TP Z2Sound.baa BSC group4 indices5/11, variant0. See the reproducible
// candidate manifest, scripts/rear_shield_audio.py. Loaded on the worker only.
template<class Voice> class Voices {
    Voice wood_,metal_;
public:
    bool Load(const wchar_t* directory){
        wchar_t wood[MAX_PATH]{},metal[MAX_PATH]{};
        return paths::Format(wood,L"%slink_rear_shield_wood.wav",directory)>0&&
            paths::Format(metal,L"%slink_rear_shield_metal.wav",directory)>0&&
            wood_.Load(wood,60,"c758da4574dfa7b5a08b16d1cce23a14c7d4eb8b83f6122cb6864a9d3de62b92")&&
            metal_.Load(metal,60,"7d620ab3350a14be9f8ed5f72769d6383bc89ac199388860ea4880217624ea69");
    }
    bool Play(Material material){return material==Material::Wood?wood_.Play():material==Material::Metal?metal_.Play():false;}
};
template<class Memory> bool CurrentSound(Memory& memory,const shield::runtime::RearSound& sound,uint32_t now){
    using namespace shield;
    if(!runtime::Enabled()||!runtime::rear_active||now-sound.tick>250)return false;
    Frame pose;uint32_t frame=0,room_before=0,room_after=0;
    return Word(memory,sound.global+0x4FAC,room_before)&&room_before==sound.room&&
        ReadPose(memory,runtime::access,pose)&&Gameplay(pose)&&pose.player==sound.player&&pose.global==sound.global&&
        pose.character==sound.character&&Word(memory,pose.global+0x530C,frame)&&frame>=sound.frame&&frame-sound.frame<=16&&
        Word(memory,pose.global+0x4FAC,room_after)&&room_after==sound.room;
}
}
