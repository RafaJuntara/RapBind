#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>

namespace {
using CMDPROC = void(__cdecl*)(const char*);
using AddCommandFn = void(__thiscall*)(uintptr_t, const char*, CMDPROC);
using SendFn = void(__thiscall*)(uintptr_t, const char*);

constexpr uintptr_t INPUT_PTR = 0x2ACA14;
constexpr uintptr_t ADD_COMMAND = 0x691B0;
constexpr uintptr_t SEND_COMMAND = 0x69340;

struct Bind { int vk; int mods; std::string cmd; };
std::vector<Bind> g_binds;
std::string g_cfg;
HMODULE g_samp = nullptr;
uintptr_t g_base = 0;
uintptr_t g_input = 0;
volatile LONG g_registered = 0;
HWND g_ui = nullptr, g_list = nullptr, g_key = nullptr, g_cmd = nullptr; HANDLE g_uiThread = nullptr; CRITICAL_SECTION g_cs;
HINSTANCE g_inst = nullptr;
POINT g_drag{};
bool g_dragging = false;

bool readable(const void* p, SIZE_T n) {
    if (!p || !n) return false;
    MEMORY_BASIC_INFORMATION m{};
    if (!VirtualQuery(p, &m, sizeof(m))) return false;
    if (m.State != MEM_COMMIT) return false;
    DWORD pr = m.Protect & 0xFF;
    if (pr == PAGE_NOACCESS || pr == PAGE_GUARD) return false;
    uintptr_t a=(uintptr_t)p, b=(uintptr_t)m.BaseAddress;
    return a>=b && n<=b+m.RegionSize-a;
}

std::string keyName(int vk, int mods) {
    std::string s;
    if(mods&1) s += "Ctrl+";
    if(mods&2) s += "Shift+";
    if(mods&4) s += "Alt+";
    UINT scan=MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    char name[64]{};
    LONG r=GetKeyNameTextA((LONG)(scan<<16), name, sizeof(name));
    if(r>0) s += name; else { std::ostringstream o; o<<"VK_"<<vk; s+=o.str(); }
    return s;
}

int modsNow(){ int m=0; if(GetAsyncKeyState(VK_CONTROL)&0x8000)m|=1; if(GetAsyncKeyState(VK_SHIFT)&0x8000)m|=2; if(GetAsyncKeyState(VK_MENU)&0x8000)m|=4; return m; }

void save(){ std::ofstream f(g_cfg); for(auto &b:g_binds) f<<b.vk<<'|'<<b.mods<<'|'<<b.cmd<<"\n"; }
void load(){
    std::ifstream f(g_cfg); std::string line;
    while(std::getline(f,line)){ size_t a=line.find('|'), b=line.find('|',a+1); if(a==std::string::npos||b==std::string::npos)continue; try { Bind x{std::stoi(line.substr(0,a)),std::stoi(line.substr(a+1,b-a-1)),line.substr(b+1)}; if(!x.cmd.empty())g_binds.push_back(x); } catch(...){} }
    if(g_binds.empty()){ g_binds.push_back({VK_F3,0,"/me take a crate and load it to benson"}); g_binds.push_back({VK_F4,0,"/me examine sultan with mechanic tools"}); save(); }
}

void refresh(){ if(!g_list)return; EnterCriticalSection(&g_cs); SendMessageA(g_list,LB_RESETCONTENT,0,0); for(auto &b:g_binds){ std::string s=keyName(b.vk,b.mods)+"  ->  "+b.cmd; SendMessageA(g_list,LB_ADDSTRING,0,(LPARAM)s.c_str()); } LeaveCriticalSection(&g_cs); }

void sendCmd(const std::string& c){ if(!g_input||!readable((void*)g_input,4))return; auto fn=(SendFn)(g_base+SEND_COMMAND); if(readable((void*)fn,1)) fn(g_input,c.c_str()); }

LRESULT CALLBACK RapBindWndProc(HWND h, UINT m, WPARAM w, LPARAM l){
    switch(m){
    case WM_LBUTTONDOWN:
        if(LOWORD(l)<760 && HIWORD(l)<42){
            g_dragging=true; g_drag.x=LOWORD(l); g_drag.y=HIWORD(l); SetCapture(h); return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if(g_dragging){
            POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)}; RECT r; GetWindowRect(h,&r);
            SetWindowPos(h,nullptr,r.left+p.x-g_drag.x,r.top+p.y-g_drag.y,0,0,SWP_NOSIZE|SWP_NOZORDER);
            return 0;
        }
        break;
    case WM_LBUTTONUP: g_dragging=false; ReleaseCapture(); return 0;
    case WM_KEYDOWN: if(w==VK_ESCAPE){ShowWindow(h,SW_HIDE);return 0;} break;
    case WM_CLOSE: ShowWindow(h,SW_HIDE); return 0;
    case WM_COMMAND:
        if(LOWORD(w)==1001){
            int sel=(int)SendMessageA(g_list,LB_GETCURSEL,0,0);
            if(sel>=0&&sel<(int)g_binds.size()){
                char c[512]{}; GetWindowTextA(g_cmd,c,sizeof(c));
                if(*c){g_binds[sel].cmd=c;save();refresh();}
            }
        } else if(LOWORD(w)==1002){
            char c[512]{}; GetWindowTextA(g_cmd,c,sizeof(c));
            if(*c){g_binds.push_back({VK_F6,0,c});save();refresh();}
        } else if(LOWORD(w)==1003){
            int s=(int)SendMessageA(g_list,LB_GETCURSEL,0,0);
            if(s>=0){g_binds.erase(g_binds.begin()+s);save();refresh();}
        } else if(LOWORD(w)==1004){ShowWindow(h,SW_HIDE);}
        return 0;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORLISTBOX: case WM_CTLCOLOREDIT:{
        HDC dc=(HDC)w; SetTextColor(dc,RGB(235,235,235)); SetBkColor(dc,RGB(25,25,29));
        static HBRUSH br=CreateSolidBrush(RGB(25,25,29)); return (LRESULT)br;
    }
    return DefWindowProcA(h,m,w,l);
}

DWORD WINAPI uiThreadProc(LPVOID){
    WNDCLASSA wc{}; wc.lpfnWndProc=RapBindWndProc; wc.hInstance=g_inst;
    wc.lpszClassName="RapBindUI"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hbrBackground=CreateSolidBrush(RGB(18,18,22)); RegisterClassA(&wc);
    g_ui=CreateWindowExA(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,"RapBindUI","RapBind",WS_POPUP|WS_BORDER,180,120,760,470,nullptr,nullptr,g_inst,nullptr);
    CreateWindowA("STATIC","RapBind 0.3.DL",WS_CHILD|WS_VISIBLE,20,12,500,30,g_ui,nullptr,g_inst,nullptr);
    g_list=CreateWindowA("LISTBOX","",WS_CHILD|WS_VISIBLE|WS_BORDER|LBS_NOTIFY,20,55,720,270,g_ui,(HMENU)1000,g_inst,nullptr);
    g_cmd=CreateWindowA("EDIT","",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,20,340,720,34,g_ui,(HMENU)1005,g_inst,nullptr);
    CreateWindowA("BUTTON","EDIT SELECTED",WS_CHILD|WS_VISIBLE,20,390,150,34,g_ui,(HMENU)1001,g_inst,nullptr);
    CreateWindowA("BUTTON","ADD (F6)",WS_CHILD|WS_VISIBLE,180,390,120,34,g_ui,(HMENU)1002,g_inst,nullptr);
    CreateWindowA("BUTTON","DELETE",WS_CHILD|WS_VISIBLE,310,390,120,34,g_ui,(HMENU)1003,g_inst,nullptr);
    CreateWindowA("BUTTON","CLOSE",WS_CHILD|WS_VISIBLE,620,390,120,34,g_ui,(HMENU)1004,g_inst,nullptr);
    refresh(); ShowWindow(g_ui,SW_SHOW); SetForegroundWindow(g_ui);
    MSG msg; while(GetMessageA(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageA(&msg);} return 0;
}

void __cdecl cmdBind(const char*) { openUI(); }

bool registerCmd(){
    auto slot=(uintptr_t*)(g_base+INPUT_PTR); if(!readable(slot,4))return false; uintptr_t input=*slot; if(!input)return false;
    auto add=(AddCommandFn)(g_base+ADD_COMMAND); if(!readable((void*)add,1))return false; add(input,"bind",cmdBind); g_input=input; InterlockedExchange(&g_registered,1); return true;
}

DWORD WINAPI keyThread(LPVOID){ std::vector<bool> was(64); for(;;){ if(g_registered){ EnterCriticalSection(&g_cs); auto copy=g_binds; LeaveCriticalSection(&g_cs); for(auto &b:copy){ bool down=(GetAsyncKeyState(b.vk)&0x8000)!=0 && modsNow()==b.mods; static std::map<int,bool> prev; bool &w=prev[b.vk]; if(down&&!w) sendCmd(b.cmd); w=down; } } Sleep(12); } }

DWORD WINAPI initThread(LPVOID){ for(int i=0;i<300;i++){g_samp=GetModuleHandleA("samp.dll");if(g_samp)break;Sleep(100);} if(!g_samp)return 0; g_base=(uintptr_t)g_samp; char p[MAX_PATH]{};GetModuleFileNameA(nullptr,p,MAX_PATH);char*s=strrchr(p,'\\');if(s)*(s+1)=0;g_cfg=std::string(p)+"RapBind.ini";load(); Sleep(2500); for(int i=0;i<120&&!g_registered;i++){registerCmd();if(!g_registered)Sleep(250);} CreateThread(nullptr,0,keyThread,nullptr,0,nullptr); return 0; }
}

BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){ if(reason==DLL_PROCESS_ATTACH){g_inst=h;InitializeCriticalSection(&g_cs);DisableThreadLibraryCalls(h);CreateThread(nullptr,0,initThread,nullptr,0,nullptr);} return TRUE; }
