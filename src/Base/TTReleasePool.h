#pragma once

#include <functional>
#include <vector>

class TTReleasePool {
public:
    TTReleasePool() = default;

    // Destructor executes all release functions in reverse order
    ~TTReleasePool() {
        for (auto it = _releaseFuncs.rbegin(); it != _releaseFuncs.rend(); ++it) {
            if (*it) {
                (*it)();
            }
        }
    }

    // Add a release function to the pool
    void autoRelease(std::function<void()> releaseFunc) {
        if (releaseFunc) {
            _releaseFuncs.push_back(std::move(releaseFunc));
        }
    }

    // Delete copy constructor and assignment operator
    TTReleasePool(const TTReleasePool&) = delete;
    TTReleasePool& operator=(const TTReleasePool&) = delete;

    // Allow move constructor and assignment
    TTReleasePool(TTReleasePool&& other) noexcept
        : _releaseFuncs(std::move(other._releaseFuncs)) {}

    TTReleasePool& operator=(TTReleasePool&& other) noexcept {
        if (this != &other) {
            // Execute current release functions if any
            for (auto it = _releaseFuncs.rbegin(); it != _releaseFuncs.rend(); ++it) {
                if (*it) {
                    (*it)();
                }
            }
            // Take ownership of other's release functions
            _releaseFuncs = std::move(other._releaseFuncs);
        }
        return *this;
    }

private:
    std::vector<std::function<void()>> _releaseFuncs;
};
