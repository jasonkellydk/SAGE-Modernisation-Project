export module games.generalszh.gameplay.aircraft.systems.airfield_heal_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.aircraft.components.airfield_healing;
import games.generalszh.gameplay.flight_deck.components.flight_deck;
import engine.gameplay.rts.aircraft.components.jet;
import engine.gameplay.rts.aircraft.components.airfield;
import engine.gameplay.rts.death.components.dying;

// ParkingPlaceBehavior::update's heals, once a tick after the jets have moved: an airfield's healees are its jets (the
// tick's jet roster, DeckJets) parked or reloading on the ground; a new healee or one gone puts the next heal 6 frames off
// (setHealee -> resetWakeFrame); when it comes each healee gets 6 x HealAmountPerSecond / 30 (HEAL_RATE_FRAMES). One
// writer: the airfields in chunk order (few).
export namespace generalszh::gameplay
{
struct AirfieldHealSystem
{
	using Query = ecs::Query<ecs::Write<AirfieldHealing>, ecs::Read<engine::gameplay::Airfield>, ecs::Optional<engine::gameplay::Dying>>;
	using Resources = ecs::Resources<ecs::Read<DeckJets>, ecs::Write<AirfieldHeals>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		auto &heals = context.Write<AirfieldHeals>().list;
		heals.clear();
		const DeckJets &roster = context.Read<DeckJets>();
		const std::uint64_t now = context.Tick();
		query.ForEachChunk([&](auto chunk) {
			auto healings = chunk.template Get<AirfieldHealing>();
			const auto fields = chunk.template Get<gp::Airfield>();
			const bool dying = !chunk.template Get<gp::Dying>().empty();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < healings.size(); ++row)
			{
				if (dying)
					continue;
				AirfieldHealing &field = healings[row];
				const std::uint32_t spaces = std::min<std::uint32_t>(fields[row].spaceCount, 32);
				std::array<ecs::Entity, 32> healee{};
				std::uint32_t healing = 0;
				roster.ForEach([&](const DeckJet &jet) {
					if (jet.carrier != entities[row] || jet.space >= spaces)
						return;
					const auto state = static_cast<gp::JetState>(jet.state);
					if (state == gp::JetState::Parked || state == gp::JetState::Reloading)
					{
						healing |= 1u << jet.space;
						healee[jet.space] = jet.jet;
					}
				});
				if (healing != field.healing)
					field.nextHeal = healing == 0 ? AirfieldHealing::Forever : now + 6;
				field.healing = healing;
				if (healing == 0 || now < field.nextHeal)
					continue;
				field.nextHeal = now + 6;
				const Engine::Math::Fixed amount = field.perSecond * Engine::Math::Fixed::FromInt(6) / Engine::Math::Fixed::FromInt(30);
				for (std::uint32_t space = 0; space < spaces; ++space)
					if ((healing & (1u << space)) != 0)
						heals.push_back({healee[space], amount});
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::AirfieldHealSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.airfield_heals";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
