#pragma once
#include "CameraController.h"

class Model;

namespace Math
{
	class BaseCamera;
}

class Scene
{
public:
	Scene() {}
	~Scene() {}

	std::vector<std::shared_ptr<Model>> mModels;
	std::unique_ptr<BaseCamera> mSceneCamera;
};
