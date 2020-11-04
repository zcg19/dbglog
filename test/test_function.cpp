#include <stdio.h>
#include "..\src\function.cpp"



struct Res_t
{
	bool r1;
	int  r2;
};

struct T1_t
{
	char *  a;
};

struct T12_t
{
	int    b;
};

struct Add_t
{
	int     a;   // 0
	int     b;   // 4
	Res_t * pr;  // 8
	Res_t   r;   // 12
	char    n[16];
	T1_t    t;
	T1_t  * tt;
	T12_t * t2;
};

int  test_fun1(bool r, T1_t * t1, Add_t ** ppa)
{
	Add_t * a = *ppa;
	a->r.r2 = a->a+a->b;
	a->r.r1 = 3;
	a->pr   = &a->r;
	strcpy(a->n, "test_fun1");
	return 0;
}




void test_function()
{
	const char * szFileName = "..\\sample\\1.h";
	int    sz  = 1024*1024*1, len = 0;
	FILE * pf  = fopen(szFileName, "rb"); Assert(pf);
	char * buf = (char*)malloc(sz); Assert(sz);

	memset(buf, 0, sz);
	len = fread(buf, 1, sz, pf); Assert(len > 0);
	fclose(pf);

	InitStructTableEx();
	DecodeFile_CDeclare(buf, len);


	//---
	const char* mode = "rb";
	FILE * pf1 = fopen(szFileName, mode);
	printf("%p,%p\n", szFileName, mode);
	LogFunction("fopen", szFileName, mode);

	T1_t  t = {0};
	t.a = "aaa";

	T12_t t2; t2.b = 999;

	Add_t a = { 0 }, *pa = 0;
	a.a = 3; a.b = 5; pa = &a;
	a.t.a = "bbb";
	a.tt = &t;
	a.t2 = &t2;
	test_fun1(1, &t, &pa);
	LogFunction("test_fun1", 1, &t, &pa);
	LogFunction("printf", "test");

	wchar_t * file_name = L"d:\\temp\\1.h";
	LogFunction("CreateFile_Test", &file_name, 2, 3, 4, 5, 6, 0);

	getchar();
}
