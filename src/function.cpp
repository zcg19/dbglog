// 所有的结构都可以描述为偏移+长度. 
// 数据类型并不是必要成员, 它是为了处理数据而设计的. 通常的数据被描述为 binary, 
// string/wstring需要进行特殊处理, 因此需要增加额外的描述. 
// binary输出总是 16进制形式. 
// 例子1:
// typedef int INT;                     => { (0,4,int) }
// typedef unsigned int UINT            => { (0,4,int) }
// 例子2: 
// struct Test_t
// {
//      int        a;                   // [0,  4,  binary]
//      __typeof(string) char s[10];    // [4,  10, string]
//      __typeof(string) char * p;      // [16, 4,  string|pointer]
// };
// 这样 Test_t可以被描述成 { (0,4,int), (4,10,string), (16,4,string+pointer). }
// 例子3:
// struct Test2_t
// {
//      __sizeof(buf) int len;          // [0,  4,  binary]
//      char     * buf;                 // [4,  4,  binary|pointer, 1 ]
// };
// 
#include <vector>
#include <string>
#include <list>
#include <map>
#include <assert.h>
#include <stdarg.h>
#include <windows.h>
#include "trace.h"


#ifndef  Assert
#define  Assert  assert
#endif

#define uint8  unsigned char
#define uint32 unsigned int


enum enumBaseDataType
{
	DType_Unknown = 0, 
	DType_Binary,                       // i
	DType_Struct,                       // t
	DType_String,                       // s
};

typedef struct StructMemberDesc_t
{
	union
	{
		__int64    t_value;
		struct
		{
			uint8  data_type:4;
			uint8  pointer_count:4;     // 
			uint8  align_max_size:4;
			uint8  align_len:4;
			uint8  is_out:1;            // __out, 默认为 in.
			uint8  pbuf_len_id:7;       // 实际的序号为 pbuf_len_id-1. 
			uint32 pbuf_len;            // data_type为指针时使用, 表示  buffer长度, 最大支持长度为 64K, 若实际大于则省略. 
										// 此处可以缩小长度, 因为输出时可能 256就够了. 
		};
	};
	uint32         off,  len,  lst_len,  ptr_len;
	std::string    name, type, src_type, ptr_type;
}StructMemberDesc_t;

typedef std::vector<StructMemberDesc_t> 
GeneralStructDesc_t;

typedef std::map<std::string, GeneralStructDesc_t> 
StructDescTable_t;

typedef struct FunctionDesc_t
{
	std::string             name, ret;
	std::list<std::string>  params;
	GeneralStructDesc_t     sd_ret;
	GeneralStructDesc_t     sd_param;
}FunctionDesc_t;

typedef std::map<std::string, FunctionDesc_t> 
FunctionDescTable_t;

typedef std::map<std::string, int> 
IndexList_t;


// 
// 支持宏定义
// __out, __no_align
// __typeof(string), 
// __sizeof(xxx), 
// 
#define PREV_FILTER_CHARS(_p)         while(*_p && (*_p == ' ' || *_p == '\t' || *_p == '*' || *_p == '\r' || *_p == '\n')) _p++;
#define POST_FILTER_CHARS(_p)         while(*_p && (*_p == ' ' || *_p == '\t' || *_p == '*' || *_p == '\r' || *_p == '\n')) _p--;
#define FIND_VALID_NAME_A(_p)	      while(*_p && *_p != ' ' && *_p != '\t' && *_p != ';' && *_p != '{' && *_p != '}' && *_p != '(' && *_p != ')' && *_p != '*') _p++;
#define FIND_VALID_NAME_S(_p)	      while(*_p && *_p != ' ' && *_p != '\t' && *_p != ';' && *_p != '{' && *_p != '}' && *_p != '(' && *_p != ')' && *_p != '*') _p--;
#define FILTER_PREV_STRING(_p, _s)    if(_p == strstr(_p, _s))  { _p += strlen(_s); PREV_FILTER_CHARS(_p); }

