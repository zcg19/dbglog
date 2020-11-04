#include <stdio.h>
#include <assert.h>

#include "trace.h"
#include "config.h"
#include "debug.h"

#include <psapi.h>


#pragma  comment(lib, "psapi.lib")
#define  Assert  assert
#define  LOG_DIR "d:\\mylog\\dbglog\\"


static  const UCHAR g_nBreakPointInt3 = 0xCC;
BOOL    CreateDebugProcess(LPTSTR szCmdLine, LPCTSTR szCurrentDir, LPPROCESS_INFORMATION ppi)
{
	STARTUPINFO si = { sizeof(si), 0};

	Assert(szCmdLine);
	LOG(">> create debug process -->%S\n", szCmdLine);
	return CreateProcess(
		0, 
		szCmdLine, 
		0, 0, FALSE, 
		DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_CONSOLE, 
		0, 
		szCurrentDir, 
		&si, 
		ppi
		);
}

int     WriteProcessMemoryEx(HANDLE hProcess, void * pAddr, LPCSTR pData, DWORD nLen)
{
	BOOL   bRet;
	DWORD  nOldProtect = 0, nCurProtect = PAGE_READWRITE;
	SIZE_T nWrited = 0;

	bRet = VirtualProtectEx(hProcess, pAddr, 1, nCurProtect, &nOldProtect); Assert(bRet);
	bRet = WriteProcessMemory(hProcess, pAddr, pData, nLen, &nWrited); Assert(bRet && nWrited == nLen);
	bRet = VirtualProtectEx(hProcess, pAddr, 1, nOldProtect, &nCurProtect); Assert(bRet);

	return 0;
}

int     GetLastErrorEx(int nDefault = -1)
{
	int nErr = GetLastError();
	return nErr ? nErr : nDefault;
}

size_t  GetProcessImageSize(HANDLE hProcess, void * pBaseAddress)
{
	size_t  nModSize = 0;
	while(1)
	{
		MEMORY_BASIC_INFORMATION mbi = {0};
		if(!VirtualQueryEx(hProcess, (char*)pBaseAddress+nModSize, &mbi, sizeof(mbi))) break;
		if(mbi.AllocationBase != pBaseAddress) break;
		nModSize += mbi.RegionSize; Assert(mbi.Type == MEM_IMAGE);
	}

	return nModSize;
}

int     ResolveDosDevicePath(LPTSTR szPath, int nSize)
{
	typedef struct DosDeviceInfo_t
	{
		TCHAR szDosDevice[128];
		TCHAR szDrive[3];
	}DosDeviceInfo_t;
	static std::list<DosDeviceInfo_t> g_maps;
	int    nAgain = 1;

	if(szPath[0] != '\\') return 0;

AGAIN_ResolveDosDevicePath:
	for(std::list<DosDeviceInfo_t>::iterator it = g_maps.begin(); it != g_maps.end(); it++)
	{
		if(_tcsstr(szPath, it->szDosDevice))
		{
			memcpy (szPath, it->szDrive, sizeof(TCHAR)*2);
			memmove(szPath+2, szPath+_tcslen(it->szDosDevice), sizeof(TCHAR)*(_tcslen(szPath)-_tcslen(it->szDosDevice)+1));
			return 0;
		}
	}

	if(nAgain)
	{
		TCHAR szDrives[512] = {0}, *p = 0;

		g_maps.clear(); nAgain = 0;
		if(GetLogicalDriveStrings(_countof(szDrives)-1, szDrives))
		{
			p = szDrives; 
			while(*p)
			{
				DosDeviceInfo_t di = {0};
				di.szDrive[0] = *p; di.szDrive[1] = ':'; p += _tcslen(p)+1;

				if(QueryDosDevice(di.szDrive, di.szDosDevice, _countof(di.szDosDevice))) g_maps.push_back(di);
			}
			goto AGAIN_ResolveDosDevicePath;
		}
	}

	return 0;
}

int     GetFileNameByHandle(HANDLE hFile, TCHAR * szFilePath, int nFileSize)
{
	DWORD  nFileSizeH = 0, nFileSizeL = 0, nRet;
	HANDLE hFileMap;

	nFileSizeL = GetFileSize(hFile, &nFileSizeH); Assert(nFileSizeL != INVALID_FILE_SIZE);
	hFileMap   = CreateFileMapping(hFile, 0, PAGE_READONLY, 0, 1, 0);  Assert(hFileMap);
	if(hFileMap)
	{
		void * pMemFile = 0;

		pMemFile = MapViewOfFile(hFileMap, FILE_MAP_READ, 0, 0, 1); Assert(pMemFile);
		if(pMemFile)
		{
			nRet = GetMappedFileName(GetCurrentProcess(), pMemFile, szFilePath, nFileSize); Assert(nRet > 0);
			if(nRet > 0) ResolveDosDevicePath(szFilePath, nFileSize);
			UnmapViewOfFile(pMemFile);
		}

		CloseHandle(hFileMap);
	}

	return 0;
}

