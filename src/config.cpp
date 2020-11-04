#include <windows.h>
#include <assert.h>
#include "config.h"
#include "trace.h"


#define Assert assert
#pragma warning(disable : 4996)


CDbgConfig::CDbgConfig()
	: m_nStackLevel(3)
	, m_bDumpFuncs(0)
	, m_bDumpMods(0)
	, m_bDumpBps(0)
	, m_nLogLevel(0)
{}

int  CDbgConfig::Load(LPCTSTR szTargetName)
{
	TCHAR szFilePath[MAX_PATH] = {0};
	GetModuleFileName(0, szFilePath, _countof(szFilePath)); Assert(szFilePath[0]);
	m_strPath.Format(_T("%s.cfg"), szFilePath);
	m_strExeName = szTargetName;
	LOG(">> config path -->%S\n", (LPCTSTR)m_strPath);

	LoadConfig();
	return 0;
}

int  CDbgConfig::GetMaxStackLevel()
{
	return m_nStackLevel;
}

BOOL CDbgConfig::IsDllNeedSetBreakpoint(const char * szCallee)
{
	BOOL bMatchAll = FALSE;
	for(CallConfigList_t::iterator it = m_configs.begin(); it != m_configs.end(); it++)
	{
		if(stricmp(it->callee.c_str(), szCallee) == 0)
		{
			return TRUE;
		}

		if(it->callee.at(0) == '*')
		{
			bMatchAll = TRUE;
		}
	}

	if(bMatchAll)
	{
		CStringA strt = szCallee; strt.MakeLower();
		DllFilterList_t::iterator it = m_filterDlls.find((LPCSTR)strt);
		if(it == m_filterDlls.end()) it = m_filterDlls.find("*");
		if(it != m_filterDlls.end()) return it->second;
	}

	return bMatchAll;
}

BOOL CDbgConfig::IsFunctionNeedSetBreakpoint(const char * szFunction)
{
	FunctionFilterList_t::iterator it;
	it = m_filterFunctions.find(szFunction);
	return it == m_filterFunctions.end();
}

BOOL CDbgConfig::IsNeedLogThisStack(const char * szCallee, const char * szCaller, int nStackDepth)
{
	for(CallConfigList_t::iterator it = m_configs.begin(); it != m_configs.end(); it++)
	{
		if(!stricmp(it->callee.c_str(), szCallee) || it->callee[0] == '*')
		{
			if(stricmp(it->caller.c_str(), szCaller) == 0)
			{
				return nStackDepth <= it->level;
			}
		}
	}

	return FALSE;
}

BOOL CDbgConfig::IsUnknownAsm(const char * szCode, int nLength)
{
	#ifdef _M_IX86
	switch(szCode[0])
	{
	case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:  // push reg
	case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:  // pop  reg
	case 0x60: case 0x61:                                                                    // pusha, popa
		return FALSE;

	case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:  // non used
		szCode++; nLength--;
		break;
	}
	#endif

	for(AsmFilterList_t::iterator it = m_filterAsms.begin(); it !=m_filterAsms.end(); it++)
	{
		Assert(!it->empty());
		if(memcmp(it->c_str(), szCode, it->length()) == 0) return FALSE;
	}

	return TRUE;
}

