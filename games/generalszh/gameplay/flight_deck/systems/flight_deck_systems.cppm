export module games.generalszh.gameplay.flight_deck.systems.flight_deck_systems;
import std;
export import games.generalszh.gameplay.aircraft.components.airfield_healing;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.flight_deck.components.flight_deck;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import games.generalszh.gameplay.effects.resources.effect_cues;
export import engine.gameplay.rts.aircraft.components.jet;
export import engine.gameplay.rts.aircraft.components.airfield;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.appearance.components.appearance;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.production.components.production_queue;
import games.generalszh.content.objects.model_conditions;

// The flight decks, once a tick after the jets have moved (PostSimulation):
//   DeckRosterSystem: every jet of a flight deck, its space, where it is in its cycle and where it stands (DeckJets).
//   FlightDeckSystem (FlightDeckBehavior::update), per carrier, in this order:
//     its jets made on its first update (buildInfo: one in every space);
//     its order passed on to its jets off the deck when it was given (propagateOrdersToPlanes);
//     heals: its parked and reloading jets are its healees; a new healee puts the next heal 6 frames off
//       (resetWakeFrame); each heal (every 6 frames, HEAL_RATE_FRAMES) gives each 6 x HealAmountPerSecond / 30;
//     every ParkingCleanupPeriod the queue moves up: going through the spaces in order, where a space is empty or its jet
//       may give it up (off the deck), the jet in the next row of its runway, if it can move forward (parked or
//       reloading), swaps spaces with it and taxies up; one swap per runway a pass, the next pass then HumanFollowPeriod
//       on;
//     with a space empty, nothing in production and its time come, a replacement is queued (PayloadTemplate) and the
//       next may come ReplacementDelay + DockAnimationDelay on;
//     per runway: its front jet parked at the runway's start with an order to launch on (guard, attack position, attack
//       move, or attack while its target stands: else the order is forgotten) and the wave's time come raises the ramp
//       (DOOR_{2+runway}_OPENING) LaunchRampDelay long; up, the jet gets the order and goes, the next wave LaunchWaveDelay
//       on, the catapult's steam CatapultFireDelay on (Runway{N}CatapultSystem at the runway's start: at its first update
//       too, the original's quirk) and the ramp down LowerRampDelay on (DOOR_{2+runway}_CLOSING).
// What reaches beyond the carrier goes out as FlightDeckEvents (made after the step).
export namespace generalszh::gameplay
{
namespace flight_deck_detail
{
namespace gp = engine::gameplay;

// JetAIUpdate: off the deck (isAirborneTarget) it may give up its space.
inline bool GivesUpSpace(const DeckJet *jet)
{
	if (jet == nullptr)
		return true;
	const auto state = static_cast<gp::JetState>(jet->state);
	return state == gp::JetState::Flying || state == gp::JetState::Returning || state == gp::JetState::AwaitLanding ||
		state == gp::JetState::ReturnToDeadAirfield || state == gp::JetState::CirclingDeadAirfield;
}

// isAbleToMoveForward: on the deck, idle or reloading.
inline bool MovesForward(const DeckJet *jet)
{
	if (jet == nullptr)
		return false;
	const auto state = static_cast<gp::JetState>(jet->state);
	return state == gp::JetState::Parked || state == gp::JetState::Reloading;
}
}

struct DeckRosterSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Jet>, ecs::Read<engine::gameplay::Transform>, ecs::Optional<engine::gameplay::Armament>>;
	using Lookup = ecs::Lookup<ecs::Read<FlightDeck>, ecs::Read<AirfieldHealing>>;
	using Resources = ecs::Resources<ecs::Write<DeckJets>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<DeckJets>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		auto &out = context.Write<DeckJets>().Slot(context);
		const auto lookup = context.Lookup<Lookup>();
		const auto jets = chunk.Get<gp::Jet>();
		const auto transforms = chunk.Get<gp::Transform>();
		const auto armaments = chunk.Get<gp::Armament>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < jets.size(); ++row)
		{
			const gp::Jet &jet = jets[row];
			// A flight deck's jets, and those of an airfield that repairs them (ParkingPlaceBehavior HealAmountPerSecond).
			if (!lookup.IsAlive(jet.airfield) || (lookup.Get<FlightDeck>(jet.airfield) == nullptr && lookup.Get<AirfieldHealing>(jet.airfield) == nullptr))
				continue;
			DeckJet entry;
			entry.carrier = jet.airfield;
			entry.jet = entities[row];
			entry.space = jet.space;
			entry.state = static_cast<std::uint32_t>(jet.state);
			entry.at = transforms[row].position;
			entry.loaded = armaments.empty() || armaments[row].readyTick != gp::OutOfAmmo ? 1 : 0;
			out.push_back(entry);
		}
	}
};

