/*
 * GTA5 2014 dev-build money hook, shipped as a dinput8.dll proxy.
 *
 * The game imports DINPUT8.dll, so dropping this next to game_win64_bankrelease.exe
 * gets us loaded before game code runs. We forward all six real dinput8 exports to
 * System32 and install inline hooks on CNewHud::UpdateSinglePlayerCash and
 * CNewHud::UpdateMultiplayerCash, both called once per frame on the game thread --
 * the only thread allowed to touch the stats manager (it has no locks).
 *
 * Hotkeys: Ctrl+Shift+0 reload ini, Ctrl+Shift+1 set bank,
 * Ctrl+Shift+2 add to bank, Ctrl+Shift+3 set story cash, Ctrl+Shift+4 set rank.
 */

#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t u32;
typedef uint64_t u64;

/* RVAs into game_win64_bankrelease.exe (image base 0x140000000). */
#define RVA_GET_STAT_ID        0x0B592A0u   /* StatId_char::GetStatId(const void* self, void* out32) */
#define RVA_GET_INT_STAT       0x10672D0u   /* StatsInterface::GetIntStat(const StatId&) */
#define RVA_GET_I64_STAT       0x10672A4u   /* StatsInterface::GetInt64Stat(const StatId&) */
#define RVA_SET_STAT_DATA_INT  0x1068DD4u   /* StatsInterface::SetStatData(const StatId&, int, u32) */
#define RVA_SET_STAT_DATA_RAW  0x10690C0u   /* StatsInterface::SetStatData(const StatId&, const void*, u32 size, u32 flags) */
#define RVA_IS_KEY_VALID       0x1067F48u   /* StatsInterface::IsKeyValid(const StatId&) */
#define RVA_GET_STATS_PREFIX   0x1067900u   /* StatsInterface::GetStatsModelPrefix() */
#define RVA_UPDATE_SP_CASH     0x06D3EDCu   /* CNewHud::UpdateSinglePlayerCash */
#define RVA_UPDATE_MP_CASH     0x06CEC48u   /* CNewHud::UpdateMultiplayerCash */
#define RVA_STAT_TOTAL_CASH    0x328C288u   /* StatId_char object, 32 bytes */
#define RVA_STAT_BANK_BALANCE  0x328C8F0u   /* StatId_static: 4-byte id; its value is an int64 stat */
#define RVA_STAT_CLIENT_EVC    0x328C2A8u   /* StatId_static: CLIENT_EVC_BALANCE (s64, profile) */
#define RVA_STAT_CLIENT_PVC    0x328B9D8u   /* StatId_static: CLIENT_PVC_BALANCE (s64, profile) */
#define RVA_STAT_RANK_FM       0x328BA20u   /* StatId_char object (CHAR_RANK_FM), int32 rank */
#define RVA_STAT_CHAR_XP_FM    0x328BC80u   /* StatId_char object (CHAR_XP_FM), int32 xp */
#define RVA_NET_PLAYER_MGR     0x28D2228u   /* CNetwork::sm_PlayerMgr, holds CNetworkPlayerMgr* */
#define RVA_NET_SESSION        0x3609E88u   /* CNetwork::sm_NetworkSession, holds CNetworkSession* */
#define RVA_STAT_GET_INT       0x127B748u   /* stats_commands::CommandStatGetInt(int, int&, int) */
#define RVA_STAT_SET_INT       0x127DF0Cu   /* stats_commands::CommandStatSetInt(int, int, bool) */
#define RVA_STAT_GET_BOOL      0x127B0B8u   /* stats_commands::CommandStatGetBool(int, int&, int) */
#define RVA_STAT_SET_BOOL      0x127D198u   /* stats_commands::CommandStatSetBool(int, bool, bool) */
#define RVA_CLOUD_LOAD_PENDING 0x1066F70u   /* StatsInterface::CloudFileLoadPending(int) */
#define RVA_CLOUD_LOAD_FAILED  0x1066F60u   /* StatsInterface::CloudFileLoadFailed(int) */

/* The HUD/player card read the rank from the live network player record, not the
   stat, so the stat has to be mirrored there by hand (UpdateUserData does this
   only on its own schedule). */
#define OFF_PLAYER_MGR_LOCAL   0x0E8u       /* CNetworkPlayerMgr -> local CNetGamePlayer* */
#define OFF_NET_PLAYER_RANK    0x21Cu       /* CNetGamePlayer -> u16 character rank */
#define OFF_SESSION_RANK       0x3A468u     /* CNetworkSession -> u16 transition gamer rank */

