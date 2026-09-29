export module games.generalszh.presentation.hud.systems.eva_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.hud.algorithms.eva_queue;
export import games.generalszh.presentation.hud.algorithms.superweapon_list;
export import games.generalszh.presentation.audio.resources.audio_resources;
export import games.generalszh.gameplay.eva.resources.eva_notices;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.rts.construction.components.under_construction;
export import engine.gameplay.rts.economy.resources.player_energy;
export import engine.gameplay.rts.production.systems.production_system;

// EVA once a tick of the logic, for whoever watches (the local player): what the simulation told it (units and
// structures lost, the general's rank, superweapons put up or launched: own, an ally's (or a neutral's) or an
// enemy's), the ready superweapon countdowns (InGameUI::postDraw: each once as it becomes ready, again after it
// fired), and finished upgrades with no sound of their own (ProductionUpdate: UPGRADECOMPLETE); then Eva::update,
// its line handed to the audio as EVA's voice.
export namespace generalszh::presentation
{
namespace eva_detail
{
namespace gp = engine::gameplay;

// The announcement for a kind of superweapon by whose it is (the SUPERWEAPON<stage>_<OWN|ALLY|ENEMY>_<kind> order).
inline std::optional<std::uint32_t> SuperweaponMessage(std::uint32_t firstOwn, generalszh::gameplay::EvaWeapon weapon, int whose)
{
	using W = generalszh::gameplay::EvaWeapon;
	const int kind = weapon == W::ParticleCannon ? 0 : weapon == W::Nuke ? 1 : weapon == W::ScudStorm ? 2 : -1;
	if (kind < 0)
		return std::nullopt;
	return firstOwn + static_cast<std::uint32_t>(whose * 3 + kind);
}

// Own 0, ally (anything not an enemy) 1, enemy 2.
inline int Whose(const gp::Relationships &relationships, std::uint32_t viewer, std::uint32_t player)
{
	if (viewer == player)
		return 0;
	return relationships.Enemies(viewer, player) ? 2 : 1;
}
}

struct EvaSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::SpecialPowerTimers>, ecs::Read<engine::gameplay::Owner>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::UnderConstruction>, ecs::Read<engine::gameplay::Disabled>, ecs::Read<engine::gameplay::Owner>>;
	using SideTables = ecs::SideTables<ecs::Write<SuperweaponEvaReady>>;
	using Resources = ecs::Resources<ecs::Read<generalszh::gameplay::EvaNotices>, ecs::Write<EvaState>, ecs::Read<PresentationFrame>,
		ecs::Read<engine::gameplay::Relationships>, ecs::Read<engine::gameplay::PlayerEnergy>, ecs::Read<engine::gameplay::SpecialPowerRules>,
		ecs::Read<engine::gameplay::SharedPowerTimers>, ecs::Read<generalszh::gameplay::ObjectTemplates>, ecs::Read<engine::gameplay::ProductionDone>,
		ecs::Write<AudioCommands>, ecs::Read<AudioState>, ecs::Read<AudioHandle>, ecs::Write<PresentationRandom>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		using namespace eva_detail;
		using generalszh::gameplay::EvaCue;
		EvaState &eva = context.Write<EvaState>();
		const std::uint64_t tick = context.Tick();
		const std::uint32_t viewer = context.Read<PresentationFrame>().viewer;
		const bool watching = viewer != PresentationFrame::NoViewer;
		const auto &relationships = context.Read<gp::Relationships>();
		const auto message = [](std::string_view name) { return *content::EvaMessageOf(name); };
		if (watching)
		{
			for (const generalszh::gameplay::EvaNotice &notice : context.Read<generalszh::gameplay::EvaNotices>().list)
				switch (notice.cue)
				{
				// isLocallyViewed / isLocalPlayer: the watcher's own.
				case EvaCue::UnitLost:
					if (notice.player == viewer)
						AskEva(eva, message("UNITLOST"));
					break;
				case EvaCue::BuildingLost:
					if (notice.player == viewer)
						AskEva(eva, message("BUILDINGLOST"));
					break;
				case EvaCue::GeneralLevelUp:
					if (notice.player == viewer)
						AskEva(eva, message("GENERALLEVELUP"));
					break;
				case EvaCue::BuildingBeingStolen:
					if (notice.player == viewer)
						AskEva(eva, message("BUILDINGBEINGSTOLEN"));
					break;
				case EvaCue::BuildingStolen:
					if (notice.player == viewer)
						AskEva(eva, message("BUILDINGSTOLEN"));
					break;
				case EvaCue::VehicleStolen:
					if (notice.player == viewer)
						AskEva(eva, message("VEHICLESTOLEN"));
					break;
				case EvaCue::SuperweaponDetected:
					if (const auto found = SuperweaponMessage(message("SUPERWEAPONDETECTED_OWN_PARTICLECANNON"), notice.weapon, Whose(relationships, viewer, notice.player)))
						AskEva(eva, *found);
					break;
				case EvaCue::SuperweaponLaunched:
				{
					const int whose = Whose(relationships, viewer, notice.player);
					static constexpr std::array<std::string_view, 3> gps{"SUPERWEAPONLAUNCHED_OWN_GPS_SCRAMBLER", "SUPERWEAPONLAUNCHED_ALLY_GPS_SCRAMBLER",
						"SUPERWEAPONLAUNCHED_ENEMY_GPS_SCRAMBLER"};
					static constexpr std::array<std::string_view, 3> sneak{"SUPERWEAPONLAUNCHED_OWN_SNEAK_ATTACK", "SUPERWEAPONLAUNCHED_ALLY_SNEAK_ATTACK",
						"SUPERWEAPONLAUNCHED_ENEMY_SNEAK_ATTACK"};
					if (notice.weapon == generalszh::gameplay::EvaWeapon::GpsScrambler)
						AskEva(eva, message(gps[static_cast<std::size_t>(whose)]));
					else if (notice.weapon == generalszh::gameplay::EvaWeapon::SneakAttack)
						AskEva(eva, message(sneak[static_cast<std::size_t>(whose)]));
					else if (const auto found = SuperweaponMessage(message("SUPERWEAPONLAUNCHED_OWN_PARTICLECANNON"), notice.weapon, whose))
						AskEva(eva, *found);
					break;
				}
				}
			// ProductionUpdate: an upgrade finished at the watcher's, named for the screen, without its own
			// ResearchSound, is EVA's.
			const auto &templates = context.Read<generalszh::gameplay::ObjectTemplates>().Content();
			const AudioHandle &audio = context.Read<AudioHandle>();
			const auto lookup = context.Lookup<Lookup>();
			context.Read<gp::ProductionDone>().ForEach([&](const gp::Produced &done) {
				if (done.kind != gp::ProductionKind::Upgrade || done.definition >= templates.upgrades.upgrades.size())
					return;
				const auto *owner = lookup.IsAlive(done.factory) ? lookup.Get<gp::Owner>(done.factory) : nullptr;
				const content::UpgradeContent &upgrade = templates.upgrades.upgrades[done.definition];
				if (owner == nullptr || owner->player != viewer || upgrade.displayName.empty())
					return;
				const bool ownSound = !upgrade.researchSound.empty() && audio.content != nullptr && audio.content->Find(upgrade.researchSound) != nullptr;
				if (!ownSound)
					AskEva(eva, message("UPGRADECOMPLETE"));
			});
			ReadyCountdowns(query, context, eva, viewer, tick);
		}
		const auto &energy = context.Read<gp::PlayerEnergy>();
		const bool lowPower = watching && !energy.Sufficient(viewer);
		const AudioState &audioState = context.Read<AudioState>();
		const bool speaking = audioState.evaSpeaking || audioState.evaServed != eva.requested;
		PresentationRandom &random = context.Write<PresentationRandom>();
		const auto pick = [&](std::size_t count) {
			return count <= 1 ? std::size_t{0} : static_cast<std::size_t>(std::uniform_int_distribution<std::size_t>(0, count - 1)(random.engine));
		};
		if (const auto line = UpdateEva(eva, tick, watching, lowPower, speaking, pick); line && !line->empty())
		{
			context.Write<AudioCommands>().pending.push_back({AudioCommand::Kind::Eva, *line});
			++eva.requested;
		}
	}

