module;
#include <memory>
#include <vector>

module games.generalszh.hosts.game.asset_source;

import Assets.Adapters.W3D;

namespace generalszh::host
{
std::vector<std::shared_ptr<const Assets::IModelAdapter>> GameModelAdapters()
{
	return {std::make_shared<Assets::W3DAdapter>()};
}
}