int     GetDllExportFunctions(HANDLE hFile, void * pModuleBaseAddress, FunctionInfoList_t & lstFunc)
{
	CImagePeDump pd;
	pd.Parser(hFile, pModuleBaseAddress);
	pd.GetExportFunctions(lstFunc);
	return -1;
}


// ============================================================================
CDebuger::CDebuger()
	: m_id(0)
	, m_is_first_bp(true)
	, m_max_stack_depth(3)
	, m_cfg(0)
	, m_sym(0)
{}

int  CDebuger::Init(CDbgConfig * pCfg, CDbgSymbol * pSym)
{
	extern void InitStructTableEx();
	m_cfg = pCfg; m_sym = pSym;
	InitStructTableEx();
	return 0;
}

int  CDebuger::Start(LPCTSTR szExePath, LPCTSTR szParam)
{
	BOOL      nRet;
	LPCTSTR   p = 0;
	CString   strCmdLine = szExePath;
	PROCESS_INFORMATION pi = {0};

	Assert(m_cfg);
	Assert(m_sym);
	Assert(szExePath && *szExePath);

	p     = _tcsrchr(szExePath, '\\');
	if(!p)  p  = _tcsrchr(szExePath, '/');
	m_exe_name = (LPCSTR)CT2A(p ? p+1 : szExePath);

	if(szParam && *szParam)
	{
		strCmdLine += _T(" "); strCmdLine += szParam;
	}

	nRet  = m_cfg->Load(CA2T(m_exe_name.c_str())); Assert(!nRet);
	nRet  = CreateDebugProcess(strCmdLine.GetBuffer(), 0, &pi); Assert(nRet);
	LOG(">> create debug process -->%x,%x,%p,%p, %S\n", pi.dwProcessId, pi.dwThreadId, pi.hProcess, pi.hThread, (LPCTSTR)strCmdLine);

	while(1)
	{
		nRet = DebugLoop(&pi);
		if(nRet)
		{
			LOG("** notice, loop break -->%d(%s)\n", nRet, nRet == 1 ? "exit":"error");
			break;
		}
	}
	return 0;
}


int  CDebuger::DebugLoop(PROCESS_INFORMATION * ppi)
{
	int         nRet, nStatus = DBG_CONTINUE;
	DEBUG_EVENT ev = {0};

	nRet = WaitForDebugEvent(&ev, INFINITE);
	if(!nRet) return GetLastErrorEx();

	nRet = DispatchDebugEvent(ppi, &ev, &nStatus);
	if(nRet)  return nRet;

	nRet = ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, nStatus);
	if(!nRet) return GetLastErrorEx();

	return 0;
}

int  CDebuger::DispatchDebugEvent(PROCESS_INFORMATION * ppi, DEBUG_EVENT * pev, int * ps)
{
	#define DEBUG_EVENT_TO_FUNCTION(_V) \
		_V(EXCEPTION_DEBUG_EVENT,      OnDebugEventException) \
		_V(LOAD_DLL_DEBUG_EVENT,       OnDebugEventLoadDll) \
		_V(UNLOAD_DLL_DEBUG_EVENT,     OnDebugEventUnloadDll) \

	#define DISPATCH_DEBUG_EVENT_CASE(_code, _func) \
		case _code: _func(ppi, pev, ps); break;

	switch(pev->dwDebugEventCode)
	{
		DEBUG_EVENT_TO_FUNCTION(DISPATCH_DEBUG_EVENT_CASE)
	case EXIT_PROCESS_DEBUG_EVENT:
		return 1;

	case CREATE_THREAD_DEBUG_EVENT:
		m_threads[pev->dwThreadId].handle     = pev->u.CreateThread.hThread;
		m_threads[pev->dwThreadId].func       = (void*)pev->u.CreateThread.lpStartAddress;
		m_threads[pev->dwThreadId].stack_base = pev->u.CreateThread.lpThreadLocalBase;
		LOG(">> create  thread -->%d, %p\n", pev->dwThreadId, pev->u.CreateThread.hThread);
		break;

	case EXIT_THREAD_DEBUG_EVENT:
		Assert(m_threads.end() != m_threads.find(pev->dwThreadId));
		m_threads.erase(pev->dwThreadId);
		LOG(">> destroy thread -->%d\n", pev->dwThreadId);
		break;

	case CREATE_PROCESS_DEBUG_EVENT:
		// 这里的进程的两个句柄不同, 但是好像都可以操作.
		Assert(ppi->hProcess != pev->u.CreateProcessInfo.hProcess);
		m_threads[pev->dwThreadId].handle     = pev->u.CreateProcessInfo.hThread;
		m_threads[pev->dwThreadId].func       = (void*)pev->u.CreateProcessInfo.lpStartAddress;
		m_threads[pev->dwThreadId].stack_base = pev->u.CreateProcessInfo.lpThreadLocalBase;
		{
			DllInfo_t  di;
			di.name         = m_exe_name;
			di.bp_count     = 0;
			di.file_handle  = pev->u.CreateProcessInfo.hFile;
			di.base         = pev->u.CreateProcessInfo.lpBaseOfImage;
			di.size         = GetProcessImageSize(ppi->hProcess, di.base);
			m_dlls[di.base] = di;
			m_sym->LoadSym(ppi->hProcess, 0, m_exe_name.c_str(), di.name.c_str(), di.base);
			LOG("** create process -->%p, %d, %s\n", di.base, di.size, di.name.c_str());
		}
		break;

	case OUTPUT_DEBUG_STRING_EVENT:
	case RIP_EVENT:
		break;

	default:
		break;
	}

	return 0;
}