#define FILTER_COMMENTS(_p1, _p2)                \
	if(_p1[0] == '/' && _p1[1] == '*') {         \
		_p2 = strstr(_p1, "*/"); Assert(_p2);    \
		_p1 = _p2+2; continue;                   \
	}                                            \
	if(_p1[0] == '/' && _p1[1] == '/') {         \
		_p2 = strstr(_p1, "\n"); if(!_p2) break; \
		_p1 = _p2+1; continue;                   \
	}                                            \

#define IF_STRING_TYPE(_p, _t)                   \
	if(_p == strstr(_p, "__typeof(string) "))  { \
		_t  = DType_String;                      \
		_p += 17; PREV_FILTER_CHARS(_p);         \
	} \


// -----------------------------------------------------------------
// struct ...
int  CalcStructSize(GeneralStructDesc_t & st)
{
	Assert(!st.empty());
	return st.back().off + st.back().len + st.back().align_len;
}

int  DecodeStructMember(const char * szMember, StructMemberDesc_t & tMem, int nOff, int nId, IndexList_t & lst_id, StructDescTable_t * st_table)
{
	#define DEL_RESET_STRING(_b, _l)   strMember.erase(_b, _l); p0 = p1 = strMember.c_str();
	const char  * g_struct_type1[] = 
	{
		"struct ", "unsigned ", "const ", "__in "
	};
	const char * p0, *p1, *p2, *p3;
	std::string  strMember, strIdName;
	StructDescTable_t::iterator it;

	tMem.off = nOff; tMem.t_value = 0; tMem.len = tMem.ptr_len = 0; tMem.lst_len = 1; strMember = szMember;
	tMem.data_type = DType_Binary;
	p0 = p1 = strMember.c_str();

	Assert(strchr(p1, '{') == 0);
	for(int i = 0; i < _countof(g_struct_type1); i++)
	{
		if(p2 = strstr(p1, g_struct_type1[i]))
		{
			DEL_RESET_STRING(p2-p0, strlen(g_struct_type1[i]));
		}
	}

	if(p2 = strstr(p1, "__typeof(string) "))
	{
		tMem.data_type = DType_String;
		DEL_RESET_STRING(p2-p0, 17);
	}

	if(p2 = strstr(p1, "__out "))
	{
		tMem.is_out = 1;
		DEL_RESET_STRING(p2-p0, 6);
	}

	if(p2 = strstr(p1, "__sizeof("))
	{
		p1 = p2+9; p3 = p2;
		p2 = strstr(p1, ") "); Assert(p2);
		strIdName.append(p1, p2-p1);

		Assert(lst_id.find(strIdName) == lst_id.end());
		lst_id[strIdName] = nId;
		DEL_RESET_STRING(p3-p0, p2+2-p3);
	}

	while(1)
	{
		p2 = strchr(p1, '*');
		if(!p2) break;

		tMem.pointer_count++;
		*(char*)p2 = ' ';
	}

	while(1)
	{
		// 结构体不允许空[]. 
		p2 = strchr(p1, '[');
		if(!p2) break;

		p1 = strchr(p2+1, ']'); Assert(p1 && p1 != p2+1);
		tMem.lst_len = atoi(p2+1);
		DEL_RESET_STRING(p2-p0, p1+1-p2);
	}

	// 现在就剩下 类型和名称了. 
	PREV_FILTER_CHARS(p1); p2 = p1;
	FIND_VALID_NAME_A(p2);

	tMem.src_type.append(p1, p2-p1);
	if(tMem.pointer_count > 0)
	{
		tMem.ptr_type = tMem.src_type;
		tMem.src_type = "*";

		it = st_table->find(tMem.ptr_type); Assert(it != st_table->end());
		tMem.ptr_len  = it->second.back().off + it->second.back().len + it->second.back().align_len;
	}

	it = st_table->find(tMem.src_type); Assert(it != st_table->end());
	tMem.align_max_size = it->second.back().align_max_size;
	tMem.len  = CalcStructSize(it->second);
	tMem.len *= tMem.lst_len;
	if(it->second.size() == 1)
	{
		if(tMem.pointer_count == 0 && tMem.lst_len == 1)
		{
			tMem.data_type = it->second.begin()->data_type;
		}

		tMem.pointer_count += it->second.begin()->pointer_count;
		if(tMem.ptr_type.empty())
		{
			Assert(tMem.ptr_len == 0);
			tMem.ptr_type = it->second.begin()->ptr_type;
			tMem.ptr_len  = it->second.begin()->ptr_len;
		}
		tMem.type = it->second.begin()->type;
		if(!it->second.begin()->src_type.empty()) tMem.src_type += "."+it->second.begin()->src_type;
	}
	else
	{
		Assert(it->second.size() > 1);
		tMem.data_type = DType_Struct;
		tMem.type = tMem.src_type;
	}

	// name
	PREV_FILTER_CHARS(p2); p1 = p2;
	FIND_VALID_NAME_A(p2);

	tMem.name.append(p1, p2-p1);
	return 0;
}

