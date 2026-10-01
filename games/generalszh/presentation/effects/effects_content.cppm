export module games.generalszh.presentation.effects.effects_content;
import std;

export import engine.effects.particles.definitions.particle_system_definition;
export import engine.config.binding.schema;
export import games.generalszh.content.loading.content_loader;

// Zero Hour's visual effects content: "ParticleSystem" blocks bound onto the
// engine's particle system definition, and "FXList" blocks (sounds, particle
// systems, view shakes, scorches, light pulses, tracers at a spot) as the
// FX director plays them.
export namespace generalszh::presentation
{
using engine::effects::ParticleSystemDefinition;
using engine::effects::Range;

struct SoundNugget
{
	std::string sound;
};

struct ParticleNugget
{
	std::string system;
	int count{1};
	std::array<float, 3> offset{};
	Range radius;
	Range height;
	Range delayMs;
	std::array<float, 3> rotate{}; // radians about x, y, z
	bool orientToObject{false};
	bool attachToObject{false};
	bool atGroundHeight{false};
	bool useCallersRadius{false};
};

enum class ShakeType : std::uint8_t
{
	Subtle,
	Normal,
	Strong,
	Severe,
	CineExtreme,
	CineInsane,
};

struct ViewShakeNugget
{
	ShakeType type{ShakeType::Normal};
};

struct ScorchNugget
{
	int type{-1}; // -1: random
	float radius{0.0f};
};

struct LightPulseNugget
{
	std::array<float, 3> color{};
	float radius{0.0f};
	float radiusOfObjectSize{0.0f};
	std::uint32_t increaseMs{0};
	std::uint32_t decreaseMs{0};
};

// TracerFXNugget (its constructor's defaults): the drawable it makes (TracerName), its speed a frame (Speed: a second in
// the INI; 0: its caller's), DecayAt, Length, Width, Color and Probability. BoneName is read and never used.
struct TracerNugget
{
	std::string name{"GenericTracer"};
	std::string bone;
	float speed{0.0f}; // a frame
	float decayAt{1.0f};
	float length{10.0f};
	float width{1.0f};
	std::array<float, 3> color{1, 1, 1};
	float probability{1.0f};
};

struct NestedFxNugget
{
	std::string fx;
	std::string bone;
	bool orientToBone{true};
};

using FxNugget = std::variant<SoundNugget, ParticleNugget, ViewShakeNugget, ScorchNugget, LightPulseNugget, TracerNugget, NestedFxNugget>;

struct FxList
{
	std::string name;
	std::vector<FxNugget> nuggets;
};

struct EffectsContent
{
	engine::config::DefinitionTable<ParticleSystemDefinition> particles;
	engine::config::DefinitionTable<FxList> fx;
};

namespace effects_detail
{
using engine::config::BindContext;
using engine::config::Node;

std::optional<float> Float(std::string_view token)
{
	if (const auto colon = token.find(':'); colon != std::string_view::npos)
		token.remove_prefix(colon + 1);
	if (!token.empty() && token.back() == '%')
		token.remove_suffix(1);
	float value = 0.0f;
	const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value);
	if (error != std::errc{})
		return std::nullopt;
	return value;
}

float At(const Node &node, std::size_t index, float fallback = 0.0f)
{
	return index < node.values.size() ? Float(node.values[index]).value_or(fallback) : fallback;
}

Range ReadRange(const Node &node) { return {At(node, 0), At(node, 1, At(node, 0))}; }

// "X:1 Y:2 Z:3"
std::array<float, 3> ReadCoord(const Node &node) { return {At(node, 0), At(node, 1), At(node, 2)}; }

// "R:255 G:128 B:0" as 0..1
std::array<float, 3> ReadRgb(const Node &node, std::size_t first = 0)
{
	return {At(node, first) / 255.0f, At(node, first + 1) / 255.0f, At(node, first + 2) / 255.0f};
}

bool ReadYes(const Node &node)
{
	const std::string_view value = node.Value();
	return value == "Yes" || value == "YES" || value == "yes" || value == "True" || value == "TRUE" || value == "true" || value == "1";
}

template<typename E, std::size_t N>
std::optional<E> ReadIndex(const Node &node, const std::array<std::string_view, N> &names, int firstValue = 0)
{
	for (std::size_t index = 0; index < names.size(); ++index)
		if (node.Value() == names[index])
			return static_cast<E>(static_cast<int>(index) + firstValue);
	return std::nullopt;
}

constexpr std::array<std::string_view, 14> PriorityNames{"NONE", "WEAPON_EXPLOSION", "SCORCHMARK", "DUST_TRAIL", "BUILDUP", "DEBRIS_TRAIL",
	"UNIT_DAMAGE_FX", "DEATH_EXPLOSION", "SEMI_CONSTANT", "CONSTANT", "WEAPON_TRAIL", "AREA_EFFECT", "CRITICAL", "ALWAYS_RENDER"};
constexpr std::array<std::string_view, 5> ShaderNames{"NONE", "ADDITIVE", "ALPHA", "ALPHA_TEST", "MULTIPLY"};
constexpr std::array<std::string_view, 6> TypeNames{"NONE", "PARTICLE", "DRAWABLE", "STREAK", "VOLUME_PARTICLE", "SMUDGE"};
constexpr std::array<std::string_view, 6> VelocityNames{"NONE", "ORTHO", "SPHERICAL", "HEMISPHERICAL", "CYLINDRICAL", "OUTWARD"};
constexpr std::array<std::string_view, 6> VolumeNames{"NONE", "POINT", "LINE", "BOX", "SPHERE", "CYLINDER"};
constexpr std::array<std::string_view, 4> WindNames{"NONE", "Unused", "PingPong", "Circular"};

engine::config::Schema<ParticleSystemDefinition> ParticleSchema()
{
	using D = ParticleSystemDefinition;
	using engine::effects::EmissionVelocity;
	using engine::effects::EmissionVolume;
	using engine::effects::ParticleKind;
	using engine::effects::ParticlePriority;
	using engine::effects::ParticleShader;
	using engine::effects::WindMotion;
	engine::config::Schema<D> schema;
	const auto range = [](Range D::*member) { return [member](const Node &n, D &d, BindContext &) { d.*member = ReadRange(n); }; };
	const auto number = [](float D::*member) { return [member](const Node &n, D &d, BindContext &) { d.*member = At(n, 0); }; };
	const auto coord = [](std::array<float, 3> D::*member) { return [member](const Node &n, D &d, BindContext &) { d.*member = ReadCoord(n); }; };
	const auto flag = [](bool D::*member) { return [member](const Node &n, D &d, BindContext &) { d.*member = ReadYes(n); }; };
	schema.On("Priority", [](const Node &n, D &d, BindContext &) {
		if (const auto p = ReadIndex<int>(n, PriorityNames); p && *p > 0)
			d.priority = static_cast<ParticlePriority>(*p - 1);
	});
	schema.On("IsOneShot", flag(&D::oneShot));
	schema.On("Shader", [](const Node &n, D &d, BindContext &) {
		if (const auto s = ReadIndex<ParticleShader>(n, ShaderNames))
			d.shader = *s;
	});
	schema.On("Type", [](const Node &n, D &d, BindContext &) {
		if (const auto t = ReadIndex<int>(n, TypeNames); t && *t > 0)
			d.kind = static_cast<ParticleKind>(*t - 1);
	});
	schema.On("ParticleName", [](const Node &n, D &d, BindContext &) { d.texture = std::string(n.Value()); });
	schema.On("AngleZ", range(&D::angleZ)).On("AngularRateZ", range(&D::angularRateZ)).On("AngularDamping", range(&D::angularDamping));
	schema.On("VelocityDamping", range(&D::velocityDamping)).On("Gravity", number(&D::gravity));
	schema.On("SlaveSystem", [](const Node &n, D &d, BindContext &) { d.slaveSystem = std::string(n.Value()); });
	schema.On("SlavePosOffset", coord(&D::slaveOffset));
	schema.On("PerParticleAttachedSystem", [](const Node &n, D &d, BindContext &) { d.attachedSystem = std::string(n.Value()); });
	schema.On("Lifetime", range(&D::lifetime));
	schema.On("SystemLifetime", [](const Node &n, D &d, BindContext &) { d.systemLifetime = static_cast<std::uint32_t>(At(n, 0)); });
	schema.On("Size", range(&D::size)).On("StartSizeRate", range(&D::startSizeRate)).On("SizeRate", range(&D::sizeRate));
	schema.On("SizeRateDamping", range(&D::sizeRateDamping));
	for (std::size_t key = 0; key < engine::effects::KeyframeCount; ++key)
	{
		schema.On("Alpha" + std::to_string(key + 1), [key](const Node &n, D &d, BindContext &) {
			d.alpha[key] = {{At(n, 0), At(n, 1)}, static_cast<std::uint32_t>(At(n, 2))};
		});
		schema.On("Color" + std::to_string(key + 1), [key](const Node &n, D &d, BindContext &) {
			d.color[key] = {ReadRgb(n), static_cast<std::uint32_t>(At(n, 3))};
		});
	}
	schema.On("ColorScale", range(&D::colorScale));
	schema.On("BurstDelay", range(&D::burstDelay)).On("BurstCount", range(&D::burstCount)).On("InitialDelay", range(&D::initialDelay));
	schema.On("DriftVelocity", coord(&D::drift));
	schema.On("VelocityType", [](const Node &n, D &d, BindContext &) {
		if (const auto v = ReadIndex<EmissionVelocity>(n, VelocityNames))
			d.velocityType = *v;
	});
	schema.On("VelOrthoX", [](const Node &n, D &d, BindContext &) { d.velocityOrtho[0] = ReadRange(n); });
	schema.On("VelOrthoY", [](const Node &n, D &d, BindContext &) { d.velocityOrtho[1] = ReadRange(n); });
	schema.On("VelOrthoZ", [](const Node &n, D &d, BindContext &) { d.velocityOrtho[2] = ReadRange(n); });
	schema.On("VelSpherical", range(&D::velocitySpherical)).On("VelHemispherical", range(&D::velocityHemispherical));
	schema.On("VelCylindricalRadial", range(&D::velocityRadial)).On("VelCylindricalNormal", range(&D::velocityNormal));
	schema.On("VelOutward", range(&D::velocityOutward)).On("VelOutwardOther", range(&D::velocityOutwardOther));
	schema.On("VolumeType", [](const Node &n, D &d, BindContext &) {
		if (const auto v = ReadIndex<int>(n, VolumeNames); v && *v > 0)
			d.volumeType = static_cast<EmissionVolume>(*v - 1);
	});
	schema.On("VolLineStart", coord(&D::lineStart)).On("VolLineEnd", coord(&D::lineEnd)).On("VolBoxHalfSize", coord(&D::boxHalfSize));
	schema.On("VolSphereRadius", number(&D::sphereRadius)).On("VolCylinderRadius", number(&D::cylinderRadius));
	schema.On("VolCylinderLength", number(&D::cylinderLength));
	schema.On("IsHollow", flag(&D::hollow)).On("IsGroundAligned", flag(&D::groundAligned));
	schema.On("IsEmitAboveGroundOnly", flag(&D::emitAboveGroundOnly)).On("IsParticleUpTowardsEmitter", flag(&D::upTowardsEmitter));
	schema.On("WindMotion", [](const Node &n, D &d, BindContext &) {
		if (const auto w = ReadIndex<int>(n, WindNames); w && *w > 1)
			d.wind = static_cast<WindMotion>(*w - 1);
	});
	schema.On("WindAngleChangeMin", number(&D::windAngleChangeMin)).On("WindAngleChangeMax", number(&D::windAngleChangeMax));
	// Only the Z rotation is used (as in the original build); volume depth and ping-pong wind limits later.
	for (const char *key : {"AngleX", "AngleY", "AngularRateX", "AngularRateY", "VolParticleDepth", "COLOR", "WindPingPongStartAngleMin",
			 "WindPingPongStartAngleMax", "WindPingPongEndAngleMin", "WindPingPongEndAngleMax"})
		schema.Ignore(key);
	return schema;
}

constexpr std::array<std::string_view, 6> ShakeNames{"SUBTLE", "NORMAL", "STRONG", "SEVERE", "CINE_EXTREME", "CINE_INSANE"};

void ReadNugget(const Node &node, FxList &list)
{
	if (node.key == "Sound")
	{
		SoundNugget sound;
		if (const Node *name = node.Find("Name"))
			sound.sound = std::string(name->Value());
		list.nuggets.emplace_back(std::move(sound));
	}
	else if (node.key == "ParticleSystem")
	{
		ParticleNugget p;
		for (const Node &c : node.children)
		{
			if (c.key == "Name")
				p.system = std::string(c.Value());
			else if (c.key == "Count")
				p.count = static_cast<int>(At(c, 0, 1));
			else if (c.key == "Offset")
				p.offset = ReadCoord(c);
			else if (c.key == "Radius")
				p.radius = ReadRange(c);
			else if (c.key == "Height")
				p.height = ReadRange(c);
			else if (c.key == "InitialDelay")
				p.delayMs = ReadRange(c);
			else if (c.key == "RotateX" || c.key == "RotateY" || c.key == "RotateZ")
				p.rotate[static_cast<std::size_t>(c.key.back() - 'X')] = At(c, 0) * 3.14159265f / 180.0f;
			else if (c.key == "OrientToObject")
				p.orientToObject = ReadYes(c);
			else if (c.key == "AttachToObject")
				p.attachToObject = ReadYes(c);
			else if (c.key == "CreateAtGroundHeight")
				p.atGroundHeight = ReadYes(c);
			else if (c.key == "UseCallersRadius")
				p.useCallersRadius = ReadYes(c);
		}
		list.nuggets.emplace_back(std::move(p));
	}
	else if (node.key == "ViewShake")
	{
		ViewShakeNugget shake;
		if (const Node *type = node.Find("Type"))
			if (const auto t = ReadIndex<ShakeType>(*type, ShakeNames))
				shake.type = *t;
		list.nuggets.emplace_back(shake);
	}
	else if (node.key == "TerrainScorch")
	{
		ScorchNugget scorch;
		// TerrainScorchFXNugget::parseScorchType: SCORCH_1..SCORCH_4, SHADOW_SCORCH, or RANDOM.
		if (const Node *type = node.Find("Type"))
		{
			constexpr std::array<std::string_view, 5> names{"SCORCH_1", "SCORCH_2", "SCORCH_3", "SCORCH_4", "SHADOW_SCORCH"};
			scorch.type = -1;
			for (std::size_t index = 0; index < names.size(); ++index)
				if (type->Value() == names[index])
					scorch.type = static_cast<int>(index);
		}
		if (const Node *radius = node.Find("Radius"))
			scorch.radius = At(*radius, 0);
		list.nuggets.emplace_back(scorch);
	}
	else if (node.key == "LightPulse")
	{
		LightPulseNugget light;
		for (const Node &c : node.children)
		{
			if (c.key == "Color")
				light.color = ReadRgb(c);
			else if (c.key == "Radius")
				light.radius = At(c, 0);
			else if (c.key == "RadiusAsPercentOfObjectSize")
				light.radiusOfObjectSize = At(c, 0) / 100.0f;
			else if (c.key == "IncreaseTime")
				light.increaseMs = static_cast<std::uint32_t>(At(c, 0));
			else if (c.key == "DecreaseTime")
				light.decreaseMs = static_cast<std::uint32_t>(At(c, 0));
		}
		list.nuggets.emplace_back(light);
	}
	else if (node.key == "Tracer")
	{
		TracerNugget tracer;
		for (const Node &c : node.children)
		{
			if (c.key == "BoneName")
				tracer.bone = std::string(c.Value());
			else if (c.key == "TracerName")
				tracer.name = std::string(c.Value());
			else if (c.key == "Speed")
				tracer.speed = At(c, 0) / 30.0f; // INI::parseVelocityReal
			else if (c.key == "DecayAt")
				tracer.decayAt = At(c, 0);
			else if (c.key == "Length")
				tracer.length = At(c, 0);
			else if (c.key == "Width")
				tracer.width = At(c, 0);
			else if (c.key == "Color")
				tracer.color = ReadRgb(c);
			else if (c.key == "Probability")
				tracer.probability = At(c, 0, 1);
		}
		list.nuggets.emplace_back(std::move(tracer));
	}
	else if (node.key == "FXListAtBonePos")
	{
		NestedFxNugget nested;
		for (const Node &c : node.children)
		{
			if (c.key == "FX")
				nested.fx = std::string(c.Value());
			else if (c.key == "BoneName")
				nested.bone = std::string(c.Value());
			else if (c.key == "OrientToBone")
				nested.orientToBone = ReadYes(c);
		}
		list.nuggets.emplace_back(std::move(nested));
	}
}
}

