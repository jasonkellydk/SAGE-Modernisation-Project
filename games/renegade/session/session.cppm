export module games.renegade.session;
import std;
export import games.renegade.gameplay.cinematics.systems.cinematic_system;
export import engine.gameplay.common.scripts.systems.timeline_behavior_system;
export import engine.ecs;
export import games.renegade.gameplay.defense.systems.defense_system;
export import games.renegade.session.scene_state;
export import engine.gameplay.common.spatial.systems.affine_snapshot_system;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.level_identity;
export import engine.gameplay.common.areas.systems.volume_presence_system;
export import games.renegade.content.armor.defense_preset;
export import engine.gameplay.common.physics.resources.collision_geometry;
export import engine.gameplay.common.physics.systems.kinematic_collision_system;
export import engine.gameplay.common.physics.systems.suspension_system;
export import games.renegade.gameplay.humans.systems.motion_system;
export import games.renegade.gameplay.humans.systems.animation_system;
export import games.renegade.gameplay.humans.systems.ladder_transition_system;
export import games.renegade.gameplay.humans.systems.goto_system;
export import engine.gameplay.common.appearance.systems.clip_playback_system;
export import engine.gameplay.common.appearance.components.draw_hidden;
export import engine.gameplay.common.appearance.components.attached_model;
export import engine.gameplay.common.appearance.components.pose_binding;
export import engine.gameplay.fps.weapons.systems.projectile_launch_system;
export import engine.gameplay.common.audio.systems.emitter_snapshot_system;
export import games.renegade.gameplay.missions.systems.mission_command_system;
export import games.renegade.gameplay.missions.systems.conversation_system;
export import games.renegade.gameplay.missions.systems.speech_animation_system;
export import games.renegade.gameplay.missions.systems.objective_system;
export import games.renegade.gameplay.weapons.systems.weapon_control_system;
export import engine.gameplay.common.inventory.systems.inventory_selection_system;
import games.renegade.content.weapons.armed_loadout;

