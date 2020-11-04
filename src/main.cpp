#include "trace.h"
#include "config.h"
#include "symbol.h"
#include "debug.h"
#include <stdio.h>
#include <assert.h>


#define   Assert assert


void wmain(int argc, wchar_t ** argv)
{
	std::wstring strExePath, strParam;

	int          ret = -1;
	CDebuger     debug;
	CDbgConfig   config;
	CDbgSymbol   symbol;

	if(argc > 1)
	{
		strExePath = argv[1];
		ret = GetFileAttributesW(strExePath.c_str());
		for(int i = 2; i < argc; i++)
		{
			if(!strParam.empty()) strParam += L" "; strParam += argv[i];
		}
	}

	if(ret == -1)
	{
		LOG("** param error, no file path -->%d:%s\n", argc, ::GetCommandLineA());
		return ;
	}

	// 
	HMODULE hM = LoadLibraryA("user32.dll");

	ret = debug.Init(&config, &symbol); Assert(!ret);
	ret = debug.Start(strExePath.c_str(), strParam.c_str());
}
