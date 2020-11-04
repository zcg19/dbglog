#pragma once
#include <windows.h>
#include <string>
#include <list>


typedef struct _IMAGE_OPTIONAL_HEADER_PRE
{
    WORD    Magic;
    BYTE    MajorLinkerVersion;
    BYTE    MinorLinkerVersion;
    DWORD   SizeOfCode;
    DWORD   SizeOfInitializedData;
    DWORD   SizeOfUninitializedData;
    DWORD   AddressOfEntryPoint;
    DWORD   BaseOfCode;

	union
	{
		ULONGLONG ImageBase64;
		struct
		{
		    DWORD BaseOfData;
			DWORD ImageBase32;
		};
	};

	DWORD   SectionAlignment;
    DWORD   FileAlignment;
    WORD    MajorOperatingSystemVersion;
    WORD    MinorOperatingSystemVersion;
    WORD    MajorImageVersion;
    WORD    MinorImageVersion;
    WORD    MajorSubsystemVersion;
    WORD    MinorSubsystemVersion;
    DWORD   Win32VersionValue;
    DWORD   SizeOfImage;
    DWORD   SizeOfHeaders;
    DWORD   CheckSum;
    WORD    Subsystem;
    WORD    DllCharacteristics;
} IMAGE_OPTIONAL_HEADER_PRE;

typedef struct _IMAGE_OPTIONAL_HEADER_POST
{
    DWORD       LoaderFlags;
    DWORD       NumberOfRvaAndSizes;
    IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
} IMAGE_OPTIONAL_HEADER_POST;

typedef struct FunctionInfo_t
{
	std::string   name;
	void        * addr;
}FunctionInfo_t;

typedef std::list<FunctionInfo_t> 
FunctionInfoList_t;


class CImagePeDump
{
public:
	CImagePeDump()
		: m_file_handle(0)
		, m_is_x64(false)
		, m_image_base(0)
		, m_section_header(0)
	{}

	~CImagePeDump()
	{
		if(m_section_header) free(m_section_header);
	}

	int  Parser(HANDLE hFile, void * pImageBase)
	{
		int  nRet;

		Assert(hFile);
		m_file_handle = hFile;
		m_image_base  = (ULONG_PTR)pImageBase;

		if(nRet = ReadDos())           return nRet;
		if(nRet = ReadCOFF())          return nRet;
		if(nRet = ReadPE())            return nRet;
		if(nRet = ReadSectionHeader()) return nRet;

		return 0;
	}

	int  GetExportFunctions(FunctionInfoList_t & lstFunc)
	{
		DWORD                   nRet, nOffset = 0, nId = 0;
		char                    szDllName[64] = {0};
		PIMAGE_DATA_DIRECTORY   pDir = &m_data_dir.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
		IMAGE_EXPORT_DIRECTORY  export_table = { 0 };

		if(pDir->Size == 0 || pDir->VirtualAddress == 0)
		{
			LOG(">> no export table!\n");
			return 0;
		}

		// 1 此处的地址为虚拟地址, 使用 ReadProcessMemory可以获取到. 
		//   但是此时使用的是 file_handle, 所以必须转换成文件偏移. 
		// 2 此处只关心有名称的的函数. 
		//   也不关心 forward函数, 因为只要加载就会设置断点. forward会直接调入真正的dll内. 
		// 3 直接在 rva上进行偏移
		// 
		nRet = ReadFileByRva(0, &export_table, sizeof(export_table), pDir->VirtualAddress, nOffset, nId); Assert(!nRet);
		nRet = ReadFileByRva(1, szDllName, sizeof(szDllName), export_table.Name, nOffset, nId); Assert(!nRet);

		for(int i = 0; i < (int)export_table.NumberOfNames; i++)
		{
			WORD  nOrdinal;
			DWORD nAddress, nNameAddress=0;
			char  szFuncName[128] = {0};
			FunctionInfo_t fi;

			nRet = ReadFileByRva(0, &nOrdinal,     sizeof(nOrdinal),     export_table.AddressOfNameOrdinals+(i*sizeof(nOrdinal)), nOffset, nId); Assert(!nRet);
			nRet = ReadFileByRva(0, &nAddress,     sizeof(nAddress),     export_table.AddressOfFunctions+(nOrdinal*sizeof(nAddress)), nOffset, nId); Assert(!nRet);
			nRet = ReadFileByRva(0, &nNameAddress, sizeof(nNameAddress), export_table.AddressOfNames+(i*sizeof(nNameAddress)), nOffset, nId); Assert(!nRet);

			if(nNameAddress)
			{
				nRet = ReadFileByRva(1, szFuncName, sizeof(szFuncName), nNameAddress, nOffset, nId);
			}

			if(nAddress >= pDir->VirtualAddress && nAddress <= pDir->VirtualAddress+pDir->Size)
			{
				// forward.
				continue;
			}

			fi.name = szFuncName;
			fi.addr = (void*)(m_image_base + nAddress);
			lstFunc.push_back(fi);
		}
		return 0;
	}


private:
	int  ReadDos()
	{
		BOOL   bRet;
		DWORD  nReaded;

		SetFilePointer(m_file_handle, 0, 0, FILE_BEGIN);
		bRet = ReadFile(m_file_handle, &m_dos_header, sizeof(m_dos_header), &nReaded, 0); Assert(bRet && nReaded == sizeof(m_dos_header));
		if(!bRet) return -1;

		if(m_dos_header.e_magic != 0x5A4D) return -2;
		if(m_dos_header.e_lfarlc < 0x40)   return -3;
		if(m_dos_header.e_lfanew < sizeof(m_dos_header)) return -4;

		SetFilePointer(m_file_handle, m_dos_header.e_lfanew, 0, FILE_BEGIN);
		return 0;
	}

