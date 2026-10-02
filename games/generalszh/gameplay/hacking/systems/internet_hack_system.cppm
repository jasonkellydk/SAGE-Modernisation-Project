export module games.generalszh.gameplay.hacking.systems.internet_hack_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.hacking.components.internet_hack;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.appearance.components.appearance;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.common.status.components.ai_activity;
export import engine.gameplay.rts.veterancy.components.experience;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.rts.movement.components.move_order;
import games.generalszh.content.objects.model_conditions;

// HackInternetStateMachine for every hacker, chunk-parallel: unpacking (UNPACKING) until its frames are spent, then
// hacking (FIRING_A): every CashUpdateDelay (CashUpdateDelayFast inside an Internet Center) its player is paid its
// veterancy level's cash and it gains XpPerCashUpdate (not while hacked: DISABLED_HACKED holds its count); packing
// (PACKING) until its frames are spent, an order given meanwhile held till then (m_pendingCommand), then idle. Each
// stage ends on the update after its frames reach none, the next begun that update. Each hacker writes only itself;
// the pay goes out as its chunk's HackEvents (ApplyHackEvents).
export namespace generalszh::gameplay
{
struct HackEvent
{
	enum class Kind : std::uint8_t
	{
		Cash,  // `amount` to its player, XpPerCashUpdate to it
		Again, // packed with a hack order pending: it hacks again
	};
	ecs::Entity hacker;
	std::uint32_t amount{0};
	Kind kind{Kind::Cash};
	std::uint8_t reserved[3]{};
};

struct HackEvents : ecs::ChunkOutputs<HackEvent>
{
};

// HackInternetState::update's pay: its level's amount, a level without one falling to the one below, none at all 1.
inline std::uint32_t HackCash(const InternetHackConfig &config, std::uint8_t level)
{
	for (std::int32_t at = std::min<std::int32_t>(level, 3); at >= 0; --at)
		if (config.cash[static_cast<std::size_t>(at)] != 0)
			return config.cash[static_cast<std::size_t>(at)];
	return 1;
}
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::HackEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.hack_events";
};
}

export namespace generalszh::gameplay
{
struct InternetHackSystem
{
	using Query = ecs::Query<ecs::Write<InternetHack>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::OptionalWrite<engine::gameplay::Appearance>,
		ecs::Optional<engine::gameplay::Disabled>, ecs::Optional<engine::gameplay::Experience>, ecs::Optional<engine::gameplay::Passenger>,
		ecs::OptionalWrite<engine::gameplay::MoveOrder>, ecs::OptionalWrite<engine::gameplay::AiActivity>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Write<HackEvents>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<HackEvents>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		static const std::uint32_t unpacking = content::ModelConditionBit("UNPACKING");
		static const std::uint32_t packing = content::ModelConditionBit("PACKING");
		static const std::uint32_t firing = content::ModelConditionBit("FIRING_A");
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		auto &events = context.Write<HackEvents>().Slot(context);
		auto hacks = chunk.Get<InternetHack>();
		const auto refs = chunk.Get<gp::DefinitionRef>();
		auto looks = chunk.Get<gp::Appearance>();
		const auto disabled = chunk.Get<gp::Disabled>();
		const auto experience = chunk.Get<gp::Experience>();
		const bool contained = !chunk.Get<gp::Passenger>().empty();
		auto orders = chunk.Get<gp::MoveOrder>();
		auto activities = chunk.Get<gp::AiActivity>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < hacks.size(); ++row)
		{
			InternetHack &hack = hacks[row];
			if (hack.stage == HackStage::Idle)
				continue;
			const InternetHackConfig *config = templates.InternetHackOf(refs[row].index);
			if (config == nullptr)
				continue;
			gp::Appearance *look = looks.empty() ? nullptr : &looks[row];
			const auto set = [&](std::uint32_t bit, bool on) {
				if (look != nullptr)
					look->Set(bit, on);
			};
			switch (hack.stage)
			{
			case HackStage::Unpacking:
				set(unpacking, true);
				if (hack.framesRemaining > 0)
				{
					--hack.framesRemaining;
					break;
				}
				// HackInternetState::onEnter.
				set(unpacking, false);
				set(firing, true);
				hack.stage = HackStage::Hacking;
				hack.framesRemaining = contained ? config->cashTicksFast : config->cashTicks;
				break;
			case HackStage::Hacking:
				if (!disabled.empty() && (disabled[row].mask & gp::disabled_type::Hacked) != 0)
					break;
				if (hack.framesRemaining > 0)
				{
					--hack.framesRemaining;
					break;
				}
				events.push_back({entities[row], HackCash(*config, experience.empty() ? 0 : experience[row].level), HackEvent::Kind::Cash});
				hack.framesRemaining = contained ? config->cashTicksFast : config->cashTicks;
				break;
			case HackStage::Packing:
				// The order given meanwhile waits (setLocomotorGoalNone while busy).
				if (hack.pending == HackPending::Order && !orders.empty())
					orders[row].held = 1;
				if (hack.framesRemaining > 0)
				{
					--hack.framesRemaining;
					break;
				}
				set(packing, false);
				hack.stage = HackStage::Idle;
				if (!activities.empty())
					activities[row].busy = 0;
				if (hack.pending == HackPending::Order && !orders.empty())
					orders[row].held = 0;
				if (hack.pending == HackPending::Hack)
					events.push_back({entities[row], 0, HackEvent::Kind::Again});
				hack.pending = HackPending::None;
				break;
			case HackStage::Idle:
				break;
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::InternetHackSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.internet_hack";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
