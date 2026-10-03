export module games.renegade.content.levels.dynamic_level;
import std;
export import games.renegade.content.levels.physics_placement;
export import engine.level.model.level;

export namespace renegade::content
{
struct ActorPlacement
{
	std::uint32_t factory{}, token{}, definition{}, instance{}, physics_token{}, reference_token{};
	std::uint32_t innate_observer{};
	std::uint32_t innate_observer_alias{};
	bool pending_delete{}, cinematic_freeze{true}, observer_created_pending{};
	bool smart{}, control_enabled{true};
	std::int32_t control_owner{};
	std::int32_t ladder_index{-1};
	std::vector<std::uint32_t> observers;
	std::optional<Engine::Math::FixedOrientedBox3> zone;
	std::optional<Engine::Math::FixedAffineTransform3> transform;
	std::vector<std::byte> data;
	// Original authored IDs remain unchanged. Unsaved IDs need an unambiguous
	// local key until the game's dynamic-ID allocator assigns a network ID.
	std::uint64_t LevelId() const noexcept { return instance ? instance : 0x100000000ull + token; }
};
struct ObserverBinding
{
	std::uint32_t token{}, owner_token{}, id{};
	std::string program, parameters;
	std::vector<std::byte> state;
};
struct DynamicLevel
{
	struct BuiltinObserver {std::uint32_t factory{}, token{}, alias{}, id{}; std::vector<std::byte> data;};
	std::vector<ActorPlacement> actors;
	std::vector<PhysicsPlacement> physics;
	std::vector<ObserverBinding> observers;
	std::vector<BuiltinObserver> builtin_observers;
};

inline std::expected<ActorPlacement, std::string> ReadActorPlacement(const persist::Chunk &factory)
{
	using namespace persist;
	const auto data = ObjectData(factory.payload), pointer = One(factory.payload, 0x100100);
	if (!data || !pointer) return std::unexpected("invalid actor persistence factory");
	const auto token = U32(pointer->payload); if (!token) return std::unexpected(token.error());
	ActorPlacement actor; actor.factory = factory.id; actor.token = *token;
	actor.data.assign(data->payload.begin(), data->payload.end());
	const auto base = Descendants(data->payload, 910991407);
	if (!base || base->size() != 1) return std::unexpected("actor requires one BaseGameObj identity");
	const auto fields = Micros(base->front().payload); if (!fields) return std::unexpected(fields.error());
	std::set<unsigned> seen;
	for (const auto &field : *fields) if (field.id >= 2 && field.id <= 5) {
		if (!seen.insert(field.id).second) return std::unexpected("duplicate actor identity field");
		if (field.id == 2 || field.id == 3) {
			const auto value = U32(field.payload); if (!value) return std::unexpected(value.error());
			(field.id == 2 ? actor.definition : actor.instance) = *value;
		} else {
			const auto value = Flag(field.payload); if (!value) return std::unexpected(value.error());
			(field.id == 4 ? actor.pending_delete : actor.cinematic_freeze) = *value;
		}
	}
	if (!actor.token || !actor.definition || !seen.contains(2) || !seen.contains(3)) return std::unexpected("missing actor identity");
	const auto scriptable = Descendants(data->payload, 627001123);
	if (!scriptable || scriptable->size() > 1) return std::unexpected("invalid ScriptableGameObj variables");
	if (!scriptable->empty()) {
		const auto values = Micros(scriptable->front().payload); if (!values) return std::unexpected(values.error());
		bool reference_seen{}, created_seen{};
		for (const auto &field : *values) {
			if (field.id == 1 || field.id == 2) {
				const auto value = U32(field.payload); if (!value) return std::unexpected(value.error());
				if (field.id == 1) {if (reference_seen) return std::unexpected("duplicate actor reference token"); actor.reference_token = *value; reference_seen = true;}
				else actor.observers.push_back(*value);
			}
			if (field.id == 3) {
				const auto value = Flag(field.payload); if (!value || created_seen) return std::unexpected("invalid observer-created state");
				actor.observer_created_pending = *value; created_seen = true;
			}
		}
	}
	const auto physical = ScopedVariables(data->payload, 910991155, 910991146);
	if (!physical || physical->size() > 1) return std::unexpected("invalid PhysicalGameObj variables");
	if (!physical->empty()) {
		const auto values = Micros(physical->front().payload); if (!values) return std::unexpected(values.error());
		bool found{};
		for (const auto &field : *values) if (field.id == 10) {
			const auto value = U32(field.payload); if (!value || found) return std::unexpected("invalid actor physics linkage");
			actor.physics_token = *value; found = true;
		}
	}
	if (factory.id == 0x4010e) {
		// SoldierGameObj uses a built-in AI observer alongside Lua mission
		// bindings. Its token is saved in the soldier's own variable namespace.
		const auto soldier = ScopedVariables(data->payload, 909991656, 909991657);
		if (!soldier || soldier->size() != 1) return std::unexpected("invalid SoldierGameObj variables");
		const auto values = Micros(soldier->front().payload); if (!values) return std::unexpected(values.error());
		for (const auto &field : *values) if (field.id == 9) {
			const auto value = U32(field.payload); if (!value) return std::unexpected(value.error()); actor.innate_observer = *value;
		}
	}
	const auto smart = ScopedVariables(data->payload, 910991119, 910991114);
	if (!smart || smart->size() > 1) return std::unexpected("invalid SmartGameObj variables");
	if (!smart->empty()) {
		actor.smart = true;
		const auto values = Micros(smart->front().payload); if (!values) return std::unexpected(values.error());
		for (const auto &field : *values) {
			if (field.id == 1) {const auto value = Flag(field.payload); if (!value) return std::unexpected(value.error()); actor.control_enabled = *value;}
			if (field.id == 4) {const auto value = U32(field.payload); if (!value) return std::unexpected(value.error()); actor.control_owner = std::bit_cast<std::int32_t>(*value);}
		}
	}
	if (factory.id == 0x40122) {
		const auto variables = One(data->payload, 922991807);
		if (!variables) return std::unexpected(variables.error());
		const auto values = Micros(variables->payload); if (!values) return std::unexpected(values.error());
		for (const auto &field : *values) if (field.id == 1) {
			const auto box = OrientedBox(field.payload); if (!box || actor.zone) return std::unexpected("invalid script-zone bounds");
			actor.zone = *box;
		}
		if (!actor.zone) return std::unexpected("script zone has no bounds");
		actor.transform = Engine::Math::FixedAffineTransform3::From_Translation(actor.zone->center);
	}
	if (factory.id == 0x40124 || factory.id == 0x40133) {
		const auto variables = One(data->payload, factory.id == 0x40124 ? 0x4247a3a7u : 207011121u);
		if (!variables) return std::unexpected(variables.error());
		const auto values = Micros(variables->payload); if (!values) return std::unexpected(values.error());
		bool ladder_index_seen{};
		for (const auto &field : *values) {
		if(factory.id==0x40124 && field.id==2) {
			const auto index=U32(field.payload);if(!index || ladder_index_seen) return std::unexpected("invalid transition ladder index");
			actor.ladder_index=std::bit_cast<std::int32_t>(*index);ladder_index_seen=true;
		}
		if (field.id == 1) {
			if (actor.transform) return std::unexpected("duplicate authored actor transform");
			if (factory.id == 0x40124) {
				const auto pose = Matrix(field.payload); if (!pose) return std::unexpected(pose.error()); actor.transform = *pose;
			} else {
				const auto position = Vector(field.payload); if (!position) return std::unexpected(position.error());
				actor.transform = Engine::Math::FixedAffineTransform3::From_Translation(*position);
			}
		}
		}
		if(factory.id==0x40124 && (!actor.transform || !ladder_index_seen)) return std::unexpected("incomplete transition placement");
	}
	return actor;
}

inline std::expected<ObserverBinding, std::string> ReadObserverBinding(const persist::Chunk &record)
{
	using namespace persist;
	if (record.id != 131001134) return std::unexpected("unexpected script collection record");
	const auto header = One(record.payload, 131001135);
	if (!header) return std::unexpected(header.error());
	const auto fields = Micros(header->payload); if (!fields) return std::unexpected(fields.error());
	ObserverBinding binding; std::set<unsigned> seen;
	for (const auto &field : *fields) if (field.id >= 1 && field.id <= 5) {
		if (!seen.insert(field.id).second) return std::unexpected("duplicate script header field");
		if (field.id <= 2) {
			const auto value = Text(field.payload); if (!value) return std::unexpected(value.error());
			(field.id == 1 ? binding.program : binding.parameters) = *value;
		} else {
			const auto value = U32(field.payload); if (!value) return std::unexpected(value.error());
			if (field.id == 3) binding.token = *value;
			if (field.id == 4) binding.owner_token = *value;
			if (field.id == 5) binding.id = *value;
		}
	}
	if (seen.size() != 5 || !binding.token || !binding.owner_token || binding.program.empty()) return std::unexpected("incomplete observer header");
	const auto children = Children(record.payload); if (!children) return std::unexpected(children.error());
	bool state_seen{};
	for (const auto &child : *children) if (child.id == 131001136) {
		if (state_seen) return std::unexpected("duplicate script persistence state");
		binding.state.assign(child.payload.begin(), child.payload.end()); state_seen = true;
	}
	return binding;
}

inline std::expected<DynamicLevel, std::string> ReadDynamicLevel(persist::Bytes level_data)
{
	using namespace persist;
	const auto level = One(level_data, 0x3c51c461); if (!level) return std::unexpected(level.error());
	const auto combat = One(level->payload, 0x40000); if (!combat) return std::unexpected(combat.error());
	const auto manager = One(combat->payload, 0x36a82ea6); if (!manager) return std::unexpected(manager.error());
	const auto objects = One(manager->payload, 916991653); if (!objects) return std::unexpected(objects.error());
	const auto factories = Children(objects->payload); if (!factories) return std::unexpected(factories.error());
	DynamicLevel result; std::set<std::uint32_t> tokens, ids;
	for (const auto &factory : *factories) {
		auto actor = ReadActorPlacement(factory); if (!actor) return std::unexpected(actor.error());
		if (!tokens.insert(actor->token).second || (actor->instance && !ids.insert(actor->instance).second)) return std::unexpected("duplicate authored actor identity");
		result.actors.push_back(std::move(*actor));
	}
	const auto scene = One(level->payload, 0x20050); if (!scene) return std::unexpected(scene.error());
	const auto dynamic_scene = One(scene->payload, 0x7001); if (!dynamic_scene) return std::unexpected(dynamic_scene.error());
	const auto dynamic_objects = One(dynamic_scene->payload, 0x890100); if (!dynamic_objects) return std::unexpected(dynamic_objects.error());
	const auto physical_records = Children(dynamic_objects->payload); if (!physical_records) return std::unexpected(physical_records.error());
	std::map<std::uint32_t, std::size_t> physics_index;
	for (const auto &record : *physical_records) {
		if (record.id != 0x890101) return std::unexpected("unexpected dynamic physical wrapper");
		const auto physical_factories = Children(record.payload);
		if (!physical_factories || physical_factories->size() != 1) return std::unexpected("invalid dynamic physical factory");
		auto physics = ReadPhysicsPlacement(physical_factories->front()); if (!physics) return std::unexpected(physics.error());
		if (!physics->token || !physics_index.emplace(physics->token, result.physics.size()).second) return std::unexpected("duplicate dynamic physics token");
		for (const auto alias : physics->remap_tokens) if (alias) {
			const auto [entry, inserted] = physics_index.emplace(alias, result.physics.size());
			if (!inserted && entry->second != result.physics.size()) return std::unexpected("ambiguous dynamic physics base-pointer alias");
		}
		result.physics.push_back(std::move(*physics));
	}
	for (auto &actor : result.actors) if (actor.physics_token) {
		const auto physical = physics_index.find(actor.physics_token);
		if (physical == physics_index.end()) return std::unexpected("actor references missing dynamic physics");
		actor.transform = result.physics[physical->second].transform;
	}
	const auto scripts = One(combat->payload, 0x36a82eab); if (!scripts) return std::unexpected(scripts.error());
	const auto records = Children(scripts->payload); if (!records) return std::unexpected(records.error());
	std::map<std::uint32_t, std::size_t> observer_index;
	std::set<std::uint32_t> observer_ids;
	for (const auto &record : *records) {
		auto observer = ReadObserverBinding(record); if (!observer) return std::unexpected(observer.error());
		if (!tokens.contains(observer->owner_token) || !observer_index.emplace(observer->token, result.observers.size()).second || !observer_ids.insert(observer->id).second)
			return std::unexpected("unresolved or duplicate observer identity");
		result.observers.push_back(std::move(*observer));
	}
	const auto persistent_observers = One(combat->payload, 0x36a82eac);
	if (!persistent_observers) return std::unexpected(persistent_observers.error());
	const auto builtin_list = One(persistent_observers->payload, 1); if (!builtin_list) return std::unexpected(builtin_list.error());
	const auto builtin_factories = Children(builtin_list->payload); if (!builtin_factories) return std::unexpected(builtin_factories.error());
	std::map<std::uint32_t, std::uint32_t> builtin_aliases;
	std::set<std::uint32_t> builtin_base_tokens;
	for (const auto &factory : *builtin_factories) {
		const auto pointer = One(factory.payload, 0x100100), data = ObjectData(factory.payload);
		if (!pointer || !data) return std::unexpected("invalid persistent built-in observer factory");
		const auto variables = Descendants(data->payload, 411001150);
		if (!variables || variables->size() != 1) return std::unexpected("invalid persistent observer variables");
		const auto values = Micros(variables->front().payload); if (!values) return std::unexpected(values.error());
		const auto token = U32(pointer->payload); if (!token) return std::unexpected(token.error());
		DynamicLevel::BuiltinObserver observer; observer.factory = factory.id; observer.token = *token;
		observer.data.assign(data->payload.begin(), data->payload.end());
		std::set<unsigned> seen;
		for (const auto &field : *values) if (field.id == 1 || field.id == 2) {
			if (!seen.insert(field.id).second) return std::unexpected("duplicate persistent observer field");
			const auto value = U32(field.payload); if (!value) return std::unexpected(value.error());
			(field.id == 1 ? observer.alias : observer.id) = *value;
		}
		if (seen.size() != 2 || !observer.token || !observer.alias || !builtin_base_tokens.insert(observer.alias).second || !builtin_aliases.emplace(observer.token, observer.alias).second)
			return std::unexpected("duplicate or null built-in observer identity");
		result.builtin_observers.push_back(std::move(observer));
	}
	for (auto &actor : result.actors) if (actor.innate_observer) {
		const auto found = builtin_aliases.find(actor.innate_observer);
		if (found == builtin_aliases.end()) return std::unexpected("unresolved soldier innate observer");
		actor.innate_observer_alias = found->second;
	}
	for (const auto &actor : result.actors) for (const auto token : actor.observers) {
		if (!token || token == actor.innate_observer_alias) continue;
		const auto found = observer_index.find(token);
		if (found == observer_index.end() || result.observers[found->second].owner_token != actor.token) return std::unexpected("unresolved actor observer linkage");
	}
	return result;
}

inline std::expected<void, std::string> AppendDynamicLevel(engine::level::Level &level, const DynamicLevel &source)
{
	std::map<std::uint32_t, const PhysicsPlacement *> physics;
	for (const auto &object : source.physics) {
		physics.emplace(object.token, &object);
		for (const auto alias : object.remap_tokens) if (alias) physics.emplace(alias, &object);
	}
	std::map<std::uint32_t, const ObserverBinding *> observers;
	for (const auto &binding : source.observers) observers.emplace(binding.token, &binding);
	// Walk actor list and each actor's saved observer order, not the separate
	// script collection's allocation order. Pending-delete editor remnants are
	// retained in source inventory, but do not instantiate active level objects.
	std::vector<engine::level::Placement> placements;
	std::vector<engine::level::TriggerVolume> volumes;
	std::vector<engine::level::BehaviorBinding> bindings;
	for (const auto &actor : source.actors) {
		if (actor.pending_delete) continue;
		engine::level::Placement placement;
		placement.id = actor.LevelId(); placement.definition = actor.definition; placement.transform = actor.transform;
		if (actor.transform) placement.position = {actor.transform->elements[3], actor.transform->elements[7], actor.transform->elements[11]};
		if (actor.physics_token) {
			const auto found = physics.find(actor.physics_token); if (found == physics.end()) return std::unexpected("missing actor physics");
			placement.model = found->second->model; placement.flags = found->second->flags;
		}
		if (actor.zone) {
			placement.kind = engine::level::PlacementKind::Trigger;
			volumes.push_back({actor.LevelId(), *actor.zone});
		}
		placements.push_back(std::move(placement));
		for (const auto token : actor.observers) {
			if (!token || token == actor.innate_observer_alias) continue;
			const auto found = observers.find(token); if (found == observers.end()) return std::unexpected("missing actor observer");
			const auto &binding = *found->second;
			bindings.push_back({binding.id, actor.LevelId(), binding.program, binding.parameters});
		}
	}
	level.placements.insert(level.placements.end(), std::make_move_iterator(placements.begin()), std::make_move_iterator(placements.end()));
	level.volumes.insert(level.volumes.end(), volumes.begin(), volumes.end());
	level.behaviors.insert(level.behaviors.end(), std::make_move_iterator(bindings.begin()), std::make_move_iterator(bindings.end()));
	return {};
}
}
