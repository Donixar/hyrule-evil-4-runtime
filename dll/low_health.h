#pragma once
#include "link_runtime_health.h"

namespace link_runtime::low_health {
// STA_INTO_SHOP stays latched during r11c combat. LowHealth alone may ignore
// that bit. A real shop/case remains blocked by SSCRN_REQUEST, SUSPEND, STOP
// and the other shared gates; SPF_KEY is deliberately kept for this alert.
// Filter only the snapshot supplied to ReadHealth: never change native flags,
// duplicate its masks, or relax the policy of its other consumers.
template <class Memory>
class AlertMemory {
    Memory& memory_;
    uint32_t global_slot_ = 0, global_ = 0;
public:
    AlertMemory(Memory& memory, uint32_t global_slot) : memory_(memory), global_slot_(global_slot) {}
    bool Read32(uint32_t address, uint32_t& value) {
        if (!memory_.Read32(address, value)) return false;
        if (address == global_slot_) global_ = value;
        else if (IsPointer(global_) && address == global_ + 0x5024) value &= ~0x00040000U;
        return true;
    }
};

template <class Memory>
bool ReadHealth(Memory& memory, uint32_t player_slot, uint32_t global_slot,
                bool shop_flag_fix, HealthState& result) {
    if (!shop_flag_fix) return link_runtime::ReadHealth(memory, player_slot, global_slot, result);
    AlertMemory<Memory> view(memory, global_slot);
    return link_runtime::ReadHealth(view, player_slot, global_slot, result);
}
}  // namespace link_runtime::low_health
