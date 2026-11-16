# GTA5 2014 dev build — bank money hook

A `dinput8.dll` proxy that sets/adds to your **bank balance** in
`game_win64_bankrelease.exe`, in story mode and in this build's unofficial online
session. It does not touch `GTA52014MPLauncher.exe` / `GTA52014Hooks.dll`.

## Install

Copy these two files next to `game_win64_bankrelease.exe` (i.e. into
`X:\gta5\build\dev_ng`):

- `dinput8.dll`
- `GTA5Money.ini`

The game imports `DINPUT8.dll`; the proxy forwards all six real exports to
System32 and installs its hooks before game code runs.

## Hotkeys (window must be focused)

No F keys (those collide with the launcher/hook). Hold **Ctrl+Shift** and press:

| Keys | Action |
|------|--------|
| `Ctrl+Shift+1` | set **bank balance** to `Amount` |
| `Ctrl+Shift+2` | add `Amount` to **bank balance** |
| `Ctrl+Shift+3` | set **story-mode cash on hand** to `Amount` (ignored in multiplayer) |
| `Ctrl+Shift+4` | set **rank/level** to `Rank.Level` |
| `Ctrl+Shift+0` | reload `GTA5Money.ini` |

The bank stat exists in both story mode and multiplayer models, so the bank
hotkeys are not mode-gated. If the game has not created the stat yet, or
`IsKeyValid` rejects it, the hook logs why and writes nothing. `Ctrl+Shift+3`
keeps the old story-mode cash path.

The bank hotkeys also write **`STAT_CLIENT_EVC_BALANCE`** (virtual cash) with the
same value. Purchases do not look at `BANK_BALANCE`: every shop price check goes
through `NETWORK_CAN_SPEND_MONEY` → `CanSpendGtaDollars`
(`game/script/commands_money.cpp`), which compares the amount with
`MoneyInterface::GetVCBalance() = GetEVCBalance() + GetPVCBalance()` — the s64
profile stats `CLIENT_EVC_BALANCE` / `CLIENT_PVC_BALANCE`. With only the bank
written, the HUD shows the money but the store still prints "insufficient cash"
and the game log says `Total virtual cash is not enough, VC='2155'`. The hook
therefore writes EVC to the same value (add mode accumulates on it), and logs
`virtual cash (model 3): ... -> ...`.

Rank has two separate consumers, so the hook writes both:

- **Display** reads the live network player record (`CNetGamePlayer + 0x21C`,
  `uint16`), which the game only copies in `CNetworkSession::UpdateUserData`, so
  the hook mirrors the value into that record (and the session's transition
  record at `CNetworkSession + 0x3A468`) at hotkey time. That is what the player
  list/card shows.
- **Shop and unlock checks are script code.** They resolve the level through
  `STAT_GET_INT` on `MP0_CHAR_RANK_FM`/`MP0_CHAR_XP_FM`, and freemode computes
  the rank it uses from XP. Writing only `CHAR_RANK_FM` therefore shows rank 120
  while the shop still sees rank 1 and asks for level 5.

So `Ctrl+Shift+4` writes the level **and** `CHAR_XP_FM` (retail rank-120 RP =
1,584,350, configurable), through the engine stats **and** through the script
stat commands (`CommandStatSetInt`) that the game's own scripts use. It also
logs what the scripts actually see on the following line
(`script view: rank ok=1 value=120, xp ok=1 value=1584350, ...`), which is the
quickest way to tell whether the game accepted it.

`Rank.AutoApply=1` keeps the level applied: `maintransition` resets
`MP0_CHAR_RANK_FM` to 1 every time a multiplayer session starts, so the hook
re-applies the configured level in multiplayer and follows the game if the
scripts themselves settle on a higher rank for the XP that was written.

## Property unlock (Simeon)

Dynasty 8 refuses to sell property until the freemode unlock bit for custom car
garages is set and shows "purchasing properties will be unlocked after you get a
call from Simeon" (or "sufficient rank", which is misleading — the purchase code
only checks `IS_FM_TYPE_UNLOCKED(FMMC_TYPE_CUSTOM_CAR_GARAGE)`; that activity's
rank requirement is 3).

