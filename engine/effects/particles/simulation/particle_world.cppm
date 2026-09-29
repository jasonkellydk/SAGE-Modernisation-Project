export module engine.effects.particles.simulation.particle_world;
import std;

export import engine.effects.particles.definitions.particle_system_definition;
import engine.jobs.job_system;

// Every live particle system and particle, stepped one presentation frame
// (1/30 s of game time) at a time with the original's rules: systems wait
// out their initial delay, then emit bursts from their volume with their
// velocity (placed by the system's transform, spread between its last and
// current position), feed slave systems and start a system on each particle
// that asks for one; particles accelerate, damp, drift, spin, grow and fade
// through their alpha and colour keyframes until their lifetime is up.
// Particles are stored as structure of arrays and updated in parallel
// chunks on the job system. Presentation only.
export namespace engine::effects
{
using ParticleSystemId = std::uint64_t;

// Row-major 3x4: rotation columns and translation, as the emitter is placed.
struct EmitterTransform
{
	std::array<float, 12> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};

	static EmitterTransform At(float x, float y, float z, float yaw = 0.0f)
	{
		const float c = std::cos(yaw), s = std::sin(yaw);
		return {{c, -s, 0, x, s, c, 0, y, 0, 0, 1, z}};
	}
	std::array<float, 3> Point(const std::array<float, 3> &p) const
	{
		return {m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3], m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7],
			m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11]};
	}
	std::array<float, 3> Vector(const std::array<float, 3> &v) const
	{
		return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[4] * v[0] + m[5] * v[1] + m[6] * v[2], m[8] * v[0] + m[9] * v[1] + m[10] * v[2]};
	}
	std::array<float, 3> Position() const { return {m[3], m[7], m[11]}; }
};

struct ParticleWorldSettings
{
	std::size_t maxParticles{6000};
	// Particles per parallel job.
	std::size_t chunk{1024};
	std::uint64_t seed{0xFA57};
};

class ParticleWorld
{
public:
	using Find = std::function<const ParticleSystemDefinition *(std::string_view)>;
	using GroundHeight = std::function<float(float x, float y)>;

	explicit ParticleWorld(Find find, GroundHeight ground = {}, ParticleWorldSettings settings = {}) :
		m_find(std::move(find)), m_ground(std::move(ground)), m_settings(settings), m_random(settings.seed | 1)
	{
	}

	// Starts `definition` placed at `transform`, after its own initial delay
	// plus `delayFrames`; `radius` (when positive) replaces its sphere or
	// cylinder emission radius (effects sized to what they happen to).
	ParticleSystemId Create(const ParticleSystemDefinition &definition, const EmitterTransform &transform, std::uint32_t delayFrames = 0,
		float radius = 0.0f)
	{
		const ParticleSystemId id = Start(definition, transform, false);
		System &system = m_systems[static_cast<std::uint32_t>(id & 0xFFFFFFFFu)];
		system.delayLeft += delayFrames;
		system.radius = radius;
		return id;
	}

	// Moves an emitter (one following an object); particles already out stay where they are.
	void Move(ParticleSystemId id, const EmitterTransform &transform)
	{
		if (System *system = Get(id))
			system->transform = transform;
	}

	// Stops emitting; what is out lives on, and the system goes once it is all gone.
	void Stop(ParticleSystemId id)
	{
		if (System *system = Get(id))
		{
			system->stopped = true;
			system->held = false;
		}
	}

	// A system its owner switches on and off (ParticleSystem::start / stop): held, it stays while stopped;
	// paused, it emits nothing; resumed, it emits again.
	void Hold(ParticleSystemId id)
	{
		if (System *system = Get(id))
			system->held = true;
	}
	void Pause(ParticleSystemId id)
	{
		if (System *system = Get(id))
			system->stopped = true;
	}
	void Resume(ParticleSystemId id)
	{
		if (System *system = Get(id))
			system->stopped = false;
	}
	// Whether it is there and emitting (not paused).
	bool Running(ParticleSystemId id) const
	{
		const System *system = Get(id);
		return system != nullptr && !system->stopped;
	}
	// ParticleSystem::trigger: its next burst (and its initial delay) now.
	void Trigger(ParticleSystemId id)
	{
		if (System *system = Get(id))
		{
			system->burstDelayLeft = 0;
			system->delayLeft = 0;
		}
	}