static const unsigned char SIG_ENTRY[16] = {
    0x48,0x89,0x5C,0x24,0x10, 0x48,0x89,0x6C,0x24,0x18, 0x48,0x89,0x74,0x24,0x20, 0x57
};
static const unsigned char SIG_MP_ENTRY[16] = {
    0x48,0x89,0x5C,0x24,0x18, 0x55,0x56,0x57,
    0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57
};
static const unsigned char SIG_GET_STAT_ID[10] = {
    0x48,0x89,0x5C,0x24,0x08, 0x57, 0x48,0x83,0xEC,0x20
};
static const unsigned char SIG_GET_INT_STAT[10] = {
    0x48,0x83,0xEC,0x28, 0x48,0x8B,0xD1, 0x48,0x8D,0x0D
};
static const unsigned char SIG_SET_STAT_RAW[10] = {
    0x48,0x83,0xEC,0x38, 0x44,0x89,0x4C,0x24,0x20, 0x45
};
static const unsigned char SIG_IS_KEY_VALID[6] = {
    0x48,0x8B,0xD1, 0x48,0x8D,0x0D
};
static const unsigned char SIG_SET_STAT[19] = {
    0x48,0x89,0x5C,0x24,0x08, 0x89,0x54,0x24,0x10, 0x55,0x56,0x57,
    0x48,0x8B,0xEC, 0x48,0x83,0xEC,0x30
};
static const unsigned char SIG_STAT_GET_INT[7] = {
    0x48,0x8B,0xC4, 0x48,0x89,0x58,0x10
};
static const unsigned char SIG_STAT_SET_INT[13] = {
    0x48,0x89,0x5C,0x24,0x18, 0x89,0x54,0x24,0x10, 0x55,0x56,0x57, 0x41
};
static const unsigned char SIG_STAT_GET_BOOL[16] = {
    0x48,0x8B,0xC4, 0x48,0x89,0x58,0x10, 0x48,0x89,0x68,0x18, 0x48,0x89,0x70,0x20, 0x57
};
static const unsigned char SIG_STAT_SET_BOOL[14] = {
    0x48,0x89,0x5C,0x24,0x18, 0x88,0x54,0x24,0x10, 0x55,0x56,0x57, 0x48,0x8B
};
/* GetStatsModelPrefix / CloudFileLoadPending are `mov reg,[rip+disp32]` where the
   displacement is build-specific, so those four bytes are skipped. */
static int mh_sig_cloud_flag(const BYTE* p)
{
    return p[0] == 0x8B && p[1] == 0x15 && p[6] == 0x0F && p[7] == 0xA3 &&
           p[8] == 0xCA && p[9] == 0x0F && p[10] == 0x92 && p[11] == 0xC0 &&
           p[12] == 0xC3;
}

typedef void (__cdecl *fn_get_stat_id)(const void* self, void* out);
typedef int  (__cdecl *fn_get_int_stat)(const void* statid);
typedef long long (__cdecl *fn_get_i64_stat)(const void* statid);
typedef void (__cdecl *fn_set_stat_data_int)(const void* statid, int value, u32 flags);
typedef unsigned char (__cdecl *fn_set_stat_data_raw)(const void* statid, const void* data,
                                                      u32 size, u32 flags);
typedef int  (__cdecl *fn_is_key_valid)(const void* statid);
typedef u32  (__cdecl *fn_get_prefix)(void);
typedef unsigned char (__cdecl *fn_cloud_flag)(int);
typedef unsigned char (__cdecl *fn_stat_get_int)(int hash, int* out, int p2);
typedef unsigned char (__cdecl *fn_stat_set_int)(int hash, int value, int net);
typedef unsigned char (__cdecl *fn_stat_get_bool)(int hash, int* out, int p2);
typedef unsigned char (__cdecl *fn_stat_set_bool)(int hash, unsigned char value, unsigned char net);

static HMODULE g_hinst;
static BYTE*   g_base;
static void*   g_stat_total_cash;
static void*   g_stat_bank_balance;
static void*   g_stat_evc_balance;
static void*   g_stat_pvc_balance;
static void*   g_stat_rank_fm;
static void*   g_stat_xp_fm;
static volatile LONG g_amount = 10000000;
static volatile LONG g_rank = 120;
static volatile LONG g_xp = 1584350;
static volatile LONG g_rank_auto = 1;
static volatile LONG g_unlock_intro = 1;
static volatile LONG g_rank_mirror;
static volatile LONG g_rank_seen;
static volatile LONG g_last_auto_tick;
static volatile LONG g_last_unlock_tick;
static volatile LONG g_hook_ok;
static volatile LONG g_stat_cmds_ok;
static volatile LONG g_stat_bool_ok;
static wchar_t g_dir[MAX_PATH];
static wchar_t g_ini[MAX_PATH];
static wchar_t g_log[MAX_PATH];

void  mh_poll_keys(void);
void* mh_trampoline_sp;
void* mh_trampoline_mp;

__attribute__((naked)) void mh_detour_sp(void)
{
    __asm__ __volatile__(
        "subq $0x28, %rsp\n\t"
        "call mh_poll_keys\n\t"
        "addq $0x28, %rsp\n\t"
        "jmp *mh_trampoline_sp(%rip)\n\t"
    );
}

__attribute__((naked)) void mh_detour_mp(void)
{
    __asm__ __volatile__(
        "subq $0x28, %rsp\n\t"
        "call mh_poll_keys\n\t"
        "addq $0x28, %rsp\n\t"
        "jmp *mh_trampoline_mp(%rip)\n\t"
    );
}

static void put_u64(unsigned char* p, u64 v) { memcpy(p, &v, sizeof v); }

static int mh_clamp(long long v)
{
    if (v < 0) return 0;
    if (v > 2000000000LL) return 2000000000;
    return (int)v;
}

static long long mh_clamp64(long long v)
{
    if (v < 0) return 0;
    if (v > 2000000000LL) return 2000000000LL;
    return v;
}

static int mh_clamp_rank(int v)
{
    if (v < 1) return 1;
    if (v > 1000) return 1000;
    return v;
}

static void mh_logf(const char* fmt, ...)
{
    char buf[600];
    int n;
    SYSTEMTIME st;
    va_list ap;
    HANDLE h;
    DWORD written;

    if (!g_log[0]) return;

    GetLocalTime(&st);
    n = snprintf(buf, sizeof buf, "[%02u:%02u:%02u.%03u] ",
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    if (n < 0 || n >= (int)sizeof buf) n = 0;

    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof buf - (size_t)n, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf - 3) n = (int)sizeof buf - 3;
    buf[n++] = '\r';
    buf[n++] = '\n';

    h = CreateFileW(g_log, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        WriteFile(h, buf, (DWORD)n, &written, NULL);
        CloseHandle(h);
    }
}