int  CDebuger::OnDebugEventException(PROCESS_INFORMATION * ppi, DEBUG_EVENT * pev, int * ps)
{
	int  nRet = 0;

	switch(pev->u.Exception.ExceptionRecord.ExceptionCode)
	{
	case EXCEPTION_BREAKPOINT:
		*ps  = DBG_CONTINUE;
		nRet = OnDebugEventBreakpoint(ppi, pev, ps);
		break;

	case EXCEPTION_SINGLE_STEP:
		*ps  = DBG_CONTINUE;
		nRet = OnDebugEventStepBreakpoint(ppi, pev, ps);
		break;

	case DBG_CONTROL_C:
		DebugSetProcessKillOnExit(false);
		break;

	case 0xE06D7363:
	case 0x406D1388:
		*ps  = DBG_EXCEPTION_NOT_HANDLED;
		LOG("!! unknown exception, no handle -->%8x, %p\n", pev->u.Exception.ExceptionRecord.ExceptionCode, pev->u.Exception.ExceptionRecord.ExceptionAddress);
		break;

	case EXCEPTION_ACCESS_VIOLATION:
		{
			CONTEXT            ctx;
			StackInfoList_t    lstStack;
			StackInfoExList_t  lstStackEx;
			std::string        strLog;

			ctx.ContextFlags = CONTEXT_FULL;
			Assert(m_threads.find(pev->dwThreadId) != m_threads.end());
			nRet = GetThreadContext(m_threads[pev->dwThreadId].handle, &ctx); Assert(nRet);

			m_sym->GetCurrentStack(ppi->hProcess, m_threads[pev->dwThreadId].handle, ctx, lstStack, 8
				#ifdef _DEBUG
				, "???"
				#endif
				);
			ParserAndTraceStack(0, lstStack, lstStackEx, strLog);

			*ps  = DBG_EXCEPTION_NOT_HANDLED; nRet = 0;
			LOG("** falt exception ....%p (%s)\n", pev->u.Exception.ExceptionRecord.ExceptionAddress, strLog.c_str());
		}
		break;

	case EXCEPTION_STACK_OVERFLOW:
	case EXCEPTION_DATATYPE_MISALIGNMENT:
	default:
		*ps  = DBG_EXCEPTION_NOT_HANDLED;
		break;
	}

	// **************************************************
	// 被删除的断点在此处重新设置
	// 因此可能会有一些情况被漏掉
	// 看来比较好的做法是hook, 而不是恢复断点再设置

	return nRet;
}

