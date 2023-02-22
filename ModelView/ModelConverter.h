#pragma once
#include <filesystem>
#include <string>

namespace glTF
{
	class Asset;
}

class Scene;

/*
	Convert glTF Model To Renderer Model
*/
namespace ModelConverter
{
	std::filesystem::path GetIBLTextureFilename(const std::wstring& name);

	void BuildMaterials(const glTF::Asset& asset);

	void BuildAllMeshes(const glTF::Asset& asset);

	void BuildScene(Scene* scene, const glTF::Asset& asset);
};