int  FindStructName(const char * szStruct, int nLength, std::string & strName)
{
	// 有可能没有名称
	const char *p1, *p2, *pe = szStruct+nLength;

	p1 = szStruct; p2 = pe;
	while(p2 > p1 && *p2 != '}') p2--; Assert(*p2 == '}');
	p1 = p2+1; PREV_FILTER_CHARS(p1);

	if(*p1 && p1 < pe)
	{
		p2 = p1;
		FIND_VALID_NAME_A(p2);
		strName.append(p1, p2-p1);
	}

	if(strName.empty())
	{
		p2 = strchr(szStruct, '{'); Assert(p2);
		p2--;    POST_FILTER_CHARS(p2);
		p1 = p2; FIND_VALID_NAME_S(p1); p1++; p2++; 

		strName.append(p1, p2-p1);
	}

	return 0;
}

void GetMemberStringList(const char * szMembers, int nLength, std::list<std::string> & mems)
{
	std::string  ms, m_pre;
	const char * pt1 = szMembers, * pt2 = szMembers+nLength;

	POST_FILTER_CHARS(pt2);
	ms.append(pt1, pt2+1-pt1);

	// 处理 ','
	pt1 = ms.c_str();
	while(1)
	{
		std::string m;
		pt2 = strchr(pt1, ',');
		if(pt2)
		{
			m.append(pt1, pt2-pt1);
			if(m_pre.empty())
			{
				const char * pt3 = strchr(m.c_str(), ' '); Assert(pt3);
				m_pre.append(m.c_str(), pt3+1-m.c_str());
			}
			else
			{
				m.insert(0, m_pre);
			}
			mems.push_back(m);
			pt1 = pt2+1;
		}
		else
		{
			m = pt1;
			if(!m_pre.empty()) m.insert(0, m_pre);
			mems.push_back(m);
			break;
		}
	}
}

void AlignStructMember(StructMemberDesc_t & md, StructMemberDesc_t & md_last, StructDescTable_t * st_table)
{
	// 可能需要重置: off, align_max_size, align_len
	int  type_size = md.len/md.lst_len;

	md.align_max_size = type_size;
	if(md.data_type == DType_Struct)
	{
		Assert(st_table->find(md.type) != st_table->end());
		md.align_max_size = st_table->find(md.type)->second.back().align_max_size;
	}

	Assert((md.len % md.lst_len) == 0);
	Assert(md.align_max_size == 1 || md.align_max_size == 2 || md.align_max_size == 4 || md.align_max_size == 8);

	if(md.off%md.align_max_size != 0)
	{
		// off与 len无关. 
		md.off = (md.off+md.align_max_size-1)/md.align_max_size*md.align_max_size;
	}

	if(md.align_max_size <  md_last.align_max_size)
	{
		if((md.off+md.len)%md_last.align_max_size)
		{
			Assert(((md.off+md.len)%md_last.align_max_size) < md_last.align_max_size);
			md.align_len = md_last.align_max_size-((md.off+md.len)%md_last.align_max_size);
		}
		md.align_max_size = md_last.align_max_size;
	}
}

