#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace vus {
struct Site {
    const char* name;
    uint32_t rva;
    std::vector<uint8_t> expected;
    uint32_t taken, fallthrough;
    uint8_t compare; // cmp esi,4 (FE) or cmp ebp,15 (FD)
    uint8_t immediate, condition;
};
inline std::vector<Site> Sites() {
    return {
        {"cycle-decreasing", 0x5CC2D4, {0x83,0xFE,0x04,0x75,0x19}, 0x5CC2F2,0x5CC2D9,0xFE,4,0x85},
        {"cycle-increasing", 0x5CC381, {0x83,0xFE,0x04,0x0F,0x84,0x91,0x03,0,0}, 0x5CC71B,0x5CC38A,0xFE,4,0x84},
        {"retain-unarmed", 0x5CC536, {0x83,0xFD,0x0F,0x0F,0x84,0xDC,0x01,0,0}, 0x5CC71B,0x5CC53F,0xFD,15,0x84},
        {"retain-on-driveby-task", 0x5CC401, {0x83,0xFD,0x08,0x74,0x20}, 0x5CC426,0x5CC406,0xFD,8,0x84}
    };
}
// Emits no calls: no CRT, native invocation, FPU or SSE state changes on the game thread.
// Entry is inside PedWeapons' selector: ESI=candidate slot, EDI=PedWeapons*,
// CPed identity is resolved from CWorld::Players[PlayerInFocus], since some
// automatic selection callers pass a null fourth argument.
inline std::vector<uint8_t> Stub(const Site& s, uint32_t location, uint32_t game, uint32_t counter) {
    std::vector<uint8_t> b;
    std::vector<size_t> reject;
    auto bytes=[&](std::initializer_list<uint8_t> x){b.insert(b.end(),x);};
    auto word=[&](uint32_t v){for(int i=0;i<4;++i)b.push_back(uint8_t(v>>(8*i)));};
    auto rel=[&](uint32_t target){word(target-(location+uint32_t(b.size())+4));};
    auto fail=[&](uint8_t cc){bytes({0x0F,cc});reject.push_back(b.size());word(0);};
    bytes({0x9C,0x60}); // pushfd / pushad: 36 bytes
    bytes({0x85,0xF6}); fail(0x85); // candidate must be unarmed
    if(s.rva==0x5CC401){bytes({0x83,0xFD,9});fail(0x85);} // only automatic best-gun mode
    bytes({0xA1});word(game+0xB2E0B4); // CWorld::PlayerInFocus
    bytes({0x83,0xF8,31});fail(0x87); // unsigned range, also rejects -1
    bytes({0x8B,0x04,0x85});word(game+0xD00498); // CWorld::Players[index]
    bytes({0x85,0xC0});fail(0x84);
    bytes({0x8B,0x80,0x8C,0x05,0,0}); // CPlayerInfo::m_pPlayerPed
    bytes({0x85,0xC0}); fail(0x84);
    bytes({0x80,0xB8,0x19,0x02,0,0,0}); fail(0x84); // CPed::m_bIsPlayer
    bytes({0xF6,0x80,0x6C,0x02,0,0,4}); fail(0x84); // in-vehicle flag
    bytes({0x83,0xB8,0x40,0x0B,0,0,0}); fail(0x84); // vehicle exists
    bytes({0x8D,0x80,0xB0,0x02,0,0,0x39,0xF8}); fail(0x85); // owner matches EDI
    bytes({0xF0,0xFF,0x05}); word(counter); // lock inc diagnostic counter
    bytes({0x61,0x9D,0xE9}); rel(game+0x5CC71B); // original game's commit path
    const size_t fallback=b.size();
    bytes({0x61,0x9D,0x83,s.compare,s.immediate,0x0F,s.condition}); rel(game+s.taken);
    bytes({0xE9}); rel(game+s.fallthrough);
    for(auto p:reject){auto d=uint32_t(fallback-p-4);std::memcpy(b.data()+p,&d,4);}
    return b;
}
inline std::vector<uint8_t> Jump(uint32_t from,uint32_t to,size_t size) {
    std::vector<uint8_t> b(size,0x90); b[0]=0xE9;
    const uint32_t d=to-from-5; std::memcpy(b.data()+1,&d,4); return b;
}
}