	int  ReadCOFF()
	{
		BOOL              bRet;
		DWORD             nSignature, nReaded;

		bRet = ReadFile(m_file_handle, &nSignature, sizeof(nSignature), &nReaded, 0); Assert(bRet && nReaded == sizeof(nSignature));
		if(!bRet) return -1;

		if(nSignature != 0x00004550) return -2;
		bRet = ReadFile(m_file_handle, &m_file_header, sizeof(m_file_header), &nReaded, 0); Assert(bRet && nReaded == sizeof(m_file_header));
		if(!bRet) return -1;

		switch(m_file_header.Machine)
		{
		case 0x014c: m_is_x64 = FALSE; break;
		case 0x8664: m_is_x64 = TRUE;  break;
		case 0x0200: LOG(">> machine not support ...\n");
		default:     return -3;
		}

		return 0;
	}

	int  ReadPE()
	{
		BOOL  bRet;
		DWORD nReaded, nOffset;

		bRet = ReadFile(m_file_handle, &m_option_header, sizeof(m_option_header), &nReaded, 0); Assert(bRet && nReaded == sizeof(m_option_header));
		if(!bRet) return -1;

		switch(m_option_header.Magic)
		{
		case 0x10b: Assert(!m_is_x64); break;
		case 0x20b: Assert(m_is_x64);  break;
		case 0x107: LOG(">> pe -->rom image ...\n"); break;
		default:    return -4;
		}

		if(m_is_x64) nOffset = sizeof(IMAGE_OPTIONAL_HEADER64)-sizeof(IMAGE_OPTIONAL_HEADER_PRE)-sizeof(IMAGE_OPTIONAL_HEADER_POST);
		else         nOffset = sizeof(IMAGE_OPTIONAL_HEADER32)-sizeof(IMAGE_OPTIONAL_HEADER_PRE)-sizeof(IMAGE_OPTIONAL_HEADER_POST);

		SetFilePointer(m_file_handle, nOffset, 0, FILE_CURRENT);
		bRet = ReadFile(m_file_handle, &m_data_dir, sizeof(m_data_dir), &nReaded, 0); Assert(bRet && nReaded == sizeof(m_data_dir));
		if(!bRet) return -1;

		Assert(m_data_dir.NumberOfRvaAndSizes == _countof(m_data_dir.DataDirectory));

		return 0;
	}

	int  ReadSectionHeader()
	{
		BOOL  bRet;
		DWORD nSectionSize = 0, nReaded;

		Assert(!m_section_header);
		Assert(m_file_header.NumberOfSections > 0);

		nSectionSize     = sizeof(IMAGE_SECTION_HEADER)*m_file_header.NumberOfSections;
		m_section_header = (PIMAGE_SECTION_HEADER)malloc(nSectionSize); Assert(m_section_header);

		bRet = ReadFile(m_file_handle, m_section_header, nSectionSize, &nReaded, 0); Assert(bRet && nReaded == nSectionSize);
		if(!bRet) return -1;

		return 0;
	}

	int  RvaToFileOffset(DWORD nRva, DWORD & nOffset, DWORD & nId)
	{
		if(nOffset > 0)
		{
			PIMAGE_SECTION_HEADER pSection = &m_section_header[nId];
			if(nRva >= pSection->VirtualAddress && nRva <= pSection->VirtualAddress+pSection->Misc.VirtualSize)
			{
				return nRva - nOffset;
			}
		}

		for(int i = 0; i < m_file_header.NumberOfSections; i++)
		{
			PIMAGE_SECTION_HEADER pSection = &m_section_header[i];
			Assert(pSection->Misc.VirtualSize);
			if(nRva >= pSection->VirtualAddress && nRva <= pSection->VirtualAddress+pSection->Misc.VirtualSize)
			{
				Assert(pSection->VirtualAddress >= pSection->PointerToRawData);
				nId     = i;
				nOffset = pSection->VirtualAddress - pSection->PointerToRawData;
				return nRva - nOffset;
			}
		}

		return -1;
	}

	int  ReadFileByRva(int nType, void * pData, int nSize, DWORD nRva, DWORD & nOffset, DWORD & nId)
	{
		DWORD  nFileOffset, nReaded = 0, bRet;
		nFileOffset = RvaToFileOffset(nRva, nOffset, nId);
		if(nFileOffset == -1) return -1;

		SetFilePointer(m_file_handle, nFileOffset, 0, FILE_BEGIN); 
		if(nType == 0)
		{
			bRet = ReadFile(m_file_handle, pData, nSize, &nReaded, 0); Assert(bRet && nReaded == nSize);
			return bRet ? 0 : -1;
		}

		Assert(nType <= 4);
		for(int i = 0, n = nType; i < nSize; i+=n)
		{
			static int j = 0;
			bRet = ReadFile(m_file_handle, (char*)pData+i, n, &nReaded, 0); Assert(bRet && nReaded == n);
			if(memcmp((char*)pData+i, &j, n) == 0) break;
		}
		return bRet ? 0 : -1;
	}


private:
	HANDLE                     m_file_handle;
	BOOL                       m_is_x64;
	DWORD64                    m_image_base;

	IMAGE_DOS_HEADER           m_dos_header;
	IMAGE_FILE_HEADER          m_file_header;
	IMAGE_OPTIONAL_HEADER_PRE  m_option_header;
	IMAGE_OPTIONAL_HEADER_POST m_data_dir;
	PIMAGE_SECTION_HEADER      m_section_header;
};