	// Stops and removes its particles at once.
	void Destroy(ParticleSystemId id)
	{
		if (System *system = Get(id))
		{
			system->stopped = true;
			system->held = false;
			system->killParticles = true;
		}
	}

	bool Alive(ParticleSystemId id) const { return Get(id) != nullptr; }

	// ParticleSystem::setSystemLifetime: the frames it has left (a system that runs forever keeps running);
	// setInitialDelay: the frames before it starts, in place of its own.
	void SetSystemLifetime(ParticleSystemId id, std::uint32_t frames)
	{
		if (System *system = Get(id))
			system->lifetimeLeft = frames;
	}
	void SetInitialDelay(ParticleSystemId id, std::uint32_t frames)
	{
		if (System *system = Get(id))
			system->delayLeft = frames;
	}

	// Scales what it emits from now on (ParticleSystem::setVelocityMultiplier, setBurstCountMultiplier,
	// setSizeMultiplier): each particle's velocity per axis in the system's own frame, how many a burst has (the
	// burst's whole count times this, truncated), and its starting size and growth.
	void SetVelocityMultiplier(ParticleSystemId id, const std::array<float, 3> &scale)
	{
		if (System *system = Get(id))
			system->velocityScale = scale;
	}
	void SetBurstCountMultiplier(ParticleSystemId id, float scale)
	{
		if (System *system = Get(id))
			system->countScale = scale;
	}
	void SetSizeMultiplier(ParticleSystemId id, float scale)
	{
		if (System *system = Get(id))
			system->sizeScale = scale;
	}

	// One presentation frame.
	void Step(jobs::JobSystem *jobs = nullptr)
	{
		++m_frame;
		for (std::size_t index = 0; index < m_systems.size(); ++index)
			UpdateSystem(index);
		UpdateParticles(jobs);
		Compact();
		RetireSystems();
	}

	// The particles (structure of arrays), for drawing.
	std::size_t ParticleCount() const noexcept { return m_px.size(); }
	std::size_t SystemCount() const noexcept { return m_liveSystems; }
	std::span<const float> X() const noexcept { return m_px; }
	std::span<const float> Y() const noexcept { return m_py; }
	std::span<const float> Z() const noexcept { return m_pz; }
	// Per-frame motion (velocity plus drift), for drawing between frames.
	std::span<const float> MotionX() const noexcept { return m_mx; }
	std::span<const float> MotionY() const noexcept { return m_my; }
	std::span<const float> MotionZ() const noexcept { return m_mz; }
	std::span<const float> Size() const noexcept { return m_size; }
	std::span<const float> Angle() const noexcept { return m_angle; }
	std::span<const float> Red() const noexcept { return m_red; }
	std::span<const float> Green() const noexcept { return m_green; }
	std::span<const float> Blue() const noexcept { return m_blue; }
	std::span<const float> Alpha() const noexcept { return m_alpha; }
	// Each particle's system (particles of a system stay in the order they were emitted).
	std::span<const std::uint32_t> Systems() const noexcept { return m_system; }
	// The definition each particle came from (texture, shader, kind).
	const ParticleSystemDefinition &DefinitionOf(std::size_t particle) const { return *m_systems[m_system[particle]].definition; }

private:
	struct System
	{
		const ParticleSystemDefinition *definition{nullptr};
		std::uint32_t generation{1};
		bool live{false};
		EmitterTransform transform;
		std::array<float, 3> lastPosition{};
		bool hasLast{false};
		std::uint32_t delayLeft{0};
		std::uint32_t burstDelayLeft{0};
		std::uint32_t lifetimeLeft{0};
		bool forever{true};
		bool stopped{false};
		bool held{false}; // kept while stopped (its owner may start it again)
		bool killParticles{false};
		bool slave{false}; // fed by its master; never emits on its own
		ParticleSystemId master{0}; // a slave's master (generation-checked)
		std::uint32_t slaveSystem{0xFFFFFFFFu};
		// Follows this particle (an attached system); none: 0xFFFFFFFF.
		std::uint32_t controlParticle{0xFFFFFFFFu};
		float sizeBonus{0.0f};
		std::uint32_t particles{0};
		float radius{0.0f}; // emission radius override (0: the definition's)
		std::array<float, 3> velocityScale{1.0f, 1.0f, 1.0f};
		float countScale{1.0f};
		float sizeScale{1.0f};
	};

