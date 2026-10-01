export module games.generalszh.gameplay.abilities.systems.command_button_hunt_system;
import std;
import engine.gameplay.common.weapons.components.weapon_slots;
import engine.gameplay.common.weapons.components.armament;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.abilities.components.command_button_hunt;
export import games.generalszh.gameplay.abilities.components.special_abilities;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.status.components.ai_activity;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.navigation.components.navigation;
export import engine.gameplay.rts.combat.components.aggression;
export import engine.gameplay.rts.combat.resources.attack_priorities;
export import engine.gameplay.rts.construction.components.builder;
export import engine.gameplay.rts.powers.components.special_power_timers;
export import engine.gameplay.rts.powers.resources.shared_power_timers;
export import engine.gameplay.rts.powers.algorithms.special_power_timing;
import games.generalszh.gameplay.orders.resources.command_bar_overrides;

// CommandButtonHuntUpdate::update for every hunting unit whose update is due, chunk-parallel: an order from outside its AI
// ends the hunt; with a special power at objects, idle and its special ability not under way, it scans (scanClosestTarget)
// the spatial index within ScanRange of its centre for what is alive, not hidden, an enemy's (a capture: anyone's but its
// own player's and its allies'; the vehicle hack: not disabled), scored as the original: its attack priority (the default
// set's 1 without a set of its own), less one per attackPriorityDistanceModifier of the distance between their bounding
// circles, at least 1 (0 priority: never); the candidates go out best first (a tie to the higher priority, then the
// nearer) as a HuntScan, and once the systems have run the unit uses its button (CMD_FROM_AI) on the first its power may
// be used on (canDoSpecialPowerAtObject). With a weapon, idle, it hunts (aiHunt). Converting to a car bomb, idle, it scans
// for the neutral near to far (the first it may convert gets the button); hijack and sabotage hunts find nothing yet.
export namespace generalszh::gameplay
{
struct HuntCandidate
{
	ecs::Entity entity;
	std::int64_t score{0};
	std::int64_t priority{0};
};

struct HuntScan
{
	ecs::Entity unit;
	std::uint32_t set{0};
	std::uint32_t slot{0};
	std::vector<HuntCandidate> candidates; // best first
};

struct HuntScans : ecs::ChunkOutputs<HuntScan>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::HuntScans>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.hunt_scans";
};
}

