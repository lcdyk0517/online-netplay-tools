#include "async.h"

#include <utility>

AsyncTask::~AsyncTask() {
    join();
}

void AsyncTask::start(std::function<void()> work, std::function<void()> apply) {
    if (running_.load())
        return;
    join();
    {
        std::lock_guard<std::mutex> lk(mu_);
        apply_ = std::move(apply);
    }
    cancel_.store(false);
    failed_.store(false);
    done_.store(false);
    running_.store(true);
    th_ = std::thread([this, work = std::move(work)] {
        try {
            work();
        } catch (...) {
            failed_.store(true);
        }
        done_.store(true);
    });
}

void AsyncTask::poll() {
    if (!running_.load() || !done_.load())
        return;
    join();
    running_.store(false);
    std::function<void()> apply;
    {
        std::lock_guard<std::mutex> lk(mu_);
        apply = std::move(apply_);
        apply_ = nullptr;
    }
    if (cancel_.load() || failed_.load())
        return;  /* result is stale or the job faulted; caller handles failure */
    if (apply)
        apply();
}

void AsyncTask::join() {
    if (th_.joinable())
        th_.join();
}
