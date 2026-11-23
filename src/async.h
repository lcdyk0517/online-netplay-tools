#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

// Runs one blocking job (HTTP, ZeroTier CLI/API, LAN discovery) off the UI
// thread. The result is handed back through `apply`, which only ever runs on
// the main thread from poll().
struct AsyncTask {
    AsyncTask() = default;
    ~AsyncTask();
    AsyncTask(const AsyncTask &) = delete;
    AsyncTask &operator=(const AsyncTask &) = delete;

    bool busy() const { return running_.load(); }
    void start(std::function<void()> work, std::function<void()> apply);
    void poll();
    void join();
    // Ask the current job to stop early; results are then discarded.
    void cancel() { cancel_.store(true); }
    bool cancelled() const { return cancel_.load(); }
    const std::atomic<bool> &cancel_flag() const { return cancel_; }
    // True once if the last job threw; the caller should surface it.
    bool consume_failure() { return failed_.exchange(false); }

private:
    std::thread th_;
    std::atomic<bool> running_{false};
    std::atomic<bool> done_{false};
    std::atomic<bool> cancel_{false};
    std::atomic<bool> failed_{false};
    std::mutex mu_;
    std::function<void()> apply_;
};
