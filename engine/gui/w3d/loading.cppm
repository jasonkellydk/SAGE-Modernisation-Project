export module engine.gui.w3d.loading;
import std;
export import engine.gui.mvvm.observable;

export namespace engine::gui::w3d
{
enum class LoadingState { Idle, Loading, Ready, Failed, Cancelled };
class LoadingModel
{
public:
	LoadingModel() {
		cancel.enabled.Set(false);
		cancel.SetAction([this] {
			if (state.Get()!=LoadingState::Loading) return;
			state.Set(LoadingState::Cancelled); cancel.enabled.Set(false);
			if (m_cancel) m_cancel();
		});
	}
	void Begin(std::u16string caption, std::function<void()> on_cancel) {
		m_cancel=std::move(on_cancel);title.Set(std::move(caption));stage.Set({});error.Set({});
		progress.Set(0);state.Set(LoadingState::Loading);cancel.enabled.Set(true);
	}
	void Report(std::u16string caption, unsigned percentage) {
		if (state.Get()!=LoadingState::Loading) return;
		stage.Set(std::move(caption));progress.Set(std::max(progress.Get(),std::min(percentage,99u)));
	}
	void Complete() {
		if (state.Get()!=LoadingState::Loading) return;
		progress.Set(100);state.Set(LoadingState::Ready);cancel.enabled.Set(false);m_cancel={};
	}
	void Fail(std::u16string message) {
		if (state.Get()!=LoadingState::Loading) return;
		error.Set(std::move(message));state.Set(LoadingState::Failed);cancel.enabled.Set(false);m_cancel={};
	}
	engine::gui::mvvm::Observable<LoadingState> state{LoadingState::Idle};
	engine::gui::mvvm::Observable<std::u16string> title,stage,error;
	engine::gui::mvvm::Observable<unsigned> progress;
	engine::gui::mvvm::Command cancel;
private:
	std::function<void()> m_cancel;
};
}