static u64 mh_module_size(void)
{
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)g_base;
    const IMAGE_NT_HEADERS* nt;

    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    if (dos->e_lfanew < (LONG)sizeof(IMAGE_DOS_HEADER) || dos->e_lfanew > 0x1000) return 0;
    nt = (const IMAGE_NT_HEADERS*)(g_base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    return nt->OptionalHeader.SizeOfImage;
}

static int mh_verify(void)
{
    const BYTE* p;

    /* Refuse to touch anything that is not the big GTA5 image. */
    if (mh_module_size() < RVA_STAT_BANK_BALANCE + 4) return 0;
    if (mh_module_size() < RVA_NET_SESSION + 4) return 0;

    p = g_base + RVA_GET_STAT_ID;
    if (memcmp(p, SIG_GET_STAT_ID, sizeof SIG_GET_STAT_ID)) return 0;
    p = g_base + RVA_GET_INT_STAT;
    if (memcmp(p, SIG_GET_INT_STAT, sizeof SIG_GET_INT_STAT)) return 0;
    p = g_base + RVA_IS_KEY_VALID;
    if (memcmp(p, SIG_IS_KEY_VALID, sizeof SIG_IS_KEY_VALID)) return 0;
    p = g_base + RVA_SET_STAT_DATA_INT;
    if (memcmp(p, SIG_SET_STAT, sizeof SIG_SET_STAT)) return 0;
    p = g_base + RVA_GET_I64_STAT;
    if (memcmp(p, SIG_GET_INT_STAT, sizeof SIG_GET_INT_STAT)) return 0;
    p = g_base + RVA_SET_STAT_DATA_RAW;
    if (memcmp(p, SIG_SET_STAT_RAW, sizeof SIG_SET_STAT_RAW)) return 0;
    p = g_base + RVA_UPDATE_SP_CASH;
    if (memcmp(p, SIG_ENTRY, sizeof SIG_ENTRY)) return 0;
    p = g_base + RVA_UPDATE_MP_CASH;
    if (memcmp(p, SIG_MP_ENTRY, sizeof SIG_MP_ENTRY)) return 0;

    /* GetStatsModelPrefix is `mov eax,[rip+disp32]; ret`. */
    p = g_base + RVA_GET_STATS_PREFIX;
    if (p[0] != 0x8B || p[1] != 0x05 || p[6] != 0xC3) return 0;

    /* The script-side stat commands and the cloud-file gates are optional: the
       money/rank engine path works without them, so a mismatch only disables
       the script-visible rank sync. */
    g_stat_cmds_ok = 0;
    if (!memcmp(g_base + RVA_STAT_GET_INT, SIG_STAT_GET_INT, sizeof SIG_STAT_GET_INT) &&
        !memcmp(g_base + RVA_STAT_SET_INT, SIG_STAT_SET_INT, sizeof SIG_STAT_SET_INT) &&
        mh_sig_cloud_flag(g_base + RVA_CLOUD_LOAD_PENDING) &&
        mh_sig_cloud_flag(g_base + RVA_CLOUD_LOAD_FAILED)) {
        g_stat_cmds_ok = 1;
    }

    g_stat_bool_ok = 0;
    if (!memcmp(g_base + RVA_STAT_GET_BOOL, SIG_STAT_GET_BOOL, sizeof SIG_STAT_GET_BOOL) &&
        !memcmp(g_base + RVA_STAT_SET_BOOL, SIG_STAT_SET_BOOL, sizeof SIG_STAT_SET_BOOL)) {
        g_stat_bool_ok = 1;
    }

    return 1;
}

static int mh_install(u32 rva, void** tramp_slot, void* detour)
{
    unsigned char* target = (unsigned char*)(g_base + rva);
    unsigned char* tramp;
    unsigned char patch[16] = { 0xFF,0x25,0,0,0,0, 0,0,0,0,0,0,0,0, 0x90,0x90 };
    DWORD old_prot;

    tramp = (unsigned char*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE,
                                         PAGE_EXECUTE_READWRITE);
    if (!tramp) return 0;

    /* 16-byte prologue is mov/push only: raw copy, then jump back to target+16. */
    memcpy(tramp, target, 16);
    tramp[16] = 0xFF; tramp[17] = 0x25;
    *(u32*)(tramp + 18) = 0;
    put_u64(tramp + 22, (u64)(uintptr_t)(target + 16));
    *tramp_slot = tramp;

    put_u64(patch + 6, (u64)(uintptr_t)detour);

    if (!VirtualProtect(target, 16, PAGE_EXECUTE_READWRITE, &old_prot)) {
        VirtualFree(tramp, 0, MEM_RELEASE);
        *tramp_slot = NULL;
        return 0;
    }
    memcpy(target, patch, 16);
    VirtualProtect(target, 16, old_prot, &old_prot);
    FlushInstructionCache(GetCurrentProcess(), target, 16);

    InterlockedExchange(&g_hook_ok, 1);
    return 1;
}

static fn_get_prefix mh_prefix(void)
{
    return (fn_get_prefix)(g_base + RVA_GET_STATS_PREFIX);
}

static u32 mh_joaat(const char* s)
{
    u32 h = 0;

    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
        h += c;
        h += h << 10;
        h ^= h >> 6;
    }
    h += h << 3;
    h ^= h >> 11;
    h += h << 15;
    return h;
}