	float Uniform(float low, float high)
	{
		if (high <= low)
			return low;
		m_random ^= m_random << 13;
		m_random ^= m_random >> 7;
		m_random ^= m_random << 17;
		return low + (high - low) * static_cast<float>(m_random >> 40) / static_cast<float>(1u << 24);
	}
	float Pick(const Range &range) { return Uniform(range.min, range.max); }

	std::array<float, 3> UnitSphere()
	{
		const float z = Uniform(-1.0f, 1.0f);
		const float angle = Uniform(0.0f, 6.2831853f);
		const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
		return {r * std::cos(angle), r * std::sin(angle), z};
	}

	System *Get(ParticleSystemId id)
	{
		const auto index = static_cast<std::uint32_t>(id & 0xFFFFFFFFu);
		const auto generation = static_cast<std::uint32_t>(id >> 32);
		return index < m_systems.size() && m_systems[index].live && m_systems[index].generation == generation ? &m_systems[index] : nullptr;
	}
	const System *Get(ParticleSystemId id) const { return const_cast<ParticleWorld *>(this)->Get(id); }
	ParticleSystemId IdOf(std::uint32_t index) const { return (static_cast<std::uint64_t>(m_systems[index].generation) << 32) | index; }

	ParticleSystemId Start(const ParticleSystemDefinition &definition, const EmitterTransform &transform, bool slave)
	{
		std::uint32_t index;
		if (!m_free.empty())
		{
			index = m_free.back();
			m_free.pop_back();
		}
		else
		{
			index = static_cast<std::uint32_t>(m_systems.size());
			m_systems.emplace_back();
		}
		System &system = m_systems[index];
		const std::uint32_t generation = system.generation;
		system = System{};
		system.generation = generation;
		system.live = true;
		system.definition = &definition;
		system.transform = transform;
		system.delayLeft = static_cast<std::uint32_t>(Pick(definition.initialDelay));
		system.lifetimeLeft = definition.systemLifetime;
		system.forever = definition.systemLifetime == 0;
		system.slave = slave;
		++m_liveSystems;
		if (!definition.slaveSystem.empty())
			if (const ParticleSystemDefinition *slaveDefinition = m_find ? m_find(definition.slaveSystem) : nullptr)
			{
				const auto slaveId = Start(*slaveDefinition, transform, true);
				const auto slaveIndex = static_cast<std::uint32_t>(slaveId & 0xFFFFFFFFu);
				m_systems[index].slaveSystem = slaveIndex;
				m_systems[slaveIndex].master = IdOf(index);
			}
		return IdOf(index);
	}

	void UpdateSystem(std::size_t index)
	{
		System &system = m_systems[index];
		if (!system.live)
			return;
		if (system.delayLeft > 0)
		{
			--system.delayLeft;
			return;
		}
		// An attached system rides its particle.
		if (system.controlParticle != 0xFFFFFFFFu)
		{
			system.transform.m[3] = m_px[system.controlParticle];
			system.transform.m[7] = m_py[system.controlParticle];
			system.transform.m[11] = m_pz[system.controlParticle];
		}
		const std::array<float, 3> position = system.transform.Position();
		if (!system.hasLast)
		{
			system.lastPosition = position;
			system.hasLast = true;
		}
		const ParticleSystemDefinition &definition = *system.definition;
		if (!system.stopped && !system.slave && (system.forever || system.lifetimeLeft > 0))
		{
			if (system.burstDelayLeft == 0)
			{
				const int count = static_cast<int>(static_cast<float>(static_cast<int>(Pick(definition.burstCount))) * system.countScale);
				for (int particle = 0; particle < count; ++particle)
					Emit(static_cast<std::uint32_t>(index), particle, count);
				// (Emitting may start attached systems and move the system list.)
				m_systems[index].burstDelayLeft = static_cast<std::uint32_t>(Pick(definition.burstDelay));
			}
			else
				--system.burstDelayLeft;
		}
		m_systems[index].lastPosition = position;
		if (!m_systems[index].forever && m_systems[index].lifetimeLeft > 0)
			--m_systems[index].lifetimeLeft;
	}