struct FlightDeckSystem
{
	using Query = ecs::Query<ecs::Write<FlightDeck>, ecs::Read<engine::gameplay::Airfield>, ecs::Read<engine::gameplay::DefinitionRef>,
		ecs::OptionalWrite<engine::gameplay::Appearance>, ecs::Optional<engine::gameplay::ProductionQueue>, ecs::Optional<engine::gameplay::Dying>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Health>, ecs::Read<engine::gameplay::Dying>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Read<DeckJets>, ecs::Write<FlightDeckEvents>, ecs::Write<EffectCues>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using namespace flight_deck_detail;
		using Engine::Math::Fixed;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const DeckJets &roster = context.Read<DeckJets>();
		FlightDeckEvents &events = context.Write<FlightDeckEvents>();
		events.Reset(1);
		auto &out = events.SlotAt(0);
		auto &cues = context.Write<EffectCues>().list;
		const auto lookup = context.Lookup<Lookup>();
		const std::uint64_t now = context.Tick();
		const auto standing = [&](ecs::Entity entity) {
			if (!lookup.IsAlive(entity) || lookup.Get<gp::Dying>(entity) != nullptr)
				return false;
			const auto *health = lookup.Get<gp::Health>(entity);
			return health == nullptr || !gp::IsDead(*health);
		};
		query.ForEachChunk([&](auto chunk) {
			auto decks = chunk.template Get<FlightDeck>();
			const auto fields = chunk.template Get<gp::Airfield>();
			const auto definitions = chunk.template Get<gp::DefinitionRef>();
			auto appearances = chunk.template Get<gp::Appearance>();
			const auto queues = chunk.template Get<gp::ProductionQueue>();
			const auto dyings = chunk.template Get<gp::Dying>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < decks.size(); ++row)
			{
				if (!dyings.empty())
					continue;
				FlightDeck &deck = decks[row];
				const gp::Airfield &field = fields[row];
				const ecs::Entity carrier = entities[row];
				const content::FlightDeckContent *config = templates.FlightDeckOf(definitions[row].index);
				if (config == nullptr)
					continue;
				if (deck.built == 0)
				{
					deck.built = 1;
					out.push_back({FlightDeckEvent::Kind::MakeJets, carrier});
				}
				// Its jets by space (purgeDead: the dead hold none).
				std::array<const DeckJet *, gp::Airfield::MaxSpaces> occupant{};
				roster.ForEach([&](const DeckJet &jet) {
					if (jet.carrier == carrier && jet.space < field.spaceCount && standing(jet.jet))
						occupant[jet.space] = &jet;
				});
				// Its order to its jets off the deck.
				if (deck.propagate != 0)
				{
					deck.propagate = 0;
					for (std::uint32_t space = 0; space < field.spaceCount; ++space)
						if (occupant[space] != nullptr && GivesUpSpace(occupant[space]))
							out.push_back({FlightDeckEvent::Kind::Order, carrier, occupant[space]->jet, {}, space, 0, deck.command, deck.target, deck.position});
				}
				// Heals.
				std::uint32_t healing = 0;
				for (std::uint32_t space = 0; space < field.spaceCount && space < 32; ++space)
					if (occupant[space] != nullptr)
					{
						const auto state = static_cast<gp::JetState>(occupant[space]->state);
						if (state == gp::JetState::Parked || state == gp::JetState::Reloading)
							healing |= 1u << space;
					}
				if ((healing & ~deck.healing) != 0)
					deck.nextHeal = now + 6;
				deck.healing = healing;
				if (now >= deck.nextHeal)
				{
					deck.nextHeal = now + 6;
					const Fixed amount = config->healPerSecond * Fixed::FromInt(6) / Fixed::FromInt(30);
					for (std::uint32_t space = 0; space < field.spaceCount && space < 32; ++space)
						if ((healing & (1u << space)) != 0)
							out.push_back({FlightDeckEvent::Kind::Heal, carrier, occupant[space]->jet, {}, space, 0, DeckOrder::None, {}, {}, amount});
				}
				// The queue moves up.
				if (now >= deck.nextCleanup)
				{
					deck.nextCleanup = now + config->cleanupTicks;
					std::array<bool, gp::Airfield::MaxRunways> complete{};
					for (std::uint32_t space = 0; space < field.spaceCount; ++space)
					{
						if (!GivesUpSpace(occupant[space]))
							continue;
						const std::uint32_t behind = space + field.runwayCount;
						const std::uint32_t runway = field.spaces[space].runway;
						if (behind >= field.spaceCount || runway >= complete.size() || complete[runway])
							continue;
						const DeckJet *next = occupant[behind];
						if (!MovesForward(next))
							continue;
						out.push_back({FlightDeckEvent::Kind::Swap, carrier, next->jet, occupant[space] != nullptr ? occupant[space]->jet : ecs::Entity{}, space});
						std::swap(occupant[space], occupant[behind]);
						complete[runway] = true;
						deck.nextCleanup = now + config->followTicks;
					}
				}
				// A replacement.
				for (std::uint32_t space = 0; space < field.spaceCount; ++space)
				{
					if (occupant[space] != nullptr)
						continue;
					const bool idle = queues.empty() || queues[row].count == 0;
					if (idle && now >= deck.nextAllowedProduction && !config->payload.empty())
					{
						out.push_back({FlightDeckEvent::Kind::Queue, carrier});
						deck.nextAllowedProduction = now + config->replacementTicks + config->dockTicks;
					}
					break;
				}
				// hasTakeoffOrders: an attack while its target stands (else the order is forgotten).
				const auto takeoffOrders = [&] {
					switch (deck.command)
					{
					case DeckOrder::Guard:
					case DeckOrder::AttackPosition:
					case DeckOrder::AttackMove: return true;
					case DeckOrder::Attack:
					case DeckOrder::ForceAttack:
						if (standing(deck.target))
							return true;
						deck.command = DeckOrder::None;
						deck.target = {};
						return false;
					default: return false;
					}
				};
				// The launches.
				for (std::uint32_t runway = 0; runway < field.runwayCount && runway < FlightDeck::MaxRunways; ++runway)
				{
					FlightDeckRunway &ramp = deck.runways[runway];
					const DeckJet *front = runway < field.spaceCount ? occupant[runway] : nullptr;
					const bool inPosition = front != nullptr &&
						Engine::Math::DistanceSquared(front->at.XY(), field.spaces[runway].prep.XY()) < Fixed::FromInt(10);
					if (front != nullptr && !GivesUpSpace(front) && inPosition && takeoffOrders() && ramp.nextLaunchWave <= now)
					{
						if (ramp.rampUp == 0)
						{
							ramp.rampUp = 1;
							ramp.rampUpTick = now + config->launchRampTicks;
							ramp.lowerRampTick = FlightDeck::Forever;
							ramp.door = 1;
						}
						else if (ramp.rampUpTick <= now)
						{
							out.push_back({FlightDeckEvent::Kind::Order, carrier, front->jet, {}, runway, 1, deck.command, deck.target, deck.position});
							ramp.nextLaunchWave = now + config->launchWaveTicks;
							ramp.catapultTick = now + config->catapultTicks;
							ramp.lowerRampTick = now + config->lowerRampTicks;
						}
					}
					if (ramp.catapultTick <= now && runway < config->catapultSystems.size() && !config->catapultSystems[runway].empty())
					{
						EffectCue steam;
						steam.effect = config->catapultSystems[runway];
						steam.at = field.runways[runway].start;
						steam.particleSystem = true;
						cues.push_back(std::move(steam));
						ramp.catapultTick = FlightDeck::Forever;
					}
					if (ramp.rampUp != 0 && ramp.lowerRampTick <= now)
					{
						ramp.rampUp = 0;
						ramp.door = 2;
					}
					if (!appearances.empty() && ramp.door != 0)
					{
						const std::uint32_t door = runway + 1; // DOOR_2 for the first runway (from 0)
						appearances[row].Set(content::model_condition::DoorOpening(door), ramp.door == 1);
						appearances[row].Set(content::model_condition::DoorClosing(door), ramp.door == 2);
					}
				}
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::DeckRosterSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.deck_roster";
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::gameplay::FlightDeckSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.flight_decks";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
