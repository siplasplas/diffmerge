#pragma once

#include <atomic>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <utility>

namespace diffcore {

// Copies refer to the same flag. The requesting thread never owns worker buffers.
class CancellationToken {
public:
    void requestCancellation() const noexcept { m_flag->store(true, std::memory_order_relaxed); }
    bool isCancellationRequested() const noexcept { return m_flag->load(std::memory_order_relaxed); }
private:
    std::shared_ptr<std::atomic_bool> m_flag = std::make_shared<std::atomic_bool>(false);
};

enum class StopReason { Cancelled, ResourceLimit };
class ComputationStopped : public std::exception {
public:
    explicit ComputationStopped(StopReason reason) : reason(reason) {}
    const char* what() const noexcept override {
        return reason == StopReason::Cancelled ? "Comparison cancelled" : "Comparison resource limit exceeded";
    }
    StopReason reason;
};

// One worker owns this counter. Only the token is shared between threads.
// Work units count loop visits/elements, not milliseconds or exact allocated bytes.
class ComputationControl {
public:
    explicit ComputationControl(CancellationToken token = {},
        std::uint64_t maxWork = std::numeric_limits<std::uint64_t>::max(),
        std::uint64_t maxTraceEntries = std::numeric_limits<std::uint64_t>::max())
        : m_token(std::move(token)), m_maxWork(maxWork), m_maxTraceEntries(maxTraceEntries) {}
    void step(std::uint64_t count = 1) {
        if (m_token.isCancellationRequested()) throw ComputationStopped(StopReason::Cancelled);
        if (count > m_maxWork - m_work) throw ComputationStopped(StopReason::ResourceLimit);
        m_work += count;
    }
    void recordTraceEntry() {
        if (m_traceEntries == m_maxTraceEntries) throw ComputationStopped(StopReason::ResourceLimit);
        ++m_traceEntries;
    }
    std::uint64_t workPerformed() const noexcept { return m_work; }
private:
    CancellationToken m_token;
    std::uint64_t m_maxWork, m_maxTraceEntries;
    std::uint64_t m_work = 0, m_traceEntries = 0;
};
inline void checkpoint(ComputationControl* control, std::uint64_t count = 1) {
    if (control) control->step(count);
}
} // namespace diffcore
