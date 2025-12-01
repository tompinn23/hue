// watch_inotify.cpp  (Linux backend)
#include "watch.hpp"

#ifndef _WIN32   // Linux backend

#include <sys/inotify.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <vector>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <fmt/format.h>

static constexpr size_t INOTIFY_BUF_SIZE = 64 * 1024;

// Internal batch representation for a single read()
struct InotifyBatch {
    std::vector<unsigned char> buf;
    size_t offset = 0; // current offset into buf for iteration
};

//
// Map inotify mask to FsWatcher::Action
//
static FsWatcher::Action map_inotify_action(uint32_t mask) {
    if (mask & (IN_CREATE | IN_MOVED_TO))    return FsWatcher::Action::Created;
    if (mask & (IN_DELETE | IN_MOVED_FROM))  return FsWatcher::Action::Deleted;
    if (mask & IN_MODIFY)                    return FsWatcher::Action::Changed;
    return FsWatcher::Action::Unknown;
}

//
// FsWatcher (Linux)
//

FsWatcher::FsWatcher(const std::string& path_utf8) {
    int fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0) {
        last_error_ = fmt::format("inotify_init1 failed: {}", strerror(errno));
        dir_ = nullptr;
        ovl_ = nullptr;
        return;
    }

    dir_ = reinterpret_cast<void*>((intptr_t)fd);

    int wd = inotify_add_watch(fd, path_utf8.c_str(),
                               IN_CREATE | IN_MODIFY | IN_DELETE |
                               IN_MOVED_FROM | IN_MOVED_TO);
    if (wd < 0) {
        last_error_ = fmt::format("inotify_add_watch failed: {}", strerror(errno));
        close(fd);
        dir_ = nullptr;
        ovl_ = nullptr;
        return;
    }

    // Allocate a reusable batch buffer and store it in ovl_
    auto* batch = new InotifyBatch();
    ovl_ = batch;
}

FsWatcher::~FsWatcher() {
    // cancel() also closes fd, but destructor must be safe if cancel()
    // was not called.
    if (dir_) {
        close((int)(intptr_t)dir_);
        dir_ = nullptr;
    }

    if (ovl_) {
        delete reinterpret_cast<InotifyBatch*>(ovl_);
        ovl_ = nullptr;
    }
}

void FsWatcher::cancel() {
    bool expected = false;
    if (cancelled_.compare_exchange_strong(expected, true,
                                           std::memory_order_acq_rel)) {
        // Wake any blocking poll() by closing the inotify fd.
        int fd = (int)(intptr_t)dir_;
        if (fd >= 0) {
            close(fd);
            dir_ = nullptr;
        }
    }
}

FsWatcher::Range FsWatcher::events(int timeout_ms) {
    return Range{ this, timeout_ms };
}

//
// Outer iterator (batches)
//

FsWatcher::Iterator::Iterator()
    : w_(nullptr), end_(true), batch_start_(nullptr), timeout_ms_(-1) {
}

FsWatcher::Iterator::Iterator(FsWatcher* w, bool is_end, int timeout_ms)
    : w_(w), end_(is_end), batch_start_(nullptr), timeout_ms_(timeout_ms) {
    if (!end_) {
        fetch_next_batch();
    }
}

bool FsWatcher::Iterator::operator==(const Iterator& o) const {
    return end_ == o.end_ && w_ == o.w_;
}

bool FsWatcher::Iterator::operator!=(const Iterator& o) const {
    return !(*this == o);
}

FsWatcher::Batch FsWatcher::Iterator::operator*() const {
    if (!batch_start_) {
        return Batch{ nullptr };
    }
    return Batch{ batch_start_ };  // batch_start_ points to InotifyBatch
}

FsWatcher::Iterator& FsWatcher::Iterator::operator++() {
    if (end_) return *this;
    fetch_next_batch();
    return *this;
}

void FsWatcher::Iterator::fetch_next_batch() {
    if (end_ || !w_) return;
    if (w_->cancelled_.load(std::memory_order_acquire)) {
        end_ = true;
        return;
    }

    int fd = (int)(intptr_t)w_->dir_;
    if (fd < 0) {
        end_ = true;
        return;
    }

    // Wait for readiness with poll()
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int poll_timeout = timeout_ms_; // -1 = infinite, 0 = non-blocking, >0 ms

    int r = poll(&pfd, 1, poll_timeout);
    if (r == 0) {
        // timeout: no events ready
        batch_start_ = nullptr;
        return;
    }
    if (r < 0) {
        if (errno != EINTR)
            end_ = true;
        batch_start_ = nullptr;
        return;
    }
    if (!(pfd.revents & POLLIN)) {
        batch_start_ = nullptr;
        return;
    }

    auto* batch = reinterpret_cast<InotifyBatch*>(w_->ovl_);
    if (!batch) {
        end_ = true;
        batch_start_ = nullptr;
        return;
    }

    batch->buf.resize(INOTIFY_BUF_SIZE);

    ssize_t n = read(fd, batch->buf.data(), batch->buf.size());
    if (n <= 0) {
        if (w_->cancelled_.load(std::memory_order_acquire))
            end_ = true;
        batch_start_ = nullptr;
        return;
    }

    batch->buf.resize(static_cast<size_t>(n));
    batch->offset = 0;

    w_->last_error_.clear();
    batch_start_ = batch; // pass InotifyBatch* to Batch
}

