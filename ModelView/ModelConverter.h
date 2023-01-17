#pragma once

namespace glTF
{
	class Asset;
}

/*
	Convert glTF Model To Renderer Model
*/
namespace ModelConverter
{
	void BuildMaterials(glTF::Asset& asset);

	void BuildAllMeshes(glTF::Asset& asset);
};

