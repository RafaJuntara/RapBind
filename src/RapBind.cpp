#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace {
using CMDPROC = void(__cdecl*)(const char*);
using AddCommandFn = void(__thiscall*)(uintptr_t, const char*, CMDPROC);

constexpr uintptr_t INPUT_PTR = 0x2ACA14;
constexpr uintptr_t ADD_COMMAND = 0x691B0;

HMODULE g_samp = nullptr;
uintptr_t g_base = 0;
volatile LONG g_registered = 0;

bool readable(const void* p, SIZE_T n) {
    if (!p || !n) return false;
    MEMORY_BASIC_INFORMATION m{};
    if (!VirtualQuery(p, &m, sizeof(m))) return false;
    if (m.State != MEM_COMMIT) return false;
    DWORD pr = m.Protect & 0xFF;
    if (pr == PAGE_NOACCESS || pr == PAGE_GUARD) return false;
    uintptr_t a = reinterpret_cast<uintptr_t>(p);
    uintptr_t b = reinterpret_cast<uintptr_t>(m.BaseAddress);
    return a >= b && n <= (b + m.RegionSize - a);
}

void cmdBind(const char*) {
    // Intentionally empty: this build only tests command registration.
}

bool tryRegister() {
    auto slot = reinterpret_cast<uintptr_t*>(g_base + INPUT_PTR);
    if (!readable(slot, sizeof(uintptr_t))) return false;

    uintptr_t input = *slot;
    if (!input || !readable(reinterpret_cast<const void*>(input), 4)) return false;

    auto add = reinterpret_cast<AddCommandFn>(g_base + ADD_COMMAND);
    if (!readable(reinterpret_cast<const void*>(add), 1)) return false;

    __try {
        add(input, "bind", cmdBind);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    InterlockedExchange(&g_registered, 1);
    return true;
}

DWORD WINAPI initThread(LPVOID) {
    // Wait until SA-MP exists, then give it a little time to finish initialization.
    for (int i = 0; i < 300; ++i) {
        g_samp = GetModuleHandleA("samp.dll");
        if (g_samp) break;
        Sleep(100);
    }
    if (!g_samp) return 0;

    g_base = reinterpret_cast<uintptr_t>(g_samp);
    Sleep(3000);

    // Retry until the input structure is ready, but never touch chat/UI yet.
    for (int i = 0; i < 120 && !g_registered; ++i) {
        if (tryRegister()) break;
        Sleep(250);
    }
    return 0;
}
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        HANDLE t = CreateThread(nullptr, 0, initThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