/* Scripts address character stats by joaat("MP<slot>_<NAME>"); stats model 3
   is MP0, 4 is MP1 and so on. */
static int mh_script_stat_hash(const char* base, u32* out)
{
    char name[48];
    u32 prefix = mh_prefix()();

    if (prefix < 3 || prefix > 7) return 0;
    snprintf(name, sizeof name, "MP%u_%s", prefix - 3, base);
    *out = mh_joaat(name);
    return 1;
}

static int mh_cloud_flag(u32 rva)
{
    return (int)((fn_cloud_flag)(g_base + rva))(0) & 1;
}

/* -1 = unavailable, 0 = the stat get failed, 1 = value written. */
static int mh_script_get(const char* base, int* value)
{
    fn_stat_get_int get = (fn_stat_get_int)(g_base + RVA_STAT_GET_INT);
    u32 hash;

    if (!g_stat_cmds_ok || !mh_script_stat_hash(base, &hash)) return -1;
    *value = 0;
    return get((int)hash, value, -1) ? 1 : 0;
}

static int mh_script_set(const char* base, int value)
{
    fn_stat_set_int set = (fn_stat_set_int)(g_base + RVA_STAT_SET_INT);
    u32 hash;

    if (!g_stat_cmds_ok || !mh_script_stat_hash(base, &hash)) return -1;
    return set((int)hash, value, 0) ? 1 : 0;
}

static int mh_script_get_bool(const char* base, int* value)
{
    fn_stat_get_bool get = (fn_stat_get_bool)(g_base + RVA_STAT_GET_BOOL);
    u32 hash;

    if (!g_stat_bool_ok || !mh_script_stat_hash(base, &hash)) return -1;
    *value = 0;
    return get((int)hash, value, -1) ? 1 : 0;
}

static int mh_script_set_bool(const char* base, int value)
{
    fn_stat_set_bool set = (fn_stat_set_bool)(g_base + RVA_STAT_SET_BOOL);
    u32 hash;

    if (!g_stat_bool_ok || !mh_script_stat_hash(base, &hash)) return -1;
    return set((int)hash, value ? 1 : 0, 0) ? 1 : 0;
}

/* Profile stats (MPPLY_*) are addressed by their full key, without the slot
   prefix. */
static int mh_script_get_bool_key(const char* key, int* value)
{
    fn_stat_get_bool get = (fn_stat_get_bool)(g_base + RVA_STAT_GET_BOOL);

    if (!g_stat_bool_ok) return -1;
    *value = 0;
    return get((int)mh_joaat(key), value, -1) ? 1 : 0;
}

static int mh_script_set_bool_key(const char* key, int value)
{
    fn_stat_set_bool set = (fn_stat_set_bool)(g_base + RVA_STAT_SET_BOOL);

    if (!g_stat_bool_ok) return -1;
    return set((int)mh_joaat(key), value ? 1 : 0, 0) ? 1 : 0;
}

/* What the game's own scripts see when they read rank/XP through STAT_GET_INT. */
static void mh_log_script_rank(void)
{
    int rank = 0, xp = 0;
    int rok = mh_script_get("CHAR_RANK_FM", &rank);
    int xok = mh_script_get("CHAR_XP_FM", &xp);

    mh_logf("script view: rank ok=%d value=%d, xp ok=%d value=%d, "
            "cloud slot 0 pending=%d failed=%d",
            rok, rank, xok, xp,
            mh_cloud_flag(RVA_CLOUD_LOAD_PENDING),
            mh_cloud_flag(RVA_CLOUD_LOAD_FAILED));
}

static int mh_story_mode(void)
{
    u32 prefix = mh_prefix()();
    if (prefix >= 3) {
        mh_logf("skipped: stats prefix %u is a multiplayer model, not story mode", prefix);
        return 0;
    }
    return 1;
}

static int mh_apply_cash(void)
{
    fn_get_stat_id get_id = (fn_get_stat_id)(g_base + RVA_GET_STAT_ID);
    fn_get_int_stat get_int = (fn_get_int_stat)(g_base + RVA_GET_INT_STAT);
    fn_set_stat_data_int set_int = (fn_set_stat_data_int)(g_base + RVA_SET_STAT_DATA_INT);
    fn_is_key_valid valid = (fn_is_key_valid)(g_base + RVA_IS_KEY_VALID);
    unsigned char statid[64];
    u32 prefix;
    int cur, wanted, now;

    if (!mh_story_mode()) return 0;

    prefix = mh_prefix()();
    memset(statid, 0, sizeof statid);
    get_id(g_stat_total_cash, statid);
    if (!valid(statid)) {
        mh_logf("total cash rejected by IsKeyValid; nothing written");
        return 0;
    }

    cur = get_int(statid);
    wanted = mh_clamp(g_amount);

    set_int(statid, wanted, 3);
    now = get_int(statid);
    mh_logf("character %u cash: %d -> %d (wanted %d, set)", prefix, cur, now, wanted);
    return 1;
}

