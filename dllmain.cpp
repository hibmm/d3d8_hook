#include <windows.h>
#include <Shlwapi.h>
#include <iostream>
#include <d3d8.h>

#pragma comment( lib, "Shlwapi.lib")

#pragma comment(linker, "/EXPORT:Direct3D8EnableMaximizedWindowedModeShim=_AheadLib_Direct3D8EnableMaximizedWindowedModeShim,@1")
#pragma comment(linker, "/EXPORT:ValidatePixelShader=_AheadLib_ValidatePixelShader,@2")
#pragma comment(linker, "/EXPORT:ValidateVertexShader=_AheadLib_ValidateVertexShader,@3")
#pragma comment(linker, "/EXPORT:DebugSetMute=_AheadLib_DebugSetMute,@4")
#pragma comment(linker, "/EXPORT:Direct3DCreate8=_AheadLib_Direct3DCreate8,@5")


PVOID pfnAheadLib_Direct3D8EnableMaximizedWindowedModeShim;
PVOID pfnAheadLib_ValidatePixelShader;
PVOID pfnAheadLib_ValidateVertexShader;
PVOID pfnAheadLib_DebugSetMute;
PVOID pfnAheadLib_Direct3DCreate8;


static
HMODULE g_OldModule = NULL;

// 全局变量记录指针
IDirect3D8* g_pCapturedD3D8 = nullptr;

EXTERN_C IDirect3D8* WINAPI Hooked_Direct3DCreate8(UINT SDKVersion)
{
	typedef IDirect3D8* (WINAPI* OriginalFunc)(UINT);
	OriginalFunc pOriginal = (OriginalFunc)pfnAheadLib_Direct3DCreate8;

	// 调用原始函数并记录返回值
	g_pCapturedD3D8 = pOriginal(SDKVersion);

	std::cout << "The hook's getting Direct3D8 device pointer: 0x" << g_pCapturedD3D8 << std::endl;

	return g_pCapturedD3D8;
}

VOID WINAPI Free()
{
	if (g_OldModule)
	{
		FreeLibrary(g_OldModule);
	}
}


BOOL WINAPI Load()
{
	TCHAR tzPath[MAX_PATH];
	TCHAR tzTemp[MAX_PATH * 2];

	//
	// 这里是否从系统目录或当前目录加载原始DLL
	//
	//GetModuleFileName(NULL,tzPath,MAX_PATH); //获取本目录下的
	//PathRemoveFileSpec(tzPath);

	GetSystemDirectory(tzPath, MAX_PATH); //默认获取系统目录的

	lstrcat(tzPath, TEXT("\\d3d8.dll"));

	g_OldModule = LoadLibrary(tzPath);
	if (g_OldModule == NULL)
	{
		wsprintf(tzTemp, TEXT("无法找到模块 %s,程序无法正常运行"), tzPath);
		MessageBox(NULL, tzTemp, TEXT("AheadLib"), MB_ICONSTOP);
	}

	return (g_OldModule != NULL);

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
			wsprintfA(szProcName, "#%d", pszProcName);
			pszProcName = szProcName;
		}

		wsprintf(tzTemp, TEXT("无法找到函数 %hs,程序无法正常运行"), pszProcName);
		MessageBox(NULL, tzTemp, TEXT("AheadLib"), MB_ICONSTOP);
		ExitProcess(-2);
	}
	return fpAddress;
}

BOOL WINAPI Init()
{
	pfnAheadLib_Direct3D8EnableMaximizedWindowedModeShim = GetAddress("Direct3D8EnableMaximizedWindowedModeShim");
	pfnAheadLib_ValidatePixelShader = GetAddress("ValidatePixelShader");
	pfnAheadLib_ValidateVertexShader = GetAddress("ValidateVertexShader");
	pfnAheadLib_DebugSetMute = GetAddress("DebugSetMute");
	pfnAheadLib_Direct3DCreate8 = GetAddress("Direct3DCreate8");
	return TRUE;
}

DWORD WINAPI ThreadProc(LPVOID lpThreadParameter)
{
	HANDLE hProcess;

	PVOID addr1 = reinterpret_cast<PVOID>(0x00401000);
	BYTE data1[] = { 0x90, 0x90, 0x90, 0x90 };

	//
	// 绕过VMP3.x 的内存保护
	//
	hProcess = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, GetCurrentProcessId());
	if (hProcess)
	{
		WriteProcessMemory(hProcess, addr1, data1, sizeof(data1), NULL);

		CloseHandle(hProcess);
	}

	return 0;
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, PVOID pvReserved)
{
	if (dwReason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hModule);

		if (Load() && Init())
		{
			TCHAR szAppName[MAX_PATH] = TEXT("gc.exe");//请修改宿主进程名
			TCHAR szCurName[MAX_PATH];

			GetModuleFileName(NULL, szCurName, MAX_PATH);
			PathStripPath(szCurName);

			//是否判断宿主进程名
			if (StrCmpI(szCurName, szAppName) == 0)
			{
				std::cout << "宿主进程名匹配" << std::endl;
				//启动补丁线程或者其他操作
				HANDLE hThread = CreateThread(NULL, NULL, ThreadProc, NULL, NULL, NULL);
				if (hThread)
				{
					CloseHandle(hThread);
				}
			}
		}
	}
	else if (dwReason == DLL_PROCESS_DETACH)
	{
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
		push[esp + 4]; 将参数压栈（SDKVersion）
		call Hooked_Direct3DCreate8
		ret 4; 清理栈并返回
	}
}

