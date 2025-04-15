#include <windows.h>
#include <Shlwapi.h>
#include <iostream>
#include <d3d8.h>
//#include <cstdio> // For OutputDebugStringA / sprintf_s

#pragma comment( lib, "Shlwapi.lib")

// 导出转发函数
#pragma comment(linker, "/EXPORT:Direct3D8EnableMaximizedWindowedModeShim=_AheadLib_Direct3D8EnableMaximizedWindowedModeShim,@1")
#pragma comment(linker, "/EXPORT:ValidatePixelShader=_AheadLib_ValidatePixelShader,@2")
#pragma comment(linker, "/EXPORT:ValidateVertexShader=_AheadLib_ValidateVertexShader,@3")
#pragma comment(linker, "/EXPORT:DebugSetMute=_AheadLib_DebugSetMute,@4")
#pragma comment(linker, "/EXPORT:Direct3DCreate8=_AheadLib_Direct3DCreate8,@5")

// 原始D3D8函数指针
PVOID pfnAheadLib_Direct3D8EnableMaximizedWindowedModeShim;
PVOID pfnAheadLib_ValidatePixelShader;
PVOID pfnAheadLib_ValidateVertexShader;
PVOID pfnAheadLib_DebugSetMute;
PVOID pfnAheadLib_Direct3DCreate8;

static HMODULE g_OldModule = NULL;

// hook 全局变量
IDirect3D8* g_pCapturedD3D8 = nullptr;         // 捕获的D3D8接口指针
IDirect3DDevice8* g_pCapturedD3DDevice8 = nullptr; // 捕获的D3D8设备指针（目标接口）

// 定义原始CreateDevice函数的指针类型
typedef HRESULT(WINAPI* PFN_OrigCreateDevice)(IDirect3D8*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice8**);
PFN_OrigCreateDevice g_pfnOrigCreateDevice = nullptr; // 原始CreateDevice函数指针

// --- 调试输出工具函数 ---
void DebugLog(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
    va_end(args);
    // debug适用
    //OutputDebugStringA(buffer);
    //OutputDebugStringA("\n");
    // 控制台适用
     std::cout << buffer << std::endl;
}


// --- Hooked CreateDevice ---
HRESULT WINAPI Hooked_CreateDevice(
    IDirect3D8* pThis, // 调用该方法的IDirect3D8实例
    UINT Adapter,
    D3DDEVTYPE DeviceType,
    HWND hFocusWindow,
    DWORD BehaviorFlags,
    D3DPRESENT_PARAMETERS* pPresentationParameters,
    IDirect3DDevice8** ppReturnedDeviceInterface)
{
    DebugLog("Hooked_CreateDevice 被调用");

    // 调用原始函数
    if (!g_pfnOrigCreateDevice) {
        DebugLog("Error: 原始CreateDevice指针为空!");
        return E_FAIL;
    }

    HRESULT hr = g_pfnOrigCreateDevice(pThis, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);

    // 捕获设备指针
    if (SUCCEEDED(hr) && ppReturnedDeviceInterface && *ppReturnedDeviceInterface)
    {
        g_pCapturedD3DDevice8 = *ppReturnedDeviceInterface;
        DebugLog(">>> IDirect3DDevice8 指针已捕获: 0x%p", g_pCapturedD3DDevice8);
    }
    else {
        DebugLog("原始CreateDevice失败或返回空指针 HRESULT: 0x%X", hr);
    }

    return hr;
}


