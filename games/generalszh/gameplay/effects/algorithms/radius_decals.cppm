export module games.generalszh.gameplay.effects.algorithms.radius_decals;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.effects.components.radius_decal;
export import games.generalszh.gameplay.effects.resources.radius_decal_looks;
import engine.gameplay.common.identity.components.owner;

export namespace generalszh::gameplay
{
// RadiusDecalTemplate::createRadiusDecal into an object's decal (clearing the one it had): none for a template with no
// texture or a radius not above zero; else laid at `at` for the player controlling the object now, until `until`.
inline void LayRadiusDecal(GameWorld &game, ecs::Entity entity, const content::RadiusDecalLook &look, Engine::Math::Fixed radius,
	Engine::Math::FixedVector3 at, RadiusDecalUntil until)
{
	auto &world = game.world;
	if (!world.IsAlive(entity))
		return;
	if (!world.Has<RadiusDecal>(entity))
		world.Add<RadiusDecal>(entity);
	RadiusDecal &decal = *world.Get<RadiusDecal>(entity);
	decal = RadiusDecal{};
	const auto *looks = world.FindResource<RadiusDecalLooks>();
	const std::uint32_t index = looks != nullptr ? looks->Find(look) : RadiusDecal::None;
	const auto *owner = world.Get<engine::gameplay::Owner>(entity);
	if (index == RadiusDecal::None || radius <= Engine::Math::Fixed{} || owner == nullptr)
		return;
	decal.at = at;
	decal.radius = radius;
	decal.look = index;
	decal.player = owner->player;
	decal.until = until;
}
}