static int mh_apply_bank(int add)
{
    fn_get_i64_stat get64 = (fn_get_i64_stat)(g_base + RVA_GET_I64_STAT);
    fn_set_stat_data_raw set_raw = (fn_set_stat_data_raw)(g_base + RVA_SET_STAT_DATA_RAW);
    fn_is_key_valid valid = (fn_is_key_valid)(g_base + RVA_IS_KEY_VALID);
    u32 statid;
    u32 prefix;
    long long cur, wanted, now;

    /* StatId_static is filled in by the game's static initializers, so read it now. */
    statid = *(volatile u32*)g_stat_bank_balance;
    if (!statid) {
        mh_logf("bank stat id not initialised by the game yet; nothing written");
        return 0;
    }

    if (!valid(&statid)) {
        mh_logf("bank stat %08X rejected by IsKeyValid (model prefix %u); nothing written",
                statid, mh_prefix()());
        return 0;
    }

    prefix = mh_prefix()();
    cur = get64(&statid);
    wanted = add ? cur + (long long)g_amount : (long long)g_amount;
    wanted = mh_clamp64(wanted);

    /* Bank is an int64 stat; the game itself writes it as 8 raw bytes with flags 1
       (MoneyInterface::ApplyVirtualCashBalance). */
    if (!set_raw(&statid, &wanted, 8, 1)) {
        mh_logf("bank set rejected by SetStatData (model prefix %u); nothing written", prefix);
        return 0;
    }

    now = get64(&statid);
    mh_logf("bank (model %u): %lld -> %lld (wanted %lld, %s)", prefix, cur, now, wanted,
            add ? "add" : "set");

    /* Purchases are not gated on BANK_BALANCE. Every shop check goes through
       NETWORK_CAN_SPEND_MONEY -> CanSpendGtaDollars, which compares the price with
       MoneyInterface::GetVCBalance() = CLIENT_EVC_BALANCE + CLIENT_PVC_BALANCE
       (commands_money.cpp / MoneyInterface.cpp), so those stats have to carry the
       money as well or the store reports "insufficient cash" while the HUD shows
       the bank balance. Both are s64 profile stats written the same raw way. */
    if (g_stat_evc_balance) {
        long long evc_cur, evc_wanted, evc_now;
        u32 evc_id = *(volatile u32*)g_stat_evc_balance;

        if (!evc_id || !valid(&evc_id)) {
            mh_logf("virtual cash stat %08X not usable (prefix %u); store purchases will "
                    "still fail with insufficient cash", evc_id, prefix);
        } else {
            evc_cur = get64(&evc_id);
            evc_wanted = add ? evc_cur + (long long)g_amount : (long long)g_amount;
            evc_wanted = mh_clamp64(evc_wanted);
            if (!set_raw(&evc_id, &evc_wanted, 8, 1)) {
                mh_logf("virtual cash set rejected by SetStatData (model prefix %u)", prefix);
            } else {
                evc_now = get64(&evc_id);
                mh_logf("virtual cash (model %u): %lld -> %lld (wanted %lld, %s)", prefix,
                        evc_cur, evc_now, evc_wanted, add ? "add" : "set");
            }
        }
    }

    return 1;
}

static int mh_sane_ptr(const void* p)
{
    uintptr_t v = (uintptr_t)p;
    return v >= 0x10000 && v < 0x00007FFFFFFF0000ull && (v & 7) == 0;
}

/* Push the rank into the live network player record so the player list / card
   and the outgoing player data see it without waiting for UpdateUserData. */
static int mh_sync_rank_mirror(void)
{
    LONG want = g_rank_mirror;
    void* mgr;
    void* local;
    void* session;
    int done = 0;

    if (!want) return 1;

    mgr = *(void**)(g_base + RVA_NET_PLAYER_MGR);
    if (mh_sane_ptr(mgr)) {
        local = *(void**)((BYTE*)mgr + OFF_PLAYER_MGR_LOCAL);
        if (mh_sane_ptr(local)) {
            *(volatile unsigned short*)((BYTE*)local + OFF_NET_PLAYER_RANK) = (unsigned short)want;
            done = 1;
        }
    }

    session = *(void**)(g_base + RVA_NET_SESSION);
    if (mh_sane_ptr(session)) {
        *(volatile unsigned short*)((BYTE*)session + OFF_SESSION_RANK) = (unsigned short)want;
        done = 1;
    }

    if (done) {
        InterlockedCompareExchange(&g_rank_mirror, 0, want);
        mh_logf("rank mirror: network player record set to %d", (int)want);
    }
    return done;
}

/* Level checks (shops, unlocks) are script code, and the scripts derive the
   player rank from CHAR_XP_FM on their own; writing only CHAR_RANK_FM changes
   the player list but leaves those checks seeing rank 1. So set XP too, and
   write both through the script stat namespace as well as the engine stats. */
static int mh_apply_rank(void)
{
    fn_get_stat_id get_id = (fn_get_stat_id)(g_base + RVA_GET_STAT_ID);
    fn_get_int_stat get_int = (fn_get_int_stat)(g_base + RVA_GET_INT_STAT);
    fn_set_stat_data_int set_int = (fn_set_stat_data_int)(g_base + RVA_SET_STAT_DATA_INT);
    fn_is_key_valid valid = (fn_is_key_valid)(g_base + RVA_IS_KEY_VALID);
    unsigned char statid[64];
    unsigned char xid[64];
    u32 prefix;
    int cur, wanted, now, xp, xcur, xnow;

    prefix = mh_prefix()();
    wanted = mh_clamp_rank((int)g_rank);

    memset(statid, 0, sizeof statid);
    get_id(g_stat_rank_fm, statid);
    if (!valid(statid)) {
        mh_logf("rank stat rejected by IsKeyValid (model prefix %u); nothing written", prefix);
        return 0;
    }

    xp = (int)g_xp;
    if (xp > 0) {
        memset(xid, 0, sizeof xid);
        get_id(g_stat_xp_fm, xid);
        if (valid(xid)) {
            xcur = get_int(xid);
            set_int(xid, xp, 3);
            xnow = get_int(xid);
            mh_logf("xp (model %u): %d -> %d (wanted %d, set)", prefix, xcur, xnow, xp);
        }
    }

    cur = get_int(statid);
    set_int(statid, wanted, 3);
    now = get_int(statid);
    mh_logf("rank (model %u): %d -> %d (wanted %d, set)", prefix, cur, now, wanted);

    if (prefix >= 3) {
        int xr = xp > 0 ? mh_script_set("CHAR_XP_FM", xp) : -1;
        int rr = mh_script_set("CHAR_RANK_FM", wanted);
        mh_logf("script set: rank ok=%d, xp ok=%d", rr, xr);
        mh_log_script_rank();
    }

    InterlockedExchange(&g_rank_seen, now);
    /* Retried from the frame poll until the player exists. */
    InterlockedExchange(&g_rank_mirror, wanted);
    mh_sync_rank_mirror();
    return 1;
}

