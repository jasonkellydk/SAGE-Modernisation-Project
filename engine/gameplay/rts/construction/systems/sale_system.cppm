export module engine.gameplay.rts.construction.systems.sale_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.construction.components.construction_progress;
export import engine.gameplay.rts.construction.components.sale;
export import engine.gameplay.rts.construction.resources.sales;

// BuildAssistant::update, once a tick in entity order: a structure being
// sold keeps its scaffold up for the scaffold time from its sale, then its
// construction falls a step a tick; crossing zero it has sunk (it shows as
// SOLD), and at the done percent its sale is over (the game refunds it and
// removes it).
export namespace engine::gameplay
{
struct SaleSystem
{
	using Query = ecs::Query<ecs::Write<Sale>, ecs::Write<ConstructionProgress>>;
	using Resources = ecs::Resources<ecs::Read<SaleSettings>, ecs::Write<SalesDone>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const SaleSettings &settings = context.Read<SaleSettings>();
		auto &done = context.Write<SalesDone>().entities;
		done.clear();
		const std::uint64_t tick = context.Tick();
		query.ForEachChunk([&](auto chunk) {
			auto sales = chunk.template Get<Sale>();
			auto progress = chunk.template Get<ConstructionProgress>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < sales.size(); ++row)
			{
				Engine::Math::Fixed &percent = progress[row].percent;
				if (tick - sales[row].since >= settings.scaffoldTicks)
				{
					const Engine::Math::Fixed before = percent;
					percent -= settings.perTick;
					if (before > Engine::Math::Fixed{} && percent <= Engine::Math::Fixed{})
						sales[row].sunk = 1;
				}
				if (percent <= settings.donePercent)
					done.push_back(entities[row]);
			}
		});
		std::sort(done.begin(), done.end(), [](ecs::Entity a, ecs::Entity b) { return a.index < b.index; });
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::SaleSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.sale";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
