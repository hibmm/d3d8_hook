#include <windows.h>
#include <Shlwapi.h>
#include <iostream>
#include <vector>
#include <string>
#include <d3d8.h>
#include <d3dx8.h>
#include <strsafe.h>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "d3dx8.lib") 

// --- 导出转发 ---
#pragma comment(linker, "/EXPORT:Direct3DCreate8=_Fake_Direct3DCreate8@4,@5")
#pragma comment(linker, "/EXPORT:Direct3D8EnableMaximizedWindowedModeShim=_AheadLib_Direct3D8EnableMaximizedWindowedModeShim,@1")
#pragma comment(linker, "/EXPORT:ValidatePixelShader=_AheadLib_ValidatePixelShader,@2")
#pragma comment(linker, "/EXPORT:ValidateVertexShader=_AheadLib_ValidateVertexShader,@3")
#pragma comment(linker, "/EXPORT:DebugSetMute=_AheadLib_DebugSetMute,@4")

// --- 全局变量 ---
PVOID pfnAheadLib_Direct3D8EnableMaximizedWindowedModeShim;
PVOID pfnAheadLib_ValidatePixelShader;
PVOID pfnAheadLib_ValidateVertexShader;
PVOID pfnAheadLib_DebugSetMute;
PVOID pfnAheadLib_Direct3DCreate8;

static HMODULE g_OldModule = NULL;
static HMODULE g_hMyModule = NULL;

IDirect3D8* g_pCapturedD3D8 = nullptr;
IDirect3DDevice8* g_pCapturedD3DDevice8 = nullptr;

// --- 动画资源 ---
std::vector<IDirect3DTexture8*> g_AnimTextures;
ID3DXSprite* g_pSprite = nullptr;
bool g_bResourcesLoaded = false;

// 播放控制
UINT g_currentFrameIndex = 0;
DWORD g_lastFrameTime = 0;
const DWORD FRAME_DELAY = 100; // 100毫秒一帧

// --- 函数指针定义 ---
typedef HRESULT(WINAPI* PFN_OrigCreateDevice)(IDirect3D8*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice8**);
typedef HRESULT(WINAPI* PFN_OrigEndScene)(IDirect3DDevice8*);
typedef HRESULT(WINAPI* PFN_OrigReset)(IDirect3DDevice8*, D3DPRESENT_PARAMETERS*);

PFN_OrigCreateDevice g_pfnOrigCreateDevice = nullptr;
PFN_OrigEndScene g_pfnOrigEndScene = nullptr;
PFN_OrigReset g_pfnOrigReset = nullptr;

// --- 日志 ---
void DebugLog(const char* format, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, format);
    StringCchVPrintfA(buffer, 2048, format, args);
    va_end(args);
    OutputDebugStringA(buffer);
}

// --- 加载 PNG 序列 ---
void LoadImages(IDirect3DDevice8* pDevice)
{
    if (g_bResourcesLoaded) return;

    char szDllPath[MAX_PATH];
    GetModuleFileNameA(g_hMyModule, szDllPath, MAX_PATH);
    PathRemoveFileSpecA(szDllPath);

    int index = 1;
    while (true)
    {
        char szFilename[MAX_PATH];
        StringCchPrintfA(szFilename, MAX_PATH, "%s\\a_%d.png", szDllPath, index);

        if (GetFileAttributesA(szFilename) == INVALID_FILE_ATTRIBUTES) {
            StringCchPrintfA(szFilename, MAX_PATH, "%s\\a_%d.PNG", szDllPath, index);
            if (GetFileAttributesA(szFilename) == INVALID_FILE_ATTRIBUTES) {
                break;
            }
        }

        IDirect3DTexture8* pTexture = nullptr;
        if (SUCCEEDED(D3DXCreateTextureFromFileA(pDevice, szFilename, &pTexture))) {
            g_AnimTextures.push_back(pTexture);
        }
        index++;
    }

    g_bResourcesLoaded = true;
    g_lastFrameTime = GetTickCount();
    DebugLog("[Proxy] Loaded %d frames.", g_AnimTextures.size());
}

// --- 清理资源 ---
void CleanupResources()
{
    for (auto* pTex : g_AnimTextures) {
        if (pTex) pTex->Release();
    }
    g_AnimTextures.clear();

    if (g_pSprite) { g_pSprite->Release(); g_pSprite = nullptr; }
    g_bResourcesLoaded = false;
}

// --- Hooked Reset ---
HRESULT WINAPI Hooked_Reset(IDirect3DDevice8* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters)
{
    if (g_pSprite) { g_pSprite->Release(); g_pSprite = nullptr; }
    // 纹理是 D3DX 加载的 Managed 资源，通常不需要手动 Release
    return g_pfnOrigReset(pDevice, pPresentationParameters);
}

