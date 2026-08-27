// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <mutex>
#include <string>

namespace npps3 {

enum class LogLevel { Info, Warning, Error };

// Minimal thread-safe logger. A single sink (the panel's log view) can be
// attached; callers are responsible for never passing secrets in messages.
class Log
{
public:
    using Sink = std::function<void(LogLevel, const std::wstring&)>;

    static Log& Instance();

    // The sink may be invoked from worker threads; the UI sink must marshal.
    void SetSink(Sink sink);
    void Write(LogLevel level, const std::wstring& message);

    void Info(const std::wstring& message) { Write(LogLevel::Info, message); }
    void Warn(const std::wstring& message) { Write(LogLevel::Warning, message); }
    void Error(const std::wstring& message) { Write(LogLevel::Error, message); }

private:
    std::mutex m_mutex;
    Sink m_sink;
};

} // namespace npps3