void CDbgConfig::LoadConfig()
{
	int         nCount = 0;
	std::string strLog;

	Assert(!m_strExeName.IsEmpty());
	Assert(!m_strPath.IsEmpty());
	m_bDumpFuncs  = GetPrivateProfileInt(_T("common"), _T("dump_functions"),   0, m_strPath);
	m_bDumpMods   = GetPrivateProfileInt(_T("common"), _T("dump_modules"),     0, m_strPath);
	m_bDumpBps    = GetPrivateProfileInt(_T("common"), _T("dump_breakpoints"), 0, m_strPath);
	m_nLogLevel   = GetPrivateProfileInt(_T("common"), _T("log_level"),        0, m_strPath);
	m_nStackLevel = GetPrivateProfileInt(_T("common"), _T("stack_level"),      1, m_strPath);
	nCount = GetPrivateProfileInt(_T("call"), _T("count"), 0, m_strPath);
	for(int i = 0; i < nCount; i++)
	{
		TCHAR          szLogDll[256] = {0}, *p1 = 0, *p2 = 0, nFind = 0, nMatchAll = 0;
		CString        strKey;
		CallConfig_t   config;

		strKey.Format(_T("call%d"), i);
		GetPrivateProfileString(_T("call"), strKey, _T(""), szLogDll, _countof(szLogDll), m_strPath);
		p1  = _tcsstr(szLogDll, _T("-->")); Assert(p1);
		*p1 = 0; p1 += 3;
		p2  = _tcschr(p1, _T(','));

		if(p2) { *p2 = 0; p2++; }
		config.level  = p2 ? _ttoi(p2) : 1;
		config.caller = (LPCSTR)CT2A(szLogDll);
		config.callee = (LPCSTR)CT2A(p1);

		if(stricmp(config.caller.c_str(), "%target%") == 0)
		{
			config.caller = (LPCSTR)CT2A(m_strExeName);
		}

		if(config.callee.length() == 1 && config.callee.at(0) == '*')
		{
			nMatchAll = 1;
		}

		for(CallConfigList_t::iterator it = m_configs.begin(); it != m_configs.end(); it++)
		{
			if(it->callee == config.callee && it->caller == config.caller)
			{
				nFind = 1;
				LOG("!! duplicate config -->%s\n", config.caller.c_str(), config.callee.c_str());
				break;
			}
		}

		if(!nFind) { if(nMatchAll) m_configs.push_back(config); else m_configs.push_front(config); }
	}

	nCount = GetPrivateProfileInt(_T("filter_func"), _T("func_count"), 0, m_strPath);
	for(int i = 0; i < nCount; i++)
	{
		TCHAR   szFuncName[256] = {0}, *p;
		CString strKey;

		strKey.Format(_T("func%d"), i);
		GetPrivateProfileString(_T("filter_func"), strKey, _T(""), szFuncName, _countof(szFuncName), m_strPath);
		if(!szFuncName[0]) 
		{
			strLog += (LPCSTR)CT2A(strKey); strLog += ",";
			continue;
		}

		p = _tcschr(szFuncName, ' '); if(p) *p = 0;
		m_filterFunctions.insert((LPCSTR)CT2A(szFuncName));
	}

	if(!strLog.empty())
	{
		LOG(">> config, filter function -->%s\n", strLog.c_str());
		strLog.clear();
	}

	nCount = GetPrivateProfileInt(_T("filter_dll"), _T("dll_count"), 0, m_strPath);
	for(int i = 0; i < nCount; i++)
	{
		TCHAR   szDllName[256] = {0};
		CString strKey;

		strKey.Format(_T("dll%d"), i);
		GetPrivateProfileString(_T("filter_dll"), strKey, _T(""), szDllName, _countof(szDllName), m_strPath);

		if(szDllName[0])
		{
			int      bSetBps = 0, nOffset = 0;
			CStringA strt;

			switch(szDllName[0])
			{
			case '+': bSetBps = 0; nOffset++; break;
			case '-': bSetBps = 1; nOffset++; break;
			}

			strt = szDllName+nOffset; strt.MakeLower();
			m_filterDlls[(LPCSTR)strt] = bSetBps;
		}
	}

	#ifdef _M_IX86
	LPCTSTR szDomain = _T("asm_x86");
	#elif  _M_X64
	LPCTSTR szDomain = _T("asm_x64");
	#else
	Assert(0);
	#endif
	nCount = GetPrivateProfileInt(szDomain, _T("count"), 0, m_strPath);
	for(int i = 0; i < nCount; i++)
	{
		TCHAR       szValue[32] = {0}, *p;
		CString     strKey;
		std::string strValue, strValue2, *pstr;

		strKey.Format(_T("asm%d"), i);
		GetPrivateProfileString(szDomain, strKey, _T(""), szValue, _countof(szValue), m_strPath);
		if(!szValue[0]) continue;

		p = _tcschr(szValue, _T(' '));
		if(p) *p = 0;
		p = _tcschr(szValue, _T('x'));
		if(p) *p = 0;

		pstr = &strValue;
		for(int j = 0; j < _countof(szValue) && szValue[j]; )
		{
			int  nChar;
			if(szValue[j] == ',')
			{
				pstr = &strValue2; j++;
				continue;
			}

			_stscanf(szValue+j, _T("%02x"), &nChar);
			pstr->push_back((char)nChar); j+=2;
		}

		if(strValue2.empty())
		{
			Assert(m_filterAsms.find(strValue) == m_filterAsms.end());
			m_filterAsms.insert(strValue);
			continue;
		}

		Assert(strValue.size() == strValue2.size() && strValue.size() == 1);
		for(unsigned char ch1 = strValue.at(0), ch2 = strValue2.at(0); ch1 <= ch2; ch1++)
		{
			std::string tmp; tmp.push_back(ch1);
			m_filterAsms.insert(tmp);
		}
	}

	nCount = GetPrivateProfileInt(_T("c_header"), _T("count"), 0, m_strPath);
	for(int i = 0; i < nCount; i++)
	{
		WCHAR       szPath[MAX_PATH] = {0};
		CStringW    strKey;

		strKey.Format(L"path%d", i);
		GetPrivateProfileStringW(L"c_header", strKey, L"", szPath, _countof(szPath), m_strPath);
		if(!szPath[0]) break;

		extern int  Load_ConfigCDef(const wchar_t * file_path);
		Load_ConfigCDef(szPath);
		m_lstCDef.push_back(szPath);
	}
}