// --- Hooked EndScene (渲染循环) ---
HRESULT WINAPI Hooked_EndScene(IDirect3DDevice8* pDevice)
{
    // 1. 资源初始化
    if (!g_bResourcesLoaded) LoadImages(pDevice);
    if (g_pSprite == nullptr) D3DXCreateSprite(pDevice, &g_pSprite);

    // 2. 绘制动画
    if (g_pSprite && !g_AnimTextures.empty())
    {
        // 计时逻辑
        DWORD currentTime = GetTickCount();
        if (currentTime - g_lastFrameTime >= FRAME_DELAY) {
            g_currentFrameIndex = (g_currentFrameIndex + 1) % g_AnimTextures.size();
            g_lastFrameTime = currentTime;
        }

        if (g_currentFrameIndex < g_AnimTextures.size())
        {
            IDirect3DTexture8* pCurTex = g_AnimTextures[g_currentFrameIndex];
            if (pCurTex)
            {
                // A. 获取屏幕尺寸 (Viewport)
                D3DVIEWPORT8 vp;
                pDevice->GetViewport(&vp);

                // B. 获取图片尺寸
                D3DSURFACE_DESC desc;
                pCurTex->GetLevelDesc(0, &desc);

                // C. 计算居中位置 (正中间偏上 150 像素)
                // x = (屏幕宽 - 图片宽) / 2
                // y = (屏幕高 - 图片高) / 2 - 偏移量
                float x = (float)(vp.Width - desc.Width) / 2.0f;
                float y = (float)(vp.Height - desc.Height) / 2.0f - 150.0f;

                D3DXVECTOR2 pos(x, y);
                D3DXVECTOR2 scale(1.0f, 1.0f);

                // D. 保存渲染状态 (解决“太亮”的问题)
                // 游戏可能开启了光照、雾效或加法混合(Additive Blending)，这会导致图片过曝或半透明
                DWORD oldLighting, oldFog, oldAlphaBlend, oldSrcBlend, oldDestBlend;
                pDevice->GetRenderState(D3DRS_LIGHTING, &oldLighting);
                pDevice->GetRenderState(D3DRS_FOGENABLE, &oldFog);
                pDevice->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldAlphaBlend);
                pDevice->GetRenderState(D3DRS_SRCBLEND, &oldSrcBlend);
                pDevice->GetRenderState(D3DRS_DESTBLEND, &oldDestBlend);

                // E. 设置适合 2D 图片的渲染状态
                pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);         // 关闭光照
                pDevice->SetRenderState(D3DRS_FOGENABLE, FALSE);        // 关闭雾效
                pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);  // 开启混合
                pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA); // 标准透明混合
                pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

                // F. 绘制
                g_pSprite->Begin();
                // 最后一个参数是颜色调制。如果你还是觉得太亮，可以改小一点，比如 0xFFCCCCCC (淡灰)
                g_pSprite->Draw(pCurTex, NULL, &scale, NULL, 0.0f, &pos, 0xFFFFFFFF);
                g_pSprite->End();

                // G. 恢复渲染状态 (避免影响游戏画面)
                pDevice->SetRenderState(D3DRS_LIGHTING, oldLighting);
                pDevice->SetRenderState(D3DRS_FOGENABLE, oldFog);
                pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, oldAlphaBlend);
                pDevice->SetRenderState(D3DRS_SRCBLEND, oldSrcBlend);
                pDevice->SetRenderState(D3DRS_DESTBLEND, oldDestBlend);
            }
        }
    }

    return g_pfnOrigEndScene(pDevice);
}

// --- Hooks 安装 (保持不变) ---
void HookDeviceMethods(IDirect3DDevice8* pDevice)
{
    if (!pDevice) return;
    void** pVTable = *(void***)pDevice;
    int indexReset = 14;
    int indexEndScene = 35;

    if (pVTable[indexReset] != (void*)Hooked_Reset) {
        g_pfnOrigReset = (PFN_OrigReset)pVTable[indexReset];
        DWORD oldProtect;
        VirtualProtect(&pVTable[indexReset], sizeof(void*), PAGE_READWRITE, &oldProtect);
        pVTable[indexReset] = (void*)Hooked_Reset;
        VirtualProtect(&pVTable[indexReset], sizeof(void*), oldProtect, &oldProtect);
    }
    if (pVTable[indexEndScene] != (void*)Hooked_EndScene) {
        g_pfnOrigEndScene = (PFN_OrigEndScene)pVTable[indexEndScene];
        DWORD oldProtect;
        VirtualProtect(&pVTable[indexEndScene], sizeof(void*), PAGE_READWRITE, &oldProtect);
        pVTable[indexEndScene] = (void*)Hooked_EndScene;
        VirtualProtect(&pVTable[indexEndScene], sizeof(void*), oldProtect, &oldProtect);
    }
}