	std::array<float, 3> EmissionPosition(const ParticleSystemDefinition &d, float radiusOverride)
	{
		switch (d.volumeType)
		{
		case EmissionVolume::Cylinder:
		{
			const float angle = Uniform(0.0f, 6.2831853f);
			const float full = radiusOverride > 0.0f ? radiusOverride : d.cylinderRadius;
			const float radius = d.hollow ? full : Uniform(0.0f, full);
			return {radius * std::cos(angle), radius * std::sin(angle), Uniform(-d.cylinderLength / 2, d.cylinderLength / 2)};
		}
		case EmissionVolume::Sphere:
		{
			const float full = radiusOverride > 0.0f ? radiusOverride : d.sphereRadius;
			const float radius = d.hollow ? full : Uniform(0.0f, full);
			const auto p = UnitSphere();
			return {p[0] * radius, p[1] * radius, p[2] * radius};
		}
		case EmissionVolume::Box:
		{
			const auto &h = d.boxHalfSize;
			std::array<float, 3> p{Uniform(-h[0], h[0]), Uniform(-h[1], h[1]), Uniform(-h[2], h[2])};
			if (d.hollow)
			{
				const int side = static_cast<int>(Uniform(0.0f, 5.999f));
				p[side % 3] = side < 3 ? -h[side % 3] : h[side % 3];
			}
			return p;
		}
		case EmissionVolume::Line:
		{
			const float t = Uniform(0.0f, 1.0f);
			return {d.lineStart[0] + t * (d.lineEnd[0] - d.lineStart[0]), d.lineStart[1] + t * (d.lineEnd[1] - d.lineStart[1]),
				d.lineStart[2] + t * (d.lineEnd[2] - d.lineStart[2])};
		}
		case EmissionVolume::Point:
			break;
		}
		return {};
	}

	std::array<float, 3> EmissionVelocityAt(const ParticleSystemDefinition &d, const std::array<float, 3> &p)
	{
		switch (d.velocityType)
		{
		case EmissionVelocity::Ortho:
			return {Pick(d.velocityOrtho[0]), Pick(d.velocityOrtho[1]), Pick(d.velocityOrtho[2])};
		case EmissionVelocity::Cylindrical:
		{
			const float speed = Pick(d.velocityRadial);
			const float angle = Uniform(0.0f, 6.2831853f);
			return {speed * std::cos(angle), speed * std::sin(angle), Pick(d.velocityNormal)};
		}
		case EmissionVelocity::Spherical:
		{
			const float speed = Pick(d.velocitySpherical);
			const auto v = UnitSphere();
			return {v[0] * speed, v[1] * speed, v[2] * speed};
		}
		case EmissionVelocity::Hemispherical:
		{
			// (The original reads the spherical speed here.)
			const float speed = Pick(d.velocitySpherical);
			auto v = UnitSphere();
			v[2] = std::fabs(v[2]);
			return {v[0] * speed, v[1] * speed, v[2] * speed};
		}
		case EmissionVelocity::Outward:
		{
			const float speed = Pick(d.velocityOutward);
			const float other = Pick(d.velocityOutwardOther);
			if (d.volumeType == EmissionVolume::Cylinder)
			{
				const float length = std::sqrt(p[0] * p[0] + p[1] * p[1]);
				return length > 0.0f ? std::array<float, 3>{speed * p[0] / length, speed * p[1] / length, other} : std::array<float, 3>{0, 0, other};
			}
			if (d.volumeType == EmissionVolume::Line)
			{
				std::array<float, 3> along{d.lineEnd[0] - d.lineStart[0], d.lineEnd[1] - d.lineStart[1], d.lineEnd[2] - d.lineStart[2]};
				const float length = std::sqrt(along[0] * along[0] + along[1] * along[1] + along[2] * along[2]);
				if (length > 0.0f)
					for (float &c : along)
						c /= length;
				// perp = up x along; up' = along x perp
				const std::array<float, 3> perp{-along[1], along[0], 0.0f};
				const std::array<float, 3> up{along[1] * perp[2] - along[2] * perp[1], along[2] * perp[0] - along[0] * perp[2],
					along[0] * perp[1] - along[1] * perp[0]};
				return {speed * perp[0] + other * up[0], speed * perp[1] + other * up[1], speed * perp[2] + other * up[2]};
			}
			std::array<float, 3> v = d.volumeType == EmissionVolume::Point ? UnitSphere() : p;
			const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
			if (length > 0.0f)
				for (float &c : v)
					c = c / length * speed;
			return v;
		}
		case EmissionVelocity::None:
			break;
		}
		return {};
	}

