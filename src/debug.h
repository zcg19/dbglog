#pragma  once
#include <windows.h>
#include <map>
#include <set>
#include <string>
#include <list>
#include "symbol.h"
#include "pe.h"


typedef struct Breakpoint_t
{
	std::string     dll;
	std::string     function;
	void          * base;
	void          * address;
	char            source[8];
	char            length, enable;
	int             id;
}Breakpoint_t;

typedef std::map<void *, Breakpoint_t>
BreakpointList_t;

typedef struct BreakpointRet_t
{
	void          * address;
	void          * callee_address;
	int             thread_id[8];
	char            source[8];
	char            length, enable, log;
}BreakpointRet_t;

typedef std::map<void*, BreakpointRet_t> 
BreakpointRetList_t;

typedef std::set<void*> 
BreakpointDelList_t;

typedef struct DllInfo_t
{
	std::string      name;
	std::string      path;
	void           * base;
	size_t           size;
	HANDLE           file_handle;
	int              bp_count;
}DllInfo_t;

typedef std::map<void *, DllInfo_t>
LoadDllList_t;

typedef struct ThreadInfo_t
{
	HANDLE          handle;
	void          * stack_base;
	void          * func;
}ThreadInfo_t;

typedef std::map<DWORD,  ThreadInfo_t>
ThreadList_t;

typedef struct StackInfoEx_t
{
	std::string     dll;
	std::string     function;
	void          * base;
	void          * addr;
	std::string     param;
}StackInfoEx_t;

typedef std::list<StackInfoEx_t> 
StackInfoExList_t;


class CDbgSymbol;
class CDbgConfig;
class CDebuger
{
public:
	CDebuger();
	int  Init(CDbgConfig * pCfg, CDbgSymbol * pSym);
	int  Start(LPCTSTR szExePath, LPCTSTR szParam);


private:
	int  DebugLoop(PROCESS_INFORMATION * ppi);
	int  DispatchDebugEvent(PROCESS_INFORMATION * ppi, DEBUG_EVENT * pev, int * ps);
	int  OnDebugEventException(PROCESS_INFORMATION * ppi, DEBUG_EVENT * pev, int * ps);
	int  OnDebugEventLoadDll(PROCESS_INFORMATION * ppi, DEBUG_EVENT * pev, int * ps);
	int  OnDebugEventUnloadDll(PROCESS_INFORMATION * ppi, DEBUG_EVENT * pev, int * ps);
	int  OnDebugEventBreakpoint(PROCESS_INFORMATION * ppi, DEBUG_EVENT * pev, int * ps);
	int  OnDebugEventStepBreakpoint(PROCESS_INFORMATION * ppi, DEBUG_EVENT * pev, int * ps);


private:
	int  SetDllBreakpoint(HANDLE hProcess, DllInfo_t * pInfo, LOAD_DLL_DEBUG_INFO * pDllInfo);
	int  UnsetDllBreakpoint(HANDLE hProcess, void * pDllBase);
	int  SetBreakpoint(HANDLE hp, void * pAddress, char * szSrcCode, int nSize, BOOL bSet);
	int  ContinueBreakpoint(HANDLE hProcess, HANDLE hThread, void * pAddress, char * szSource, int nLength, BOOL bStepIn);
	int  ParserAndTraceStack(Breakpoint_t * pbp, StackInfoList_t & lstStack, StackInfoExList_t & lstStackEx, std::string & strLog);
	void DumpDllImage(HANDLE hProcess, const char * szDllName);
	void DumpFunctions(FunctionInfoList_t & funcs, DllInfo_t * pInfo, int i);
	void DumpBreakpoint(Breakpoint_t * pbp);


private:
	BOOL IsDllNeedSetBreakpoint(const char * szDll);
	BOOL IsFunctionNeedSetBreakpoint(const char * szFunction);


private:
	LoadDllList_t         m_dlls;
	BreakpointList_t      m_lstbp;
	BreakpointRetList_t   m_lstbpr;
	BreakpointDelList_t   m_lstbpd;

	CDbgSymbol          * m_sym;
	CDbgConfig          * m_cfg;
	DWORD                 m_id, m_max_stack_depth;
	BOOL                  m_is_first_bp;

	std::string           m_exe_name;
	ThreadList_t          m_threads;
};
