#pragma once
#include "CoreHeader.h"
#include "Common.h"

enum eShaderType
{
	kVS,
	kGS,
	kPS,
	kCS
};


class ShaderUnit : public NonCopyable
{
	friend class ShaderCompositor;
public:
	ShaderUnit(const std::filesystem::path& filename, const std::vector<const char*>& defaultDefines, eShaderType type);
	~ShaderUnit() {}

	std::unique_ptr<D3D_SHADER_MACRO[]> GetShaderMacros();

	void SetMacro(const std::string& name, const std::string& def) { mShaderMacros[name] = def; }

	void ReCompile();

	ID3DBlob* GetBlob() const { return mBlob.Get(); }
private:
	eShaderType mType;
	Microsoft::WRL::ComPtr<ID3DBlob> mBlob;
	std::filesystem::path mFilename;
	std::unordered_map<std::string, std::string> mShaderMacros;
};


class ShaderCompositor : public Singleton<ShaderCompositor>
{
	USE_SINGLETON;
public:
	~ShaderCompositor() {}

	void InitAllShaders();

	const ShaderUnit& AddShader(const std::string& shaderName,
		const std::filesystem::path& filename, 
		eShaderType type, 
		const std::vector<const char*>& defaultDefines = {});

	ID3DBlob* GetShaderByteCode(const std::string& shaderName) { return GetShader(shaderName).GetBlob(); }

	ShaderUnit& GetShader(const std::string& shaderName);
	
	std::filesystem::path GetFinalRootPath() const { return std::filesystem::current_path() / mRootPath; }
private:
	ShaderCompositor(const std::filesystem::path& rootPath) : mRootPath(rootPath) {}

	void CompileShader(std::filesystem::path realPath, ShaderUnit& shaderUnit);
private:
	std::filesystem::path mRootPath;
	std::unordered_map<std::string, ShaderUnit> mShaders;
};

#define ADD_SHADER(shaderName, filename, type, ...) (ShaderCompositor::GetInstance()->AddShader(shaderName, filename, type, __VA_ARGS__))
#define GET_SHADER(shaderName) ShaderCompositor::GetInstance()->GetShader(shaderName)