	void Emit(std::uint32_t systemIndex, int number, int count)
	{
		if (m_px.size() >= m_settings.maxParticles && m_systems[systemIndex].definition->priority != ParticlePriority::AlwaysRender)
			return;
		System &system = m_systems[systemIndex];
		const ParticleSystemDefinition &d = *system.definition;
		std::array<float, 3> local = EmissionPosition(d, system.radius);
		std::array<float, 3> velocity = EmissionVelocityAt(d, local);
		for (int axis = 0; axis < 3; ++axis)
			velocity[axis] *= system.velocityScale[axis];
		std::array<float, 3> position = system.transform.Point(local);
		velocity = system.transform.Vector(velocity);
		// Spread a burst along the emitter's path since last frame.
		const std::array<float, 3> now = system.transform.Position();
		const float back = 1.0f - static_cast<float>(number) / static_cast<float>(count);
		for (int axis = 0; axis < 3; ++axis)
			position[axis] -= back * (now[axis] - system.lastPosition[axis]);
		if (d.emitAboveGroundOnly && m_ground && position[2] < m_ground(position[0], position[1]))
			return;
		float size = Pick(d.size) * system.sizeScale + system.sizeBonus;
		system.sizeBonus = std::min(system.sizeBonus + Pick(d.startSizeRate), 50.0f);
		const std::uint32_t particle = Add(systemIndex, position, velocity, size, now);
		if (system.slaveSystem != 0xFFFFFFFFu && m_systems[system.slaveSystem].live)
		{
			const std::uint32_t slave = system.slaveSystem;
			const auto &offset = d.slaveOffset;
			Add(slave, {position[0] + offset[0], position[1] + offset[1], position[2] + offset[2]}, velocity,
				Pick(m_systems[slave].definition->size), now);
		}
		if (!d.attachedSystem.empty())
			if (const ParticleSystemDefinition *attached = m_find ? m_find(d.attachedSystem) : nullptr)
			{
				const auto id = Start(*attached, EmitterTransform::At(position[0], position[1], position[2]), false);
				m_systems[static_cast<std::uint32_t>(id & 0xFFFFFFFFu)].controlParticle = particle;
			}
	}

