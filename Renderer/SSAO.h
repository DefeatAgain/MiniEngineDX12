#pragma once
#include "DescriptorHandle.h"

class ComputeCommandList;
class ColorBuffer;
class DepthBuffer;


namespace SSAORenderer
{
	void Initialize();

	void RenderTaskSSAO(ComputeCommandList& ghCommandList, DescriptorHandle GBufferSRVandDepthSRV, 
		ColorBuffer& Gbuffer0, DepthBuffer& depthBuffer);

	DescriptorHandle GetSSAOFinalHandle(size_t frameIndex);
};