Freemode builds its FM activity unlock bits in `SET_FM_UNLOCKS_BIT_SET`, which
bails and clears every bit while `MP<slot>_FM_INTRO_MISS_DONE` is false (its debug
script prints `SET_FM_UNLOCKS_BIT_SET called but intro miss not done`). For the
garage bit specifically, `UNLOCK_FM_ACTIVITY` additionally requires
`FM_TRIGTUT_DONE` (or the player chose to skip tutorials) and
`FM_CUT_DONE` bit 0 — `biFmCut_IntroToGarages`, the Simeon garage cutscene — and
prints `Trying to unlock custom car garage but Simeon cutscene not done` without
them.

`Unlock.IntroDone=1` makes the hook set `FM_INTRO_MISS_DONE`,
`FM_INTRO_CUT_DONE`, `FM_RACETUT_DONE`, `FM_TRIGTUT_DONE` and bit 0 of the
`FM_CUT_DONE` bitset through `CommandStatSetInt`/`CommandStatSetBool` on the game
thread, re-checking every 2 s in multiplayer. Freemode reads `FM_CUT_DONE` from a
per-slot cache filled at session start, so the unlock bits are rebuilt at the
next transition into freemode; if the website still shows the Simeon message,
leave and rejoin once (or restart) after the log shows `unlock: ...` with
`garage-cut ... value=1` and `trigger-tut ... value=1`.

## Online caveat

The bank balance and rank are server-applied stats. In `MoneyInterface`, the
session host pushes the authoritative balance with `ApplyVirtualCashBalance`, and
`SyncAndValidateBalances` / `MakeSureClientCashMatches` can overwrite a local
edit later. In this build's unofficial (non-Rockstar) session the host is your own
launcher, so the value may stick — but a host-side re-sync can still revert it.
The log prints the immediate read-back after each write so you can see what stuck.

## Config — `GTA5Money.ini`

```ini
[Money]
Amount=10000000

[Rank]
Level=120
XP=1584350
AutoApply=1

[Unlock]
IntroDone=1
```

`Amount` is clamped to 0 .. 2,000,000,000; `Level` to 1 .. 1000 (this build's
`CHAR_RANK_FM` max); `XP` to 0 .. 40,000,000. Press `Ctrl+Shift+0` after editing
(no restart needed).

## Log — `GTA5Money.log`

Append-only, next to the DLL. Each action writes one line, e.g.:

```
[23:10:02.100] injected into exe base 00007FF653680000
[23:10:02.101] hooks: singleplayer=ok (...6D3EDC), multiplayer=ok (...6CEC48)
[23:10:03.402] bank (model 3): 12500 -> 10000000 (wanted 10000000, set)
[23:10:03.402] virtual cash (model 3): 2155 -> 10000000 (wanted 10000000, set)
[23:10:09.877] bank (model 3): 10000000 -> 20000000 (wanted 20000000, add)
[23:10:09.877] virtual cash (model 3): 10000000 -> 20000000 (wanted 20000000, add)
[23:10:15.010] rank (model 3): 12 -> 120 (wanted 120, set)
[23:10:15.010] rank mirror: network player record set to 120
[23:10:17.004] unlock: set intro=1 cut=1 race-tut=1 cutscene-profile=1 garage-cut=1 trigger-tut=1; read back intro ok=1 value=1, cut ok=1 value=1, race-tut ok=1 value=1, cutscene-profile ok=1 value=1, garage-cut ok=1 value=1, trigger-tut ok=1 value=1, cloud slot 0 pending=0 failed=0
```

If the log says `signature check failed` or `host image too small`, the DLL is
running against something other than this build and it deliberately does nothing.

## Uninstall

Delete `dinput8.dll` from the game folder. (`GTA5Money.ini` and `GTA5Money.log`
can go too; nothing else is modified.)

## Build

Requires only a C compiler; the script uses the portable Zig toolchain.

```bat
build.bat
```