/* maintransition resets CHAR_RANK_FM to 1 when a multiplayer session starts, so
   keep the requested value applied while in MP. If the scripts themselves move
   the rank above the requested one (they recompute it from XP), follow them
   instead of fighting. */
static void mh_rank_tick(void)
{
    fn_get_stat_id get_id = (fn_get_stat_id)(g_base + RVA_GET_STAT_ID);
    fn_get_int_stat get_int = (fn_get_int_stat)(g_base + RVA_GET_INT_STAT);
    fn_is_key_valid valid = (fn_is_key_valid)(g_base + RVA_IS_KEY_VALID);
    unsigned char statid[64];
    void* mgr;
    void* local;
    DWORD now, last;
    int cur, wanted;

    if (!g_rank_auto || mh_prefix()() < 3) return;

    now = GetTickCount();
    last = (DWORD)g_last_auto_tick;
    if (now - last < 2000) return;

    mgr = *(void**)(g_base + RVA_NET_PLAYER_MGR);
    if (!mh_sane_ptr(mgr)) return;
    local = *(void**)((BYTE*)mgr + OFF_PLAYER_MGR_LOCAL);
    if (!mh_sane_ptr(local)) return;

    memset(statid, 0, sizeof statid);
    get_id(g_stat_rank_fm, statid);
    if (!valid(statid)) return;

    InterlockedExchange(&g_last_auto_tick, (LONG)now);

    cur = get_int(statid);
    wanted = mh_clamp_rank((int)g_rank);
    if (cur == (int)g_rank_seen) return;

    if (cur > wanted) {
        mh_logf("rank moved to %d by the game (xp implies a higher level); following it",
                cur);
        InterlockedExchange(&g_rank_seen, cur);
        InterlockedExchange(&g_rank_mirror, cur);
        mh_sync_rank_mirror();
        return;
    }

    mh_logf("rank is %d in the stat (want %d); re-applying", cur, wanted);
    mh_apply_rank();
}

/* Dynasty 8 checks IS_FM_TYPE_UNLOCKED(FMMC_TYPE_CUSTOM_CAR_GARAGE) (60) before
   it sells anything and prints the "sufficient rank" error when that is false,
   even though the actual rank requirement for it is 3. Freemode only sets that
   unlock bit in SET_FM_UNLOCKS_BIT_SET, which needs:
     - MP<slot>_FM_INTRO_MISS_DONE (else it bails and clears every bit),
     - MP<slot>_FM_INTRO_CUT_DONE (else "intro mission done, but not cutscen"),
     - MP<slot>_FM_TRIGTUT_DONE (UNLOCK_FM_ACTIVITY refuses before the trigger
       tutorial, unless the player chose to skip tutorials),
     - MP<slot>_FM_CUT_DONE bit 0 (biFmCut_IntroToGarages - the Simeon garage
       cutscene; UNLOCK_FM_ACTIVITY refuses custom car garages without it).
   Freemode reads FM_CUT_DONE from a per-slot cache filled at session start, so
   the bits are rebuilt on the next transition into freemode, not mid-session. */
