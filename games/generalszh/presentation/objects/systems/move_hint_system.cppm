export module games.generalszh.presentation.objects.systems.move_hint_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.objects.resources.move_hints;
export import games.generalszh.presentation.interaction.resources.interaction_resources;
export import games.generalszh.presentation.interaction.components.selected;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import engine.gameplay.common.identity.components.definition_ref;
import Engine.Core.Math.FixedPresentation;

// InGameUI::createMoveHint (HintSpy, for each move the player orders: a move or an attack move) and W3DInGameUI::
// drawMoveHints, once a frame after the frame's orders: each ordered spot takes the next of the 256 hints (none while the
// one thing selected is IMMOBILE); a hint shows the MoveHintName model, its animation once from when it was ordered,
// aligned with the ground there (alignOnTerrain), for 40 client frames (40/30 s).
export namespace generalszh::presentation
{
namespace move_hint_detail
{
// alignOnTerrain: the model's up along the ground's normal there, its x keeping to the world's x (angle 0).
inline std::array<float, 16> AlignedOnGround(const std::function<float(float, float)> &ground, float x, float y, float z)
{
	constexpr float Step = 5.0f; // half a height map cell (MAP_XY_FACTOR 10)
	std::array<float, 3> up{0, 0, 1};
	if (ground)
	{
		const float dx = (ground(x + Step, y) - ground(x - Step, y)) / (2.0f * Step);
		const float dy = (ground(x, y + Step) - ground(x, y - Step)) / (2.0f * Step);
		const float length = std::sqrt(dx * dx + dy * dy + 1.0f);
		up = {-dx / length, -dy / length, 1.0f / length};
	}
	// x: the world's x made square to up; y: up cross x.
	std::array<float, 3> ax{1.0f - up[0] * up[0], -up[0] * up[1], -up[0] * up[2]};
	const float lx = std::sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
	ax = {ax[0] / lx, ax[1] / lx, ax[2] / lx};
	const std::array<float, 3> ay{up[1] * ax[2] - up[2] * ax[1], up[2] * ax[0] - up[0] * ax[2], up[0] * ax[1] - up[1] * ax[0]};
	return {ax[0], ay[0], up[0], x, ax[1], ay[1], up[1], y, ax[2], ay[2], up[2], z, 0, 0, 0, 1};
}
}

struct MoveHintSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>>;
	using SideTables = ecs::SideTables<ecs::Read<Selected>>;
	using Resources = ecs::Resources<ecs::Read<PlayerOrders>, ecs::Read<PresentationFrame>, ecs::Read<LookCatalog>, ecs::Read<SelectionCatalog>,
		ecs::Read<TerrainHeightHandle>, ecs::Write<MoveHints>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		MoveHints &hints = context.Write<MoveHints>();
		const PresentationFrame &frame = context.Read<PresentationFrame>();
		const auto &ground = context.Read<TerrainHeightHandle>().at;
		// "Don't allow move hints to be created if our selected object can't move!"
		const auto selected = context.SideRead<SideTables, Selected>().Entities();
		bool immobile = false;
		if (selected.size() == 1)
		{
			const auto lookup = context.Lookup<Lookup>();
			const SelectionCatalog &catalog = context.Read<SelectionCatalog>();
			if (const auto *ref = lookup.IsAlive(selected.front()) ? lookup.Get<engine::gameplay::DefinitionRef>(selected.front()) : nullptr;
				ref != nullptr && ref->index < catalog.kinds.size())
				immobile = content::HasKindOf(catalog.kinds[ref->index], content::KindOfName("IMMOBILE").bit);
		}
		const auto add = [&](const Engine::Math::FixedVector2 &at) {
			if (immobile)
				return;
			const float x = Engine::Math::ToFloat(at.x), y = Engine::Math::ToFloat(at.y);
			hints.hints[hints.next] = {{x, y, ground ? ground(x, y) : 0.0f}, frame.clock, true};
			hints.next = (hints.next + 1) % MoveHints::Capacity;
		};
		for (const commands::GameCommand &order : context.Read<PlayerOrders>().pending)
		{
			if (const auto *move = std::get_if<commands::MoveTo>(&order))
				add(move->destination);
			else if (const auto *attackMove = std::get_if<commands::AttackMoveTo>(&order))
				add(attackMove->position);
		}
		hints.instances.clear();
		const std::uint32_t look = context.Read<LookCatalog>().moveHintLook;
		if (look == LookCatalog::NoLook)
			return;
		for (MoveHint &hint : hints.hints)
		{
			if (!hint.live)
				continue;
			const double elapsed = frame.clock - hint.since;
			if (elapsed > MoveHints::ShownSeconds)
			{
				hint.live = false;
				continue;
			}
			ObjectInstance shown;
			shown.look = look;
			shown.clipLook = look;
			shown.world = move_hint_detail::AlignedOnGround(ground, hint.at[0], hint.at[1], hint.at[2]);
			shown.animationSeconds = static_cast<float>(elapsed);
			hints.instances.push_back(shown);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::MoveHintSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.move_hints";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