// --- CreateDevice Hook (保持不变) ---
HRESULT WINAPI Hooked_CreateDevice(IDirect3D8* pThis, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice8** ppReturnedDeviceInterface)
{
    if (!g_pfnOrigCreateDevice) return E_FAIL;
    HRESULT hr = g_pfnOrigCreateDevice(pThis, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);
    if (SUCCEEDED(hr) && ppReturnedDeviceInterface && *ppReturnedDeviceInterface) {
        g_pCapturedD3DDevice8 = *ppReturnedDeviceInterface;
        HookDeviceMethods(g_pCapturedD3DDevice8);
    }
    return hr;
}

// --- Fake Direct3DCreate8 (保持不变) ---
extern "C" IDirect3D8* WINAPI Fake_Direct3DCreate8(UINT SDKVersion)
{
    if (!pfnAheadLib_Direct3DCreate8) return nullptr;
    typedef IDirect3D8* (WINAPI* PFN_D3DCREATE8)(UINT);
    IDirect3D8* pD3D8 = ((PFN_D3DCREATE8)pfnAheadLib_Direct3DCreate8)(SDKVersion);
    if (pD3D8) {
        g_pCapturedD3D8 = pD3D8;
        void** pVTable = *(void***)pD3D8;
        int createDeviceIndex = 15;
        if (pVTable[createDeviceIndex] != (void*)Hooked_CreateDevice) {
            g_pfnOrigCreateDevice = (PFN_OrigCreateDevice)pVTable[createDeviceIndex];
            DWORD oldProtect;
            VirtualProtect(&pVTable[createDeviceIndex], sizeof(void*), PAGE_READWRITE, &oldProtect);
            pVTable[createDeviceIndex] = (void*)Hooked_CreateDevice;
            VirtualProtect(&pVTable[createDeviceIndex], sizeof(void*), oldProtect, &oldProtect);
        }
    }
    return pD3D8;
}

// --- DLL 基础 (保持不变) ---
VOID WINAPI Free() {
    CleanupResources();
    if (g_OldModule) { FreeLibrary(g_OldModule); g_OldModule = NULL; }
}
BOOL WINAPI Load() {
    TCHAR tzPath[MAX_PATH];
    if (GetSystemDirectory(tzPath, MAX_PATH) == 0) return FALSE;
    StringCchCat(tzPath, MAX_PATH, TEXT("\\d3d8.dll"));
    g_OldModule = LoadLibrary(tzPath);
    return (g_OldModule != NULL);
}
FARPROC WINAPI GetAddress(PCSTR pszProcName) { return GetProcAddress(g_OldModule, pszProcName); }
BOOL WINAPI Init() {
    pfnAheadLib_Direct3D8EnableMaximizedWindowedModeShim = GetAddress("Direct3D8EnableMaximizedWindowedModeShim");
    pfnAheadLib_ValidatePixelShader = GetAddress("ValidatePixelShader");
    pfnAheadLib_ValidateVertexShader = GetAddress("ValidateVertexShader");
    pfnAheadLib_DebugSetMute = GetAddress("DebugSetMute");
    pfnAheadLib_Direct3DCreate8 = GetAddress("Direct3DCreate8");
    return (pfnAheadLib_Direct3DCreate8 != NULL);
}
BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, PVOID pvReserved) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        g_hMyModule = hModule;
        DisableThreadLibraryCalls(hModule);
        if (!Load() || !Init()) return FALSE;
    }
    else if (dwReason == DLL_PROCESS_DETACH) {
        Free();
    }
    return TRUE;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_Direct3D8EnableMaximizedWindowedModeShim(void) { __asm jmp pfnAheadLib_Direct3D8EnableMaximizedWindowedModeShim }
EXTERN_C __declspec(naked) void __cdecl AheadLib_ValidatePixelShader(void) { __asm jmp pfnAheadLib_ValidatePixelShader }
EXTERN_C __declspec(naked) void __cdecl AheadLib_ValidateVertexShader(void) { __asm jmp pfnAheadLib_ValidateVertexShader }
EXTERN_C __declspec(naked) void __cdecl AheadLib_DebugSetMute(void) { __asm jmp pfnAheadLib_DebugSetMute }