static void mh_apply_unlocks(void)
{
    static LONG last_state = -1;
    LONG state;
    int iset, cset, rset, iget, cget, rget;
    int ival = 0, cval = 0, rval = 0;
    int pset, pget, pval = 0;
    int tset, tget, tval = 0;
    int gset = -1, gget, gval = 0;

    iset = mh_script_set_bool("FM_INTRO_MISS_DONE", 1);
    cset = mh_script_set_bool("FM_INTRO_CUT_DONE", 1);
    rset = mh_script_set_bool("FM_RACETUT_DONE", 1);
    pset = mh_script_set_bool_key("MPPLY_FM_INTRO_CUT_DONE", 1);
    tset = mh_script_set_bool("FM_TRIGTUT_DONE", 1);

    gget = mh_script_get("FM_CUT_DONE", &gval);
    if (gget >= 0) {
        if (gval & 1) {
            gset = 1;
        } else {
            gset = mh_script_set("FM_CUT_DONE", gval | 1);
            gget = mh_script_get("FM_CUT_DONE", &gval);
        }
    }

    iget = mh_script_get_bool("FM_INTRO_MISS_DONE", &ival);
    cget = mh_script_get_bool("FM_INTRO_CUT_DONE", &cval);
    rget = mh_script_get_bool("FM_RACETUT_DONE", &rval);
    pget = mh_script_get_bool_key("MPPLY_FM_INTRO_CUT_DONE", &pval);
    tget = mh_script_get_bool("FM_TRIGTUT_DONE", &tval);

    state = ((LONG)(iset + 1) << 26) | ((LONG)(cset + 1) << 24) |
            ((LONG)(rset + 1) << 22) | ((LONG)(pset + 1) << 20) |
            ((LONG)(iget + 1) << 18) | ((LONG)(cget + 1) << 16) |
            ((LONG)(rget + 1) << 14) | ((LONG)(pget + 1) << 12) |
            ((LONG)(tset + 1) << 8) | ((LONG)(tget + 1) << 6) |
            ((LONG)(gset + 1) << 4) | ((LONG)((gval & 1) != 0) << 3) |
            ((LONG)(ival != 0) << 2) | ((LONG)(cval != 0) << 1) |
            (LONG)(rval != 0);
    if (state != last_state) {
        mh_logf("unlock: set intro=%d cut=%d race-tut=%d cutscene-profile=%d "
                "garage-cut=%d trigger-tut=%d; "
                "read back intro ok=%d value=%d, cut ok=%d value=%d, "
                "race-tut ok=%d value=%d, cutscene-profile ok=%d value=%d, "
                "garage-cut ok=%d value=%d, trigger-tut ok=%d value=%d, "
                "cloud slot 0 pending=%d failed=%d",
                iset, cset, rset, pset, gset, tset,
                iget, ival, cget, cval, rget, rval, pget, pval,
                gget, gval, tget, tval,
                mh_cloud_flag(RVA_CLOUD_LOAD_PENDING),
                mh_cloud_flag(RVA_CLOUD_LOAD_FAILED));
        last_state = state;
    }
}

static void mh_unlock_tick(void)
{
    static LONG reported;
    DWORD now, last;
    int iv = 0, cv = 0, rv = 0, tv = 0, gv = 0;

    if (!g_unlock_intro || !g_stat_bool_ok || !g_stat_cmds_ok || mh_prefix()() < 3) return;

    now = GetTickCount();
    last = (DWORD)g_last_unlock_tick;
    if (now - last < 2000) return;
    InterlockedExchange(&g_last_unlock_tick, (LONG)now);

    if (mh_script_get_bool("FM_INTRO_MISS_DONE", &iv) == 1 && iv &&
        mh_script_get_bool("FM_INTRO_CUT_DONE", &cv) == 1 && cv &&
        mh_script_get_bool("FM_RACETUT_DONE", &rv) == 1 && rv &&
        mh_script_get_bool("FM_TRIGTUT_DONE", &tv) == 1 && tv &&
        mh_script_get("FM_CUT_DONE", &gv) == 1 && (gv & 1)) {
        if (!reported) {
            mh_logf("unlock: intro/tutorial flags already set (intro=%d cut=%d "
                    "race-tut=%d trigger-tut=%d garage-cut value=%d)",
                    iv, cv, rv, tv, gv);
            reported = 1;
        }
        return;
    }

    reported = 0;
    mh_apply_unlocks();
}

static void mh_load_config(void)
{
    int v = (int)GetPrivateProfileIntW(L"Money", L"Amount", 10000000, g_ini);
    int r = (int)GetPrivateProfileIntW(L"Rank", L"Level", 120, g_ini);
    int x = (int)GetPrivateProfileIntW(L"Rank", L"XP", 1584350, g_ini);
    int a = (int)GetPrivateProfileIntW(L"Rank", L"AutoApply", 1, g_ini);
    int u = (int)GetPrivateProfileIntW(L"Unlock", L"IntroDone", 1, g_ini);

    InterlockedExchange(&g_amount, mh_clamp(v));
    InterlockedExchange(&g_rank, mh_clamp_rank(r));
    if (x < 0) x = 0;
    if (x > 40000000) x = 40000000;
    InterlockedExchange(&g_xp, x);
    InterlockedExchange(&g_rank_auto, a ? 1 : 0);
    InterlockedExchange(&g_unlock_intro, u ? 1 : 0);
    mh_logf("config: amount=%d rank=%d xp=%d autoapply=%d intro-done=%d (%ls)",
            (int)g_amount, (int)g_rank, (int)g_xp, (int)g_rank_auto,
            (int)g_unlock_intro, g_ini);
}