bool FindStruct(const char * pText, int nLength)
{
	bool         have_typedef = false, have_struct = false;
	const char * p1, * p2;

	p1 = pText;
	if(p1 == strstr(p1, "typedef "))
	{
		have_typedef = true;
		p1 += 8;
		PREV_FILTER_CHARS(p1);
	}

	FILTER_PREV_STRING(p1, "__no_align ");
	if(p1 == strstr(p1, "struct "))
	{
		have_struct = true;
		p1 += 7;
	}

	if(!have_struct)
	{
		// 不支持 union， enum. enum可以定义为 int. 
		p2 = strstr(p1, "union ");
		if(p2) Assert(0);
		p2 = strstr(p1, "enum ");
		if(p2) Assert(0);
	}

	return have_typedef || have_struct;
}

void DecodeStringLen(GeneralStructDesc_t & sd, IndexList_t & lst_id, StructDescTable_t * st_table)
{
	for(IndexList_t::iterator it = lst_id.begin(); it != lst_id.end(); it++)
	{
		bool find = false;
		for(GeneralStructDesc_t::iterator it2 = sd.begin(); it2 != sd.end(); it2++)
		{
			if(!strcmp(it2->name.c_str(), it->first.c_str()))
			{
				find = true;
				it2->pbuf_len_id = it->second;
				break;
			}
		}

		Assert(find);
	}
}

int  DecodeStruct(const char * szStruct, int nLength, StructDescTable_t * st_table)
{
	const char        * p1 = szStruct, * p2, * p3, *pe;
	int                 off = 0,  id = 0, is_align = 1;
	IndexList_t         lst_id;
	std::string         st_name, st_cur_name;
	GeneralStructDesc_t sd;

	FILTER_PREV_STRING(p1, "typedef ");
	if(!(p3 = strchr(p1, '{')))
	{
		StructMemberDesc_t md;

		p2 = strrchr(p1, ' '); Assert(p2); p3 = p2 + 1;
		p2 = p3; FIND_VALID_NAME_A(p2);
		st_name.append(p3, p2-p3);

		DecodeStructMember(p1, md, 0, 0, lst_id, st_table);
		Assert(st_table->find(st_name) == st_table->end());
		(*st_table)[st_name].push_back(md);
		return 0;
	}

	FindStructName(p1, nLength, st_name);
	Assert(!st_name.empty());

	p2 = strstr(p1, "__no_align ");
	if(p2) { Assert(p2 < p3); is_align = 0; }

	pe = strrchr(p1, '}'); Assert(pe);
	p1 = p3+1;

	PREV_FILTER_CHARS(p1);
	while(p1 < pe)
	{
		if(*p1 == '}')
		{
			p2 = strrchr(st_cur_name.c_str(), '.');
			if(p2) st_cur_name.resize(p2-st_cur_name.c_str());
			else   st_cur_name.clear();

			p2 = strchr(p1, ';'); Assert(p2 && p2 < pe);
			p1 = p2+1;
			goto CONTINUE_DecodeStruct;
		}

		p2 = strchr(p1, ';'); Assert(p2 && p2 < pe);
		p3 = strchr(p1, '{');
		if(!p3 || p3 > p2)
		{
			std::list<std::string> mems;

			Assert((p2-1) > p1);
			GetMemberStringList(p1, int(p2-1-p1), mems);

			for(std::list<std::string>::iterator it = mems.begin(); it != mems.end(); it++)
			{
				StructMemberDesc_t md;

				DecodeStructMember(it->c_str(), md, off, id, lst_id, st_table); Assert(!md.name.empty());
				if(!st_cur_name.empty()) { md.name.insert(0, "."); md.name.insert(0, st_cur_name); }
				if(is_align && !sd.empty())
				{
					Assert(md.off == off);
					AlignStructMember(md, sd.back(), st_table);
					Assert(md.off >= (unsigned int)off);
					off  = md.off;
				}

				if(md.data_type == DType_Struct)
				{
					// 此处应该把所有的 struct members放到这个类型里. 
					StructDescTable_t::iterator it = st_table->find(md.type); Assert(it != st_table->end());
					for(GeneralStructDesc_t::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++)
					{
						StructMemberDesc_t  md_tmp = *it2;
						md_tmp.name.insert(0, "."); md_tmp.name.insert(0, md.name);
						md_tmp.src_type.insert(0, "."); md_tmp.src_type.insert(0, md.src_type);
						md_tmp.off += md.off;
						sd.push_back(md_tmp);
					}
				}
				else
				{
					sd.push_back(md); 
				}

				off += md.len; id++;
			}

			p1 = p2+1;
		}
		else
		{
			int         bracket_count = 0;
			std::string sub_name;

			// 暂时不支持解析 union. 
			//p2 = strstr(p1, "union "); Assert(!p2);
			//if(p2 && p2 < p3) is_union = true;

			p2 = p1;
			while(1)
			{
				if(*p2 == ';' && bracket_count == 0) break;
				if(*p2 == '{') bracket_count++;
				if(*p2 == '}') bracket_count--;

				p2++;
			}

			FindStructName(p3, int(p2-p3), sub_name);
			if(!sub_name.empty()) { if(!st_cur_name.empty()) st_cur_name += "."; st_cur_name += sub_name; }
			p1 = p3+1;
		}

CONTINUE_DecodeStruct:
		PREV_FILTER_CHARS(p1);
	}

	DecodeStringLen(sd, lst_id, st_table);
	Assert(st_table->find(st_name) == st_table->end());
	(*st_table)[st_name] = sd;
	return 2;
}

