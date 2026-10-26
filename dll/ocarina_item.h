// dll/ocarina_item.h
#pragma once
#include <cstdint>
namespace link_runtime::ocarina {
// Native item 0x0D ("Krauser's Knife", a key item the campaign never grants) is the
// Ocarina of Time. Evidence: docs/ocarina-item-census-2026-10-11.md ("Item 0x0D");
// every RVA below is pinned in scripts/ocarina_item_pins.py. No hook is installed here:
// both classes are pure decisions over an injected accessor (Task 10 wires them).
constexpr uint16_t kOcarinaItem = 0x0D;
namespace rva {
constexpr uint32_t kItemMgrVa = 0xC70FD4;    // ItemMgr global (thiscall this)
constexpr uint32_t kGetThunk = 0x3F8F;       // cItemMgr::get(id,num): ret 8, al = 1 if created
constexpr uint32_t kGetBody = 0x307A80;      // type 7 is not stacked: a second get() makes a copy
constexpr uint32_t kSearchThunk = 0xE791;    // cItemMgr::search(u16 id): ret 4, cItem* or 0
constexpr uint32_t kSearchBody = 0x305E80;   // alive (flags bit0) and owned by m_char (+0x13)
constexpr uint32_t kUseThunk = 0x519B;       // cItemMgr::use(cItem*): ret 4, al = used
constexpr uint32_t kUseCall = 0x371DE1;      // ItemCommand::move key-list Use: call rel32 -> 0x40519B
constexpr uint32_t kUseSuccess = 0x371DEE;   // al == 1: sub-menu flags, SE 8, state 1
constexpr uint32_t kUseFailure = 0x371EFB;   // al != 1 (jne at 0x371DE8): SE 7, the error buzz
constexpr uint32_t kItemSelVa = 0xC7B500;    // cItem* item_sel pushed as use()'s argument
}
// Host contract (game thread only):
//  - LoadEpoch(): changes on every save/room load (and new game); never repeats within a session.
//  - GameplayValid(): in-game control of Link, with no menu, case, pause, load, door, event or
//    cutscene running, and the current character is Link (search() is per character).
//  - HasItem(id): ItemMgr.search(id) != 0 through thunk 0xE791.
//  - GetItem(id,num): ItemMgr.get(id,num) through thunk 0x3F8F; true when al == 1.
//  - Log(text).
enum class Grant : uint8_t { Waiting, AlreadyOwned, Granted, GetFailed, Done };
// Decides on the first GameplayValid tick after each load whether Link lacks the ocarina, and
// requests at most one native get(0x0D,1) per load epoch: never in menus, loads or events, never a
// second one in the same epoch (also when get() failed), never when search() already finds it.
template<class Host> class OcarinaGrant {
    bool decided=false; uint32_t epoch=0;
public:
    void Reset(){decided=false;epoch=0;}
    Grant Tick(Host& h){
        if(!h.GameplayValid())return Grant::Waiting;
        const uint32_t now=h.LoadEpoch();
        if(decided&&now==epoch)return Grant::Done;
        decided=true;epoch=now;
        if(h.HasItem(kOcarinaItem)){h.Log("ocarina item present");return Grant::AlreadyOwned;}
        if(!h.GetItem(kOcarinaItem,1)){h.Log("ocarina item get failed");return Grant::GetFailed;}
        h.Log("ocarina item granted");return Grant::Granted;
    }
};
// "Usar" on the key-item list. The case offers it for 0x0D: 0x0D (type 7) is listed in the key column
// (makeItemList), ItemCommand::init (0x370850) passes setCommandId (0x36F450) only the column, and the key
// column always gets Use/Combine/Examine; ItemCommand::move maps cursor 0 of that column to Use without
// looking at the item (pins in ocarina_item_pins.py; GC ss_item.cpp:888-950 agrees). No data or EXE change
// is needed for the entry to appear; still the first thing to confirm in game. Task 10 replaces the call at rva::kUseCall with a thiscall hook
// (ecx = ItemMgr, one cItem* argument, ret 4). The hook calls OnUse(item ? item->id : 0xFFFF,
// epoch): when it returns true the hook returns al = 1 without calling the native use(), so the
// caller takes its success branch (SE 8, no SE 7, no flagclear/used_id/consumption); otherwise it
// tail-calls the original target 0x40519B. The case menu fails the session gates, so the toggle is
// latched and taken once by TakeToggle() on the first GameplayValid tick, then OR-ed into
// InputEvents::toggle (the same entry as the O / LB+RB shortcut). Repeated Use calls before that
// tick publish one toggle; a load before that tick drops it, and a press made after a load always
// counts even when a toggle from the previous load is still pending.
// The granted item is a real inventory entry and persists in the save (intended): a save played without
// the mod shows it as "Krauser's Knife".
class OcarinaUse {
    bool pending=false; uint32_t epoch=0;
public:
    void Reset(){pending=false;epoch=0;}
    bool Pending() const {return pending;}
    bool OnUse(uint16_t id,uint32_t load_epoch){
        if(id!=kOcarinaItem)return false;
        if(!pending||epoch!=load_epoch){pending=true;epoch=load_epoch;}   // a press always counts in its own load
        return true;
    }
    bool TakeToggle(bool gameplay_valid,uint32_t load_epoch){
        if(!pending)return false;
        if(load_epoch!=epoch){pending=false;return false;}
        if(!gameplay_valid)return false;
        pending=false;return true;
    }
};
}
