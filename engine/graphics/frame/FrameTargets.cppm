export module Graphics.FrameTargets;

export import Graphics.RHI;

namespace Graphics
{

export struct FrameTargets final
{
	RHIBackbuffer backbuffer{};
	RHIDepthTarget depth{};
};

}