Produces `dist\dinput8.dll` and `dist\GTA5Money.ini`.
`test\` holds two throwaway harnesses used during development
(`test_hook.exe` checks the detour mechanics, `test_load.exe` checks safe loading).

## How it works

- Two per-frame HUD functions are hooked on the game thread (the only thread
  allowed to touch the stats manager, which has no locks):
  `CNewHud::UpdateSinglePlayerCash` (`RVA 0x6D3EDC`) and
  `CNewHud::UpdateMultiplayerCash` (`RVA 0x6CEC48`). Each 16-byte prologue is
  `mov`/`push` only, so each trampoline is a raw copy plus `jmp target+16`; each
  detour polls hotkeys, then tail-jumps into its trampoline.
- Bank is `STAT_BANK_BALANCE` (`StatId_static`, 4-byte id at `RVA 0x328C8F0`,
  filled by the game's static initializers). Its **value is an int64**, so it is
  read with `StatsInterface::GetInt64Stat` (`RVA 0x10672A4`) and written with the
  raw 8-byte `StatsInterface::SetStatData(id, &value, 8, 1)` (`RVA 0x10690C0`) —
  the same call `MoneyInterface::ApplyVirtualCashBalance` uses for the bank.
  The id is read lazily at hotkey time, never during DLL load.
- Virtual cash is `STAT_CLIENT_EVC_BALANCE` (`StatId_static` id at
  `RVA 0x328C2A8`) — also an s64 profile stat, written with the same raw
  `SetStatData`. It is what the shop actually spends against
  (`GetVCBalance() = EVC + PVC`), so it gets the same value as the bank. PVC
  (`RVA 0x328B9D8`) is left alone; it only adds to the total.
- Story cash is `STAT_TOTAL_CASH`, a per-character `StatId_char`: the prefix
  (`SP0`/`SP1`/`SP2`) is resolved through `StatId_char::GetStatId`, then
  `StatsInterface::SetStatData(id, value, 3)` is called — the same path the game's
  own money commands use. It is skipped for `MP0`..`MP4` models.
- Rank is `STAT_RANK_FM` (key `CHAR_RANK_FM`), also a per-character `StatId_char`
  holding an int32 at `RVA 0x328BA20`; it is resolved and written the same way.
  The visible level is rendered from the network player record
  (`CNetGamePlayer + 0x21C`, `uint16`), not from the stat, and the game only
  refreshes that record in `CNetworkSession::UpdateUserData`. The hook therefore
  also writes the new value into the local player record (resolved the same way:
  `[CNetwork::sm_PlayerMgr + 0xE8]`) and into `CNetworkSession + 0x3A468`, so the
  number shows up without waiting for a session update.
- XP is `STAT_CHAR_XP_FM` (`StatId_char` at `RVA 0x328BC80`), an int32 written
  with the same `SetStatData(id, value, 3)` call.
- Scripts read stats through `stats_commands::CommandStatGetInt` (hash, out,
  p2) at `RVA 0x127B748` and write them through `CommandStatSetInt` (hash, value,
  network) at `RVA 0x127DF0C`, keyed on `joaat("MP<slot>_<NAME>")` — e.g.
  `MP0_CHAR_RANK_FM`. The rank hotkey uses both, so the script-visible value
  matches the stat and the display. There is no hotkey needed for the probe:
  it is logged on every rank apply (`script view: ...`), together with
  `StatsInterface::CloudFileLoadPending/Failed(0)` (`RVA 0x1066F70`/`0x1066F60`),
  because those flags gate script reads of server-authoritative stats.
- Bool stats go through `CommandStatGetBool` (`RVA 0x127B0B8`) and
  `CommandStatSetBool` (`RVA 0x127D198`) with the same `MP<slot>_<NAME>` hash.
  `Unlock.IntroDone` uses them to set `FM_INTRO_MISS_DONE`, `FM_INTRO_CUT_DONE`,
  `FM_RACETUT_DONE` and `FM_TRIGTUT_DONE`, and `CommandStatGetInt`/`SetInt` for
  bit 0 of the `FM_CUT_DONE` int stat (Simeon garage cutscene). Freemode's
  `SET_FM_UNLOCKS_BIT_SET` only builds the FM activity unlock bits once the intro
  flag is true, and `UNLOCK_FM_ACTIVITY` additionally needs the trigger tutorial
  and garage cutscene bits before it unlocks `FMMC_TYPE_CUSTOM_CAR_GARAGE` —
  the bit Dynasty 8 actually checks.
- Every RVA is signature-checked before patching, and the host image size is
  checked first so the DLL is harmless if another process loads it.