	std::uint32_t Add(std::uint32_t systemIndex, const std::array<float, 3> &p, const std::array<float, 3> &v, float size,
		const std::array<float, 3> &emitter)
	{
		const ParticleSystemDefinition &d = *m_systems[systemIndex].definition;
		const auto index = static_cast<std::uint32_t>(m_px.size());
		m_px.push_back(p[0]);
		m_py.push_back(p[1]);
		m_pz.push_back(p[2]);
		m_vx.push_back(v[0]);
		m_vy.push_back(v[1]);
		m_vz.push_back(v[2]);
		m_mx.push_back(v[0] + d.drift[0]);
		m_my.push_back(v[1] + d.drift[1]);
		m_mz.push_back(v[2] + d.drift[2]);
		m_size.push_back(size);
		m_sizeRate.push_back(Pick(d.sizeRate) * m_systems[systemIndex].sizeScale);
		m_sizeDamping.push_back(Pick(d.sizeRateDamping));
		m_velocityDamping.push_back(Pick(d.velocityDamping));
		float angle = Pick(d.angleZ);
		if (d.upTowardsEmitter)
			angle = std::atan2(p[1] - emitter[1], p[0] - emitter[0]) + 1.5707963f;
		m_angle.push_back(angle);
		m_angularRate.push_back(Pick(d.angularRateZ));
		m_angularDamping.push_back(Pick(d.angularDamping));
		m_lifetime.push_back(static_cast<std::uint32_t>(Pick(d.lifetime)));
		m_age.push_back(0);
		std::array<float, KeyframeCount> alphas{};
		for (std::size_t key = 0; key < KeyframeCount; ++key)
			alphas[key] = Pick(d.alpha[key].value);
		m_alphaKeys.push_back(alphas);
		m_alpha.push_back(alphas[0]);
		m_alphaTarget.push_back(1);
		m_alphaRate.push_back(0.0f);
		m_red.push_back(d.color[0].color[0]);
		m_green.push_back(d.color[0].color[1]);
		m_blue.push_back(d.color[0].color[2]);
		m_colorTarget.push_back(1);
		m_colorScale.push_back(Pick(d.colorScale));
		m_system.push_back(systemIndex);
		m_dead.push_back(0);
		ComputeAlphaRate(index);
		ComputeColorRate(index);
		++m_systems[systemIndex].particles;
		return index;
	}

	void ComputeAlphaRate(std::size_t i)
	{
		const auto &keys = m_systems[m_system[i]].definition->alpha;
		const std::uint8_t target = m_alphaTarget[i];
		if (target >= KeyframeCount || keys[target].frame == 0)
		{
			m_alphaRate[i] = 0.0f;
			return;
		}
		const float frames = static_cast<float>(keys[target].frame - keys[target - 1].frame);
		m_alphaRate[i] = frames > 0.0f ? (m_alphaKeys[i][target] - m_alphaKeys[i][target - 1]) / frames : 0.0f;
	}

	std::array<float, 3> ColorRate(std::size_t i) const
	{
		const auto &keys = m_systems[m_system[i]].definition->color;
		const std::uint8_t target = m_colorTarget[i];
		if (target >= KeyframeCount || keys[target].frame == 0)
			return {};
		const float frames = static_cast<float>(keys[target].frame - keys[target - 1].frame);
		if (frames <= 0.0f)
			return {};
		return {(keys[target].color[0] - keys[target - 1].color[0]) / frames, (keys[target].color[1] - keys[target - 1].color[1]) / frames,
			(keys[target].color[2] - keys[target - 1].color[2]) / frames};
	}

	void ComputeColorRate(std::size_t i)
	{
		const auto rate = ColorRate(i);
		if (m_colorRate.size() <= i)
			m_colorRate.resize(i + 1);
		m_colorRate[i] = rate;
	}

