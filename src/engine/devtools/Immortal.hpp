#pragma once

namespace eve::dev {

/**
 * @brief Process-immortal singleton holder for DevTools objects.
 *
 * AiPanel / ConsolePanel / McpServer / RenderVision are deliberately never
 * destroyed: McpServer's stdio reader thread can outlive normal teardown (it
 * is detached when reading from stdin), and the singletons call into each
 * other from request handling. Destroying any of them could let that thread
 * lock a destroyed mutex (macOS libc++ aborts). This holder makes the
 * immortal-lifetime contract explicit and centralized instead of scattering
 * raw `new` leaks through every accessor.
 *
 * @tparam T Singleton type; constructed on first get(), never destroyed.
 */
template <typename T>
struct Immortal {
    /** @brief Returns the process-lifetime instance. */
    static T& get() {
        static Immortal holder;  // function-local static: constructed once
        return *holder.value;
    }

    Immortal() : value(new T()) {}
    Immortal(const Immortal&) = delete;
    Immortal& operator=(const Immortal&) = delete;

    T* value;
};

}  // namespace eve::dev
