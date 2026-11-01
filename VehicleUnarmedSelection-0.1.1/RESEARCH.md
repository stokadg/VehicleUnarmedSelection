# Research and implementation record


## Primary sources

- [Zolika1351 IV-SDK](https://github.com/Zolika1351/iv-sdk), pinned commit `dddbd3f54b146bdb3a9852809e48e5cb169af3cd`. Its README supports 1.0.7.0 / 1.0.8.0 EFIGS.
- [CPed.h](https://github.com/Zolika1351/iv-sdk/blob/dddbd3f54b146bdb3a9852809e48e5cb169af3cd/include/CPed.h): `PedWeapons::m_nActiveWeaponSlot` at +0x18, `CPed::m_pWeaponData` at +0x2B0, player flag +0x219, vehicle pointer +0xB40. `CPed::ProcessWeaponSwitch()` resolves 1.0.8.0 RVA 0x597180.
- [CWorld.h](https://github.com/Zolika1351/iv-sdk/blob/dddbd3f54b146bdb3a9852809e48e5cb169af3cd/include/CWorld.h): `Players` RVA 0xD00498 and `PlayerInFocus` RVA 0xB2E0B4.
- [CPlayerInfo.h](https://github.com/Zolika1351/iv-sdk/blob/dddbd3f54b146bdb3a9852809e48e5cb169af3cd/include/CPlayerInfo.h): `m_pPlayerPed` +0x58C, `GetPlayerInfo(uint32_t)` RVA 0x3CD210. Disassembly of the supplied EXE confirms the table lookup; the adjacent player-ped getter uses the same focus/table/ped chain.
- [CPad.h](https://github.com/Zolika1351/iv-sdk/blob/dddbd3f54b146bdb3a9852809e48e5cb169af3cd/include/CPad.h) and [ePadControls.h](https://github.com/Zolika1351/iv-sdk/blob/dddbd3f54b146bdb3a9852809e48e5cb169af3cd/include/ePadControls.h): distinct `INPUT_VEH_NEXT_WEAPON` / `INPUT_VEH_PREV_WEAPON`. No guessed controller-button mapping is needed or used.
- [BaseScripting.h](https://github.com/Zolika1351/iv-sdk/blob/dddbd3f54b146bdb3a9852809e48e5cb169af3cd/include/Scripting/BaseScripting.h): exact native wrappers listed below.
- [IVSDK.cpp](https://github.com/Zolika1351/iv-sdk/blob/dddbd3f54b146bdb3a9852809e48e5cb169af3cd/include/IVSDK.cpp) and [Hooks.h](https://github.com/Zolika1351/iv-sdk/blob/dddbd3f54b146bdb3a9852809e48e5cb169af3cd/include/Hooks.h): importing the normal SDK bootstrap installs nine hooks, including pad processing and script processing. The final implementation omits that bootstrap to avoid shared-hook dependencies.


## Selector findings from local disassembly

Addresses are RVAs added to the loaded executable base. Static disassembly used preferred base 0x400000. The code handles relocation by using the actual module base.



The function at RVA 0x5CC270 takes its weapon-state object in ECX and four stack arguments; it returns with `ret 0x10`. Its mode jump table routes 2–5 to decreasing-slot vehicle selection, 20–23 to increasing-slot selection, 11–15 to vehicle eligibility selection, and 6–9 to another selection path. Modes 2/20 have different weapon eligibility than 3–5/21–23. The existing iteration helpers can produce slot zero, but vehicle filtering discards it. The function already has a legitimate zero-slot commit path at RVA 0x5CC71B.

| Hook RVA | Original bytes | Change |
|---|---|---|
| 0x5CC2D4 | `83 FE 04 75 19` | Accept zero candidate belonging to local player in vehicle; otherwise replay original comparison/branch |
| 0x5CC381 | `83 FE 04 0F 84 91 03 00 00` | Same for opposite cycle direction |
| 0x5CC536 | `83 FD 0F 0F 84 DC 01 00 00` | Preserve current zero through automatic vehicle eligibility selection |
| 0x5CC401 | `83 FD 08 74 20` | Preserve zero specifically for automatic best-gun mode 9; modes 6/7/8 remain unchanged |

Drive-by-related callers at RVAs 0xC56BE and 0xC5E50 can request mode 9 when no weapon is equipped. This motivated the fourth hook. Other explicit scripted selection modes are untouched.

All stubs save/restore EFLAGS and general registers and make no function calls. FPU/SSE state is never touched. The original comparison is replayed on fallback, producing the original flags. Accepted zero follows the existing commit path, so the mod never writes inventory or ammo fields. Counters use `lock inc` with flags subsequently restored. All four source patches fit one code page, which is protected once during startup installation; stubs are allocated RW then made RX. Installation is for ASI-loader startup, not concurrent hot injection. No generic trampoline relocation is attempted.



## Verified native signatures (researched, not invoked by this mod)

```cpp
namespace Scripting {
b8 GET_CURRENT_CHAR_WEAPON(Ped ped, unsigned int* pWeapon);
void SET_CURRENT_CHAR_WEAPON(Ped ped, unsigned int w, b8 unknownTrue);
void GET_CHAR_WEAPON_IN_SLOT(Ped ped, unsigned int slot,
                           unsigned int* pWeapon, unsigned int* pAmmo0,
                           unsigned int* pAmmo1);
void GET_AMMO_IN_CHAR_WEAPON(Ped ped, unsigned int weapon, unsigned int* pAmmo);
b8 IS_CHAR_IN_ANY_CAR(Ped ped);
}
```

`b8` is a global typedef for `bool` in the SDK's Types.h. `GET_CURRENT_CHAR_WEAPON` returns a boolean and writes the weapon through a pointer; it does not return a weapon ID. `WEAPON_UNARMED` and `WEAPON_SLOT_UNARMED` are zero. There is no exposed native here that inserts a new vehicle-cycle option or guarantees the vehicle task will not choose a firearm later. Therefore this implementation changes the existing selector rather than repeatedly calling SET_CURRENT_CHAR_WEAPON.