// --- Hooked Direct3DCreate8 ---
EXTERN_C IDirect3D8* WINAPI Hooked_Direct3DCreate8(UINT SDKVersion)
{
    DebugLog("Hooked_Direct3DCreate8 被调用 (SDKVersion: %u)", SDKVersion);

    typedef IDirect3D8* (WINAPI* OriginalFunc)(UINT);
    OriginalFunc pOriginal = (OriginalFunc)pfnAheadLib_Direct3DCreate8;

    IDirect3D8* pD3D8 = pOriginal(SDKVersion);

    if (pD3D8)
    {
        g_pCapturedD3D8 = pD3D8; // Store the interface pointer
        DebugLog("调用原始Direct3DCreate8成功。IDirect3D8指针: 0x%p", g_pCapturedD3D8);

        if (!g_pfnOrigCreateDevice)// 确保只hook一次
        {
            // hook虚表操作
            void** pVTable = *(void***)g_pCapturedD3D8;
            DebugLog("IDirect3D8 VTable address: 0x%p", pVTable);

            // createDevice 虚函数索引定义
            int createDeviceIndex = 15;

            // 保存CreateDevice的原始函数指针
            g_pfnOrigCreateDevice = (PFN_OrigCreateDevice)pVTable[createDeviceIndex];
            DebugLog("Original CreateDevice address: 0x%p", g_pfnOrigCreateDevice);

            // 修改内存保护属性并替换函数指针
            DWORD oldProtect;
            if (VirtualProtect(&pVTable[createDeviceIndex], sizeof(void*), PAGE_READWRITE, &oldProtect))
            {
                // 替换虚表中的CreateDevice函数指针
                pVTable[createDeviceIndex] = (void*)Hooked_CreateDevice;
                DebugLog("VTable patched successfully. Hooked_CreateDevice address: 0x%p", Hooked_CreateDevice);

                // 恢复原来的内存保护属性
                DWORD temp;
                VirtualProtect(&pVTable[createDeviceIndex], sizeof(void*), oldProtect, &temp);
            }
            else
            {
                // 处理错误
                DebugLog("Error: VirtualProtect failed to make VTable writable! Error code: %lu", GetLastError());
                // 恢复原始函数指针
                g_pfnOrigCreateDevice = nullptr;
            }
        }
        else {
            DebugLog("CreateDevice already hooked.");
        }
    }
    else {
        DebugLog("Original Direct3DCreate8 failed!");
    }

    return pD3D8; // 返回捕获的IDirect3D8指针
}



VOID WINAPI Free()
{
    if (g_OldModule)
    {
        FreeLibrary(g_OldModule);
        g_OldModule = NULL;
        DebugLog("Original d3d8.dll unloaded.");
    }

    g_pCapturedD3D8 = nullptr;
    g_pCapturedD3DDevice8 = nullptr;
    g_pfnOrigCreateDevice = nullptr; // 清空原始CreateDevice指针
}

BOOL WINAPI Load()
{
    TCHAR tzPath[MAX_PATH];
    TCHAR tzTemp[MAX_PATH * 2];

    GetSystemDirectory(tzPath, MAX_PATH);
    lstrcat(tzPath, TEXT("\\d3d8.dll"));

    g_OldModule = LoadLibrary(tzPath);
    if (g_OldModule == NULL)
    {
        wsprintf(tzTemp, TEXT("无法从%s加载原始d3d8.dll。错误代码：%lu"), tzPath, GetLastError());
        MessageBox(NULL, tzTemp, TEXT("Proxy Error"), MB_ICONSTOP);
        return FALSE;
    }
    DebugLog("已从%S加载原始d3d8.dll。", tzPath);
    return TRUE;
}

FARPROC WINAPI GetAddress(PCSTR pszProcName)
{
    FARPROC fpAddress;
    CHAR szProcName[64];
    TCHAR tzTemp[MAX_PATH];

    fpAddress = GetProcAddress(g_OldModule, pszProcName);
    if (fpAddress == NULL)
    {
        if (HIWORD(pszProcName) == 0)
        {
            wsprintfA(szProcName, "#%d", LOWORD(pszProcName));
            pszProcName = szProcName;
        }

        wsprintf(tzTemp, TEXT("无法在原始d3d8.dll中找到函数%hs"), pszProcName);
        MessageBox(NULL, tzTemp, TEXT("Proxy Error"), MB_ICONSTOP | MB_SYSTEMMODAL);
        return NULL;
    }
    return fpAddress;
}

