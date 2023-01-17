#include "DebugUtils.h"

std::wstring Utility::CSTRToWideString(const std::string& pSrc)
{
	int ssize = MultiByteToWideChar(CP_ACP, 0, pSrc.c_str(), -1, nullptr, 0);
	ASSERT(ssize > 0);
	std::wstring pDest(ssize, '0');
	MultiByteToWideChar(CP_ACP, 0, pSrc.c_str(), -1, pDest.data(), ssize);
	return pDest;
}

std::string Utility::WideStringToCSTR(const std::wstring& pSrc)
{
	DWORD ssize = WideCharToMultiByte(CP_OEMCP, 0, pSrc.c_str(), -1, nullptr, 0, nullptr, FALSE);
	ASSERT(ssize > 0);
	std::string pDest(ssize, '0');
	WideCharToMultiByte(CP_OEMCP, 0, pSrc.c_str(), -1, pDest.data(), ssize, nullptr, FALSE);
	return pDest;
}
