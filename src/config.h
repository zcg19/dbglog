#include <atlstr.h>
#include <list>
#include <string>
#include <set>
#include <map>


typedef struct CallConfig_t
{
	std::string caller;
	std::string callee;
	int         level;
}CallConfig_t;

typedef std::list<CallConfig_t> 
CallConfigList_t;

typedef std::set<std::string> 
FunctionFilterList_t;

typedef std::map<std::string, int> 
DllFilterList_t;

typedef std::set<std::string> 
AsmFilterList_t;

typedef std::list<std::wstring> 
CDefHeaderList_t;


class CDbgConfig
{
public:
	CDbgConfig();

	int  Load(LPCTSTR szTargetName);
	int  GetMaxStackLevel();
	BOOL IsDllNeedSetBreakpoint(const char * szCallee);
	BOOL IsFunctionNeedSetBreakpoint(const char * szFunction);
	BOOL IsNeedLogThisStack(const char * szCallee, const char * szCaller, int nStackDepth);
	BOOL IsUnknownAsm(const char * szCode, int nLength);
	BOOL IsDumpFunctions()  { return m_bDumpFuncs; }
	BOOL IsDumpModules()    { return m_bDumpMods; }
	BOOL IsDumpBreakpoint() { return m_bDumpBps; }
	int  LogLevel()         { return m_nLogLevel; }


private:
	void LoadConfig();


private:
	CString                  m_strExeName;
	CString                  m_strPath;
	CallConfigList_t         m_configs;
	FunctionFilterList_t     m_filterFunctions;
	DllFilterList_t          m_filterDlls;
	AsmFilterList_t          m_filterAsms;
	CDefHeaderList_t         m_lstCDef;

	BOOL                     m_bDumpFuncs,  m_bDumpMods, m_bDumpBps;
	int                      m_nStackLevel, m_nLogLevel;
};
