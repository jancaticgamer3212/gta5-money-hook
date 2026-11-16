/* Validates the detour/trampoline mechanism in isolation: a stand-in function
 * with the exact 16-byte prologue of CNewHud::UpdateSinglePlayerCash. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint64_t u64;

static const unsigned char SIG_ENTRY[16] = {
    0x48,0x89,0x5C,0x24,0x10, 0x48,0x89,0x6C,0x24,0x18, 0x48,0x89,0x74,0x24,0x20, 0x57
};

static volatile LONG g_calls;
void mh_poll_keys(void);
void* mh_trampoline;

__attribute__((naked)) void mh_detour(void)
{
    __asm__ __volatile__(
        "subq $0x28, %rsp\n\t"
        "call mh_poll_keys\n\t"
        "addq $0x28, %rsp\n\t"
        "jmp *mh_trampoline(%rip)\n\t"
    );
}

void mh_poll_keys(void) { InterlockedIncrement(&g_calls); }

__attribute__((naked, noinline)) int victim(void)
{
    __asm__ __volatile__(
        "movq %rbx, 0x10(%rsp)\n\t"
        "movq %rbp, 0x18(%rsp)\n\t"
        "movq %rsi, 0x20(%rsp)\n\t"
        "pushq %rdi\n\t"
        "movl $7, %eax\n\t"
        "popq %rdi\n\t"
        "ret\n\t"
    );
}

typedef int (*fn_t)(void);

static void put_u64(unsigned char* p, u64 v) { memcpy(p, &v, sizeof v); }

int main(void)
{
    unsigned char* target = (unsigned char*)&victim;
    unsigned char* tramp;
    unsigned char patch[16] = { 0xFF,0x25,0,0,0,0, 0,0,0,0,0,0,0,0, 0x90,0x90 };
    DWORD old_prot;
    int r, i, ok = 1;

    printf("victim prologue: ");
    for (i = 0; i < 16; i++) printf("%02X ", target[i]);
    printf("\n");

    if (memcmp(target, SIG_ENTRY, 16)) {
        printf("FAIL: prologue differs from game signature\n");
        return 1;
    }

    r = ((fn_t)target)();
    printf("before hook: ret=%d calls=%ld\n", r, g_calls);

    tramp = (unsigned char*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(tramp, target, 16);
    tramp[16] = 0xFF; tramp[17] = 0x25;
    *(uint32_t*)(tramp + 18) = 0;
    put_u64(tramp + 22, (u64)(uintptr_t)(target + 16));
    mh_trampoline = tramp;

    put_u64(patch + 6, (u64)(uintptr_t)&mh_detour);
    VirtualProtect(target, 16, PAGE_EXECUTE_READWRITE, &old_prot);
    memcpy(target, patch, 16);
    VirtualProtect(target, 16, old_prot, &old_prot);
    FlushInstructionCache(GetCurrentProcess(), target, 16);

    for (i = 0; i < 3; i++) {
        r = ((fn_t)target)();
        printf("after hook %d: ret=%d calls=%ld\n", i + 1, r, g_calls);
        if (r != 7) ok = 0;
    }
    if (g_calls != 3) ok = 0;

    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