int  DecodeFunction(const char * szFunction, int nLength, StructDescTable_t * st_table, FunctionDescTable_t * fn_table)
{
	// extern "C" int __stdcall test();
	// 
	const char  * g_function_type1[] = 
	{
		"extern ", "\"C\" ", "\"c\" ", 
	};
	const char  * g_function_type2[] = 
	{
		"__stdcall", "WINAPI ", 
	};

	const char       * p1   = szFunction, * p2, *pe;
	int                off  = 0, j = 0;
	FunctionDesc_t     fd;
	enumBaseDataType   type = DType_Unknown;
	IndexList_t        lst_id;
	StructDescTable_t::iterator it;

	pe = strrchr(p1, ')');  Assert(pe && pe < p1+nLength);
	PREV_FILTER_CHARS(p1);
	for(int i = 0; i < _countof(g_function_type1); i++)
	{
		FILTER_PREV_STRING(p1, g_function_type1[i]);
	}

	IF_STRING_TYPE(p1, type);
	p2 = p1; FIND_VALID_NAME_A(p2);
	fd.ret.append(p1, p2-p1);

	it = st_table->find(fd.ret); Assert(it != st_table->end());
	fd.sd_ret = it->second;
	if(type != DType_Unknown)
	{
		Assert(fd.sd_ret.size() == 1);
		fd.sd_ret.front().data_type = type;
	}

	p1 = p2; PREV_FILTER_CHARS(p1);
	while(1)
	{
		if(*p1 != '*') break;
		fd.sd_ret.front().pointer_count++;
		p1++; PREV_FILTER_CHARS(p1);
	}

	for(int i = 0; i < _countof(g_function_type2); i++)
	{
		FILTER_PREV_STRING(p1, g_function_type2[i]);
	}

	p2 = p1; FIND_VALID_NAME_A(p2);
	fd.name.append(p1, p2-p1);

	p2 = strchr(p1, '('); Assert(p2);
	p1 = p2+1; PREV_FILTER_CHARS(p1);

	while(1)
	{
		std::string s_param;
		p2 = strchr(p1, ',');
		if(!p2)
		{
			Assert(p1 < pe);
			while(*(pe-1) == ' ') pe--;
			if(pe > p1 && (memcmp(p1, "void", 4) && memcmp(p1, "VOID", 4) && memcmp(p1, "...", 3)))
			{
				s_param.append(p1, pe-p1);
				fd.params.push_back(s_param);
			}
			break;
		}

		s_param.append(p1, p2-p1); Assert(!s_param.empty());
		fd.params.push_back(s_param);
		p1 = p2+1; PREV_FILTER_CHARS(p1);
	}

	for(std::list<std::string>::iterator it = fd.params.begin(); it != fd.params.end(); it++)
	{
		StructMemberDesc_t md;

		if(0 != (off % sizeof(void*)))  off = (off+sizeof(void*)-1)/sizeof(void*)*sizeof(void*);
		DecodeStructMember(it->c_str(), md, off, j++, lst_id, st_table);
		fd.sd_param.push_back(md); off += md.len;
	}

	(*fn_table)[fd.name] = fd;
	return 0;
}


