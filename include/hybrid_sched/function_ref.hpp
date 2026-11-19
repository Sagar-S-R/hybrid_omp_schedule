#pragma once
/// @file function_ref.hpp
/// @brief Non-owning callable wrapper — zero heap allocation.
///
/// Lifetime contract: a FunctionRef MUST NOT outlive the callable it was
/// constructed from.  The caller (hybrid_for etc.) guarantees this because
/// FunctionRef is only used synchronously within the same call frame.

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace hs {

/// Primary template (not defined) — only the specialisation below is used.
template <class Signature>
class FunctionRef;

/// Specialisation for `Ret(Args...)`.
template <class Ret, class... Args>
class FunctionRef<Ret(Args...)> {
public:
    // ── Construction from any callable ──────────────────────────────────────
    template <class F,
              class = std::enable_if_t<!std::is_same_v<std::decay_t<F>, FunctionRef> &&
                                       std::is_invocable_r_v<Ret, F&&, Args...>>>
    // NOLINTNEXTLINE(bugprone-forwarding-reference-overload)
    FunctionRef(F&& f) noexcept
        : obj_(const_cast<void*>(static_cast<const void*>(std::addressof(f)))),
          call_([](void* obj, Args... args) -> Ret {
              return std::invoke(*static_cast<std::remove_reference_t<F>*>(obj),
                                 std::forward<Args>(args)...);
          }) {}

    // ── Invoke ───────────────────────────────────────────────────────────────
    Ret operator()(Args... args) const { return call_(obj_, std::forward<Args>(args)...); }

    // ── Validity check ───────────────────────────────────────────────────────
    [[nodiscard]] explicit operator bool() const noexcept { return call_ != nullptr; }

    // ── Default/copy/move ────────────────────────────────────────────────────
    FunctionRef() noexcept : obj_(nullptr), call_(nullptr) {}
    FunctionRef(const FunctionRef&) noexcept = default;
    FunctionRef& operator=(const FunctionRef&) noexcept = default;

private:
    void* obj_;
    Ret (*call_)(void*, Args...);
};

} // namespace hs
