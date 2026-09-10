
#pragma once

namespace totk::core {

template <class Value, class Error>
struct [[nodiscard]] Result {
    Value value{};
    Error error{};
    bool succeeded = false;

    [[nodiscard]] static constexpr Result success(const Value& result) {
        return Result{result, Error{}, true};
    }

    [[nodiscard]] static constexpr Result failure(Error reason) {
        return Result{Value{}, reason, false};
    }

    [[nodiscard]] constexpr explicit operator bool() const { return succeeded; }
};

} // namespace totk::core