private:
	// InGameUI::postDraw's countdown loop: a countdown that has become ready (past the first frame) is announced for a
	// particle cannon, a nuke or a Scud Storm, once; one no longer ready may be announced again.
	static void ReadyCountdowns(Query &query, ecs::SystemContext &context, EvaState &eva, std::uint32_t viewer, std::uint64_t tick)
	{
		using namespace eva_detail;
		const auto lookup = context.Lookup<Lookup>();
		const auto &rules = context.Read<gp::SpecialPowerRules>();
		const auto &shared = context.Read<gp::SharedPowerTimers>();
		const auto &templates = context.Read<generalszh::gameplay::ObjectTemplates>().Content().powers.templates;
		std::vector<SuperweaponSource> sources;
		query.ForEachChunk([&](auto chunk) {
			const auto timers = chunk.template Get<gp::SpecialPowerTimers>();
			const auto owners = chunk.template Get<gp::Owner>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < timers.size(); ++row)
				for (std::uint32_t slot = 0; slot < timers[row].count; ++slot)
					if (const gp::SpecialPowerTimer &timer = timers[row].timers[slot];
						(timer.flags & gp::power_flag::PublicTimer) != 0 && timer.power < templates.size())
					{
						const auto *off = lookup.Get<gp::Disabled>(entities[row]);
						sources.push_back({entities[row], owners[row].player, slot, timer, lookup.Get<gp::UnderConstruction>(entities[row]) != nullptr,
							off != nullptr && off->mask != 0});
					}
		});
		if (sources.empty() || tick == 0)
			return;
		auto &table = context.Side<SideTables, SuperweaponEvaReady>();
		auto &commands = context.Commands();
		const auto names = [&](std::uint32_t power) { return std::string_view(templates[power].name); };
		const auto &relationships = context.Read<gp::Relationships>();
		for (const SuperweaponLine &line : OrderSuperweapons(sources, names, rules, shared, tick))
		{
			if (!line.shown)
				continue;
			const SuperweaponSource &source = sources[line.source];
			SuperweaponEvaReady *seen = table.Get(source.entity);
			const std::uint32_t bit = 1u << source.slot;
			std::uint32_t played = seen != nullptr ? seen->played : 0u;
			if (line.ready && (played & bit) == 0)
			{
				const auto weapon = generalszh::gameplay::EvaWeaponOf(templates[source.timer.power].type);
				if (const auto found = SuperweaponMessage(*content::EvaMessageOf("SUPERWEAPONREADY_OWN_PARTICLECANNON"), weapon,
						Whose(relationships, viewer, source.player)))
					AskEva(eva, *found);
				played |= bit;
			}
			else if (!line.ready)
				played &= ~bit;
			if (seen != nullptr)
				seen->played = played;
			else if (played != 0)
				commands.Add<SuperweaponEvaReady>(source.entity, SuperweaponEvaReady{played});
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::EvaSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.eva";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