//
// Inner iterator: iterate through events inside a batch
// (matches layout declared in watch.hpp)
//

FsWatcher::Batch::Iterator::Iterator(void* ptr, bool end)
    : cur_(ptr), end_(end) {
    if (!end_ && cur_) {
        auto* batch = reinterpret_cast<InotifyBatch*>(cur_);
        if (batch->offset >= batch->buf.size()) {
            end_ = true;
        } else {
            const unsigned char* base = batch->buf.data();
            const inotify_event* ev =
                reinterpret_cast<const inotify_event*>(base + batch->offset);

            current_.name   = ev->len ? std::string(ev->name) : "";
            current_.action = map_inotify_action(ev->mask);
        }
    }
}

FsWatcher::Event& FsWatcher::Batch::Iterator::operator*() {
    return current_;
}

FsWatcher::Batch::Iterator&
FsWatcher::Batch::Iterator::operator++() {
    if (end_ || !cur_) return *this;

    auto* batch = reinterpret_cast<InotifyBatch*>(cur_);
    const unsigned char* base = batch->buf.data();
    const inotify_event* ev =
        reinterpret_cast<const inotify_event*>(base + batch->offset);

    batch->offset += sizeof(inotify_event) + ev->len;

    if (batch->offset >= batch->buf.size()) {
        end_ = true;
    } else {
        const inotify_event* ev2 =
            reinterpret_cast<const inotify_event*>(base + batch->offset);

        current_.name   = ev2->len ? std::string(ev2->name) : "";
        current_.action = map_inotify_action(ev2->mask);
    }

    return *this;
}

bool FsWatcher::Batch::Iterator::operator==(const Iterator& other) const {
    return end_ == other.end_ && cur_ == other.cur_;
}

bool FsWatcher::Batch::Iterator::operator!=(const Iterator& other) const {
    return !(*this == other);
}

//
// Debounced API (same logic as Win32 version, but builds on Linux backend)
//

FsWatcher::DebouncedRange FsWatcher::debounced(int debounce_ms) {
    return DebouncedRange{ this, debounce_ms };
}

FsWatcher::DebouncedRange::Iterator::Iterator(FsWatcher* watcher,
                                               int ms,
                                               bool end)
    : w(watcher),
      debounce_ms(ms),
      end_(end) {}

FsWatcher::DebouncedRange::Iterator&
FsWatcher::DebouncedRange::Iterator::operator++() {
    if (end_ || !w || w->cancelled_.load(std::memory_order_acquire)) {
        end_ = true;
        return *this;
    }

    std::unordered_map<std::string, FsWatcher::Action> accum;

    // 1) Block until we get at least one batch
    auto t_last = std::chrono::steady_clock::now();
    bool got_initial = false;

    for (auto batch : w->events(-1)) { // blocking
        if (w->cancelled_.load(std::memory_order_acquire)) {
            end_ = true;
            return *this;
        }

        for (auto& ev : batch) {
            accum[ev.name] = ev.action;
        }

        t_last      = std::chrono::steady_clock::now();
        got_initial = true;
        break;
    }

    if (!got_initial) {
        end_ = true;
        return *this;
    }

    // 2) Keep collecting non-blocking until inactivity window elapses
    const auto timeout = std::chrono::milliseconds{ debounce_ms };

    for (;;) {
        if (w->cancelled_.load(std::memory_order_acquire)) {
            end_ = true;
            return *this;
        }

        auto elapsed = std::chrono::steady_clock::now() - t_last;
        if (elapsed >= timeout)
            break;

        bool updated = false;

        for (auto batch : w->events(0)) { // non-blocking
            for (auto& ev : batch) {
                accum[ev.name] = ev.action;
                updated = true;
            }
        }

        if (updated)
            t_last = std::chrono::steady_clock::now();
        else
            std::this_thread::sleep_for(std::chrono::milliseconds{ 5 });
    }

    // 3) Produce the debounced batch
    batch.events.clear();
    batch.events.reserve(accum.size());

    for (auto& [name, act] : accum)
        batch.events.push_back({ name, act });

    return *this;
}

#endif // !_WIN32
