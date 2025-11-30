// watch.cpp
#include "watch.hpp"

#define WIN32_LEAN_AND_MEAN
#include <chrono>
#include <windows.h>
#include <stdexcept>
#include <cstring>
#include <thread>
#include <unordered_map>

static const size_t WATCHER_BUFFER_SIZE = 64 * 1024;

// ---------------------------
// UTF-8/UTF-16 Helpers
// ---------------------------
std::wstring utf8_to_utf16(const std::string& s) {
    if (s.empty()) return L"";

    int size = MultiByteToWideChar(
        CP_UTF8, 0, s.data(), (int)s.size(),
        nullptr, 0
    );
    std::wstring out(size, 0);
    MultiByteToWideChar(
        CP_UTF8, 0, s.data(), (int)s.size(),
        out.data(), size
    );
    return out;
}

std::string utf16_to_utf8(const std::wstring& w) {
    if (w.empty()) return "";

    int size = WideCharToMultiByte(
        CP_UTF8, 0,
        w.data(), (int)w.size(),
        nullptr, 0,
        nullptr, nullptr
    );
    std::string out(size, 0);
    WideCharToMultiByte(
        CP_UTF8, 0,
        w.data(), (int)w.size(),
        out.data(), size,
        nullptr, nullptr
    );
    return out;
}

// ---------------------------
// FsWatcher internals
// ---------------------------
void FsWatcher::set_last_error_from_win32(const char* api) {
    DWORD err = GetLastError();

    LPWSTR tmp = nullptr;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&tmp,
        0,
        nullptr
    );

    std::wstring ws = tmp ? std::wstring(tmp) : L"Unknown error";
    if (tmp) LocalFree(tmp);

    last_error_ = std::string(api) + ": " + utf16_to_utf8(ws);
}