int  CDebuger::OnDebugEventLoadDll(PROCESS_INFORMATION * ppi, DEBUG_EVENT * pev, int * ps)
{
	LOAD_DLL_DEBUG_INFO * pDllInfo;
	DllInfo_t             di;
	BOOL                  bSame = FALSE;

	pDllInfo = &pev->u.LoadDll;
	Assert(pDllInfo->lpBaseOfDll && pDllInfo->hFile);

	if(pDllInfo->lpImageName)
	{
		DWORD  nStep = 1, nEnd = 0; SIZE_T nReaded = 0;
		void * pNameAddress = 0;
		ReadProcessMemory(ppi->hProcess, pDllInfo->lpImageName, &pNameAddress, sizeof(void*), &nReaded);
		if(pNameAddress)
		{
			CHAR  szDllName[MAX_PATH*2]  = {0};
			if(pDllInfo->fUnicode) nStep = 2;
			for(int i = 0; i < sizeof(szDllName); i += nStep)
			{
				ReadProcessMemory(ppi->hProcess, (char*)pNameAddress+i, szDllName+i, nStep, &nReaded); Assert(nReaded == nStep);
				if(memcmp(szDllName+i, &nEnd, nStep) == 0) break;
			}
			if(pDllInfo->fUnicode) di.path = (LPCSTR)CW2A((WCHAR*)szDllName);
			else  di.path = szDllName;
		}
	}

	if(di.path.empty())
	{
		TCHAR szDllPath[MAX_PATH] = {0};

		//GetMappedFileName(ppi->hProcess, pDllInfo->lpBaseOfDll, szPath, _countof(szPath));
		GetFileNameByHandle(pDllInfo->hFile, szDllPath, _countof(szDllPath));
		if(szDllPath[0]) di.path = (LPCSTR)CW2A(szDllPath);
	}

	if(!di.path.empty())
	{
		const char * p = strrchr(di.path.c_str(), '\\');
		if(!p) p = strrchr(di.path.c_str(), '/');
		di.name  = p ? p+1 : di.path.c_str();
	}

	di.base        = pDllInfo->lpBaseOfDll;
	di.file_handle = pDllInfo->hFile;
	di.size        = GetProcessImageSize(ppi->hProcess, pDllInfo->lpBaseOfDll);
	di.bp_count    = 0;

	Assert(di.base && di.size);
	Assert(!di.name.empty());
	for(LoadDllList_t::iterator it = m_dlls.begin(); it != m_dlls.end(); it++)
	{
		if(stricmp(it->second.name.c_str(), di.name.c_str()) == 0)
		{
			bSame = it->second.base == di.base;
			LOG("** duplication loaded dll name.....%d(%p)\n", bSame, di.base);
			if(bSame)  break;
		}
	}

	if(!bSame)
	{
		m_dlls[pDllInfo->lpBaseOfDll] = di;
		m_sym->LoadSym(ppi->hProcess, 0, m_exe_name.c_str(), di.name.c_str(), pDllInfo->lpBaseOfDll);

		if(IsDllNeedSetBreakpoint(di.name.c_str()))
		{
			SetDllBreakpoint(ppi->hProcess,  &di, pDllInfo);
		}

		// 此处 dump的是加断点后的内存映像. 
		if(m_cfg->IsDumpModules()) DumpDllImage(ppi->hProcess, di.name.c_str());

	}
	LOG(">> load dll -->0x%08p, 0x%06x, %4d, %s\n", di.base, di.size, di.bp_count, di.path.c_str());
	return 0;
}

int  CDebuger::OnDebugEventUnloadDll(PROCESS_INFORMATION * ppi, DEBUG_EVENT * pev, int * ps)
{
	UNLOAD_DLL_DEBUG_INFO * pDllInfo;
	BOOL  bUnknown = FALSE;

	pDllInfo = &pev->u.UnloadDll;

	Assert(pDllInfo->lpBaseOfDll);
	LoadDllList_t::iterator it = m_dlls.find(pDllInfo->lpBaseOfDll);
	bUnknown = it == m_dlls.end();
	LOG(">> unload dll -->(%d), %p, %s\n", bUnknown, pDllInfo->lpBaseOfDll, bUnknown ? "???" : it->second.name.c_str());

	if(bUnknown) return 1;
	UnsetDllBreakpoint(ppi->hProcess, it->second.base);
	m_dlls.erase(it);

	return 0;
}

