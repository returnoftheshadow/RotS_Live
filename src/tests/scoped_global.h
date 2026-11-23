/* scoped_global.h */

#ifndef ROTS_TESTS_SCOPED_GLOBAL_H
#define ROTS_TESTS_SCOPED_GLOBAL_H

namespace rots_tests {

// Replaces a global for the life of the scope and restores its old value afterwards.
template <typename Value>
class ScopedGlobal {
public:
    ScopedGlobal(Value& global, Value replacement)
        : target(global)
        , saved_value(global)
    {
        target = replacement;
    }

    ~ScopedGlobal()
    {
        target = saved_value;
    }

    ScopedGlobal(const ScopedGlobal&) = delete;
    ScopedGlobal& operator=(const ScopedGlobal&) = delete;

private:
    // The global this scope replaces.
    Value& target;
    // The global's value before the scope, restored when the scope ends.
    Value saved_value;
};

} // namespace rots_tests

#endif /* ROTS_TESTS_SCOPED_GLOBAL_H */