BOOL WINAPI Init()
{
    pfnAheadLib_Direct3D8EnableMaximizedWindowedModeShim = GetAddress("Direct3D8EnableMaximizedWindowedModeShim");
    pfnAheadLib_ValidatePixelShader = GetAddress("ValidatePixelShader");
    pfnAheadLib_ValidateVertexShader = GetAddress("ValidateVertexShader");
    pfnAheadLib_DebugSetMute = GetAddress("DebugSetMute");
    pfnAheadLib_Direct3DCreate8 = GetAddress("Direct3DCreate8"); // Get original Direct3DCreate8

    
    if (!pfnAheadLib_Direct3D8EnableMaximizedWindowedModeShim ||
        !pfnAheadLib_ValidatePixelShader ||
        !pfnAheadLib_ValidateVertexShader ||
        !pfnAheadLib_DebugSetMute ||
        !pfnAheadLib_Direct3DCreate8)
    {
        DebugLog("无法获取一个或多个导出函数的地址。");
        return FALSE;
    }

    DebugLog("已获取原始函数指针。");
    return TRUE;
}

// --- 可选的线程过程（按需保留）---
DWORD WINAPI ThreadProc(LPVOID lpThreadParameter)
{
    HANDLE hProcess;
    PVOID addr1 = reinterpret_cast<PVOID>(0x00401000); // 注意硬编码地址的风险
    BYTE data1[] = { 0x90, 0x90, 0x90, 0x90 };

    DebugLog("ThreadProc已启动。尝试在0x%p进行内存修补...", addr1);

    hProcess = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, GetCurrentProcessId());
    if (hProcess)
    {
        SIZE_T bytesWritten = 0;
        if (WriteProcessMemory(hProcess, addr1, data1, sizeof(data1), &bytesWritten) && bytesWritten == sizeof(data1)) {
            DebugLog("内存修补成功.");
        }
        else {
            DebugLog("WriteProcessMemory失败！错误代码：%lu", GetLastError());
        }
        CloseHandle(hProcess);
    }
    else {
        DebugLog("OpenProcess失败！错误代码：%lu", GetLastError());
    }

    return 0;
}

// --- DllMain ---
BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, PVOID pvReserved)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        // 使用DebugLog记录初始信息，因为控制台可能尚未就绪
        DebugLog("Proxy d3d8.dll attached (PID: %lu).", GetCurrentProcessId());

        if (Load() && Init())
        {
            DebugLog("Proxy initialized successfully.");

            TCHAR szAppName[MAX_PATH] = TEXT("gc.exe"); // Target host process name
            TCHAR szCurName[MAX_PATH];
            GetModuleFileName(NULL, szCurName, MAX_PATH);
            PathStripPath(szCurName); // Get just the exe name

            // Optional: Check host process name
            if (StrCmpI(szCurName, szAppName) == 0)
            {
                DebugLog("Host process name matches (%S). Starting ThreadProc.", szAppName);
                // Start optional patch thread
                HANDLE hThread = CreateThread(NULL, 0, ThreadProc, NULL, 0, NULL);
                if (hThread)
                {
                    CloseHandle(hThread);
                }
                else {
                    DebugLog("Failed to create ThreadProc thread. Error code: %lu", GetLastError());
                }
            }
            else {
                DebugLog("Host process name (%S) does not match target (%S).", szCurName, szAppName);
            }
        }
        else {
            DebugLog("Proxy initialization failed!");
            // Optional: Unload the original DLL if Init failed after Load succeeded
            if (g_OldModule) FreeLibrary(g_OldModule);
            return FALSE; // Prevent DLL from staying loaded if init fails
        }
    }
    else if (dwReason == DLL_PROCESS_DETACH)
    {
        DebugLog("Proxy d3d8.dll detaching.");
        Free();
    }

    return TRUE;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_Direct3D8EnableMaximizedWindowedModeShim(void)
{
    __asm jmp pfnAheadLib_Direct3D8EnableMaximizedWindowedModeShim;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_ValidatePixelShader(void)
{
    __asm jmp pfnAheadLib_ValidatePixelShader;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_ValidateVertexShader(void)
{
    __asm jmp pfnAheadLib_ValidateVertexShader;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_DebugSetMute(void)
{
    __asm jmp pfnAheadLib_DebugSetMute;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_Direct3DCreate8(void)
{
    __asm {
        mov eax, [esp + 4]
        push eax
        call Hooked_Direct3DCreate8 
        ret 4 
    }
}