int  CDebuger::OnDebugEventBreakpoint(PROCESS_INFORMATION * ppi, DEBUG_EVENT * pev, int * ps)
{
	DWORD                          bRet, nRet, bLog = 0; SIZE_T nReaded;
	void                         * pAddress;
	ThreadList_t::iterator         ith;
	BreakpointList_t::iterator     itb;
	BreakpointRetList_t::iterator  itbr;

	pAddress = pev->u.Exception.ExceptionRecord.ExceptionAddress;
	if(m_is_first_bp)
	{
		m_is_first_bp = false;
		//Assert(m_lstbp.find(pAddress) == m_lstbp.end());
		//return 0;
	}

	ith  = m_threads.find(pev->dwThreadId); Assert(ith != m_threads.end());
	itb  = m_lstbp.find(pAddress);
	itbr = m_lstbpr.find(pAddress);
	if(itb != m_lstbp.end())
	{
		StackInfoList_t    lstStack;
		StackInfoExList_t  lstStackEx;
		CONTEXT            ctx;
		BreakpointRet_t    bpr = {0};
		void             * pReturnAddress = 0;
		std::string        strLog;
		StackInfoEx_t    * pStackEx = 0;
		unsigned char      szCode[8] = {0};

		ctx.ContextFlags = CONTEXT_FULL;
		bRet = GetThreadContext(ith->second.handle, &ctx); Assert(bRet);

		Assert(itb->second.enable);
		m_sym->GetCurrentStack(ppi->hProcess, ith->second.handle, ctx, lstStack, m_max_stack_depth+1
			#ifdef _DEBUG
			, itb->second.function.c_str()
			#endif
			);
		if(ParserAndTraceStack(&itb->second, lstStack, lstStackEx, strLog))
		{
			Assert(!lstStackEx.empty()); bLog = true;
		}

		if(!lstStackEx.empty()) pStackEx = &lstStackEx.front();
		if(m_cfg->LogLevel() > 0 || bLog)
		{
			if(pStackEx && !pStackEx->dll.empty())
			{
				Assert(lstStackEx.front().addr > (char*)lstStackEx.front().base+8);
				ReadProcessMemory(ppi->hProcess, (char*)lstStackEx.front().addr-8, szCode, sizeof(szCode), &nReaded); Assert(nReaded == sizeof(szCode));
			}

			LOG(">> trace%d -->[%d], 0x%p, 0x%p, %02x%02x%02x%02x%02x%02x%02x%02x, %s!%s, %s\n", 
				bLog, pev->dwThreadId, pAddress, !lstStackEx.empty() ? lstStackEx.front().addr:0, 
				szCode[0], szCode[1], szCode[2], szCode[3], szCode[4], szCode[5], szCode[6], szCode[7], 
				itb->second.dll.c_str(), itb->second.function.c_str(), strLog.c_str());

			#ifdef _M_IX86
			extern int  DbgLog_FunctionIn32(const char * szFunction, void * pRegStack_Param1, void *);
			char * esp = (char*)ctx.Esp; esp += sizeof(esp);
			DbgLog_FunctionIn32(itb->second.function.c_str(), (void*)esp, ppi->hProcess);
			#elif  _M_X64
			extern  int  DbgLog_FunctionIn64(const char * function_name, void * reg_stack_to_param0, void * user/*handle_process*/, DWORD64 param_addr[6]);
			char  * rsp = (char*)ctx.Rsp; rsp += sizeof(rsp);
			DWORD64 param[4] = { ctx.Rcx, ctx.Rdx, ctx.R8, ctx.R9, };
			DbgLog_FunctionIn64(itb->second.function.c_str(), (void*)rsp, ppi->hProcess, param);
			#else
			Assert(0);
			#endif
		}

		// 1 首先恢复这个断点, 恢复 EIP
		//   再设置返回断点.  -->在返回断点中可以解析出返回值和返回参数. 
		//   ------
		//   引申另一个问题, 假如返回断点不是这个线程的话呢, 那么需要恢复再设置 ???
		// 2 还有一种方法是设置时将源代码拷贝到内存中, 在此处修改 EIP以执行内存. 
		//   这样的话不需要删除再恢复这个断点
		//   这个和 hook就一样了, 需要注意几个指令的处理: 
		//   -- 相对地址, 现在知道的需要处理的有: E8(CALL), E9(JMP), EB(JMP)
		//      JMP 指令, 这个就不需要执行后处理了
		//      RET 指令, 这个
		// 3 最简单的还是设置单步模式. 
		// 
		itb->second.enable    = 0;
		if(lstStackEx.empty())
		{
			LOG("!! no stack, breakpoint be deleted -->%p, %s\n", pAddress, itb->second.function.c_str());
			m_lstbpd.insert(pAddress);
			itb->second.enable = 2;
		}
		else if(pStackEx->dll.empty())
		{
			LOG("!! dll name is empty, breakpoint be deleted -->%p, %p, %s\n", pStackEx->addr, pAddress, itb->second.function.c_str());
			m_lstbpd.insert(pAddress);
			itb->second.enable = 2;
		}

		ContinueBreakpoint(ppi->hProcess, ith->second.handle, itb->second.address, itb->second.source, itb->second.length, itb->second.enable == 2);
		if(itb->second.enable == 2) return 0;

		bpr.address           = pStackEx->addr;
		itbr = m_lstbpr.find(bpr.address);
		if(itbr != m_lstbpr.end())
		{
			for(int i = 0; i < _countof(itbr->second.thread_id); i++)
			{
				if(itbr->second.thread_id[i] == pev->dwThreadId) break;
				if(!itbr->second.thread_id[i])
				{
					itbr->second.thread_id[i] = pev->dwThreadId;
					break;
				}
			}
			return 0;
		}

		bpr.callee_address    = itb->second.address;
		bpr.enable            = 1;
		bpr.length            = sizeof(bpr.length);
		bpr.log               = (char)bLog;
		memset(bpr.thread_id, 0, sizeof(bpr.thread_id));
		memset(bpr.source, 0, sizeof(bpr.source));
		bpr.thread_id[0]      = pev->dwThreadId;
		nRet = SetBreakpoint(ppi->hProcess, bpr.address, bpr.source, bpr.length, TRUE); Assert(!nRet);
		m_lstbpr[bpr.address] = bpr;
		return 0;
	}

	if(itbr == m_lstbpr.end())
	{
		// 这儿可能会进来, 不处理 ???
		DllInfo_t di;
		for(LoadDllList_t::iterator it = m_dlls.begin(); it != m_dlls.end(); it++)
		{
			if(pAddress >= it->second.base && pAddress <= (char*)it->second.base+it->second.size)
			{
				di = it->second;
				break;
			}
		}

		// LdrpDoDebuggerBreak函数中有个断点
		MEMORY_BASIC_INFORMATION mbi = {0};
		nRet = (DWORD)VirtualQueryEx(ppi->hProcess, pAddress, &mbi, sizeof(mbi));
		LOG("?? unknown breakpoint1 --->%p, %p, %s (??????)\n", pAddress, di.base, di.name.c_str());
		//Assert(0);
		return 0;
	}

	if(itbr != m_lstbpr.end())
	{
		// 此处就把它删除就可以了. 如果非线程
		int  nFind = 0;

		//LOG(">> trace2 -->[%d], 0x%p, 0x%p, \n", pev->dwThreadId, pAddress, itbr->second.callee_address);
		for(int i = 0; i < _countof(itbr->second.thread_id); i++)
		{
			if(itbr->second.thread_id[i] == pev->dwThreadId)
			{
				nFind = 1; itbr->second.thread_id[i] = 0;
				break;
			}
		}

		itb  = m_lstbp.find(itbr->second.callee_address); Assert(itb != m_lstbp.end());
		nRet = SetBreakpoint(ppi->hProcess, itb->second.address,  itb->second.source,  itb->second.length,  TRUE);  Assert(!nRet);
		nRet = ContinueBreakpoint(ppi->hProcess, ith->second.handle, itbr->second.address, itbr->second.source, itbr->second.length, FALSE); Assert(!nRet);
		m_lstbpr.erase(itbr);  itb->second.enable = 1;
	}

	return 0;
}

