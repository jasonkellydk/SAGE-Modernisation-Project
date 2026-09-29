export module games.generalszh.presentation.rendering.model_bones;
import std;

// Where a model's bones are at rest (model space), from its loaded rig: the
// seam that lets presentation effects sit on bones (damage smoke, fire)
// without knowing the renderer. A model still loading has no bones yet.
export namespace generalszh::presentation
{
struct BonePose
{
	std::array<float, 3> position{};
	float yaw{0.0f}; // the bone's turn around z (radians)
};

class ModelBones
{
public:
	ModelBones();
	~ModelBones();
	ModelBones(const ModelBones &) = delete;
	ModelBones &operator=(const ModelBones &) = delete;

	// `bone` itself, or (`family`) its numbered family: Smoke01, Smoke02, ...
	std::vector<std::array<float, 3>> Find(std::string_view model, std::string_view bone, bool family);
	// Whether the model has loaded (asks for it the first time).
	bool Ready(std::string_view model);
	// One bone's rest pose; none while the model loads or when it has no such bone.
	std::optional<BonePose> Pose(std::string_view model, std::string_view bone);
	// Whether `bone` hangs below `ancestor`; false while the model loads or when either is missing.
	bool Descends(std::string_view model, std::string_view bone, std::string_view ancestor);

private:
	struct State;
	std::unique_ptr<State> m_state;
};
}
