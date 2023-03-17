#include "ShaderCompositor.h"
#include "Utils/DebugUtils.h"
#include "Utils/ThreadPoolExecutor.h"
#include <d3dcompiler.h>

namespace
{
	using TaskType = std::future<void>;
	std::queue<TaskType> mTaskQueue;
	std::mutex sTaskMutex;
	bool sShaderCompositorInited = true;
}


constexpr std::pair<const char*, const char*> GetShaderEntryAndTarget(eShaderType type)
{
	switch (type)
	{
	case kVS:
		return { "main", "vs_5_1" };
	case kGS:
		return { "main", "gs_5_1" };
	case kPS:
		return { "main", "ps_5_1" };
	case kCS:
		return { "main", "cs_5_1" };
	default:
		return{ "", "" };
	}
}

Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(const std::wstring& filename, const D3D_SHADER_MACRO* defines,
	const char* entrypoint, const char* target)
{
	UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)  
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	HRESULT hr = S_OK;

	Microsoft::WRL::ComPtr<ID3DBlob> byteCode = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errors;
	hr = D3DCompileFromFile(filename.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entrypoint, target, compileFlags, 0, byteCode.GetAddressOf(), errors.GetAddressOf());

	if (errors)
		OutputDebugStringA((char*)errors->GetBufferPointer());

	CheckHR(hr);

	return byteCode;
}


// -- ShaderCompositor --
void ShaderCompositor::InitAllShaders()
{
	while (!mTaskQueue.empty())
	{
		mTaskQueue.front().get();
		mTaskQueue.pop();
	}

	sShaderCompositorInited = true;
}

const ShaderUnit& ShaderCompositor::AddShader(const std::string& shaderName,
	const std::filesystem::path& filename,
	eShaderType type,
	const std::vector<std::string>& defaultDefines)
{
	WARN_IF_NOT(sShaderCompositorInited, L"Async Load Shader is False!");

	std::filesystem::path realPath(GetFinalRootPath() / filename);
	ASSERT(std::filesystem::exists(realPath));

	auto iter = mShaders.find(shaderName);
	if (mShaders.find(shaderName) != mShaders.end())
		return iter->second;
	
	auto insertIter = mShaders.emplace(std::piecewise_construct,
										std::forward_as_tuple(shaderName),
										std::forward_as_tuple(filename, defaultDefines, type));

	//CompileShader(realPath, insertIter.first->second);
	if (!sShaderCompositorInited)
		mTaskQueue.emplace(Utility::gThreadPoolExecutor.Submit(
			&ShaderCompositor::CompileShader, this, realPath, std::ref(insertIter.first->second)));
	else
		CompileShader(realPath, insertIter.first->second);

	return insertIter.first->second;
}

ShaderUnit& ShaderCompositor::GetShader(const std::string& shaderName)
{
	ASSERT(sShaderCompositorInited);

	auto findIter = mShaders.find(shaderName);
	ASSERT(findIter != mShaders.end());
	return findIter->second;
}

void ShaderCompositor::CompileShader(std::filesystem::path realPath, ShaderUnit& shaderUnit)
{
	auto [entryPoint, target] = GetShaderEntryAndTarget(shaderUnit.mType);
	shaderUnit.mBlob = ::CompileShader(realPath, shaderUnit.GetShaderMacros().get(), entryPoint, target);
}


// -- ShaderUnit --
ShaderUnit::ShaderUnit(const std::filesystem::path& filename, const std::vector<std::string>& defaultDefines, eShaderType type) :
	mType(type), mBlob(nullptr), mFilename(filename)
{
	for (size_t i = 0; i < defaultDefines.size(); i+=2)
		mShaderMacros[defaultDefines[i]] = defaultDefines[i + 1];
}

std::unique_ptr<D3D_SHADER_MACRO[]> ShaderUnit::GetShaderMacros()
{
	D3D_SHADER_MACRO* macros = new D3D_SHADER_MACRO[mShaderMacros.size() + 2];
	D3D_SHADER_MACRO* temp = macros;
	for (auto& macro : mShaderMacros)
	{
		temp->Name = macro.first.c_str();
		temp->Definition = macro.second.c_str();
		temp++;
	}
	temp->Name = NULL;
	temp->Definition = NULL;
	return std::unique_ptr<D3D_SHADER_MACRO[]>(macros);
}

void ShaderUnit::ReCompile()
{
	auto [entryPoint, target] = GetShaderEntryAndTarget(mType);
	mBlob = CompileShader(mFilename, GetShaderMacros().get(), entryPoint, target);
}
