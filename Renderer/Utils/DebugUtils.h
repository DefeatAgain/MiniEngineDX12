#pragma once
#include "../CoreHeader.h"

#define USE_BREAK


namespace Utility
{
    inline void Print(const char* msg) { OutputDebugStringA(msg); }
    inline void Print(const wchar_t* msg) { OutputDebugString(msg); }

    inline void PrintMessage(const char* format, ...)
    {
        Print("--> ");
        char buffer[256];
        va_list ap;
        va_start(ap, format);
        vsprintf_s(buffer, 256, format, ap);
        va_end(ap);
        Print(buffer);
        Print("\n");
    }
    inline void PrintMessage(const wchar_t* format, ...)
    {
        Print("--> ");
        wchar_t buffer[256];
        va_list ap;
        va_start(ap, format);
        vswprintf(buffer, 256, format, ap);
        va_end(ap);
        Print(buffer);
        Print("\n");
    }

    inline void PrintMessage()
    {
    }

    std::wstring CSTRToWideString(const std::string& str);
    std::string WideStringToCSTR(const std::wstring& wstr);


#define BreakIfFailed( hr ) if (FAILED(hr)) __debugbreak()

	inline void CheckHResult(HRESULT hr, const WCHAR* file = __FILEW__, const DWORD line = __LINE__)
	{
		if (FAILED(hr))
		{
			PrintMessage(L"hr = 0x%08X", hr);
			PrintMessage(L"HRESULT failed in %s Line:%d \n", file, line);
#ifdef USE_BREAK
            __debugbreak();
#endif
		}
	}
};

#define STRINGIFY(x) #x
#define STRINGIFY_BUILTIN(x) STRINGIFY(x)
#define WSTRINGIFY(x) L#x
#define WSTRINGIFY_BUILTIN(x) STRINGIFY(x)
#define PPCAT(A, B) A ## B
#define PPCAT_BUILTIN(A, B) PPCAT(A, B)

#ifdef _DEBUG
    #define CheckHR(hr) \
        { \
            Utility::CheckHResult(hr, __FILEW__, (DWORD)__LINE__); \
        } 

    #define WARN_IF( isTrue, ... ) \
        { \
            if ((bool)(isTrue)) \
            { \
                Utility::Print("\nWarning issued in " STRINGIFY_BUILTIN(__FILE__) " @ " STRINGIFY_BUILTIN(__LINE__) "\n"); \
                Utility::PrintMessage("\'" #isTrue "\' is true"); \
                Utility::PrintMessage(__VA_ARGS__); \
                Utility::Print("\n"); \
            } \
        }

    #define WARN_IF_NOT( isTrue, ... ) WARN_IF(!(isTrue), __VA_ARGS__)

    #define ASSERT(isFalse, ... ) \
        if (!(bool)(isFalse)) \
        { \
            Utility::Print("\nAssertion failed in " STRINGIFY_BUILTIN(__FILE__) " @ " STRINGIFY_BUILTIN(__LINE__) "\n"); \
            Utility::PrintMessage("\'" #isFalse "\' is false"); \
            Utility::PrintMessage(__VA_ARGS__); \
            Utility::Print("\n"); \
            __debugbreak(); \
        }
#else
    #define CheckHR(hr) (void)(hr)
    #define WARN_IF(isTrue, ... ) (void)(isTrue)
    #define WARN_IF_NOT(isTrue, ... ) (void)(!isTrue)
    #define ASSERT(isFalse, ... ) (void)(isFalse)
#endif // _DEBUG
