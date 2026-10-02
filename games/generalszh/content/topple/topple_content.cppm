export module games.generalszh.content.topple.topple_content;
import std;

export import engine.gameplay.rts.topple.components.topple;
export import games.generalszh.content.objects.object_definition;

// An object's toppling, from its ToppleUpdate module: when it dies (as it
// starts or once it is down), whether it only falls sideways, the shares of
// the crusher's speed it falls with, and what it plays and leaves: the FX as
// it starts and as it bounces, and a stump object left standing.
export namespace generalszh::content
{
struct ObjectTopple
{
	engine::gameplay::Topple topple;
	std::string toppleFX;
	std::string bounceFX;
	std::string stump;
};

std::optional<ObjectTopple> ReadObjectTopple(const ObjectDefinition &object)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr || module.type != "ToppleUpdate")
			continue;
		const engine::config::Node &block = *module.block;
		namespace flag = engine::gameplay::topple_flag;
		ObjectTopple result;
		auto &topple = result.topple;
		const auto yes = [&](std::string_view key, bool fallback) {
			const auto *node = block.Find(key);
			return node != nullptr ? engine::config::values::ParseBool(node->Value()).value_or(fallback) : fallback;
		};
		const auto share = [&](std::string_view key, Engine::Math::Fixed fallback) {
			const auto *node = block.Find(key);
			if (node == nullptr)
				return fallback;
			std::string_view text = node->Value();
			if (!text.empty() && text.back() == '%')
				text.remove_suffix(1);
			const auto value = engine::config::values::ParseFixed(text);
			return value ? *value / Engine::Math::Fixed::FromInt(100) : fallback;
		};
		const auto text = [&](std::string_view key) {
			const auto *node = block.Find(key);
			return node != nullptr ? std::string(node->Value()) : std::string{};
		};
		topple.flags = (yes("KillWhenStartToppling", false) ? flag::KillWhenStarting : 0u) | (yes("KillWhenFinishedToppling", true) ? flag::KillWhenDown : 0u) |
			(yes("ToppleLeftOrRightOnly", false) ? flag::LeftOrRightOnly : 0u) | (yes("ReorientToppledRubble", false) ? flag::ReorientRubble : 0u);
		topple.initialVelocity = share("InitialVelocityPercent", topple.initialVelocity);
		topple.initialAcceleration = share("InitialAccelPercent", topple.initialAcceleration);
		topple.bounceVelocity = share("BounceVelocityPercent", topple.bounceVelocity);
		result.toppleFX = text("ToppleFX");
		result.bounceFX = text("BounceFX");
		result.stump = text("StumpName");
		return result;
	}
	return std::nullopt;
}

// A map tree's own falling and bending, from its W3DTreeDraw module (the tree buffer topples and pushes it aside on
// the client): whether crushers topple it and it then sinks away, the shares of the topple speed it falls with, the
// slowest it falls, how long and how far it sinks, how long it takes to lean out of a passing unit's way and back and
// how far (per unit of height), and the FX as it starts to fall and as it bounces. Durations in logic frames.
struct TreeDrawMotion
{
	bool doTopple{false};
	bool killWhenToppled{true};
	Engine::Math::Fixed initialVelocity{Engine::Math::Fixed::FromRatio(2, 10)};
	Engine::Math::Fixed initialAcceleration{Engine::Math::Fixed::FromRatio(1, 100)};
	Engine::Math::Fixed bounceVelocity{Engine::Math::Fixed::FromRatio(3, 10)};
	Engine::Math::Fixed minimumToppleSpeed{Engine::Math::Fixed::FromRatio(1, 2)};
	std::uint32_t sinkFrames{10 * 30};
	Engine::Math::Fixed sinkDistance{Engine::Math::Fixed::FromInt(20)};
	std::uint32_t framesToMoveOutward{1};
	std::uint32_t framesToMoveInward{1};
	Engine::Math::Fixed maxOutwardMovement{Engine::Math::Fixed::One()};
	std::string toppleFX;
	std::string bounceFX;
	bool doShadow{false}; // DoShadow: the tree buffer casts its shadow
	// DarkeningFactor: a tree pushed aside darkens by this much of how far it leans (W3DTreeBuffer: 1 - it x pushAside).
	Engine::Math::Fixed darkening;
	// TextureName: the texture the tree buffer draws it with, in place of its model's own.
	std::string texture;
};

std::optional<TreeDrawMotion> ReadTreeDraw(const ObjectDefinition &object)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Draw || module.block == nullptr || module.type != "W3DTreeDraw")
			continue;
		const engine::config::Node &block = *module.block;
		TreeDrawMotion result;
		const auto yes = [&](std::string_view key, bool fallback) {
			const auto *node = block.Find(key);
			return node != nullptr ? engine::config::values::ParseBool(node->Value()).value_or(fallback) : fallback;
		};
		const auto number = [&](std::string_view key, Engine::Math::Fixed fallback, bool percent) {
			const auto *node = block.Find(key);
			if (node == nullptr)
				return fallback;
			std::string_view text = node->Value();
			if (!text.empty() && text.back() == '%')
				text.remove_suffix(1);
			const auto value = engine::config::values::ParseFixed(text);
			return value ? (percent ? *value / Engine::Math::Fixed::FromInt(100) : *value) : fallback;
		};
		// INI::parseDurationUnsignedInt: milliseconds to logic frames, rounded up.
		const auto frames = [&](std::string_view key, std::uint32_t fallback) {
			const auto *node = block.Find(key);
			if (node == nullptr)
				return fallback;
			const auto value = engine::config::values::ParseFixed(node->Value());
			if (!value || *value < Engine::Math::Fixed{})
				return fallback;
			return static_cast<std::uint32_t>((*value * Engine::Math::Fixed::FromInt(30) / Engine::Math::Fixed::FromInt(1000)).Ceil());
		};
		const auto text = [&](std::string_view key) {
			const auto *node = block.Find(key);
			return node != nullptr ? std::string(node->Value()) : std::string{};
		};
		result.doTopple = yes("DoTopple", false);
		result.doShadow = yes("DoShadow", false);
		result.darkening = number("DarkeningFactor", Engine::Math::Fixed{}, false);
		result.texture = text("TextureName");
		result.killWhenToppled = yes("KillWhenFinishedToppling", true);
		result.initialVelocity = number("InitialVelocityPercent", result.initialVelocity, true);
		result.initialAcceleration = number("InitialAccelPercent", result.initialAcceleration, true);
		result.bounceVelocity = number("BounceVelocityPercent", result.bounceVelocity, true);
		result.minimumToppleSpeed = number("MinimumToppleSpeed", result.minimumToppleSpeed, false);
		result.sinkDistance = number("SinkDistance", result.sinkDistance, false);
		result.sinkFrames = frames("SinkTime", result.sinkFrames);
		result.framesToMoveOutward = frames("MoveOutwardTime", result.framesToMoveOutward);
		result.framesToMoveInward = frames("MoveInwardTime", result.framesToMoveInward);
		result.maxOutwardMovement = number("MoveOutwardDistanceFactor", result.maxOutwardMovement, false);
		result.toppleFX = text("ToppleFX");
		result.bounceFX = text("BounceFX");
		return result;
	}
	return std::nullopt;
}
}
