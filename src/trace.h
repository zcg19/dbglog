#pragma  once
#include <stdio.h>
#include <time.h>


inline void GetNowTime(char * szTime, int nSize, int nFmtType = 0)
{
	time_t now;
	struct tm *curtime;
	const  char * fmt = 0;

	switch(nFmtType)
	{
	case  1: fmt = "%Y%m%d%H%M%S"; break;
	case  2: fmt = "%Y%m%d"; break;
	case  3: fmt = "%H%M%S"; break;
	default: fmt = "%Y-%m-%d %H:%M:%S"; break;
	}

	time(&now);
	curtime = localtime(&now);
	strftime(szTime, nSize, fmt, curtime);
}

#define LOG(_fmt, ...)   { \
	char szTimes[64] = {0}; \
	GetNowTime(szTimes, _countof(szTimes), 0); \
	printf("%s %5d " _fmt, szTimes, GetCurrentThreadId(), __VA_ARGS__); \
}

#define LOG_DATA(_fmt, ...) { \
	printf(_fmt, __VA_ARGS__); \
}