// ---------------------------
// FsWatcher
// ---------------------------
FsWatcher::FsWatcher(const std::string& path_utf8)
{
    buffer_ = new unsigned char[WATCHER_BUFFER_SIZE];
    std::memset(buffer_, 0, WATCHER_BUFFER_SIZE);

    ovl_ = new OVERLAPPED();
    std::memset(ovl_, 0, sizeof(OVERLAPPED));

    std::wstring path_w = utf8_to_utf16(path_utf8);

    HANDLE dir = CreateFileW(
        path_w.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (dir == INVALID_HANDLE_VALUE) {
        set_last_error_from_win32("CreateFileW");
        delete[] buffer_;
        delete static_cast<OVERLAPPED*>(ovl_);
        throw std::runtime_error(last_error_);
    }

    dir_ = dir;

    HANDLE iocp = CreateIoCompletionPort(dir, nullptr, (ULONG_PTR)this, 0);
    if (!iocp) {
        set_last_error_from_win32("CreateIoCompletionPort");
        CloseHandle(dir);
        delete[] buffer_;
        delete static_cast<OVERLAPPED*>(ovl_);
        throw std::runtime_error(last_error_);
    }

    iocp_ = iocp;

    issue_read();
}

FsWatcher::~FsWatcher()
{
    // request cancellation and wake any waiter
    cancel();

    if (dir_) {
        CloseHandle((HANDLE)dir_);
        dir_ = nullptr;
    }
    if (iocp_) {
        CloseHandle((HANDLE)iocp_);
        iocp_ = nullptr;
    }

    delete[] buffer_;
    delete static_cast<OVERLAPPED*>(ovl_);
    buffer_ = nullptr;
    ovl_    = nullptr;
}

void FsWatcher::issue_read()
{
    DWORD tmp = 0;
    BOOL ok = ReadDirectoryChangesW(
        (HANDLE)dir_,
        buffer_,
        WATCHER_BUFFER_SIZE,
        TRUE,
        FILE_NOTIFY_CHANGE_FILE_NAME |
        FILE_NOTIFY_CHANGE_DIR_NAME |
        FILE_NOTIFY_CHANGE_LAST_WRITE |
        FILE_NOTIFY_CHANGE_SIZE,
        &tmp,
        (OVERLAPPED*)ovl_,
        nullptr
    );

    if (!ok && GetLastError() != ERROR_IO_PENDING) {
        set_last_error_from_win32("ReadDirectoryChangesW");
        throw std::runtime_error(last_error_);
    }
}

FsWatcher::Range FsWatcher::events(int timeout_ms) {
    return Range{ this, timeout_ms };
}

void FsWatcher::cancel() {
    bool expected = false;
    if (cancelled_.compare_exchange_strong(expected, true,
                                           std::memory_order_acq_rel)) {
        // First time we cancel: wake any blocking GQCS
        if (iocp_) {
            PostQueuedCompletionStatus(
                (HANDLE)iocp_,
                0,
                0,
                nullptr
            );
        }
        if (dir_) {
            CancelIoEx((HANDLE)dir_, (OVERLAPPED*)ovl_);
        }
    }
}

// ---------------------------
// Outer Iterator (batches)
// ---------------------------
FsWatcher::Iterator::Iterator()
    : w_(nullptr), end_(true), batch_start_(nullptr), timeout_ms_(-1)
{
}

FsWatcher::Iterator::Iterator(FsWatcher* w, bool is_end, int timeout_ms)
    : w_(w), end_(is_end), batch_start_(nullptr), timeout_ms_(timeout_ms)
{
    if (!end_) {
        fetch_next_batch();
    }
}

FsWatcher::Batch FsWatcher::Iterator::operator*() const {
    return Batch{ batch_start_ };
}

FsWatcher::Iterator& FsWatcher::Iterator::operator++()
{
    if (end_) return *this;
    fetch_next_batch();
    return *this;
}

bool FsWatcher::Iterator::operator==(const Iterator& other) const {
    return end_ && other.end_;
}
bool FsWatcher::Iterator::operator!=(const Iterator& other) const {
    return !(*this == other);
}

void FsWatcher::Iterator::fetch_next_batch()
{
    if (!w_ || w_->cancelled_.load(std::memory_order_acquire)) {
        end_ = true;
        return;
    }

    DWORD bytes = 0;
    ULONG_PTR key = 0;
    OVERLAPPED* ov = nullptr;

    DWORD timeout_win32 =
        (timeout_ms_ < 0) ? INFINITE :
        static_cast<DWORD>(timeout_ms_);

    BOOL ok = GetQueuedCompletionStatus(
        (HANDLE)w_->iocp_,
        &bytes,
        &key,
        &ov,
        timeout_win32
    );

    if (w_->cancelled_.load(std::memory_order_acquire)) {
        // Wake-up from cancel() via PostQueuedCompletionStatus
        end_ = true;
        return;
    }

    if (!ok) {
        DWORD err = GetLastError();
        if (err == WAIT_TIMEOUT) {
            // No completion within timeout: end range for this call
            end_ = true;
            return;
        }
        if (ov == nullptr) {
            // Some other error
            w_->set_last_error_from_win32("GetQueuedCompletionStatus");
            end_ = true;
            return;
        }
    }

    if (ov == nullptr) {
        // No valid OVERLAPPED but no cancellation: treat as end
        end_ = true;
        return;
    }

    w_->last_error_.clear();
    batch_start_ = w_->buffer_;

    std::memset(w_->ovl_, 0, sizeof(OVERLAPPED));
    w_->issue_read();
}

// ---------------------------
// Batch::Iterator (events within a batch)
// ---------------------------
FsWatcher::Batch::Iterator::Iterator(void* ptr, bool end)
    : cur_(ptr), end_(end)
{
}

FsWatcher::Event& FsWatcher::Batch::Iterator::operator*()
{
    auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(cur_);

    std::wstring wname(info->FileName,
                       info->FileNameLength / sizeof(WCHAR));
    current_.name = utf16_to_utf8(wname);

    switch (info->Action) {
    case FILE_ACTION_ADDED:
        current_.action = FsWatcher::Action::Created; break;
    case FILE_ACTION_REMOVED:
        current_.action = FsWatcher::Action::Deleted; break;
    case FILE_ACTION_MODIFIED:
        current_.action = FsWatcher::Action::Changed; break;
    default:
        current_.action = FsWatcher::Action::Unknown; break;
    }

    return current_;
}

FsWatcher::Batch::Iterator& FsWatcher::Batch::Iterator::operator++()
{
    if (end_) return *this;

    auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(cur_);

    if (info->NextEntryOffset != 0) {
        cur_ = reinterpret_cast<unsigned char*>(cur_) + info->NextEntryOffset;
    } else {
        cur_ = nullptr;
        end_ = true;
    }
    return *this;
}

bool FsWatcher::Batch::Iterator::operator==(const Iterator& other) const {
    return end_ == other.end_ && cur_ == other.cur_;
}
bool FsWatcher::Batch::Iterator::operator!=(const Iterator& other) const {
    return !(*this == other);
}

FsWatcher::DebouncedRange FsWatcher::debounced(int debounce_ms) {
    return DebouncedRange{this, debounce_ms};
}

FsWatcher::DebouncedRange::Iterator::Iterator(FsWatcher* watcher,
                                   int ms,
                                   bool end)
    : w(watcher),
      debounce_ms(ms),
      end_(end)
{}

FsWatcher::DebouncedRange::Iterator&
FsWatcher::DebouncedRange::Iterator::operator++()
{
    if (end_ || !w || w->cancelled_.load(std::memory_order_acquire)) {
        end_ = true;
        return *this;
    }

    std::unordered_map<std::string, FsWatcher::Action> accum;

    // ------------------------------------------------------------
    // 1) BLOCK until at least one batch is received
    // ------------------------------------------------------------
    auto t_last = std::chrono::steady_clock::now();
    bool got_initial = false;

    for (auto batch : w->events(-1)) {    // BLOCKING call
        if (w->cancelled_.load(std::memory_order_acquire)) {
            end_ = true;
            return *this;
        }

        for (auto& ev : batch) {
            accum[ev.name] = ev.action;
        }

        t_last = std::chrono::steady_clock::now();
        got_initial = true;
        break;
    }

    if (!got_initial) {
        end_ = true;
        return *this;
    }

    // ------------------------------------------------------------
    // 2) Continue collecting, non-blocking, until inactivity window
    // ------------------------------------------------------------
    const auto timeout = std::chrono::milliseconds{debounce_ms};

    for (;;) {
        if (w->cancelled_.load(std::memory_order_acquire)) {
            end_ = true;
            return *this;
        }

        auto elapsed = std::chrono::steady_clock::now() - t_last;
        if (elapsed >= timeout)
            break; // stable

        bool updated = false;

        for (auto batch : w->events(0)) { // NON-BLOCKING poll
            for (auto& ev : batch) {
                accum[ev.name] = ev.action;
                updated = true;
            }
        }

        if (updated)
            t_last = std::chrono::steady_clock::now();
        else
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }

    // ------------------------------------------------------------
    // 3) Produce the stable debounced batch
    // ------------------------------------------------------------
    batch.events.clear();
    batch.events.reserve(accum.size());

    for (auto& [name, act] : accum)
        batch.events.push_back({name, act});

    return *this;
}
