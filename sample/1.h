// test
//typedef unsigned char UCHAR;
//typedef unsigned short USHORT;
//typedef char * LPSTR;
/*
struct D
{
	struct A
	{
		struct B
		{
			char ** b;
		}B;
		char a;
		
	}A;
	
	char c;
	struct D
	{
		char * d;
		__sizeof(D.d) int l;
	}D;	
};


typedef struct {
	{
	char a, b;
	int  c;
	char* d, e, *f;
	};
}C;

typedef struct _A0
{             //               len, algn_size, algn_len
	char  a;  // o = 0,        1,   1,         0
	int   b;  // o = 1 -->4,   4,   4,         0
	char  c;  // o = 8,        1,   4,         3
	char  d;  // o = 9,        1,   4,         2
	int   e;  // o = 10 -->12, 4,   4,         0, 

	char  f;  // o = 16,       1,   4,         3, 
	short g;  // o = 17 -->18, 2,   4,         0, 

	char  h;  // o = 20,       1,   4,         3, 
	char  i;  // o = 21,       1,   4,         2, 
	char  j;  // o = 22,       1,   4,         1, 

	short k;  // o = 23 -->24, 2,   4,         2, 
	int   l;  // o = 26 -->28, 4,   4,         0, 
} 
A0;



extern "c" int a;
typedef struct _A
{
	char a;
	__int64 b;   // o = 1->8
	char c;      // o = 16,     1, 8, 7
	short  d;    // o = 17->18, 2, 8, 4 
	short e;     // o = 20,     2, 8, 2
} 
A;


struct B
{
	int a;
	char d[2];
	int  e;
	A    at;
	char g[2];
};

//extern "c" int __stdcall test_funct(int a, int b);

void   test_1(bool a, int b, __int64 c, int d);*/
typedef __typeof(string) wchar_t * LPCWSTR;
typedef void * FILE;


struct Res_t
{
	bool r1;
	int  r2;
};

struct T1_t
{
	__typeof(string) char *  a;
};

struct T12_t
{
	int    b;
};

struct Add_t
{
	int     a;
	int     b;
	Res_t * pr;
	Res_t   r;
	__typeof(string) char    n[16];
	T1_t    t;
	T1_t  * tt;
	T12_t * t2;
};


int  test_fun1(bool r, T1_t * t, Add_t ** a);
//int  printf(__typeof(string) const char * fmt, ...);


//typedef void    * FILE;
typedef void    * HANDLE;
typedef int       DWORD;
typedef void    * LPSECURITY_ATTRIBUTES;

int   printf(__typeof(string) const char * fmt, ...);
FILE  fopen(__typeof(string) const char * path, __typeof(string) const char * mode);

HANDLE WINAPI CreateFile_Test(
  __in          LPCWSTR * lpFileName,
  __in          DWORD dwDesiredAccess,
  __in          DWORD dwShareMode,
  __in          LPSECURITY_ATTRIBUTES lpSecurityAttributes,
  __in          DWORD dwCreationDisposition,
  __in          DWORD dwFlagsAndAttributes,
  __in          void ** hTemplateFile
);