	// One frame of particles [begin, end): independent rows, safe in parallel.
	void UpdateRange(std::size_t begin, std::size_t end)
	{
		for (std::size_t i = begin; i < end; ++i)
		{
			const System &system = m_systems[m_system[i]];
			if (system.killParticles)
			{
				m_dead[i] = 1;
				continue;
			}
			const ParticleSystemDefinition &d = *system.definition;
			m_vz[i] += d.gravity;
			m_vx[i] *= m_velocityDamping[i];
			m_vy[i] *= m_velocityDamping[i];
			m_vz[i] *= m_velocityDamping[i];
			m_mx[i] = m_vx[i] + d.drift[0];
			m_my[i] = m_vy[i] + d.drift[1];
			m_mz[i] = m_vz[i] + d.drift[2];
			m_px[i] += m_mx[i];
			m_py[i] += m_my[i];
			m_pz[i] += m_mz[i];
			m_angle[i] += m_angularRate[i];
			m_angularRate[i] *= m_angularDamping[i];
			m_size[i] += m_sizeRate[i];
			m_sizeRate[i] *= m_sizeDamping[i];
			const std::uint32_t age = m_age[i];
			if (d.shader != ParticleShader::Additive)
			{
				m_alpha[i] += m_alphaRate[i];
				const std::uint8_t target = m_alphaTarget[i];
				if (target < KeyframeCount && d.alpha[target].frame != 0)
				{
					if (age >= d.alpha[target].frame)
					{
						m_alpha[i] = m_alphaKeys[i][target];
						++m_alphaTarget[i];
						ComputeAlphaRate(i);
					}
				}
				else
					m_alphaRate[i] = 0.0f;
				m_alpha[i] = std::clamp(m_alpha[i], 0.0f, 1.0f);
			}
			m_red[i] += m_colorRate[i][0];
			m_green[i] += m_colorRate[i][1];
			m_blue[i] += m_colorRate[i][2];
			const std::uint8_t target = m_colorTarget[i];
			if (target < KeyframeCount && d.color[target].frame != 0)
			{
				if (age >= d.color[target].frame)
				{
					++m_colorTarget[i];
					m_colorRate[i] = ColorRate(i);
				}
			}
			else
				m_colorRate[i] = {};
			m_red[i] = std::clamp(m_red[i] + m_colorScale[i], 0.0f, 1.0f);
			m_green[i] = std::clamp(m_green[i] + m_colorScale[i], 0.0f, 1.0f);
			m_blue[i] = std::clamp(m_blue[i] + m_colorScale[i], 0.0f, 1.0f);
			m_age[i] = age + 1;
			if (m_lifetime[i] != 0 && --m_lifetime[i] == 0)
				m_dead[i] = 1;
			if (Invisible(i, d))
				m_dead[i] = 1;
		}
	}

	// Gone for good by its shader: black when added, transparent when
	// blended, white when multiplied (unless still heading to another colour).
	bool Invisible(std::size_t i, const ParticleSystemDefinition &d) const
	{
		const std::uint8_t target = m_colorTarget[i];
		const bool settled = target >= KeyframeCount || d.color[target].frame == 0;
		switch (d.shader)
		{
		case ParticleShader::Additive:
			return settled && m_red[i] < 0.01f && m_green[i] < 0.01f && m_blue[i] < 0.01f;
		case ParticleShader::Alpha:
			return m_alpha[i] < 0.01f;
		case ParticleShader::Multiply:
			return settled && m_red[i] > 0.99f && m_green[i] > 0.99f && m_blue[i] > 0.99f;
		case ParticleShader::AlphaTest:
		case ParticleShader::None:
			break;
		}
		return false;
	}

	struct RangeJob
	{
		ParticleWorld *world;
		std::size_t begin;
		std::size_t end;
	};

	void UpdateParticles(jobs::JobSystem *jobs)
	{
		const std::size_t count = m_px.size();
		if (jobs == nullptr || count <= m_settings.chunk)
		{
			UpdateRange(0, count);
			return;
		}
		m_jobContexts.clear();
		for (std::size_t begin = 0; begin < count; begin += m_settings.chunk)
			m_jobContexts.push_back({this, begin, std::min(count, begin + m_settings.chunk)});
		m_jobs.clear();
		for (RangeJob &context : m_jobContexts)
			m_jobs.push_back({[](void *raw) {
				auto &job = *static_cast<RangeJob *>(raw);
				job.world->UpdateRange(job.begin, job.end);
			}, &context});
		jobs->Execute(m_jobs);
	}