// -----------------------------------------------------------------
const char         * g_data_type_name[] = { "?", "B", "T", "S", };
StructDescTable_t    g_struct_table;
FunctionDescTable_t  g_function_table;


void InitStructTable()
{
	// 基本类型, 所有的类型最终均被解析成这四种基本类型. 
	// 基本类型 name为空. 
	struct BaseStructType_t{ const char * name; int len; }
	base_type[] = 
	{
		"void",     0, 
		"int8",     1, 
		"int16",    2, 
		"int32",    4, 
		"int64",    8, 
		"*",        sizeof(void*), 
	};

	for(int i = 0; i < _countof(base_type); i++)
	{
		GeneralStructDesc_t  td;
		StructMemberDesc_t   md = {0};
		md.data_type       = DType_Binary;
		md.len             = base_type[i].len;
		md.type            = base_type[i].name;
		md.align_max_size  = base_type[i].len;

		td.push_back(md);
		g_struct_table[base_type[i].name] = td;
	}
}

void InitStructTableEx()
{
	const char * g_szBaseTypeEx[] = 
	{
		"typedef int8  char;"     ,
		"typedef int8  bool;"     ,
		"typedef int8  byte_t;"   ,
		"typedef int16 wchar_t;"  ,
		"typedef int16 short;"    ,
		"typedef int32 int;"      ,
		"typedef int32 long;"     ,
		"typedef int64 __int64;"  ,
	};

	InitStructTable();
	for(int i = 0; i < _countof(g_szBaseTypeEx); i++)
	{
		DecodeStruct(g_szBaseTypeEx[i], sizeof(g_szBaseTypeEx)-1, &g_struct_table);
	}
}

int  DecodeFile_CDeclare(const char * szText, int nLength)
{
	const char *p1, *p2, *pe;
	p1 = szText; pe = szText+nLength;
	std::string            s_text;
	std::list<std::string> lst_struct;
	std::list<std::string> lst_function;

	// 1 先把注释去掉
	while(*p1 && p1 <= pe)
	{
		if(*p1 == '/')
		{
			PREV_FILTER_CHARS(p1);
			FILTER_COMMENTS(p1, p2);
		}

		switch(*p1)
		{
		case '\t': case '\r': case '\n':
			s_text.push_back(' ');
			break;
		case '*':
			s_text.push_back(' ');
			s_text.push_back('*');
			break;
		default:
			s_text.push_back(*p1);
			break;
		}
		p1++;
	}

	p1 = s_text.c_str(); pe = p1 + s_text.length();
	PREV_FILTER_CHARS(p1);
	while(p1 < pe)
	{
		bool is_find_struct;
		int  bracket_count = 0;

		Assert(p1 <= pe);
		p2 = p1;

		// 先找 struct
		is_find_struct = FindStruct(p1, -1);
		if(is_find_struct)
		{
			// 一个 struct. 找结尾
			std::string s_struct;
			p2 = p1;
			while(1)
			{
				s_struct.push_back(*p2);
				if(*p2 == ';' && bracket_count == 0) break;
				if(*p2 == '{')   bracket_count++;
				if(*p2 == '}')   bracket_count--;
				p2++;
			}
			lst_struct.push_back(s_struct);
			p1 = p2+1;
		}
		else
		{
			std::string s_function;
			const char * pt1, * pt2;

			Assert(p1 == p2);
			p2 = strchr(p1, ';'); Assert(p2);
			s_function.append(p1, p2-p1+1);

			// format function.
			pt1 = strrchr(s_function.c_str(), ')');
			if(pt1)
			{
				pt1++; pt2 = pt1; PREV_FILTER_CHARS(pt1);
				if(*pt1 == ';')
				{
					*(char*)pt2 = ';'; *(char*)(pt2+1) = 0;
					lst_function.push_back(s_function.c_str());
				}
			}

			p1 = p2+1;
		}

		PREV_FILTER_CHARS(p1);
	}

	for(std::list<std::string>::iterator it = lst_struct.begin(); it != lst_struct.end(); it++)
	{
		Assert(!it->empty());
		DecodeStruct(it->c_str(), (int)it->length(), &g_struct_table);
	}

	for(std::list<std::string>::iterator it = lst_function.begin(); it != lst_function.end(); it++)
	{
		Assert(!it->empty());
		DecodeFunction(it->c_str(), (int)it->length(), &g_struct_table, &g_function_table);
	}

	LOG("%d,%d\n", lst_struct.size(), lst_function.size());
	return 0;
}

