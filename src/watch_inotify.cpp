#include "watch.hpp"

#ifndef _WIN32   // Linux backend

#include <sys/inotify.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <vector>
#include <fmt/format.h>

static constexpr size_t INOTIFY_BUF_SIZE = 64 * 1024;

// Internal batch representation
struct InotifyBatch {
    std::vector<unsigned char> buf;
    size_t offset = 0;
};


//
// FsWatcher
//

FsWatcher::FsWatcher(const std::string& path_utf8) {
    int fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0) {
        last_error_ = fmt::format("inotify_init1 failed: {}", strerror(errno));
        dir_ = nullptr;
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
        return;
    }
}

FsWatcher::~FsWatcher() {
    if (dir_) {
        close((int)(intptr_t)dir_);
    }
}

void FsWatcher::cancel() {
    cancelled_.store(true, std::memory_order_release);
}


//
// Map to your Action enum
//

static FsWatcher::Action map_inotify_action(uint32_t mask) {
    if (mask & (IN_CREATE | IN_MOVED_TO))    return FsWatcher::Action::Created;
    if (mask & (IN_DELETE | IN_MOVED_FROM))  return FsWatcher::Action::Deleted;
    if (mask & IN_MODIFY)                    return FsWatcher::Action::Changed;
    return FsWatcher::Action::Unknown;
}


//
// Iterator: outer (batches)
//

FsWatcher::Iterator::Iterator()
    : w_(nullptr), end_(true), timeout_ms_(-1) {
}

FsWatcher::Iterator::Iterator(FsWatcher* w, bool is_end, int timeout_ms)
    : w_(w), end_(is_end), timeout_ms_(timeout_ms) {
}

bool FsWatcher::Iterator::operator==(const Iterator& o) const {
    return end_ == o.end_ && w_ == o.w_;
}

bool FsWatcher::Iterator::operator!=(const Iterator& o) const {
    return !(*this == o);
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

    // --- Wait using poll() ---
    {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int poll_timeout = timeout_ms_;

        int r = poll(&pfd, 1, poll_timeout);
        if (r == 0) {
            // timeout: no events
            batch_ = nullptr;
            return;
        }
        if (r < 0) {
            if (errno != EINTR)
                end_ = true;
            batch_ = nullptr;
            return;
        }
        if (!(pfd.revents & POLLIN)) {
            batch_ = nullptr;
            return;
        }
    }

    // --- Read available data ---
    auto data = std::make_unique<InotifyBatch>();
    data->buf.resize(INOTIFY_BUF_SIZE);

    ssize_t n = read(fd, data->buf.data(), data->buf.size());
    if (n <= 0) {
        if (w_->cancelled_.load())
            end_ = true;
        batch_ = nullptr;
        return;
    }

    data->buf.resize(n);
    data->offset = 0;

    current_batch_ = std::move(data);
    batch_ = current_batch_.get();
}

FsWatcher::Batch FsWatcher::Iterator::operator*() const {
    if (!batch_) return Batch{ nullptr };
    return Batch{ (void*)batch_ };
}

FsWatcher::Iterator& FsWatcher::Iterator::operator++() {
    fetch_next_batch();
    return *this;
}


//
// Inner iterator: iterate through events inside a batch
//

FsWatcher::Batch::Iterator::Iterator(void* ptr, bool end)
    : batch_(reinterpret_cast<InotifyBatch*>(ptr)), end_(end) {
    if (!end_ && batch_) advance();
}

bool FsWatcher::Batch::Iterator::operator==(const Iterator& o) const {
    return end_ == o.end_ && batch_ == o.batch_ &&
        cur_offset_ == o.cur_offset_;
}

bool FsWatcher::Batch::Iterator::operator!=(const Iterator& o) const {
    return !(*this == o);
}

void FsWatcher::Batch::Iterator::advance() {
    if (!batch_) { end_ = true; return; }

    if (batch_->offset >= batch_->buf.size()) {
        end_ = true;
        return;
    }

    const unsigned char* base = batch_->buf.data();
    const inotify_event* ev =
        reinterpret_cast<const inotify_event*>(base + batch_->offset);

    current_.name = ev->len ? std::string(ev->name) : "";
    current_.action = map_inotify_action(ev->mask);

    cur_offset_ = batch_->offset;
}

FsWatcher::Event& FsWatcher::Batch::Iterator::operator*() {
    return current_;
}

FsWatcher::Batch::Iterator& FsWatcher::Batch::Iterator::operator++() {
    if (end_ || !batch_) return *this;

    const unsigned char* base = batch_->buf.data();
    const inotify_event* ev =
        reinterpret_cast<const inotify_event*>(base + batch_->offset);

    batch_->offset += sizeof(inotify_event) + ev->len;

    if (batch_->offset >= batch_->buf.size()) {
        end_ = true;
    }
    else {
        advance();
    }

    return *this;
}

#endif // !_WIN32
