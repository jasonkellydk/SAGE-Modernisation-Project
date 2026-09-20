module;
#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

export module engine.navigation.scheduling.continuation;

export extern "C++" {
namespace navigation {
// A single-owner simulation continuation. It never creates a worker thread.
// Destroying it cancels suspended work and destroys the coroutine's local state.
template<class T>
class Continuation {
public:
    struct promise_type;
private:
    using Handle=std::coroutine_handle<promise_type>;
    Handle handle_{};
    explicit Continuation(Handle handle):handle_(handle) {}
public:
    struct promise_type {
        std::optional<T> value;
        std::exception_ptr error;
        Continuation get_return_object() { return Continuation(Handle::from_promise(*this)); }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_value(T result) { value.emplace(std::move(result)); }
        void unhandled_exception() noexcept { error=std::current_exception(); }
    };
    Continuation()=default;
    Continuation(const Continuation&)=delete;
    Continuation& operator=(const Continuation&)=delete;
    Continuation(Continuation&& other) noexcept:handle_(std::exchange(other.handle_,{})) {}
    Continuation& operator=(Continuation&& other) noexcept {
        if (this!=&other) {
            if (handle_) handle_.destroy();
            handle_=std::exchange(other.handle_,{});
        }
        return *this;
    }
    ~Continuation() { if (handle_) handle_.destroy(); }
    bool done() const { return !handle_ || handle_.done(); }
    bool resume() {
        if (done()) return false;
        handle_.resume();
        if (handle_.promise().error) std::rethrow_exception(handle_.promise().error);
        return !handle_.done();
    }
    T take() {
        if (!handle_ || !handle_.done()) throw std::logic_error("Continuation is not complete");
        auto& promise=handle_.promise();
        if (promise.error) std::rethrow_exception(promise.error);
        if (!promise.value) throw std::logic_error("Continuation result already taken");
        auto value=std::move(*promise.value);
        promise.value.reset();
        return value;
    }
};
}
}
