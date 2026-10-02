export module games.generalszh.content.containment.combat_drop_content;
import std;

export import games.generalszh.content.objects.object_definition;
export import Engine.Core.Math.Fixed;
export import engine.config.binding.schema;

// ChinookAIUpdate's combat drop module data (ChinookAIUpdateModuleData: its field table and constructor defaults):
//   RopeName: the rope's drawable ("GenericRope"); NumRopes (4);
//   PerRopeDelayMin / PerRopeDelayMax: milliseconds up to whole frames (parseDurationUnsignedInt), how long a rope waits
//     after a rappeller before the next goes down it (0x7fffffff frames each, unconverted, when not given);
//   RopeWidth (0.5), RopeColor (0.9, 0.8, 0.7; RGBColor: each 0-255 over 255), RopeWobbleLen (10),
//     RopeWobbleAmplitude (1), RopeWobbleRate (degrees a second to radians a frame; 0.1 unconverted when not given);
//   RopeDropSpeed: how fast a rope may unroll, a second to a frame (1e10 unconverted when not given: no limit);
//   RopeFinalHeight: how far above the ground a rope stops (0);
//   RappelSpeed: how fast rappellers come down, a second to a frame (|gravity| x 30 x 0.5 when not given);
//   MinDropHeight: how far over a building it hovers at least (30);
//   WaitForRopesToDrop (yes): nobody goes down a rope until it is all the way out.
export namespace generalszh::content
{
struct CombatDropContent
{
	std::string ropeName{"GenericRope"};
	std::uint32_t numRopes{4};
	std::uint64_t perRopeDelayMin{0x7fffffff};
	std::uint64_t perRopeDelayMax{0x7fffffff};
	Engine::Math::Fixed ropeWidth{Engine::Math::Fixed::FromRatio(1, 2)};
	std::array<Engine::Math::Fixed, 3> ropeColor{Engine::Math::Fixed::FromRatio(9, 10), Engine::Math::Fixed::FromRatio(8, 10), Engine::Math::Fixed::FromRatio(7, 10)};
	Engine::Math::Fixed ropeWobbleLen{Engine::Math::Fixed::FromInt(10)};
	Engine::Math::Fixed ropeWobbleAmplitude{Engine::Math::Fixed::One()};
	Engine::Math::Fixed ropeWobbleRate{Engine::Math::Fixed::FromRatio(1, 10)}; // radians a frame
	Engine::Math::Fixed ropeDropSpeed{Engine::Math::Fixed::FromInt(1000000)}; // 1e10 in the original: never reached
	Engine::Math::Fixed ropeFinalHeight;
	Engine::Math::Fixed rappelSpeed{Engine::Math::Fixed::FromRatio(64 * 30, 900 * 2)}; // |gravity| x 30 x 0.5
	Engine::Math::Fixed minDropHeight{Engine::Math::Fixed::FromInt(30)};
	bool waitForRopesToDrop{true};
};

inline std::optional<CombatDropContent> ReadCombatDrop(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	using Engine::Math::Fixed;
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "ChinookAIUpdate")
			continue;
		CombatDropContent out;
		const auto perSecond = Fixed::FromInt(static_cast<std::int64_t>(step.TicksPerSecond()));
		for (const engine::config::Node &field : module.block->children)
		{
			if (field.values.empty())
				continue;
			const std::string_view key = field.key;
			const auto fixed = [&] { return engine::config::values::ParseFixed(field.Value()); };
			const auto real = [&] { return engine::config::values::ParseFixed(field.Value()).value_or(Fixed{}); };
			// INI::parseDurationUnsignedInt: ceilf(ms * frames a second / 1000).
			const auto frames = [&] {
				const std::int64_t ms = std::max<std::int64_t>(0, engine::config::values::ParseInt(field.Value()).value_or(0));
				return static_cast<std::uint64_t>((ms * static_cast<std::int64_t>(step.TicksPerSecond()) + 999) / 1000);
			};
			if (key == "RopeName")
				out.ropeName = std::string(field.Value());
			else if (key == "NumRopes")
				out.numRopes = static_cast<std::uint32_t>(std::max<std::int64_t>(0, engine::config::values::ParseInt(field.Value()).value_or(4)));
			else if (key == "PerRopeDelayMin")
				out.perRopeDelayMin = frames();
			else if (key == "PerRopeDelayMax")
				out.perRopeDelayMax = frames();
			else if (key == "RopeWidth")
				out.ropeWidth = real();
			else if (key == "RopeColor")
			{
				// RGBColor: R:<0-255> G:<0-255> B:<0-255>, each over 255.
				for (const std::string_view token : field.values)
				{
					const auto colon = token.find(':');
					if (colon == std::string_view::npos)
						continue;
					const char channel = static_cast<char>(std::toupper(static_cast<unsigned char>(token.front())));
					const Fixed value = Fixed::FromRatio(engine::config::values::ParseInt(token.substr(colon + 1)).value_or(0), 255);
					if (channel == 'R')
						out.ropeColor[0] = value;
					else if (channel == 'G')
						out.ropeColor[1] = value;
					else if (channel == 'B')
						out.ropeColor[2] = value;
				}
			}
			else if (key == "RopeWobbleLen")
				out.ropeWobbleLen = real();
			else if (key == "RopeWobbleAmplitude")
				out.ropeWobbleAmplitude = real();
			else if (key == "RopeWobbleRate")
				out.ropeWobbleRate = real() * Fixed::FromRatio(314159265, 100000000) / Fixed::FromInt(180 * 30); // parseAngularVelocityReal
			else if (key == "RopeDropSpeed")
			{
				if (const auto value = fixed())
					out.ropeDropSpeed = *value / perSecond;
			}
			else if (key == "RopeFinalHeight")
				out.ropeFinalHeight = fixed().value_or(out.ropeFinalHeight);
			else if (key == "RappelSpeed")
			{
				if (const auto value = fixed())
					out.rappelSpeed = *value / perSecond;
			}
			else if (key == "MinDropHeight")
				out.minDropHeight = fixed().value_or(out.minDropHeight);
			else if (key == "WaitForRopesToDrop")
				out.waitForRopesToDrop = engine::config::values::ParseBool(field.Value()).value_or(true);
		}
		return out;
	}
	return std::nullopt;
}
}
