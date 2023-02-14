#pragma once

#define NOMINMAX

// simplify windows.h
#define NODRAWTEXT
#define NOGDI
#define NOBITMAP
#define NOMCX
#define NOSERVICE
#define NOHELP
#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <wrl/client.h>
#include <wrl/event.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dx12.h>
#ifdef _DEBUG
	#include <dxgidebug.h>
#endif

#include <map>
#include <set>
#include <vector>
#include <array>
#include <queue>
#include <memory>
#include <string>
#include <cstdio>
#include <cwctype>
#include <numeric>
#include <functional>
#include <thread>
#include <future>
#include <mutex>
#include <algorithm>
#include <filesystem>

#pragma comment(lib, "runtimeobject.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "zlib.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "D3DCompiler.lib")
#pragma comment(lib, "WinPixEventRuntime.lib")
#pragma comment(lib, "user32.lib")

#define D3D12_VIRTUAL_ADDRESS_NULL ((D3D12_GPU_VIRTUAL_ADDRESS)0)
#define D3D12_DEFAULT_SAMEPLE_MASK 0xFFFFFFFF

#define MAX_DESCRIPTOR_HEAP_SIZE 1024
#define MAX_DESCRIPTOR_ALLOC_CACHE_SIZE 16
#define MAX_BARRIERS_CACHE_FLUSH 16
#define MAX_ALLOCATOR_PAGES 16

#define SWAP_CHAIN_BUFFER_COUNT 3
#define SWAP_CHAIN_FORMAT DXGI_FORMAT_R10G10B10A2_UNORM
#define HDR_FORMAT DXGI_FORMAT_R11G11B10_FLOAT
#define DSV_FORMAT DXGI_FORMAT_D16_UNORM

#include "tracy/Tracy.hpp"