int  CDebuger::OnDebugEventStepBreakpoint(PROCESS_INFORMATION * ppi, DEBUG_EVENT * pev, int * ps)
{
	//Assert(!m_lstbpd.empty());
	for(BreakpointDelList_t::iterator it = m_lstbpd.begin(); it != m_lstbpd.end(); it++)
	{
		Assert(m_lstbp.find(*it) != m_lstbp.end());
		m_lstbp[*it].enable = 1;
		SetBreakpoint(ppi->hProcess, *it, 0, 0, TRUE);
	}
	return 0;
}


int  CDebuger::SetDllBreakpoint(HANDLE hProcess, DllInfo_t * pInfo, LOAD_DLL_DEBUG_INFO * pDllInfo)
{
	int                nCount = 0, nRet;
	FunctionInfoList_t funcs;
	GetDllExportFunctions(pDllInfo->hFile, pDllInfo->lpBaseOfDll, funcs);

	if(m_cfg->IsDumpFunctions()) DumpFunctions(funcs, pInfo, 0);

	for(FunctionInfoList_t::iterator it = funcs.begin(); it != funcs.end(); it++)
	{
		// 如果没有符号(函数名称)就不关心了. 
		// 符号地址有重复, 例如 ntdll.dll
		if(it->name.empty()) continue;
		if(!IsFunctionNeedSetBreakpoint(it->name.c_str())) continue;
		if(m_lstbp.find(it->addr) != m_lstbp.end()) continue;

		MEMORY_BASIC_INFORMATION mbi = {0};
		nRet  = (int)VirtualQueryEx(hProcess, it->addr, &mbi, sizeof(mbi)); Assert(nRet > 0);
		if(mbi.Protect != PAGE_EXECUTE && mbi.Protect != PAGE_EXECUTE_READ && mbi.Protect != PAGE_EXECUTE_READWRITE && mbi.Protect != PAGE_EXECUTE_WRITECOPY)
		{
			LOG("?? not function -->%d, %p, %s!%s\n", mbi.Protect, it->addr, pInfo->name.c_str(), it->name.c_str());
			continue;
		}

		Breakpoint_t bp;
		bp.address   = it->addr;
		bp.base      = pInfo->base;
		bp.dll       = pInfo->name;
		bp.function  = it->name;
		bp.id        = m_id++;
		bp.length    = sizeof(bp.source);
		bp.enable    = 1;
		memset(bp.source, 0, sizeof(bp.source));
		nRet = SetBreakpoint(hProcess, bp.address, bp.source, bp.length, TRUE); Assert(!nRet);
		DumpBreakpoint(&bp);

		m_lstbp[bp.address] = bp; nCount++;
	}

	pInfo->bp_count = nCount;
	return 0;
}

