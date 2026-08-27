// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Log.h"

namespace npps3 {

Log& Log::Instance()
{
    static Log instance;
    return instance;
}

void Log::SetSink(Sink sink)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sink = std::move(sink);
}

void Log::Write(LogLevel level, const std::wstring& message)
{
    Sink sink;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        sink = m_sink;
    }
    if (sink)
        sink(level, message);
}

} // namespace npps3
