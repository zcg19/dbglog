#pragma  once
#include <windows.h>
#include <dbghelp.h>
#include <assert.h>
#include <list>


#pragma comment(lib, "dbghelp.lib")
#ifndef Assert
#define Assert assert
#endif


typedef struct StackInfo_t
{
	DWORD64 pc;
	DWORD64 ret;
	DWORD64 frame;     // eip, rip
	DWORD64 stack;     // esp, rsp.
	DWORD64 param[4];
}StackInfo_t;

typedef std::list<StackInfo_t> 
StackInfoList_t;


#ifdef _M_X64
static DWORD const WOW64_CS_32BIT = 0x23; // Wow64 32-bit code segment on Windows 2003
static DWORD const TLS_OFFSET = 0x1480;   // offsetof(ntdll!_TEB, TlsSlots) on Windows 2003
#pragma pack(4)
struct Wow64_SaveContext
{
	ULONG unknown1;
	WOW64_CONTEXT ctx;
	ULONG unknown2;
};
#pragma pack()

typedef BOOL     (WINAPI *PFUN_Wow64GetThreadContext)(HANDLE, WOW64_CONTEXT*);
typedef NTSTATUS (WINAPI *PFUN_NtQueryInformationThread)(HANDLE ThreadHandle, ULONG ThreadInformationClass, PVOID Buffer, ULONG Length, PULONG ReturnLength);
#endif


class CDbgSymbol
{
public:
	CDbgSymbol()
		: m_is_inited(FALSE)
	{}

	int  Init(LPCTSTR szSymPath)
	{
		m_sym_path = (LPCSTR)CT2A(szSymPath);
		return 0;
	}

	int  LoadSym(HANDLE hProcess, HANDLE hExeFile, LPCSTR szExeName, LPCSTR szModName, void * pModBase)
	{
		BOOL    bRet;
		DWORD64 nRet, nBaseOfMod = (ULONG_PTR)pModBase;

		if(!m_is_inited)
		{
			bRet = SymInitialize(hProcess, m_sym_path, FALSE); Assert(bRet);
			m_is_inited = TRUE;
		}

		nRet = SymLoadModule64(hProcess, hExeFile, szExeName, szModName, nBaseOfMod, 0);
		if(nRet != nBaseOfMod)
		{
			LOG("** sym load module failed -->%I64d, %I64x, %s\n", nRet, nBaseOfMod, szModName);
			//Assert(0);
		}
		return 0;
	}

	int  GetCurrentStack(HANDLE hProcess, HANDLE hThread, const CONTEXT & context, StackInfoList_t & lstStack, int nMaxDepth
		#ifdef _DEBUG
		, const char * szFunc
		#endif
		)
	{
		BOOL         bRet, bWow64 = FALSE;
		DWORD        nMachineType = 0;
		STACKFRAME64 frame = {0};
		CONTEXT      ctx   = {0};
		LPVOID       pctx  = 0;

		try
		{
			ctx  = context;
		}
		catch (...)
		{
			// stack based context may be missing later sections -- based on the flags
			Assert(0);
		}

		frame.AddrPC.Mode      = AddrModeFlat;
		frame.AddrFrame.Mode   = AddrModeFlat;
		frame.AddrStack.Mode   = AddrModeFlat;
		#ifdef _M_IX86
			nMachineType = IMAGE_FILE_MACHINE_I386;
			frame.AddrPC.Offset    = ctx.Eip;
			frame.AddrFrame.Offset = ctx.Ebp;
			frame.AddrStack.Offset = ctx.Esp;
		#elif  _M_X64
			nMachineType = IMAGE_FILE_MACHINE_AMD64;
			frame.AddrPC.Offset    = ctx.Rip;
			frame.AddrFrame.Offset = ctx.Rbp;
			frame.AddrStack.Offset = ctx.Rsp;

			// zcg+, 
			// 测试发现 64位下遍历 wow64的进程的堆栈还是有问题, 但是使用 32位跟踪就没有问题
			// 因此 64系统的 32位进程还是应该使用 32的 track进行跟踪. 
			// 
			// 下面的代码作用很小, 经常获取不到堆栈信息. 
			WOW64_CONTEXT ctx_wow64 = {0};
			ctx_wow64.ContextFlags = WOW64_CONTEXT_FULL;
			if(IsWow64Process(hProcess, &bWow64) && bWow64)
			{
				if(GetWow64ThreadContext(hProcess, hThread, ctx, &ctx_wow64))
				{
					pctx         = &ctx_wow64;
					nMachineType = IMAGE_FILE_MACHINE_I386;
					frame.AddrPC.Offset    = ctx_wow64.Eip;
					frame.AddrFrame.Offset = ctx_wow64.Ebp;
					frame.AddrStack.Offset = ctx_wow64.Esp;
				}
			}
		#else
			#error 'unsupported target platform...
		#endif

		pctx = &ctx;
		for(int i = 0; i < nMaxDepth; i++)
		{
			StackInfo_t si = {0};

			// Despite having GetModuleBase in the call to StackWalk it needs help for the first address
			//::SymGetModuleBase64(hProcess, frame.AddrPC.Offset);

			bRet = StackWalk64(nMachineType, hProcess, hThread, &frame, pctx, 0, ::SymFunctionTableAccess64, ::SymGetModuleBase64, 0);
			if(!bRet) break;
			if(frame.AddrFrame.Offset == 0 || frame.AddrReturn.Offset == 0) break; // last.

			Assert(frame.AddrPC.Offset);
			if(frame.Far || frame.Virtual)
			{
				//Assert(0);
				//LOG("!! far and virtual -->%d,%d\n", frame.Far, frame.Virtual);
			}

			si.pc    = frame.AddrPC.Offset;
			si.ret   = frame.AddrReturn.Offset;
			si.frame = frame.AddrFrame.Offset;
			si.stack = frame.AddrStack.Offset;
			memcpy(si.param, frame.Params, sizeof(si.param));
			lstStack.push_back(si);
		}

		if(lstStack.empty())
		{
			#ifdef _DEBUG
			const char * g_nostack_funcs[] = 
			{
				"RtlUserThreadStart", "LdrInitializeThunk", "NtContinue", "RtlInitializeExceptionChain", 
				"__ftol2", "_ftol2", 
			};

			for(int i = 0; i < _countof(g_nostack_funcs); i++)
			{
				if(stricmp(g_nostack_funcs[i], szFunc) == 0) return 0;
			}
			#endif
			//Assert(0);
		}

		return 0;
	}

