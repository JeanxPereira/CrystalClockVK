#pragma once

#include <deque>
#include <functional>

class DeletionQueue {
public:
    void push(std::function<void()>&& deletor) {
        m_deletors.push_back(std::move(deletor));
    }

    void flush() {
        for (auto it = m_deletors.rbegin(); it != m_deletors.rend(); ++it) {
            (*it)();
        }
        m_deletors.clear();
    }

    ~DeletionQueue() {
        flush();
    }

private:
    std::deque<std::function<void()>> m_deletors;
};