export namespace renegade
{
// Execution ownership is confined to the game composition root. All entity
// data lives in the shared archetype world, in contiguous component columns.
class Session
{
public:
	static constexpr std::uint32_t TicksPerSecond=60;
	explicit Session(std::size_t workers = 1, std::size_t chunkCapacity = 128) :
		m_world(ecs::WorldConfig{chunkCapacity})
	{
		m_world.RegisterComponent<engine::gameplay::Health>();
		m_world.RegisterComponent<engine::gameplay::Shield>();
		m_world.RegisterComponent<engine::gameplay::InventoryItem>();m_world.RegisterComponent<engine::gameplay::InventorySelection>();m_world.RegisterComponent<engine::gameplay::InventoryControl>();
		m_world.RegisterComponent<engine::gameplay::Magazine>();m_world.RegisterComponent<WeaponState>();m_world.RegisterComponent<WeaponControl>();m_world.RegisterComponent<WeaponInput>();
		m_world.RegisterComponent<Defense>();
		m_world.RegisterComponent<engine::gameplay::AffinePose>();
		m_world.RegisterComponent<engine::gameplay::ModelOverride>();
		m_world.RegisterComponent<engine::gameplay::AttachedModel>();
		m_world.RegisterComponent<engine::gameplay::PoseBinding>();
		m_world.RegisterComponent<engine::gameplay::DefinitionRef>();
		m_world.RegisterComponent<engine::gameplay::Transform>();
		m_world.RegisterComponent<engine::gameplay::Owner>();
		m_world.RegisterComponent<PhysicsIdentity>();
		m_world.RegisterComponent<ActorIdentity>();
		m_world.RegisterComponent<engine::gameplay::LevelIdentity>();
		m_world.RegisterComponent<engine::gameplay::VolumeProbe>();
		m_world.RegisterComponent<engine::gameplay::VolumeContact>();
		m_world.RegisterComponent<HumanMovement>();m_world.RegisterComponent<HumanState>();m_world.RegisterComponent<HumanControl>();
		m_world.RegisterComponent<engine::gameplay::InputPermission>();
		m_world.RegisterComponent<HumanGoto>();m_world.RegisterComponent<engine::gameplay::RouteTraversal>();m_world.RegisterComponent<engine::gameplay::RouteSteering>();
		m_world.RegisterComponent<engine::gameplay::SweptHull>();m_world.RegisterComponent<engine::gameplay::SweepContacts>();
		m_world.RegisterComponent<engine::gameplay::ClipPlayback>();
		m_world.RegisterComponent<engine::gameplay::KinematicCollider>();
		m_world.RegisterComponent<engine::gameplay::ClipTransition>();m_world.RegisterComponent<HumanAnimation>();
		m_world.RegisterComponent<engine::gameplay::FireTrigger>();m_world.RegisterComponent<engine::gameplay::FireSequence>();
		m_world.RegisterComponent<engine::gameplay::ProjectileLauncher>();m_world.RegisterComponent<engine::gameplay::LinearFlight>();
		m_world.RegisterComponent<engine::gameplay::DrawHidden>();
		m_world.RegisterComponent<engine::gameplay::TimelinePlayback>();m_world.RegisterComponent<engine::gameplay::SampledPose>();
		m_world.RegisterComponent<CinematicActor>();m_world.RegisterComponent<CinematicDirector>();
		m_world.RegisterComponent<engine::gameplay::Suspension>();m_world.RegisterComponent<engine::gameplay::SuspensionState>();
		m_world.RegisterComponent<engine::gameplay::SoundEmitter>();
		m_world.RegisterComponent<engine::gameplay::LuaBehavior>();m_world.RegisterComponent<engine::gameplay::LuaBehaviorState>();
		m_world.RegisterComponent<engine::gameplay::TimelineBehavior>();
		m_world.RegisterComponent<engine::gameplay::ScheduledBehaviorMessage>();
		m_world.RegisterComponent<ConversationState>();m_world.RegisterComponent<ConversationVoice>();
		m_world.RegisterComponent<SpeechAnimation>();
		m_world.RegisterComponent<MissionObjective>();m_world.RegisterComponent<engine::gameplay::SimulationAge>();m_world.RegisterComponent<engine::gameplay::TrackedPosition>();
		m_world.FinalizeComponents();
		m_world.EmplaceResource<DamageRules>();
		m_world.EmplaceResource<DamageRequests>();
		m_world.EmplaceResource<DefenseHits>();
		m_world.EmplaceResource<engine::gameplay::InventoryEntries>();m_world.EmplaceResource<engine::gameplay::InventorySnapshot>();m_world.EmplaceResource<WeaponDefinitions>();m_world.EmplaceResource<WeaponShots>();
		m_world.EmplaceResource<SceneState>();
		m_world.EmplaceResource<HumanAnimationCatalog>();
		m_world.EmplaceResource<LadderPortals>();
		m_world.EmplaceResource<MissionRoutes>();m_world.EmplaceResource<HumanGotoRequests>();m_world.EmplaceResource<engine::gameplay::RouteCurves>();
		m_world.EmplaceResource<engine::gameplay::TimelineLibrary>();m_world.EmplaceResource<engine::gameplay::FiredTimelineCues>();
		m_world.EmplaceResource<engine::gameplay::PoseTrackLibrary>();m_world.EmplaceResource<CinematicLibrary>();
		m_world.EmplaceResource<engine::gameplay::CollisionGeometry>();
		m_world.EmplaceResource<engine::gameplay::KinematicCollisionLibrary>();m_world.EmplaceResource<engine::gameplay::KinematicCollisionOutputs>();
		m_world.EmplaceResource<engine::gameplay::ClipRequests>();m_world.EmplaceResource<MissionAnimationLibrary>();
		m_world.EmplaceResource<engine::gameplay::SuspensionSnapshots>();
		m_world.EmplaceResource<engine::gameplay::AffineVisibleObjects>();
		m_world.EmplaceResource<engine::gameplay::ProjectileVisuals>();
		m_world.EmplaceResource<engine::gameplay::SoundEmitterSnapshots>();
		m_world.EmplaceResource<engine::gameplay::TriggerVolumes>();
		m_world.EmplaceResource<engine::gameplay::VolumeTransitions>();
		m_world.EmplaceResource<engine::gameplay::VolumeProbePositions>();
		m_world.EmplaceResource<engine::gameplay::VolumeProbePositionOutputs>();
		m_world.EmplaceResource<engine::gameplay::BehaviorPrograms>();m_world.EmplaceResource<engine::gameplay::BehaviorInbox>();
		m_world.EmplaceResource<engine::gameplay::DueBehaviorMessages>();m_world.EmplaceResource<engine::gameplay::BehaviorEmissions>();
		m_world.EmplaceResource<engine::gameplay::BehaviorInvocations>();m_world.EmplaceResource<engine::gameplay::BehaviorAttachments>();
		m_world.EmplaceResource<engine::gameplay::InputPermissionRequests>();
		m_world.EmplaceResource<engine::gameplay::PositionRequests>();
		m_world.EmplaceResource<engine::gameplay::HeadingTargets>();m_world.EmplaceResource<engine::gameplay::HeadingRequests>();
		m_world.EmplaceResource<CameraLookRequests>();
		m_world.EmplaceResource<MissionRequests>();
		m_world.EmplaceResource<ObjectiveVocabulary>();m_world.EmplaceResource<ObjectiveTransaction>();m_world.EmplaceResource<ObjectiveChanges>();
		m_world.EmplaceResource<ConversationLibrary>();m_world.EmplaceResource<ConversationLineEvents>();
		m_world.EmplaceResource<SpeechAnimations>();
		m_registry.Register(m_defense);
		m_registry.Register(m_inventory_snapshot);m_registry.Register(m_inventory_selection);m_registry.Register(m_weapon_control);m_registry.Register(m_weapon_state);
		m_registry.Register(m_timeline);m_registry.Register(m_pose_track);m_registry.Register(m_cinematic_actor);m_registry.Register(m_cinematic_director);
		m_registry.Register(m_timeline_behaviors);
		m_registry.Register(m_human_motion);
		m_registry.Register(m_ladder_transition);
		m_registry.Register(m_route_traversal);m_registry.Register(m_human_goto);m_registry.Register(m_human_goto_requests);
		m_registry.Register(m_suspension);m_registry.Register(m_suspension_snapshot);
		m_registry.Register(m_human_animation);
		m_registry.Register(m_clip_playback);
		m_registry.Register(m_clip_requests);m_registry.Register(m_kinematic_collision);
		m_registry.Register(m_fire_sequence);
		m_registry.Register(m_projectile_launch);m_registry.Register(m_linear_flight);
		m_registry.Register(m_projectile_snapshot);
		m_registry.Register(m_scene_snapshot);
		m_registry.Register(m_emitter_snapshot);
		m_registry.Register(m_probe_snapshot);
		m_registry.Register(m_volume_presence);
		m_registry.Register(m_message_timers);m_registry.Register(m_lua_behaviors);m_registry.Register(m_mission_commands);
		m_registry.Register(m_behavior_attachments);
		m_registry.Register(m_input_permissions);
		m_registry.Register(m_position_requests);
		m_registry.Register(m_heading_targets);m_registry.Register(m_rigid_heading);m_registry.Register(m_human_heading);
		m_registry.Register(m_conversations);m_registry.Register(m_conversation_voices);
		m_registry.Register(m_speech_animation);
		m_registry.Register(m_objectives);m_registry.Register(m_objective_age);m_registry.Register(m_tracked_position);
		m_registry.OrderBefore<DefenseSystem,WeaponControlSystem>();
		m_registry.OrderBefore<MissionCommandSystem,ObjectiveSystem>();
		m_registry.OrderBefore<engine::gameplay::RigidHeadingSystem,ObjectiveSystem>();
		m_registry.OrderBefore<engine::gameplay::PositionRequestSystem,engine::gameplay::TrackedPositionSystem>();
		m_registry.OrderBefore<engine::gameplay::KinematicCollisionSystem,engine::gameplay::LinearFlightSystem>();
		m_registry.OrderBefore<MissionCommandSystem,HumanGotoRequestSystem>();
		m_registry.OrderBefore<HumanGotoControlSystem,LadderTransitionSystem>();
		m_registry.OrderBefore<MissionCommandSystem,engine::gameplay::BehaviorAttachmentSystem>();
		m_registry.OrderBefore<MissionCommandSystem,engine::gameplay::InputPermissionSystem>();
		m_registry.OrderBefore<MissionCommandSystem,engine::gameplay::PositionRequestSystem>();
		m_registry.OrderBefore<engine::gameplay::PositionRequestSystem,engine::gameplay::AffineSnapshotSystem>();
		m_registry.OrderBefore<engine::gameplay::PositionRequestSystem,engine::gameplay::VolumeProbeSnapshotSystem>();
		m_registry.OrderBefore<engine::gameplay::PositionRequestSystem,engine::gameplay::EmitterSnapshotSystem>();
		m_registry.OrderBefore<engine::gameplay::PositionRequestSystem,engine::gameplay::KinematicCollisionSystem>();
		m_registry.OrderBefore<engine::gameplay::PositionRequestSystem,engine::gameplay::LinearFlightSystem>();
		m_registry.OrderBefore<engine::gameplay::PositionRequestSystem,engine::gameplay::HeadingTargetSystem>();
		m_registry.OrderBefore<engine::gameplay::RigidHeadingSystem,engine::gameplay::AffineSnapshotSystem>();
		m_registry.OrderBefore<engine::gameplay::RigidHeadingSystem,engine::gameplay::VolumeProbeSnapshotSystem>();
		m_registry.OrderBefore<engine::gameplay::RigidHeadingSystem,engine::gameplay::EmitterSnapshotSystem>();
		m_registry.OrderBefore<engine::gameplay::RigidHeadingSystem,engine::gameplay::KinematicCollisionSystem>();
		m_registry.OrderBefore<engine::gameplay::RigidHeadingSystem,engine::gameplay::LinearFlightSystem>();
		m_registry.OrderBefore<MissionCommandSystem,engine::gameplay::VolumePresenceSystem>();
		m_registry.Finalize(m_world.Components());
		m_scheduler = std::make_unique<ecs::Scheduler>(m_world, m_registry, engine::jobs::JobSystemConfig{workers});
		m_scheduler->Finalize(engine::time::FixedStep{TicksPerSecond});
	}
	ecs::World &World() noexcept { return m_world; }
	const ecs::World &World() const noexcept { return m_world; }
	std::uint64_t Tick() const noexcept { return m_tick; }
	std::expected<void, std::string> LoadScene(const content::LevelScene &level, const content::DefinitionCatalog &catalog,
		const content::ArmorCatalog &armor, int difficulty = 1)
	{
		using namespace engine::gameplay;
		if (m_world.EntityCount() || m_world.IsScheduledExecutionActive()) return std::unexpected("level loading requires a fresh idle session");
		if (!level.first_load) return std::unexpected("saved mission needs a resume-state adapter");
		const auto valid = engine::level::ValidateSpatialLevel(level.level);
		if (!valid) return std::unexpected(valid.error());
		const auto ladders=PrepareLadderPortals(level.level);if(!ladders) return std::unexpected(ladders.error());
		const auto start = content::ResolvePlayerStart(level, catalog);
		if (!start) return std::unexpected(start.error());
		auto vitals = content::ReadDefensePreset(*catalog.Find(start->definition), armor);
		if (!vitals) return std::unexpected(vitals.error());
		content::ApplySoloDifficulty(*vitals, difficulty);
		const auto movement=content::ReadHumanMovementDefinition(*catalog.Find(start->definition),*catalog.Find(start->physics));
		if(!movement) return std::unexpected(movement.error());
		const auto routes=PrepareMissionRoutes(level.level,m_world.Resource<engine::gameplay::RouteCurves>(),m_world.Resource<MissionRoutes>());
		if(!routes) return std::unexpected(routes.error());
		for (const auto &placement : level.statics)
			if (placement.definition && !catalog.Find(placement.definition)) return std::unexpected("static physics references a missing definition");
		for (const auto &placement : level.level.placements)
			if (placement.definition && !catalog.Find(placement.definition)) return std::unexpected("level placement references a missing definition");
		if (std::ranges::count(level.level.placements, engine::level::PlacementKind::Geometry, &engine::level::Placement::kind) != static_cast<std::ptrdiff_t>(level.statics.size()))
			return std::unexpected("composed geometry inventory differs from source physics");
		for (const auto &actor : level.dynamic.actors) if (!actor.pending_delete) {
			const auto placement = std::ranges::find(level.level.placements, actor.LevelId(), &engine::level::Placement::id);
			if (placement == level.level.placements.end() || placement->kind == engine::level::PlacementKind::Geometry)
				return std::unexpected("missing composed actor placement");
		}
		for (const auto &volume : level.level.volumes) {
			const auto owner = std::ranges::find(level.level.placements, volume.subject, &engine::level::Placement::id);
			const auto *definition = catalog.Find(owner->definition);
			if (!definition || !definition->zone) return std::unexpected("trigger owner is missing its typed script-zone definition");
		}
		auto &state = m_world.Resource<SceneState>();
		std::map<std::string, std::uint32_t> model_ids;
		const auto model_id = [&](const std::string &model) -> std::uint32_t {
			if (model.empty() || model == "NULL") return 0;
			if (const auto found = model_ids.find(model); found != model_ids.end()) return found->second;
			const auto id = static_cast<std::uint32_t>(state.models.size()); state.models.push_back(model); model_ids.emplace(model, id); return id;
		};
		ecs::CommandBuffer commands; std::vector<ecs::DeferredEntity> statics;
		std::map<std::uint64_t, ecs::DeferredEntity> authored_entities;
		std::size_t geometry_index{};
		for (const auto &placement : level.level.placements) {
			if (placement.kind != engine::level::PlacementKind::Geometry) continue;
			const auto &source = level.statics[geometry_index++];
			const auto entity = commands.Create(); statics.push_back(entity);
			authored_entities.emplace(placement.id, entity);
			commands.Add<LevelIdentity>(entity, LevelIdentity{placement.id});
			commands.Add<AffinePose>(entity, AffinePose{*placement.transform});
			commands.Add<ModelOverride>(entity, ModelOverride{model_id(placement.model)});
			commands.Add<DefinitionRef>(entity, DefinitionRef{placement.definition});
			commands.Add<PhysicsIdentity>(entity, PhysicsIdentity{source.factory, source.token, source.instance, source.flags, source.render_factory});
		}
		std::vector<ecs::DeferredEntity> actors;
		std::vector<ecs::DeferredEntity> smart_probes;
		for (const auto &source : level.dynamic.actors) {
			if (source.pending_delete) continue;
			const auto placement = std::ranges::find(level.level.placements, source.LevelId(), &engine::level::Placement::id);
			if (placement == level.level.placements.end()) return std::unexpected("missing composed actor placement");
			const auto entity = commands.Create(); actors.push_back(entity); authored_entities.emplace(placement->id, entity);
			commands.Add<LevelIdentity>(entity, LevelIdentity{placement->id});
			commands.Add<DefinitionRef>(entity, DefinitionRef{placement->definition});
			commands.Add<ActorIdentity>(entity, ActorIdentity{source.factory, source.token, source.instance, source.innate_observer});
			if(source.physics_token) {
				const auto physics=std::ranges::find_if(level.dynamic.physics,[&](const auto& item) {
					return item.token==source.physics_token || std::ranges::find(item.remap_tokens,source.physics_token)!=item.remap_tokens.end();
				});
				if(physics==level.dynamic.physics.end()) return std::unexpected("actor physics identity is missing");
				commands.Add<PhysicsIdentity>(entity,PhysicsIdentity{physics->factory,physics->token,physics->instance,physics->flags,physics->render_factory});
			}
			const auto* definition=catalog.Find(placement->definition);if(!definition) return std::unexpected("actor has no content definition");
			if(definition->hidden || definition->editor_only) commands.Add<DrawHidden>(entity);
			if (placement->transform) {
				commands.Add<AffinePose>(entity, AffinePose{*placement->transform});
				if (!definition->editor_only && !placement->model.empty()) commands.Add<ModelOverride>(entity, ModelOverride{model_id(placement->model)});
				commands.Add<Transform>(entity, Transform{placement->position, Engine::Math::Atan2(placement->transform->elements[4], placement->transform->elements[0])});
				if (source.smart) { commands.Add<VolumeProbe>(entity, VolumeProbe{2, actors.size()}); smart_probes.push_back(entity); }
				if(source.factory==0x4010e && !definition->editor_only) {
					const auto* physics=catalog.Find(definition->physics);if(!physics) return std::unexpected("soldier physics definition is missing");
					const auto motion=content::ReadHumanMovementDefinition(*definition,*physics);if(!motion) return std::unexpected("soldier "+definition->name+" ("+std::to_string(placement->id)+"): "+motion.error());
					commands.Add<HumanMovement>(entity,HumanMovement{*motion});commands.Add<HumanState>(entity);
					HumanControl control;control.facing=Engine::Math::Atan2(placement->transform->elements[4],placement->transform->elements[0]);
					commands.Add<HumanControl>(entity,control);commands.Add<HumanGoto>(entity);
					commands.Add<InputPermission>(entity);
					commands.Add<RouteTraversal>(entity);commands.Add<RouteSteering>(entity);
				}
			}
		}
		const auto player = commands.Create();
		commands.Add<AffinePose>(player, AffinePose{start->transform});
		commands.Add<DefinitionRef>(player, DefinitionRef{start->definition});
		commands.Add<Owner>(player, Owner{1});
		commands.Add<Transform>(player, Transform{{start->transform.elements[3], start->transform.elements[7], start->transform.elements[11]}, Engine::Math::Atan2(start->transform.elements[4], start->transform.elements[0])});
		commands.Add<VolumeProbe>(player, VolumeProbe{1, 0});
		commands.Add<Health>(player, vitals->health);
		commands.Add<Shield>(player, vitals->shield);
		commands.Add<Defense>(player, vitals->defense);
		commands.Add<HumanMovement>(player,HumanMovement{*movement});commands.Add<HumanState>(player);
		HumanControl control;control.facing=Engine::Math::Atan2(start->transform.elements[4],start->transform.elements[0]);
		commands.Add<HumanControl>(player,control);
		commands.Add<InputPermission>(player);
		// Keep the local player's render model out of the static scene snapshot;
		// first-person body/weapon drawing belongs to the player presentation.
		m_world.Commit(commands);
		m_world.Resource<DamageRules>() = armor.rules;
		for (const auto entity : statics) state.statics.push_back(commands.Resolve(entity));
		for (const auto entity : actors) state.actors.push_back(commands.Resolve(entity));
		for (const auto &[id, entity] : authored_entities) state.authored_entities.emplace(id, commands.Resolve(entity));
		state.player = commands.Resolve(player); state.start = *start; state.start_script = level.start_script;
		auto &volumes = m_world.Resource<TriggerVolumes>(); volumes.exit_on_removal = false;
		volumes.require_live_subject=true;volumes.retire_missing_probes=true;
		ecs::CommandBuffer contacts;
		std::uint64_t contact_order{};
		for (const auto &volume : level.level.volumes) {
			const auto owner = state.authored_entities.find(volume.subject);
			if (owner == state.authored_entities.end()) return std::unexpected("unresolved trigger volume owner");
			const auto index = static_cast<std::uint32_t>(volumes.volumes.size());
			const auto *zone = m_world.Get<DefinitionRef>(owner->second);
			const bool stars_only = catalog.Find(zone->index)->zone->check_stars_only;
			volumes.volumes.push_back({owner->second, volume.bounds, stars_only ? 1ull : 3ull, true});
			const auto contact = contacts.Create(); contacts.Add<VolumeContact>(contact, VolumeContact{index, state.player, 0, 0, contact_order++});
			if (!stars_only) for (const auto probe : smart_probes) {
				const auto pair = contacts.Create(); contacts.Add<VolumeContact>(pair, VolumeContact{index, commands.Resolve(probe), 0, 0, contact_order++});
			}
		}
		m_world.Commit(contacts);
		m_world.Resource<LadderPortals>()=*ladders;
		const auto inventories=InstallWeaponLoadouts(level,catalog);if(!inventories) return inventories;
		return {};
	}
	std::expected<void,std::string> InstallWeaponLoadouts(const content::LevelScene& level,const content::DefinitionCatalog& definitions) {
		using namespace engine::gameplay;
		auto catalog=content::ReadWeaponCatalog(definitions);if(!catalog) return std::unexpected(catalog.error());
		m_world.Resource<WeaponDefinitions>().catalog=std::move(*catalog);const auto& weapons=m_world.Resource<WeaponDefinitions>().catalog;
		const auto& scene=m_world.Resource<SceneState>();std::vector<ecs::Entity> actors=scene.actors;actors.push_back(scene.player);
		ecs::CommandBuffer bags,items;std::vector<std::pair<ecs::Entity,ecs::DeferredEntity>> selections;
		for(const auto actor:actors) {
			const auto* identity=m_world.Get<DefinitionRef>(actor);if(!identity) continue;const auto* definition=definitions.Find(identity->index);if(!definition) return std::unexpected("missing armed actor definition");
			const auto preset=content::ReadArmedLoadout(*definition);if(!preset) return std::unexpected(preset.error());
			std::optional<content::SavedLoadout> saved;
			if(const auto* authored=m_world.Get<LevelIdentity>(actor)) {
				const auto source=std::ranges::find_if(level.dynamic.actors,[&](const auto& entry) {return entry.LevelId()==authored->value;});
				if(source!=level.dynamic.actors.end()) {auto inventory=content::ReadSavedLoadout(source->data);if(!inventory) return std::unexpected(definition->name+": "+inventory.error());saved=std::move(*inventory);}
			}
			if(!*preset && !saved && actor!=scene.player) continue;
			bags.Add<InventorySelection>(actor);bags.Add<InventoryControl>(actor);bags.Add<WeaponInput>(actor);
			std::vector<content::SavedWeapon> entries;std::uint32_t selected{};
			if(saved) {entries=std::move(saved->weapons);selected=saved->selected;}
			else if(*preset) {
				for(const auto id:{(**preset).primary,(**preset).secondary}) if(id) {
					const auto* weapon=weapons.Find(id);if(!weapon) return std::unexpected("missing initial weapon definition");
					Magazine stock{0,0,weapon->clip_size,weapon->maximum_reserve};AddWeaponRounds(stock,(**preset).rounds);
					content::SavedWeapon entry;entry.definition=id;entry.loaded=stock.loaded;entry.reserve=stock.reserve;entries.push_back(entry);
				}
				if(!entries.empty()) selected=1;
			}
			std::set<std::uint32_t> seen;
			for(std::size_t index=0;index<entries.size();++index) {
				const auto& entry=entries[index];const auto* weapon=weapons.Find(entry.definition);
				if(!weapon || !seen.insert(entry.definition).second) return std::unexpected("missing or duplicate inventory weapon");
				const auto entity=items.Create();const auto key=std::max<std::int64_t>(0,weapon->key_number.Raw());
				items.Add<InventoryItem>(entity,InventoryItem{actor,weapon->id,std::uint32_t(key/Engine::Math::Fixed::OneRaw),std::uint32_t(key),entry.exists});
				Magazine stock{entry.loaded,entry.reserve,weapon->clip_size,weapon->maximum_reserve};if(saved && !stock.loaded && stock.reserve<0) stock.loaded=stock.capacity;
				items.Add<Magazine>(entity,stock);WeaponState state;state.phase=WeaponPhase(entry.phase);state.remaining=entry.remaining;state.burst_timer=entry.burst_timer;state.burst_count=entry.burst_count;state.shots=entry.shots;
				state.active=saved && selected==index+1;items.Add<WeaponState>(entity,state);WeaponControl control;control.safety=entry.safety;items.Add<WeaponControl>(entity,control);
				if(selected==index+1) selections.emplace_back(actor,entity);
			}
		}
		m_world.Commit(bags);m_world.Commit(items);
		for(const auto& [actor,item]:selections) m_world.Get<InventorySelection>(actor)->selected=items.Resolve(item);
		return {};
	}
	std::expected<void,std::string> InstallBehaviors(const content::PreparedBehaviors& prepared)
	{
		using namespace engine::gameplay;
		if(!m_world.Resource<BehaviorPrograms>().programs.empty()) return std::unexpected("mission behaviors already installed");
		const auto& scene=m_world.Resource<SceneState>();
		for(const auto& binding:prepared.bindings) {
			if(binding.definition>=prepared.library.programs.size()) return std::unexpected("invalid mission behavior definition");
			if(binding.subject==1 ? !m_world.IsAlive(scene.player) : !scene.authored_entities.contains(binding.subject)) return std::unexpected("unresolved mission behavior subject");
		}
		ecs::CommandBuffer commands;
		auto& invocations=m_world.Resource<BehaviorInvocations>();
		for(const auto& binding:prepared.bindings) {
			const auto actor=binding.subject==1 ? scene.player : scene.authored_entities.at(binding.subject);
			LuaBehavior behavior{actor,binding.subject,binding.order,binding.definition};
			if(binding.arguments) {behavior.invocation=std::uint32_t(invocations.arguments.size());invocations.arguments.push_back(*binding.arguments);}
			invocations.next_order=(std::max)(invocations.next_order,binding.order+1);
			const auto observer=commands.Create();commands.Add<LuaBehavior>(observer,behavior);commands.Add<LuaBehaviorState>(observer);
		}
		m_world.Resource<BehaviorPrograms>()=prepared.library;m_world.Commit(commands);return {};
	}
	void Step()
	{
		using namespace engine::gameplay;
		auto& inbox=m_world.Resource<BehaviorInbox>().messages;
		m_world.Resource<VolumeTransitions>().ForEach([&](const auto& transition) {
			const auto* subject=m_world.Get<LevelIdentity>(transition.subject);const auto* probe=m_world.Get<LevelIdentity>(transition.probe);
			if(!subject) return;const auto actor=transition.probe==m_world.Resource<SceneState>().player ? 1ull : probe ? probe->value : 0ull;
			if(!actor) return;BehaviorMessage message;message.recipient=transition.subject;message.due_tick=m_tick+1;message.order=transition.order;
			message.event=std::uint32_t(transition.entered ? content::MissionEvent::Entered : content::MissionEvent::Exited);
			message.arguments={std::int64_t(subject->value),std::int64_t(actor),0,0};message.argument_count=2;inbox.push_back(message);
		});
		m_scheduler->Execute(engine::time::SimulationTime{++m_tick, engine::time::FixedStep{TicksPerSecond}});
		inbox.clear();
		m_world.Resource<DamageRequests>().byTarget.clear();
	}
private:
	ecs::World m_world;
	DefenseSystem m_defense;
	engine::gameplay::InventorySnapshotSystem m_inventory_snapshot;engine::gameplay::InventorySelectionSystem m_inventory_selection;WeaponControlSystem m_weapon_control;WeaponStateSystem m_weapon_state;
	engine::gameplay::TimelineSystem m_timeline;engine::gameplay::PoseTrackSystem m_pose_track;
	engine::gameplay::TimelineBehaviorSystem m_timeline_behaviors;
	CinematicActorSystem m_cinematic_actor;CinematicDirectorSystem m_cinematic_director;
	HumanMotionSystem m_human_motion;
	LadderTransitionSystem m_ladder_transition;
	engine::gameplay::RouteTraversalSystem m_route_traversal;HumanGotoControlSystem m_human_goto;HumanGotoRequestSystem m_human_goto_requests;
	engine::gameplay::SuspensionSystem m_suspension;engine::gameplay::SuspensionSnapshotSystem m_suspension_snapshot;
	HumanAnimationSystem m_human_animation;
	engine::gameplay::ClipPlaybackSystem m_clip_playback;
	engine::gameplay::ClipRequestSystem m_clip_requests;engine::gameplay::KinematicCollisionSystem m_kinematic_collision;
	engine::gameplay::FireSequenceSystem m_fire_sequence;
	engine::gameplay::ProjectileLaunchSystem m_projectile_launch;engine::gameplay::LinearFlightSystem m_linear_flight;
	engine::gameplay::ProjectileSnapshotSystem m_projectile_snapshot;
	engine::gameplay::AffineSnapshotSystem m_scene_snapshot;
	engine::gameplay::EmitterSnapshotSystem m_emitter_snapshot;
	engine::gameplay::VolumeProbeSnapshotSystem m_probe_snapshot;
	engine::gameplay::VolumePresenceSystem m_volume_presence;
	engine::gameplay::MessageTimerSystem m_message_timers;engine::gameplay::LuaBehaviorSystem m_lua_behaviors;
	engine::gameplay::BehaviorAttachmentSystem m_behavior_attachments;
	engine::gameplay::InputPermissionSystem m_input_permissions;
	engine::gameplay::PositionRequestSystem m_position_requests;
	engine::gameplay::HeadingTargetSystem m_heading_targets;engine::gameplay::RigidHeadingSystem m_rigid_heading;HumanHeadingSystem m_human_heading;
	MissionCommandSystem m_mission_commands;
	ConversationSystem m_conversations;ConversationVoiceSystem m_conversation_voices;
	SpeechAnimationSystem m_speech_animation;
	ObjectiveSystem m_objectives;engine::gameplay::SimulationAgeSystem m_objective_age;engine::gameplay::TrackedPositionSystem m_tracked_position;
	ecs::SystemRegistry m_registry;
	std::unique_ptr<ecs::Scheduler> m_scheduler;
	std::uint64_t m_tick{0};
};
}
