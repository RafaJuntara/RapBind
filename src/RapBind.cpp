#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>

namespace {
using CMDPROC = void(__cdecl*)(const char*);
using AddCommandFn = void(__thiscall*)(uintptr_t, const char*, CMDPROC);

// SA-MP 0.3.DL
constexpr uintptr_t CHAT_INPUT_INFO_OFFSET = 0x2ACA14;
constexpr uintptr_t ADD_CLIENT_CMD_OFFSET = 0x691B0;

volatile LONG g_registered = 0;

void cmdBind(const char*) {
    MessageBoxA(nullptr,
        "RapBind: /bind berhasil dipanggil!",
        "RapBind test",
        MB_OK);
}

DWORD WINAPI initThread(LPVOID) {
    HMODULE samp = nullptr;
    for (int i = 0; i < 200; ++i) {
        samp = GetModuleHandleA("samp.dll");
        if (samp) break;
        Sleep(100);
    }
    if (!samp) return 0;

    // Give SA-MP time to finish constructing its chat input object.
    Sleep(5000);

    uintptr_t base = reinterpret_cast<uintptr_t>(samp);
    uintptr_t inputSlot = base + CHAT_INPUT_INFO_OFFSET;
    uintptr_t input = *reinterpret_cast<uintptr_t*>(inputSlot);
    if (!input) return 0;

    auto addCommand = reinterpret_cast<AddCommandFn>(base + ADD_CLIENT_CMD_OFFSET);

    // This is the exact equivalent of the documented 0.3.DL
    // registerClientCommand call: struct = *(base + 0x2ACA14),
    // method = base + 0x691B0, args = command name + callback.
    addCommand(input, "bind", cmdBind);
    InterlockedExchange(&g_registered, 1);

    MessageBoxA(nullptr,
        "RapBind loaded. Command /bind registered.",
        "RapBind test",
        MB_OK);

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
