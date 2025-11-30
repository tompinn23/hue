// watch.hpp
#pragma once

#include <string>
#include <iterator>
#include <cstdint>
#include <atomic>
#include <vector>
#include <fmt/format.h>

class FsWatcher {
public:
    enum class Action {
        Created,
        Deleted,
        Changed,
        Unknown
    };

    struct Event {
        std::string name;  // UTF-8 filename
        Action      action;
    };

    struct Batch {
        struct Iterator;

        void* first{};     // pointer to first FILE_NOTIFY_INFORMATION in buffer

        Iterator begin() const;
        Iterator end()   const;
    };

    struct DebouncedBatch {
        std::vector<Event> events;

        auto begin() const { return events.begin(); }
        auto end()   const { return events.end(); }
    };

    struct DebouncedRange {
        FsWatcher* w{};
        int debounce_ms{};

        struct Iterator {
            FsWatcher* w{};
            int debounce_ms{};
            bool end_{false};
            DebouncedBatch batch;

            Iterator() = default;
            Iterator(FsWatcher* watcher, int ms, bool end);

            DebouncedBatch& operator*() { return batch; }
            Iterator& operator++();

            bool operator==(const Iterator& o) const { return end_ == o.end_; }
            bool operator!=(const Iterator& o) const { return !(*this == o); }
        };

        Iterator begin() const { return Iterator(w, debounce_ms, false); }
        Iterator end()   const { return Iterator(w, debounce_ms, true); }
    };


    class Iterator;

    struct Range {
        FsWatcher* w{};
        int        timeout_ms{};  // -1 = infinite, 0 = non-blocking, >0 = ms

        Iterator begin() const;
        Iterator end()   const;
    };

    explicit FsWatcher(const std::string& path_utf8);
    ~FsWatcher();

    // Outer infinite stream of batches. Each iteration may block according
    // to timeout_ms. -1 = infinite wait, 0 = non-blocking, >0 = timeout in ms.
    Range events(int timeout_ms = -1);

    DebouncedRange debounced(int debounce_ms);


    // Request cancellation: wakes any blocking wait and shuts the iterator down.
    void cancel();

    // Last Win32 error formatted as UTF-8 (empty if none)
    const std::string& last_error() const { return last_error_; }

private:
    void*          dir_{};      // HANDLE
    void*          iocp_{};     // HANDLE
    void*          ovl_{};      // OVERLAPPED*
    unsigned char* buffer_{};   // shared IO buffer
    std::string    last_error_;
    std::atomic<bool> cancelled_{false};

    void issue_read();
    void set_last_error_from_win32(const char* api);

    friend class Iterator;
    friend struct Range;
    friend struct Batch::Iterator;
};

// Iterate events within a single IOCP batch.
struct FsWatcher::Batch::Iterator {
    Iterator(void* ptr, bool end);

    Event&   operator*();
    Iterator& operator++();

    bool operator==(const Iterator& other) const;
    bool operator!=(const Iterator& other) const;

private:
    void* cur_{};
    bool  end_{true};
    Event current_;
};

// Iterate over batches from IOCP.
class FsWatcher::Iterator {
public:
    Iterator();
    Iterator(FsWatcher* w, bool is_end, int timeout_ms);

    Batch    operator*() const;
    Iterator& operator++();

    bool operator==(const Iterator& other) const;
    bool operator!=(const Iterator& other) const;

private:
    FsWatcher* w_{};
    bool       end_{true};
    void*      batch_start_{};   // pointer into buffer_ of first FILE_NOTIFY_INFORMATION
    int        timeout_ms_{-1};  // -1 = infinite, 0 = non-blocking, >0 = ms

    void fetch_next_batch();
};




// UTF-8 helpers
std::wstring utf8_to_utf16(const std::string& s);
std::string  utf16_to_utf8(const std::wstring& w);

// Range begin/end
inline FsWatcher::Iterator FsWatcher::Range::begin() const {
    return Iterator(w, false, timeout_ms);
}
inline FsWatcher::Iterator FsWatcher::Range::end() const {
    return Iterator(w, true, timeout_ms);
}

// Batch begin/end
inline FsWatcher::Batch::Iterator FsWatcher::Batch::begin() const {
    return Iterator(first, first == nullptr);
}
inline FsWatcher::Batch::Iterator FsWatcher::Batch::end() const {
    return Iterator(nullptr, true);
}

// fmt formatters
template<>
struct fmt::formatter<FsWatcher::Action> : fmt::formatter<std::string_view> {
    auto format(FsWatcher::Action a, fmt::format_context& ctx) const {
        std::string_view s;
        switch (a) {
        case FsWatcher::Action::Created: s = "created"; break;
        case FsWatcher::Action::Deleted: s = "deleted"; break;
        case FsWatcher::Action::Changed: s = "changed"; break;
        default:                         s = "unknown"; break;
        }
        return fmt::formatter<std::string_view>::format(s, ctx);
    }
};

template<>
struct fmt::formatter<FsWatcher::Event> : fmt::formatter<std::string> {
    auto format(const FsWatcher::Event& e, fmt::format_context& ctx) const {
        auto s = fmt::format("{{ name: \"{}\", action: {} }}", e.name, e.action);
        return fmt::formatter<std::string>::format(s, ctx);
    }
};