int  Load_ConfigCDef(const wchar_t * file_path)
{
	int    len = 0;
	FILE * pf  = _wfopen(file_path, L"rb");
	if(!pf) return -1;

	fseek(pf, 0, SEEK_END);
	len = ftell(pf);
	if(!len) { fclose(pf); return -2; }

	fseek(pf, 0, SEEK_SET);
	char * buf = (char*)malloc(len+1); Assert(buf);
	int    r   = (int)fread(buf, 1, len, pf); Assert(r == len);
	buf[len]   = 0;
	DecodeFile_CDeclare(buf, len);

	free(buf);
	fclose(pf);
	return 0;
}


int  LogStruct(StructMemberDesc_t & sd, void * data, bool is_addr, int layer, int index, void * user, int (*ReadMemoryAddress)(void * dst, void * src, int len, void * user))
{
	StructDescTable_t::iterator it = g_struct_table.end();
	int          len = sd.len, data_size = 0, index_sub = 0, ret = 0, pointer_count = 0;
	char         err[64] = {0};
	std::string  buf;

	#define PRINT_SPACE() for(int i = 0; i < layer; i++) LOG_DATA("\t");
	PRINT_SPACE(); LOG_DATA("%d.name  -->%d,%d,%s >>%s,%d,[%s]\n", index, sd.off, sd.len, sd.name.c_str(), sd.type.c_str(), sd.pointer_count, g_data_type_name[sd.data_type]);

	if(!sd.ptr_type.empty())
	{
		it = g_struct_table.find(sd.ptr_type);  Assert(it != g_struct_table.end());
		pointer_count = sd.pointer_count; Assert(pointer_count > 0);
		if(it->second.size() > 1) pointer_count -= 1;
	}

	if(is_addr) pointer_count += 1;
	if(sd.data_type == DType_String)
	{
		Assert(pointer_count >= 1);
		pointer_count -= 1;
	}

	for(int i = 0; i < pointer_count; i++)
	{
		// 有可能指针不可读. 
		if(!data) break;
		if(ret = ReadMemoryAddress(&data, data, sizeof(void*), user))
		{
			sprintf(err, "read ptr error(%p)", data);
			data = 0; break;
		}
	}

	switch(sd.data_type)
	{
	case DType_Binary:
		if(data && it != g_struct_table.end())
		{
			for(GeneralStructDesc_t::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++)
			{
				LogStruct(*it2, (char*)data+it2->off, it->second.size() > 1, layer+1, index_sub++, user, ReadMemoryAddress);
			}
			return 0;
		}

		PRINT_SPACE(); LOG_DATA("%d.value -->", index);
		switch(sd.len)
		{
		case 1: LOG_DATA("%02x", (unsigned char)data);  break;
		case 2: LOG_DATA("%04x", (unsigned short)data); break;
		case 4: LOG_DATA("%08x", (unsigned int)data);   break;
		case 8: LOG_DATA("%016I64x", (unsigned __int64)data); break;

		default:
			Assert(sd.len == 0 && !strcmp(sd.type.c_str(), "void"));
			LOG_DATA("%p !!!", data);
			break;
		}
		if(err[0]) LOG_DATA(" \?\?\?(%s)", err) LOG_DATA("\n");
		break;
	case DType_Struct:
		it = g_struct_table.find(sd.type); Assert(it != g_struct_table.end());
		for(GeneralStructDesc_t::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++)
		{
			void * addr2 = (char*)data + it2->off;
			LogStruct(*it2, addr2, true, layer+1, index_sub++, user, ReadMemoryAddress);
		}
		break;
	case DType_String:
		Assert(sd.pointer_count > 0 || sd.lst_len > 1);
		PRINT_SPACE(); LOG_DATA("%d.value -->", index);

		data_size = sd.pointer_count > 0 ? sd.ptr_len : sd.lst_len/sd.len;
		while(user)
		{
			char ch[4] = {0};
			ret = ReadMemoryAddress(&ch, data, data_size, user); Assert(!ret);
			if(!memcmp(ch, L"\0", data_size))
			{
				data = &buf[0];
				break;
			}
			for(int i = 0; i < data_size; i++) buf.push_back(ch[i]);
			data = (char*)data+data_size;
		}

		// 验证字符串数组大小没有越界. 
		if(sd.lst_len > 1)
		{
			int j = 0;
			while(1)
			{
				if(!memcmp((char*)data+j, L"\0", data_size)) break;
				j += data_size;
			}
			Assert(j < (int)sd.len);
		}

		if(data_size == sizeof(char))         LOG_DATA("%p,%s\n", data, (char*)data)
		else if(data_size == sizeof(wchar_t)) LOG_DATA("%p,%S\n", data, (wchar_t*)data)
		else { Assert(0); }
		break;
	default:
		Assert(0);
		break;
	}

	return 0;
}


