export module Assets.States;
import std;

namespace Assets
{

export enum class AssetState : std::uint8_t
{
	Unloaded,
	Loading,
	Ready,
	Failed
};

}
