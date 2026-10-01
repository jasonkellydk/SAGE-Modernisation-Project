export module games.generalszh.presentation.objects.systems.projectile_stream_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.components.projectile;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.identity.components.definition_ref;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.object_frame;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.objects.systems.effect_attachment_systems;
import Engine.Core.Math.FixedPresentation;

// A weapon's projectile stream (ProjectileStreamUpdate and W3DProjectileStreamDraw: the Toxin Tractor's and the Dragon
// Tank's), drawn each frame from what flies: a shooter's projectiles from that weapon in the order fired (the stream's
// ring, oldest first), a break before each whose target (object, else spot) differs from the one before (addProjectile's
// hole; the first too); the newest MaxSegments points of that list kept (holes counting); a point near a vehicle shooter
// (within one and a half its major radius, flat) lifted to half a unit over its top (getAllPoints: skimming its roof);
// each unbroken run of two or more drawn as a line of Width, its texture tiled TileFactor times a segment and scrolled
// ScrollRate each 1/30 s, additive. The stream object is only a drawing in the original (INERT, no body): here there is
// none, the projectiles are the stream. (A projectile gone from the middle of a stream no longer breaks it.) Not drawn
// while its shooter is shrouded from the viewer (the stream object's own shroud, at its shooter).
export namespace generalszh::presentation
{
struct ProjectileStreamSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::ProjectileFlight>, ecs::Read<engine::gameplay::Transform>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<PresentationFrame>, ecs::Read<WeaponStreams>, ecs::Read<PresentedObjects>, ecs::Read<LookCatalog>,
		ecs::Write<LaserFrame>>;

	struct Flying
	{
		ecs::Entity source;
		std::uint32_t weapon{0};
		std::uint64_t fireTick{0};
		ecs::Entity entity;
		ecs::Entity target;
		Engine::Math::FixedVector3 aim;
	};

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const WeaponStreams &streams = context.Read<WeaponStreams>();
		if (streams.byWeapon.empty())
			return;
		std::vector<Flying> flying;
		query.ForEachChunk([&](auto chunk) {
			const auto flights = chunk.template Get<engine::gameplay::ProjectileFlight>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < flights.size(); ++row)
			{
				const engine::gameplay::Shot &shot = flights[row].shot;
				if (streams.Of(shot.weapon) != nullptr)
					flying.push_back({shot.source, shot.weapon, shot.fireTick, entities[row], shot.target, shot.aim});
			}
		});
		if (flying.empty())
			return;
		std::ranges::sort(flying, [](const Flying &a, const Flying &b) {
			if (a.source.index != b.source.index)
				return a.source.index < b.source.index;
			if (a.source.generation != b.source.generation)
				return a.source.generation < b.source.generation;
			if (a.weapon != b.weapon)
				return a.weapon < b.weapon;
			return a.fireTick != b.fireTick ? a.fireTick < b.fireTick : a.entity.index < b.entity.index;
		});
		const auto lookup = context.Lookup<Lookup>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const PresentationFrame &frame = context.Read<PresentationFrame>();
		std::vector<std::pair<ecs::Entity, const PresentedObject *>> shown;
		context.Read<PresentedObjects>().ForEach([&](const PresentedObject &object) { shown.emplace_back(object.entity, &object); });
		std::ranges::sort(shown, [](const auto &a, const auto &b) { return a.first.index < b.first.index; });
		const auto presented = [&](ecs::Entity entity) -> const PresentedObject * {
			const auto found = std::ranges::lower_bound(shown, entity.index, {}, [](const auto &entry) { return entry.first.index; });
			return found != shown.end() && found->first == entity ? found->second : nullptr;
		};
		const auto at = [&](ecs::Entity entity) -> std::optional<std::array<float, 3>> {
			if (const PresentedObject *object = presented(entity))
				return object->position;
			if (const auto *transform = lookup.IsAlive(entity) ? lookup.Get<engine::gameplay::Transform>(entity) : nullptr)
				return std::array<float, 3>{Engine::Math::ToFloat(transform->position.x), Engine::Math::ToFloat(transform->position.y),
					Engine::Math::ToFloat(transform->position.z)};
			return std::nullopt;
		};
		LaserFrame &out = context.Write<LaserFrame>();
		for (std::size_t first = 0; first < flying.size();)
		{
			std::size_t last = first;
			while (last < flying.size() && flying[last].source == flying[first].source && flying[last].weapon == flying[first].weapon)
				++last;
			const content::StreamLook &look = *streams.Of(flying[first].weapon);
			const ecs::Entity source = flying[first].source;
			const bool sourceAlive = lookup.IsAlive(source);
			// Shrouded shooters' streams are not drawn (a shooter alive but not presented).
			if (sourceAlive && presented(source) == nullptr)
			{
				first = last;
				continue;
			}
			// Its vehicle roof (getAllPoints).
			std::optional<std::array<float, 3>> roof;
			float reach = 0.0f;
			if (sourceAlive)
				if (const auto *ref = lookup.Get<engine::gameplay::DefinitionRef>(source))
					if (const DefinitionLooks *looks = catalog.Of(ref->index); looks != nullptr && looks->vehicle)
						if (const auto origin = at(source))
						{
							roof = std::array<float, 3>{(*origin)[0], (*origin)[1], (*origin)[2] + looks->constructionHeight + 0.5f};
							reach = looks->majorRadius * 1.5f;
						}
			// The ring, oldest first, with holes.
			std::vector<std::optional<std::array<float, 3>>> points;
			for (std::size_t index = first; index < last; ++index)
			{
				const Flying &shot = flying[index];
				const bool changed = index == first || (shot.target != flying[index - 1].target) ||
					(shot.target == ecs::Entity{} && !(shot.aim.x == flying[index - 1].aim.x && shot.aim.y == flying[index - 1].aim.y && shot.aim.z == flying[index - 1].aim.z));
				if (changed)
					points.push_back(std::nullopt);
				auto point = at(shot.entity);
				if (point && roof)
				{
					const float dx = (*roof)[0] - (*point)[0], dy = (*roof)[1] - (*point)[1];
					if (std::sqrt(dx * dx + dy * dy) <= reach)
						(*point)[2] = std::max((*point)[2], (*roof)[2]);
				}
				points.push_back(point);
			}
			const std::size_t start = look.maxSegments != 0 && points.size() > look.maxSegments ? points.size() - look.maxSegments : 0;
			const float scroll = static_cast<float>(frame.clock * 30.0) * look.scrollRate;
			for (std::size_t index = start; index + 1 < points.size(); ++index)
				if (points[index] && points[index + 1])
					out.beams.push_back({*points[index], *points[index + 1], look.width, {1.0f, 1.0f, 1.0f, 1.0f}, look.texture, look.tileFactor, scroll});
			first = last;
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::ProjectileStreamSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.projectile_streams";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	// After the lasers start the frame's beams afresh (RegisterProjectileStreams orders it).
	using After = SystemTypeList<>;
};
}
