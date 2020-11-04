typedef void    * FILE;
typedef void    * HANDLE;
typedef int       DWORD;
typedef void    * LPSECURITY_ATTRIBUTES;
typedef __typeof(string) wchar_t * LPCWSTR;
typedef __typeof(string) char *    LPCSTR;


int   printf(__typeof(string) const char * fmt, ...);
FILE  fopen(__typeof(string) const char * path, __typeof(string) const char * mode);

HANDLE WINAPI CreateFileW(
  __in          LPCWSTR lpFileName,
  __in          DWORD dwDesiredAccess,
  __in          DWORD dwShareMode,
  __in          LPSECURITY_ATTRIBUTES lpSecurityAttributes,
  __in          DWORD dwCreationDisposition,
  __in          DWORD dwFlagsAndAttributes,
  __in          HANDLE hTemplateFile
);