int  CDebuger::UnsetDllBreakpoint(HANDLE hProcess, void * pDllBase)
{
	for(BreakpointList_t::iterator it = m_lstbp.begin(); it != m_lstbp.end();)
	{
		if(it->second.base == pDllBase)
		{
			BreakpointList_t::iterator itd = it++;
			//SetBreakpoint(hProcess, itd->second.address, itd->second.source, itd->second.length, FALSE);
			m_lstbp.erase(itd); continue;
		}

		it++;
	}

	return 0;
}

int  CDebuger::SetBreakpoint(HANDLE hp, void * pAddress, char * szSrcCode, int nSize, BOOL bSet)
{
	DWORD nRet; SIZE_T nReaded = 0;

	if(bSet)
	{
		if(nSize > 0)
		{
			nRet = ReadProcessMemory(hp, pAddress, szSrcCode, nSize, &nReaded); Assert(nReaded == nSize);
			if(((unsigned char)szSrcCode[0]) == 0xcc)
			{
				BreakpointList_t::iterator it = m_lstbp.find(pAddress);
				LOG("** already be setted!!! (%p),(%d)\n", pAddress, it != m_lstbp.end());
				/*Assert(0);*/ return 0;
			}
		}

		nRet = WriteProcessMemoryEx(hp, pAddress, (char*)&g_nBreakPointInt3, 1);
	}
	else
	{
		Assert(szSrcCode[0] != 0xcc);
		nRet = WriteProcessMemoryEx(hp, pAddress, szSrcCode, 1);
	}

	FlushInstructionCache(hp, NULL, 0);
	Assert(nRet == 0);
	return nRet;
}

int  CDebuger::ContinueBreakpoint(HANDLE hProcess, HANDLE hThread, void * pAddress, char * szSource, int nLength, BOOL bStepIn)
{
	int      nRet;
	CONTEXT  ctx;

	// 恢复后执行. 
	nRet = SetBreakpoint(hProcess, pAddress, szSource, nLength, FALSE); Assert(!nRet);

	ctx.ContextFlags = CONTEXT_CONTROL;
	nRet = GetThreadContext(hThread, &ctx); Assert(nRet);
	#ifdef _M_IX86
	ctx.Eip = (DWORD)pAddress;
	#elif  _M_X64
	ctx.Rip = (DWORD64)pAddress;
	#else
	Assert(0);
	#endif

	if(bStepIn) ctx.EFlags |= 0x100;
	nRet = SetThreadContext(hThread, &ctx); Assert(nRet);

	return nRet ? 0 : -1;
}

int  CDebuger::ParserAndTraceStack(Breakpoint_t * pbp, StackInfoList_t & lstStack, StackInfoExList_t & lstStackEx, std::string & strLog)
{
	int i = 0, bTrace = 0;

	for(StackInfoList_t::iterator it = lstStack.begin(); it != lstStack.end(); it++, i++)
	{
		// ???
		StackInfoEx_t  sie;

		sie.addr = (void*)it->pc; sie.base = 0;
		Assert(!pbp || sie.addr != pbp->address); // 迭代 ???

		for(LoadDllList_t::iterator it2 = m_dlls.begin(); it2 != m_dlls.end(); it2++)
		{
			if(sie.addr >= it2->second.base && sie.addr <= (char*)it2->second.base+it2->second.size)
			{
				sie.base = it2->second.base;
				sie.dll  = it2->second.name;
				break;
			}
		}

		if(i > 0)
		{
			static const char * g_suspious_func_address[] = 
			{
				"RtlExtendedIntegerMultiply", "RtlExtendedMagicDivide", 
			};

			// log suspious address. 
			if(!sie.base)
			{
				for(int j = 0; j < _countof(g_suspious_func_address); j++)
				{
					if(stricmp(g_suspious_func_address[j], pbp->function.c_str()) == 0) { sie.base = (void*)1; break; }
				}

				if(!sie.base) LOG("!! a suspious stack address -->%d,%I64x, %s!%s(???)\n", i, it->pc, pbp->dll.c_str(), pbp->function.c_str());
				continue;
			}

			lstStackEx.push_back(sie);
			if(!bTrace && pbp)  bTrace = m_cfg->IsNeedLogThisStack(pbp->dll.c_str(), sie.dll.c_str(), i);

			char szTmp[64] = {0}; sprintf(szTmp, "!%p", sie.addr);
			strLog += sie.dll + szTmp; strLog += "-->";
		}
	}

	return bTrace;
}

