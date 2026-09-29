export module games.generalszh.gameplay.appearance.systems.extension_look_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.appearance.components.appearance;
export import games.generalszh.gameplay.appearance.components.building_extensions;
export import engine.gameplay.rts.economy.components.overcharge;
import games.generalszh.content.objects.model_conditions;

// RadarUpdate::update and PowerPlantUpdate::update, each tick in parallel: an
// extending radar dish shows RADAR_EXTENDING until the tick after it is done
// (getFrame() > m_extendDoneFrame), then RADAR_UPGRADED; extending control rods
// show POWER_PLANT_UPGRADING until the tick they are done (their wake frame),
// then POWER_PLANT_UPGRADED; retracted rods show neither. An overcharge switched
// off draws them in at once (PowerPlantUpdate::extendRods(FALSE)).
export namespace generalszh::gameplay
{
struct ExtensionLookSystem
{
	using Query = ecs::Query<ecs::Write<engine::gameplay::Appearance>, ecs::OptionalWrite<RadarDish>, ecs::OptionalWrite<ControlRods>, ecs::OptionalWrite<engine::gameplay::Overcharge>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		constexpr std::uint32_t radarExtending = content::ModelConditionBit("RADAR_EXTENDING"), radarUpgraded = content::ModelConditionBit("RADAR_UPGRADED"),
								rodsUpgrading = content::ModelConditionBit("POWER_PLANT_UPGRADING"),
								rodsUpgraded = content::ModelConditionBit("POWER_PLANT_UPGRADED");
		const std::uint64_t now = context.Tick();
		auto appearances = chunk.Get<engine::gameplay::Appearance>();
		auto dishes = chunk.Get<RadarDish>();
		auto rods = chunk.Get<ControlRods>();
		auto overcharges = chunk.Get<engine::gameplay::Overcharge>();
		if (dishes.empty() && rods.empty())
			return;
		for (std::size_t row = 0; row < appearances.size(); ++row)
		{
			engine::gameplay::Appearance &look = appearances[row];
			if (!dishes.empty())
			{
				RadarDish &dish = dishes[row];
				if (dish.state == ExtensionState::Extending && now > dish.doneTick)
					dish.state = ExtensionState::Extended;
				look.Set(radarExtending, dish.state == ExtensionState::Extending);
				look.Set(radarUpgraded, dish.state == ExtensionState::Extended);
			}
			if (!rods.empty())
			{
				ControlRods &rod = rods[row];
				if (!overcharges.empty() && overcharges[row].retract != 0)
				{
					rod.state = ExtensionState::Retracted;
					overcharges[row].retract = 0;
				}
				if (rod.state == ExtensionState::Extending && now >= rod.doneTick)
					rod.state = ExtensionState::Extended;
				look.Set(rodsUpgrading, rod.state == ExtensionState::Extending);
				look.Set(rodsUpgraded, rod.state == ExtensionState::Extended);
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::ExtensionLookSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.extension_look";
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