void mh_poll_keys(void)
{
    static unsigned char prev[5];
    static const int vk[5] = { '0', '1', '2', '3', '4' };
    HWND fg;
    DWORD pid = 0;
    int combo;
    int i;

    if (!g_hook_ok) return;

    if (g_rank_mirror) mh_sync_rank_mirror();
    mh_rank_tick();
    mh_unlock_tick();

    fg = GetForegroundWindow();
    if (fg) GetWindowThreadProcessId(fg, &pid);
    if (pid != GetCurrentProcessId()) {
        memset(prev, 0, sizeof prev);
        return;
    }

    combo = (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
            (GetAsyncKeyState(VK_SHIFT) & 0x8000);

    for (i = 0; i < 5; i++) {
        int down = (GetAsyncKeyState(vk[i]) & 0x8000) != 0;
        if (down && !prev[i] && combo) {
            switch (i) {
            case 0: mh_load_config(); break;
            case 1: mh_apply_bank(0); break;
            case 2: mh_apply_bank(1); break;
            case 3: mh_apply_cash(); break;
            case 4: mh_apply_rank(); break;
            }
        }
        prev[i] = (unsigned char)down;
    }
}

static DWORD WINAPI mh_worker(LPVOID param)
{
    wchar_t* slash;
    DWORD len;

    (void)param;

    len = GetModuleFileNameW(g_hinst, g_dir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return 0;
    slash = wcsrchr(g_dir, L'\\');
    if (!slash) return 0;
    *slash = L'\0';
    wsprintfW(g_ini, L"%ls\\GTA5Money.ini", g_dir);
    wsprintfW(g_log, L"%ls\\GTA5Money.log", g_dir);

    g_base = (BYTE*)GetModuleHandleW(NULL);
    mh_logf("injected into exe base %p", g_base);

    if (mh_module_size() < RVA_STAT_BANK_BALANCE + 4) {
        mh_logf("host image too small for this build, hook not installed");
        return 0;
    }
    if (!mh_verify()) {
        mh_logf("signature check failed; not the expected build, hook not installed");
        return 0;
    }

    g_stat_total_cash = g_base + RVA_STAT_TOTAL_CASH;
    g_stat_bank_balance = g_base + RVA_STAT_BANK_BALANCE;
    g_stat_evc_balance = g_base + RVA_STAT_CLIENT_EVC;
    g_stat_pvc_balance = g_base + RVA_STAT_CLIENT_PVC;
    g_stat_rank_fm = g_base + RVA_STAT_RANK_FM;
    g_stat_xp_fm = g_base + RVA_STAT_CHAR_XP_FM;
    mh_logf("script stat commands: int %s, bool %s",
            g_stat_cmds_ok ? "ok" : "unavailable",
            g_stat_bool_ok ? "ok" : "unavailable");

    {
        int ok_sp = mh_install(RVA_UPDATE_SP_CASH, &mh_trampoline_sp, (void*)&mh_detour_sp);
        int ok_mp = mh_install(RVA_UPDATE_MP_CASH, &mh_trampoline_mp, (void*)&mh_detour_mp);

        mh_logf("hooks: singleplayer=%s (%p), multiplayer=%s (%p)",
                ok_sp ? "ok" : "failed", g_base + RVA_UPDATE_SP_CASH,
                ok_mp ? "ok" : "failed", g_base + RVA_UPDATE_MP_CASH);
        if (!ok_sp && !ok_mp) {
            mh_logf("no hook installed");
            return 0;
        }
    }

    mh_logf("hotkeys: Ctrl+Shift+0 reload ini, Ctrl+Shift+1 set bank, "
            "Ctrl+Shift+2 add to bank, Ctrl+Shift+3 set story cash, Ctrl+Shift+4 set rank");
    mh_logf("total cash stat at %p, bank stat at %p, rank stat at %p",
            g_stat_total_cash, g_stat_bank_balance, g_stat_rank_fm);
    mh_load_config();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        HANDLE t;
        g_hinst = hinst;
        DisableThreadLibraryCalls(hinst);
        t = CreateThread(NULL, 0, mh_worker, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
    return TRUE;
}

/* ---------------- dinput8 proxy ---------------- */

typedef HRESULT (WINAPI *pfn_DirectInput8Create)(HINSTANCE, DWORD, const void*, void**, void*);
typedef HRESULT (WINAPI *pfn_NoArg)(void);
typedef HRESULT (WINAPI *pfn_DllGetClassObject)(const void*, const void*, void**);
typedef const void* (WINAPI *pfn_GetdfDIJoystick)(void);

static HMODULE mh_real_module(void)
{
    static HMODULE real;
    if (!real) {
        wchar_t path[MAX_PATH];
        UINT n = GetSystemDirectoryW(path, MAX_PATH);
        if (n == 0 || n > MAX_PATH - 16) return NULL;
        path[n] = L'\0';
        wcscat(path, L"\\dinput8.dll");
        real = LoadLibraryExW(path, NULL,
                              LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    }
    return real;
}

static FARPROC mh_real_proc(const char* name, const void* self)
{
    HMODULE real = mh_real_module();
    FARPROC p;
    if (!real) return NULL;
    p = GetProcAddress(real, name);
    if (p && (const void*)p == self) return NULL;  /* loader handed us back our own module */
    return p;
}

HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD version,
                                  const void* riid, void** out,
                                  void* outer)
{
    pfn_DirectInput8Create fn = (pfn_DirectInput8Create)mh_real_proc("DirectInput8Create",
                                                                     (const void*)&DirectInput8Create);
    if (!fn) return E_FAIL;
    return fn(hinst, version, riid, out, outer);
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    pfn_NoArg fn = (pfn_NoArg)mh_real_proc("DllCanUnloadNow", (const void*)&DllCanUnloadNow);
    return fn ? fn() : S_FALSE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, LPVOID* out)
{
    pfn_DllGetClassObject fn = (pfn_DllGetClassObject)mh_real_proc("DllGetClassObject",
                                                                   (const void*)&DllGetClassObject);
    return fn ? fn(clsid, iid, out) : E_FAIL;
}

HRESULT WINAPI DllRegisterServer(void)
{
    pfn_NoArg fn = (pfn_NoArg)mh_real_proc("DllRegisterServer", (const void*)&DllRegisterServer);
    return fn ? fn() : E_FAIL;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    pfn_NoArg fn = (pfn_NoArg)mh_real_proc("DllUnregisterServer", (const void*)&DllUnregisterServer);
    return fn ? fn() : E_FAIL;
}

const void* WINAPI GetdfDIJoystick(void)
{
    pfn_GetdfDIJoystick fn = (pfn_GetdfDIJoystick)mh_real_proc("GetdfDIJoystick",
                                                               (const void*)&GetdfDIJoystick);
    return fn ? fn() : NULL;
}
