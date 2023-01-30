#pragma once

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
	void BuildMaterials(const glTF::Asset& asset);

	void BuildAllMeshes(const glTF::Asset& asset);

	Scene* BuildScene(const glTF::Asset& asset);
};