	// Removes the dead in order (so draw order is stable) and repoints the
	// systems riding particles.
	void Compact()
	{
		const std::size_t count = m_px.size();
		m_remap.assign(count, 0xFFFFFFFFu);
		std::size_t out = 0;
		for (std::size_t i = 0; i < count; ++i)
		{
			if (m_dead[i])
			{
				--m_systems[m_system[i]].particles;
				continue;
			}
			m_remap[i] = static_cast<std::uint32_t>(out);
			if (out != i)
				MoveRow(i, out);
			++out;
		}
		Resize(out);
		for (System &system : m_systems)
			if (system.live && system.controlParticle != 0xFFFFFFFFu)
			{
				system.controlParticle = system.controlParticle < count ? m_remap[system.controlParticle] : 0xFFFFFFFFu;
				// Its particle is gone: it stops and fades out.
				if (system.controlParticle == 0xFFFFFFFFu)
					system.stopped = true;
			}
	}

	template<typename... Columns>
	static void MoveEach(std::size_t from, std::size_t to, Columns &...columns)
	{
		((columns[to] = columns[from]), ...);
	}

	template<typename... Columns>
	static void ResizeEach(std::size_t size, Columns &...columns)
	{
		(columns.resize(size), ...);
	}

	void MoveRow(std::size_t from, std::size_t to)
	{
		MoveEach(from, to, m_px, m_py, m_pz, m_vx, m_vy, m_vz, m_mx, m_my, m_mz, m_size, m_sizeRate, m_sizeDamping, m_velocityDamping,
			m_angle, m_angularRate, m_angularDamping, m_lifetime, m_age, m_alphaKeys, m_alpha, m_alphaTarget, m_alphaRate, m_red, m_green,
			m_blue, m_colorTarget, m_colorRate, m_colorScale, m_system, m_dead);
		m_dead[to] = 0;
	}

	void Resize(std::size_t size)
	{
		ResizeEach(size, m_px, m_py, m_pz, m_vx, m_vy, m_vz, m_mx, m_my, m_mz, m_size, m_sizeRate, m_sizeDamping, m_velocityDamping, m_angle,
			m_angularRate, m_angularDamping, m_lifetime, m_age, m_alphaKeys, m_alpha, m_alphaTarget, m_alphaRate, m_red, m_green, m_blue,
			m_colorTarget, m_colorRate, m_colorScale, m_system, m_dead);
	}

	// Systems done emitting with nothing left out go (a slave with its master).
	void RetireSystems()
	{
		for (std::uint32_t index = 0; index < m_systems.size(); ++index)
		{
			System &system = m_systems[index];
			if (!system.live || system.particles != 0 || system.delayLeft > 0)
				continue;
			// A slave lives as long as its master; others until they stop emitting.
			const bool done = system.slave ? Get(system.master) == nullptr
											: ((system.stopped && !system.held) || (!system.forever && system.lifetimeLeft == 0));
			if (!done)
				continue;
			system.live = false;
			++system.generation;
			--m_liveSystems;
			m_free.push_back(index);
			if (system.slaveSystem != 0xFFFFFFFFu && m_systems[system.slaveSystem].live)
				m_systems[system.slaveSystem].stopped = true;
		}
	}

	Find m_find;
	GroundHeight m_ground;
	ParticleWorldSettings m_settings;
	std::uint64_t m_random;
	std::uint64_t m_frame{0};

	std::vector<System> m_systems;
	std::vector<std::uint32_t> m_free;
	std::size_t m_liveSystems{0};

	std::vector<float> m_px, m_py, m_pz, m_vx, m_vy, m_vz, m_mx, m_my, m_mz;
	std::vector<float> m_size, m_sizeRate, m_sizeDamping, m_velocityDamping;
	std::vector<float> m_angle, m_angularRate, m_angularDamping;
	std::vector<std::uint32_t> m_lifetime, m_age;
	std::vector<std::array<float, KeyframeCount>> m_alphaKeys;
	std::vector<float> m_alpha, m_alphaRate;
	std::vector<std::uint8_t> m_alphaTarget;
	std::vector<float> m_red, m_green, m_blue, m_colorScale;
	std::vector<std::uint8_t> m_colorTarget;
	std::vector<std::array<float, 3>> m_colorRate;
	std::vector<std::uint32_t> m_system;
	std::vector<std::uint8_t> m_dead;
	std::vector<std::uint32_t> m_remap;

	std::vector<RangeJob> m_jobContexts;
	std::vector<jobs::Job> m_jobs;
};
}
