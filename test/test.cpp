#include <stdio.h>
#include <assert.h>

#include <string>
#include <map>
#include <list>
#define  Assert assert


void main()
{
	{
		extern void test_function();
		test_function();
		return ;
	}
}
