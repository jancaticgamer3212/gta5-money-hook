#include <windows.h>
#include <stdio.h>

int main(void)
{
    wchar_t full[MAX_PATH];
    HMODULE h;
    DWORD n = GetFullPathNameW(L"dist/dinput8.dll", MAX_PATH, full, NULL);
    printf("path (%lu): %ls\n", n, full);
    fflush(stdout);
    h = LoadLibraryW(full);
    printf("LoadLibrary -> %p (err %lu)\n", (void*)h, GetLastError());
    fflush(stdout);
    Sleep(1500);
    printf("still alive\n");
    fflush(stdout);
    if (h) FreeLibrary(h);
    return 0;
}