// dbglog使用
int  DbgLog_ReadProcessMemory(void * dst, void * src, int len, void * user)
{
	int    ret    = 0;
	SIZE_T readed = 0;
	void * handle_process  = user;

	Assert(dst); Assert(len > 0);
	if(!user)
	{
		// test.
		if(!IsBadReadPtr(src, len))
		{
			memcpy(dst, src, len);
			return 0;
		}

		return -1;
	}
	
	ret  = ReadProcessMemory(handle_process, src, dst, len, &readed);
	return ret ? 0 : -1;
}

int  DbgLog_FunctionIn32(const char * function_name, void * reg_stack_to_param0, void * user/*handle_process*/)
{
	FunctionDescTable_t::iterator it;
	int    layer = 1, index = 0, ret = 0;

	it = g_function_table.find(function_name);
	if(it == g_function_table.end()) return 0;

	LOG("function -->%s(%d)\n", function_name, it->second.params.size());
	for(GeneralStructDesc_t::iterator it2 = it->second.sd_param.begin(); it2 != it->second.sd_param.end(); it2++)
	{
		LogStruct(*it2, (char*)reg_stack_to_param0 + it2->off, true, layer, index++, user, DbgLog_ReadProcessMemory);
	}

	return 0;
}

int  DbgLog_FunctionIn64(const char * function_name, void * reg_stack_to_param0, void * user/*handle_process*/, DWORD64 param_addr[4])
{
	FunctionDescTable_t::iterator it;
	int    layer = 1, index = 0, ret = 0;

	it = g_function_table.find(function_name);
	if(it == g_function_table.end()) return 0;

	LOG("function -->%s(%d)\n", function_name, it->second.params.size());
	for(GeneralStructDesc_t::iterator it2 = it->second.sd_param.begin(); it2 != it->second.sd_param.end(); it2++)
	{
		bool   is_addr = false;
		void * addr = 0;

		if(index < 4)
		{
			addr = (void*)param_addr[index]; is_addr = false;
		}
		else
		{
			addr = (char*)reg_stack_to_param0 + index*8; is_addr = true;
		}

		LogStruct(*it2, addr, is_addr, layer, index++, user, DbgLog_ReadProcessMemory);
	}

	return 0;
}


// 使用方法: 
// test_add(1, 2);
// LogFunction("test_add", 1, 2); 
void LogFunction(const char * func_name, ...)
{
	int      layer = 1, index = 0;
	FunctionDescTable_t::iterator it;
	va_list  args;
	va_start(args, func_name);

	char * params_begin = (char*)args;
	DbgLog_FunctionIn32(func_name, params_begin, 0);

	va_end(args);
}