export namespace generalszh::gameplay
{
namespace hunt_system_detail
{
namespace gp = engine::gameplay;
using Engine::Math::Fixed;

// The button it hunts with (its command set's slot), if it has one.
inline const content::CommandButtonContent *ButtonOf(const ObjectTemplates &templates, const CommandBarOverrides *overrides, const CommandButtonHunt &hunt)
{
	if (!hunt.Hunting())
		return nullptr;
	const content::GameContent &content = templates.Content();
	const auto effective = EffectiveCommandSet(content.commands, overrides, templates.CommandSetName(hunt.set));
	const auto *set = effective ? &*effective : nullptr;
	if (set == nullptr || hunt.slot >= set->buttons.size() || set->buttons[hunt.slot].empty())
		return nullptr;
	return content.commands.Button(set->buttons[hunt.slot]);
}
}

struct CommandButtonHuntSystem
{
	using Query = ecs::Query<ecs::Write<CommandButtonHunt>, ecs::Write<engine::gameplay::AiActivity>, ecs::Read<engine::gameplay::Transform>,
		ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::OptionalWrite<engine::gameplay::MoveOrder>,
		ecs::OptionalWrite<engine::gameplay::AttackTarget>, ecs::OptionalWrite<engine::gameplay::Route>, ecs::OptionalWrite<engine::gameplay::Aggression>,
		ecs::Optional<engine::gameplay::Builder>, ecs::Optional<SpecialAbilities>, ecs::Optional<engine::gameplay::SpecialPowerTimers>,
		ecs::Optional<engine::gameplay::TeamMember>, ecs::OptionalWrite<engine::gameplay::WeaponSlots>, ecs::OptionalWrite<engine::gameplay::Armament>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Disabled>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::SpatialIndex>, ecs::Read<engine::gameplay::Relationships>,
		ecs::Read<engine::gameplay::AttackPriorities>, ecs::Read<engine::gameplay::SpecialPowerRules>, ecs::Read<engine::gameplay::SharedPowerTimers>,
		ecs::Write<HuntScans>, ecs::Read<CommandBarOverrides>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<HuntScans>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using namespace hunt_system_detail;
		const std::uint64_t tick = context.Tick();
		const auto &templates = context.Read<ObjectTemplates>();
		const auto &spatial = context.Read<gp::SpatialIndex>();
		const auto &relationships = context.Read<gp::Relationships>();
		const auto &priorities = context.Read<gp::AttackPriorities>();
		const auto &rules = context.Read<gp::SpecialPowerRules>();
		const auto &shared = context.Read<gp::SharedPowerTimers>();
		const auto lookup = context.Lookup<Lookup>();
		auto &scans = context.Write<HuntScans>().Slot(context);
		auto hunts = chunk.Get<CommandButtonHunt>();
		auto activities = chunk.Get<gp::AiActivity>();
		const auto transforms = chunk.Get<gp::Transform>();
		const auto owners = chunk.Get<gp::Owner>();
		const auto teamRows = chunk.Get<gp::TeamMember>();
		const auto refs = chunk.Get<gp::DefinitionRef>();
		auto orders = chunk.Get<gp::MoveOrder>();
		auto attacks = chunk.Get<gp::AttackTarget>();
		auto routes = chunk.Get<gp::Route>();
		auto aggressions = chunk.Get<gp::Aggression>();
		auto slotRows = chunk.Get<gp::WeaponSlots>();
		auto armamentRows = chunk.Get<gp::Armament>();
		const auto builders = chunk.Get<gp::Builder>();
		const auto abilities = chunk.Get<SpecialAbilities>();
		const auto timers = chunk.Get<gp::SpecialPowerTimers>();
		const auto entities = chunk.Entities();
		const Fixed modifier = templates.Content().aiData.attackPriorityDistanceModifier;
		for (std::size_t row = 0; row < hunts.size(); ++row)
		{
			CommandButtonHunt &hunt = hunts[row];
			if (!hunt.Hunting() || hunt.nextTick > tick)
				continue;
			const bool again = hunt.again != 0;
			hunt.again = 0;
			std::optional<std::uint64_t> sleep;
			const content::CommandButtonContent *button = ButtonOf(templates, &context.Read<CommandBarOverrides>(), hunt);
			gp::MoveOrder *order = orders.empty() ? nullptr : &orders[row];
			gp::AttackTarget *attack = attacks.empty() ? nullptr : &attacks[row];
			const bool idle = (order == nullptr || order->mode == gp::MoveMode::Idle) && (attack == nullptr || attack->target == ecs::Entity{}) &&
				activities[row].busy == 0 && builders.empty();
			if (order == nullptr || button == nullptr)
				sleep = std::nullopt;
			else if (activities[row].commanded != 0)
			{
				hunt.set = CommandButtonHunt::NotHunting; // an order from outside its AI: it stops hunting
				sleep = std::nullopt;
			}
			else if (button->commandName == "SPECIAL_POWER")
			{
				sleep = hunt.scanTicks;
				const auto power = templates.Content().powers.Template(button->specialPower);
				// findSpecialAbilityUpdate: the first special ability of the power's kind.
				const AbilitySlot *ability = nullptr;
				if (power && !abilities.empty())
				{
					const std::string &type = templates.Content().powers.templates[*power].type;
					for (std::uint8_t index = 0; index < abilities[row].count && ability == nullptr; ++index)
						if (templates.Content().powers.templates[abilities[row].slots[index].power].type == type)
							ability = &abilities[row].slots[index];
				}
				if (!power || ability == nullptr)
					sleep = idle ? std::nullopt : sleep;
				if (idle && power && ability != nullptr && !ability->Has(ability_flag::Active))
				{
					const gp::SpecialPowerTimer *timer = timers.empty() ? nullptr : timers[row].Find(*power);
					const std::uint32_t player = owners[row].player;
					const std::uint32_t team = teamRows.empty() ? gp::Relationships::NoTeam : teamRows[row].team;
					// canDoSpecialPowerAtObject from its AI: its module, fully ready.
					if (timer != nullptr && gp::PeekIsReady(*timer, rules, shared, player, tick))
					{
						const std::string &type = templates.Content().powers.templates[*power].type;
						const bool capture = type == "SPECIAL_INFANTRY_CAPTURE_BUILDING";
						const bool vehicleHack = type == "SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK";
						const auto self = transforms[row].position.XY();
						const Fixed selfRadius = content::BoundingCircleRadius(templates.DefinitionAt(refs[row].index).geometry);
						const std::uint16_t set = aggressions.empty() ? 0 : aggressions[row].prioritySet;
						struct Seen
						{
							HuntCandidate candidate;
							Fixed distance;
						};
						std::vector<Seen> seen;
						spatial.ForEachWithin(self, hunt.scanRange, [&](const gp::SpatialEntry &entry) {
							if (entry.entity == entities[row] || (entry.classes & gp::target_class::Hidden) != 0)
								return;
							const Fixed centre = Engine::Math::DistanceSquared(entry.position.XY(), self);
							if (centre > hunt.scanRange * hunt.scanRange)
								return;
							// Object::getRelationship (the teams' overrides, then the players').
							const gp::Relationship relation = relationships.Between(team, player, entry.team, entry.player);
							if (capture ? (entry.player == player || relation == gp::Relationship::Allies) : relation != gp::Relationship::Enemies)
								return;
							if (vehicleHack)
								if (const auto *off = lookup.Get<gp::Disabled>(entry.entity); off != nullptr && off->mask != 0)
									return;
							const auto *ref = lookup.Get<gp::DefinitionRef>(entry.entity);
							if (ref == nullptr)
								return;
							const std::int64_t priority = priorities.Priority(set, ref->index);
							if (priority == 0)
								return;
							const Fixed apart = std::max(Fixed{}, Engine::Math::Sqrt(centre) - selfRadius - entry.radius);
							const std::int64_t less = modifier > Fixed{} ? (apart / modifier).Floor() : 0;
							seen.push_back({{entry.entity, std::max<std::int64_t>(1, priority - less), priority}, centre});
						});
						// ITER_SORTED_NEAR_TO_FAR, then the best score (ties: the higher priority, else the nearer).
						std::stable_sort(seen.begin(), seen.end(), [](const Seen &a, const Seen &b) {
							return a.distance < b.distance || (a.distance == b.distance && a.candidate.entity.index < b.candidate.entity.index);
						});
						std::stable_sort(seen.begin(), seen.end(), [](const Seen &a, const Seen &b) {
							return a.candidate.score > b.candidate.score || (a.candidate.score == b.candidate.score && a.candidate.priority > b.candidate.priority);
						});
						if (!seen.empty())
						{
							HuntScan scan{entities[row], hunt.set, hunt.slot, {}};
							for (const Seen &one : seen)
								scan.candidates.push_back(one.candidate);
							scans.push_back(std::move(scan));
						}
					}
				}
			}
			else if (button->commandName == "SWITCH_WEAPON" || button->commandName == "FIRE_WEAPON")
			{
				// huntWeapon: aiHunt when idle; every update its button's weapon slot is locked for the attack
				// (setWeaponLock LOCKED_TEMPORARILY: until the clip is empty or the attack is done).
				if (!slotRows.empty() && !armamentRows.empty())
					gp::LockSlotTemporarily(slotRows[row], armamentRows[row], button->weaponSlot);
				if (idle && !aggressions.empty())
				{
					if (!routes.empty())
						routes[row].planned = false;
					order->mode = gp::MoveMode::Idle;
					if (attack != nullptr)
						*attack = {};
					activities[row].commanded = 0;
					aggressions[row].stance = gp::Stance::Hunt;
				}
				sleep = 1;
			}
			else if (button->commandName == "CONVERT_TO_CARBOMB" || button->commandName == "HIJACK_VEHICLE" || button->commandName == "SABOTAGE_BUILDING")
			{
				// huntEnter: idle, it scans (scanClosestTarget) within ScanRange of its centre for the living, not hidden,
				// neutral to it (a car bomber's) or its enemies (a hijacker's, a saboteur's), near to far; the first it may
				// enter (canConvertObjectToCarBomb, canHijackVehicle, canSabotageBuilding) gets the button.
				sleep = hunt.scanTicks;
				const gp::Relationship wanted = button->commandName == "CONVERT_TO_CARBOMB" ? gp::Relationship::Neutral : gp::Relationship::Enemies;
				if (idle)
				{
					const std::uint32_t player = owners[row].player;
					const std::uint32_t team = teamRows.empty() ? gp::Relationships::NoTeam : teamRows[row].team;
					const auto self = transforms[row].position.XY();
					std::vector<std::pair<Fixed, ecs::Entity>> seen;
					spatial.ForEachWithin(self, hunt.scanRange, [&](const gp::SpatialEntry &entry) {
						if (entry.entity == entities[row] || (entry.classes & gp::target_class::Hidden) != 0)
							return;
						const Fixed centre = Engine::Math::DistanceSquared(entry.position.XY(), self);
						if (centre > hunt.scanRange * hunt.scanRange || relationships.Between(team, player, entry.team, entry.player) != wanted)
							return;
						seen.emplace_back(centre, entry.entity);
					});
					std::ranges::sort(seen, [](const auto &a, const auto &b) { return a.first < b.first || (a.first == b.first && a.second.index < b.second.index); });
					if (!seen.empty())
					{
						HuntScan scan{entities[row], hunt.set, hunt.slot, {}};
						for (const auto &[distance, entity] : seen)
							scan.candidates.push_back({entity, 0, 0});
						scans.push_back(std::move(scan));
					}
				}
			}
			// setCommandButton wakes it again the next tick, whatever this update said.
			hunt.nextTick = again ? tick + 1 : sleep ? tick + std::max<std::uint64_t>(*sleep, 1) : std::numeric_limits<std::uint64_t>::max();
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::CommandButtonHuntSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.command_button_hunts";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