void CDebuger::DumpDllImage(HANDLE hProcess, const char * szDllName)
{
	for(LoadDllList_t::iterator it = m_dlls.begin(); it != m_dlls.end(); it++)
	{
		if(strstr(it->second.name.c_str(), szDllName))
		{
			DWORD  nTmp = 0, nOff = 0, bRet = 0, nRet; SIZE_T nReaded;
			DWORD  nCurProtect = PAGE_READWRITE, nOldProtect = 0;
			std::string strLogName; strLogName = LOG_DIR; strLogName += it->second.name; strLogName += ".mod";
			char * pMem = (char*)malloc(it->second.size);  Assert(pMem);
			FILE * pf   = fopen(strLogName.c_str(), "wb"); if(!pf) return;
			MEMORY_BASIC_INFORMATION mbi = {0};

			memset(pMem, 0, it->second.size);
			while((int)nOff < it->second.size)
			{
				nTmp = (int)VirtualQueryEx(hProcess, (char*)it->second.base+nOff, &mbi, sizeof(mbi)); Assert(nTmp > 0);

				if(mbi.State != MEM_RESERVE)
				{
					ReadProcessMemory(hProcess, (char*)it->second.base+nOff, pMem+nOff, mbi.RegionSize, &nReaded);
					if(nReaded != mbi.RegionSize)
					{
						bRet = VirtualProtectEx(hProcess, (char*)it->second.base+nOff, mbi.RegionSize, nCurProtect, &nOldProtect);
						Assert(bRet);
						ReadProcessMemory(hProcess, (char*)it->second.base+nOff, pMem+nOff, mbi.RegionSize, &nReaded);
						Assert(nReaded == mbi.RegionSize);
						bRet = VirtualProtectEx(hProcess, (char*)it->second.base+nOff, mbi.RegionSize, nOldProtect, &nCurProtect); Assert(bRet);
					}
				}

				nOff += (DWORD)mbi.RegionSize;
			}

			nRet = (int)fwrite(pMem, 1, it->second.size, pf); Assert(nRet == it->second.size);
			fclose(pf); free(pMem);
			break;
		}
	}
}

void CDebuger::DumpFunctions(FunctionInfoList_t & funcs, DllInfo_t * pInfo, int i)
{
	std::string strFileName; 
	FILE * pf = 0;
	static int  g_nId = 100, j = 0;

	// 不处理这个dll. 
	if(!stricmp(pInfo->name.c_str(), "kernelbase.dll")) return;

	strFileName = LOG_DIR; strFileName += ('0'+j++); strFileName += pInfo->name; strFileName += ".func";
	pf = fopen(strFileName.c_str(), "wb"); if(!pf) return ;

	if(g_nId % 10 != 0) g_nId = (g_nId+10)/10*10;
	fprintf(pf, ";;; %s\r\n", pInfo->name.c_str());
	for(FunctionInfoList_t::iterator it = funcs.begin(); it != funcs.end(); it++, i++)
	{
		// 导出函数及地址
		//fprintf(pf, "func%d=%s\r\n", i, it->name.c_str());

		// 重置过滤表
		if(!IsFunctionNeedSetBreakpoint(it->name.c_str()))
		{
			fprintf(pf, "func%d=%s\r\n", g_nId, it->name.c_str());
			g_nId++;
		}
	}
	fclose(pf);
}

void CDebuger::DumpBreakpoint(Breakpoint_t * pbp)
{
	if(!m_cfg->IsDumpBreakpoint()) return;
	if(!m_cfg->IsUnknownAsm(pbp->source, sizeof(pbp->source))) return;

	FILE          * pf = 0;
	std::string     strFileName;
	unsigned char * code = (unsigned char*)pbp->source;
	static bool     gfirst = true;
	const char    * mode = gfirst ? "wb" : "ab";

	strFileName = LOG_DIR; strFileName += "bps.log"; gfirst = false;
	pf = fopen(strFileName.c_str(), mode); if(!pf) return;

	fprintf(pf, "%p,%p, %02x%02x%02x%02x%02x%02x%02x%02x, %s!%s, \r\n", 
		pbp->base, pbp->address, 
		code[0], code[1], code[2], code[3], code[4], code[5], code[6], code[7], 
		pbp->dll.c_str(), pbp->function.c_str());

	fclose(pf);
}


BOOL CDebuger::IsDllNeedSetBreakpoint(const char * szDll)
{
	return m_cfg->IsDllNeedSetBreakpoint(szDll);
}

BOOL CDebuger::IsFunctionNeedSetBreakpoint(const char * szFunction)
{
	return m_cfg->IsFunctionNeedSetBreakpoint(szFunction);
}