	#ifdef _M_X64
	// 下面这段代码是从 nttrace中拷贝而来, 为了支持 2003/xp
	// Helper function to delay load Wow64GetThreadContext or emulate on W2K3
	BOOL GetWow64ThreadContext(HANDLE hProcess, HANDLE hThread, CONTEXT const & context, WOW64_CONTEXT * pContext64)
	{
		static HMODULE hKernel32 = GetModuleHandle(_T("kernel32.dll"));
		static PFUN_Wow64GetThreadContext fGetContext = (PFUN_Wow64GetThreadContext)GetProcAddress(hKernel32, "Wow64GetThreadContext");

		if(fGetContext)
		{
			// Vista And Above.
			return fGetContext(hThread, pContext64);
		}
		else if(context.SegCs == WOW64_CS_32BIT)
		{
			if(pContext64->ContextFlags & CONTEXT_CONTROL)
			{
				pContext64->Ebp    = (ULONG)context.Rbp;
				pContext64->Eip    = (ULONG)context.Rip;
				pContext64->SegCs  = context.SegCs;
				pContext64->EFlags = context.EFlags;
				pContext64->Esp    = (ULONG)context.Rsp;
				pContext64->SegSs  = context.SegSs;
			}

			if(pContext64->ContextFlags & CONTEXT_INTEGER)
			{
				pContext64->Edi    = (ULONG)context.Rdi;
				pContext64->Esi    = (ULONG)context.Rsi;
				pContext64->Ebx    = (ULONG)context.Rbx;
				pContext64->Edx    = (ULONG)context.Rdx;
				pContext64->Ecx    = (ULONG)context.Rcx;
				pContext64->Eax    = (ULONG)context.Rax;
			}
			return TRUE;
		}
		else
		{
			static HMODULE hNtdll = GetModuleHandle(_T("ntdll.dll"));
			static PFUN_NtQueryInformationThread fQueryThread = (PFUN_NtQueryInformationThread)GetProcAddress(hNtdll, "NtQueryInformationThread");
			ULONG_PTR threadInfo[6] = {0};
			if(fQueryThread && fQueryThread(hThread, 0, &threadInfo, sizeof(threadInfo), 0))
			{
				PVOID pTls = (PVOID*)(threadInfo[1]+TLS_OFFSET);
				Wow64_SaveContext saveContext = {0}, * pSaveContext = 0;
				if(ReadProcessMemory(hProcess, (char*)pTls+1, &pSaveContext, sizeof(pSaveContext), 0) && ReadProcessMemory(hProcess, pSaveContext, &saveContext, sizeof(saveContext), 0))
				{
					*pContext64 = saveContext.ctx;
					return TRUE;
				}
			}
		}

		return FALSE;
	}
	#endif


private:
	BOOL     m_is_inited;
	CStringA m_sym_path;
};
