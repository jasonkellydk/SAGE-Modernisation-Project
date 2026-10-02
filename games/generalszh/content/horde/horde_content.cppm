export module games.generalszh.content.horde.horde_content;
import std;

export import engine.gameplay.rts.horde.resources.horde_catalog;
export import games.generalszh.content.objects.object_definition;

// An object's HordeUpdate module (UpdateRate in milliseconds, rounded up to
// ticks as INI::parseDurationUnsignedInt; KindOf, Count, Radius, RubOffRadius,
// AlliesOnly, ExactMatch, AllowedNationalism; the retail action is HORDE), with
// its kinds and bounding sphere.
export namespace generalszh::content
{
inline engine::gameplay::HordeDefinition ReadObjectHorde(const ObjectDefinition &object, std::uint64_t ticksPerSecond)
{
	engine::gameplay::HordeDefinition horde;
	horde.kinds = {object.kinds[0], object.kinds[1]};
	horde.infantry = object.Is("INFANTRY");
	horde.sphereRadius = BoundingSphereRadius(object.geometry);
	horde.centerHeight = object.geometry.shape == GeometryShape::Sphere ? Engine::Math::Fixed{} : object.geometry.height / Engine::Math::Fixed::FromInt(2);
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "HordeUpdate")
			continue;
		horde.hordes = true;
		const auto number = [&](std::string_view key) -> std::optional<Engine::Math::Fixed> {
			const auto *node = module.block->Find(key);
			return node != nullptr ? engine::config::values::ParseFixed(node->Value()) : std::nullopt;
		};
		const auto flag = [&](std::string_view key, bool fallback) {
			const auto *node = module.block->Find(key);
			return node != nullptr ? engine::config::values::ParseBool(node->Value()).value_or(fallback) : fallback;
		};
		if (const auto rate = number("UpdateRate"))
			horde.updateTicks = static_cast<std::uint64_t>((*rate * Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(ticksPerSecond)) /
				Engine::Math::Fixed::FromInt(1000)).Ceil());
		else
			horde.updateTicks = ticksPerSecond; // DEFAULT_UPDATE_RATE: LOGICFRAMES_PER_SECOND
		if (const auto *kinds = module.block->Find("KindOf"))
			for (const std::string_view name : kinds->values)
				if (const std::size_t bit = KindOfBit(name); bit < KindOfNames.size())
					horde.requiredKinds[bit / 64] |= std::uint64_t{1} << (bit % 64);
		horde.count = static_cast<std::uint32_t>(std::max<std::int64_t>(0, number("Count").value_or(Engine::Math::Fixed{}).Floor()));
		horde.radius = number("Radius").value_or(Engine::Math::Fixed{});
		horde.rubOffRadius = number("RubOffRadius").value_or(Engine::Math::Fixed::FromInt(20));
		horde.alliesOnly = flag("AlliesOnly", true);
		horde.exactMatch = flag("ExactMatch", false);
		horde.upgradeBonusAllowed = flag("AllowedNationalism", true);
		break;
	}
	return horde;
}
}
