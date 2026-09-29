export module games.generalszh.presentation.objects.systems.script_flash_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.identity.components.owner;
export import games.generalszh.presentation.objects.components.object_presentation;
export import games.generalszh.presentation.objects.algorithms.tint_envelope;

// Once a tick (Drawable::updateDrawable): a thing a script set flashing flashes on every DRAWABLE_FRAMES_PER_FLASH-th
// frame (15: twice a second) while it has flashes left, each a colorFlash of its tint in the script's colour (attack 0,
// decay DEF_DECAY_FRAMES 4, no sustain).
export namespace generalszh::presentation
{
inline constexpr std::uint64_t FramesPerFlash = 15;

struct ScriptFlashSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Owner>>;
	using SideTables = ecs::SideTables<ecs::Write<ScriptFlash>, ecs::Write<TintEnvelope>>;

	void Execute(ecs::SystemContext &context) const
	{
		if (context.Tick() % FramesPerFlash != 0)
			return;
		auto &flashes = context.Side<SideTables, ScriptFlash>();
		auto &tints = context.Side<SideTables, TintEnvelope>();
		const auto entities = flashes.Entities();
		for (std::size_t index = 0; index < flashes.Size(); ++index)
		{
			ScriptFlash &flash = flashes.Value(index);
			if (flash.count <= 0)
				continue;
			--flash.count;
			if (TintEnvelope *tint = tints.Get(entities[index]))
				PlayTint(*tint, flash.color, 0, 4, 0.0f);
			else
			{
				TintEnvelope fresh;
				PlayTint(fresh, flash.color, 0, 4, 0.0f);
				context.Commands().Add<TintEnvelope>(entities[index], fresh);
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::ScriptFlashSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.script_flash";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
