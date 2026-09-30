#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <fstream>
#include <vector>

namespace {
using CMDPROC = void(__cdecl*)(const char*);
using AddCommandFn = void(__thiscall*)(uintptr_t, const char*, CMDPROC);
using AddMessageFn = void(__thiscall*)(uintptr_t, uint32_t, const char*);

constexpr uintptr_t INPUT_PTR = 0x2ACA14;
constexpr uintptr_t ADD_COMMAND = 0x691B0;
constexpr uintptr_t CHAT_PTR = 0x2ACA10;
constexpr uintptr_t ADD_MESSAGE = 0x64010;

HMODULE g_samp = nullptr;
uintptr_t g_base = 0;
volatile LONG g_registered = 0;

std::string configPath() {
    char p[MAX_PATH]{};
    GetModuleFileNameA(nullptr, p, MAX_PATH);
    char* s = strrchr(p, '\\');
    if (s) *(s + 1) = '\0';
    return std::string(p) + "RapBind.ini";
}

bool readable(const void* p, SIZE_T n) {
    if (!p || !n) return false;
    MEMORY_BASIC_INFORMATION m{};
    if (!VirtualQuery(p, &m, sizeof(m))) return false;
    if (m.State != MEM_COMMIT) return false;
    DWORD pr = m.Protect & 0xFF;
    if (pr == PAGE_NOACCESS || pr == PAGE_GUARD) return false;
    uintptr_t a = reinterpret_cast<uintptr_t>(p);
    uintptr_t b = reinterpret_cast<uintptr_t>(m.BaseAddress);
    return a >= b && n <= b + m.RegionSize - a;
}

void chat(const char* s) {
    auto slot = reinterpret_cast<uintptr_t*>(g_base + CHAT_PTR);
    if (!readable(slot, sizeof(uintptr_t))) return;
    uintptr_t chatPtr = *slot;
    if (!chatPtr) return;
    auto fn = reinterpret_cast<AddMessageFn>(g_base + ADD_MESSAGE);
    if (!readable(reinterpret_cast<const void*>(fn), 1)) return;
    fn(chatPtr, 0xFFFFFFFFu, s);
}

void loadConfig() {
    std::ifstream f(configPath());
    if (!f) {
        std::ofstream o(configPath());
        o << "[binds]\n";
        o << "F3=/me take a crate and load it to benson\n";
        o << "F4=/me examine sultan with mechanic tools\n";
        o << "F5=/do The crate is secured in the Benson\n";
    }
}

void cmdBind(const char*) {
    chat("~ RapBind: loaded. UI version coming next.");
    chat("~ RapBind: config: RapBind.ini");
}

bool tryRegister() {
    auto slot = reinterpret_cast<uintptr_t*>(g_base + INPUT_PTR);
    if (!readable(slot, sizeof(uintptr_t))) return false;
    uintptr_t input = *slot;
    if (!input) return false;

    auto add = reinterpret_cast<AddCommandFn>(g_base + ADD_COMMAND);
    if (!readable(reinterpret_cast<const void*>(add), 1)) return false;

    add(input, "bind", cmdBind);
    InterlockedExchange(&g_registered, 1);
    chat("~ RapBind 0.3.DL loaded. /bind is registered.");
    return true;
}

DWORD WINAPI initThread(LPVOID) {
    for (int i = 0; i < 300; ++i) {
        g_samp = GetModuleHandleA("samp.dll");
        if (g_samp) break;
        Sleep(100);
    }
    if (!g_samp) return 0;
    g_base = reinterpret_cast<uintptr_t>(g_samp);
    loadConfig();

    for (int i = 0; i < 300 && !g_registered; ++i) {
        if (tryRegister()) break;
        Sleep(100);
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