void BindEffects(std::span<const engine::config::Document *const> documents, EffectsContent &content, engine::config::BindContext &context)
{
	const auto particles = effects_detail::ParticleSchema();
	for (const engine::config::Document *document : documents)
		for (const engine::config::Node &root : document->Roots())
		{
			const std::string name(root.Value());
			if (name.empty())
				continue;
			if (root.key == "ParticleSystem")
			{
				ParticleSystemDefinition definition;
				definition.name = name;
				particles.Bind(root, definition, context);
				content.particles.Define(name) = std::move(definition);
			}
			else if (root.key == "FXList")
			{
				FxList list;
				list.name = name;
				for (const engine::config::Node &nugget : root.children)
					effects_detail::ReadNugget(nugget, list);
				content.fx.Define(name) = std::move(list);
			}
		}
}

EffectsContent LoadEffectsContent(content::ContentLoader &loader)
{
	EffectsContent content;
	for (std::vector<std::string_view> sets : {std::vector<std::string_view>{"Data\\INI\\Default\\ParticleSystem", "Data\\INI\\ParticleSystem"},
			 std::vector<std::string_view>{"Data\\INI\\Default\\FXList", "Data\\INI\\FXList"}})
	{
		const engine::config::Document &document = loader.Load(sets);
		engine::config::BindContext context{loader.DiagnosticsFor(document), engine::time::FixedStep{30}};
		const engine::config::Document *one[] = {&document};
		BindEffects(one, content, context);
	}
	return content;
}
}
