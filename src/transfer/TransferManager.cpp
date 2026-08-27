// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TransferManager.h"

#include "../util/StringUtil.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace npps3 {

const wchar_t* TransferOpName(TransferOp op)
{
    switch (op)
    {
    case TransferOp::ListBuckets: return L"List Buckets";
    case TransferOp::List: return L"List";
    case TransferOp::Head: return L"Properties";
    case TransferOp::Download: return L"Download";
    case TransferOp::Upload: return L"Upload";
    case TransferOp::UploadBytes: return L"Create";
    case TransferOp::Delete: return L"Delete";
    case TransferOp::Copy: return L"Copy";
    case TransferOp::TestConnection: return L"Connect";
    }
    return L"?";
}

const wchar_t* TransferStateName(TransferState st)
{
    switch (st)
    {
    case TransferState::Pending: return L"Pending";
    case TransferState::Running: return L"Running";
    case TransferState::Completed: return L"Completed";
    case TransferState::Failed: return L"Failed";
    case TransferState::Cancelled: return L"Cancelled";
    }
    return L"?";
}

TransferManager::TransferManager(ITransferObserver* observer)
    : m_observer(observer)
{
}

TransferManager::~TransferManager()
{
    Shutdown();
}

void TransferManager::Start()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_started || m_stopping)
        return;
    m_started = true;
    for (int lane = 0; lane < 2; ++lane)
        m_workers[lane] = std::thread(&TransferManager::WorkerLoop, this, lane);
}

void TransferManager::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_started || m_stopping)
        {
            m_stopping = true;
            return;
        }
        m_stopping = true;
        for (int lane = 0; lane < 2; ++lane)
        {
            for (auto& job : m_queue[lane])
                job->cancelled.store(true);
            if (m_active[lane])
                m_active[lane]->cancelled.store(true);
        }
    }
    m_cv.notify_all();
    for (int lane = 0; lane < 2; ++lane)
        if (m_workers[lane].joinable())
            m_workers[lane].join();
}

int TransferManager::LaneFor(TransferOp op)
{
    switch (op)
    {
    case TransferOp::Download:
    case TransferOp::Upload:
        return 1; // data lane
    default:
        return 0; // control lane
    }
}

std::wstring TransferManager::LabelFor(const TransferRequest& req)
{
    if (!req.displayLabel.empty())
        return req.displayLabel;

    std::string label;
    switch (req.op)
    {
    case TransferOp::ListBuckets:
    case TransferOp::TestConnection:
        label = req.bucket;
        break;
    case TransferOp::List:
        label = req.bucket + "/" + req.prefix;
        break;
    case TransferOp::Copy:
        label = req.key + " -> " + req.dstKey;
        break;
    default:
        label = req.key.empty() ? req.bucket : req.key;
        break;
    }
    return Utf8ToWide(label);
}

unsigned long long TransferManager::Enqueue(TransferRequest req)
{
    JobPtr job;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping)
            return 0;
        job = std::make_shared<Job>();
        job->id = m_nextId++;
        job->req = std::move(req);
        m_queue[LaneFor(job->req.op)].push_back(job);
    }
    m_cv.notify_all();

    TransferEvent ev;
    ev.type = TransferEvent::Type::Started;
    ev.id = job->id;
    ev.op = job->req.op;
    ev.state = TransferState::Pending;
    ev.label = LabelFor(job->req);
    ev.context = job->req.context;
    ev.bucket = job->req.bucket;
    ev.key = job->req.key;
    ev.localPath = job->req.localPath;
    Emit(ev);
    return job->id;
}

void TransferManager::Cancel(unsigned long long id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (int lane = 0; lane < 2; ++lane)
    {
        for (auto& job : m_queue[lane])
            if (job->id == id)
                job->cancelled.store(true);
        if (m_active[lane] && m_active[lane]->id == id)
            m_active[lane]->cancelled.store(true);
    }
}

bool TransferManager::HasActiveWork() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_queue[0].empty() || !m_queue[1].empty() || m_active[0] || m_active[1];
}

void TransferManager::Emit(const TransferEvent& ev)
{
    if (m_observer)
        m_observer->OnTransferEvent(ev);
}

void TransferManager::WorkerLoop(int lane)
{
    for (;;)
    {
        JobPtr job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [&] { return m_stopping || !m_queue[lane].empty(); });
            if (m_stopping && m_queue[lane].empty())
                return;
            job = m_queue[lane].front();
            m_queue[lane].pop_front();
            m_active[lane] = job;
        }

        Execute(*job);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_active[lane].reset();
        }
    }
}

void TransferManager::Execute(Job& job)
{
    TransferEvent base;
    base.id = job.id;
    base.op = job.req.op;
    base.label = LabelFor(job.req);
    base.context = job.req.context;
    base.bucket = job.req.bucket;
    base.key = job.req.key;
    base.localPath = job.req.localPath;

    if (job.cancelled.load())
    {
        TransferEvent ev = base;
        ev.type = TransferEvent::Type::Finished;
        ev.state = TransferState::Cancelled;
        Emit(ev);
        return;
    }

    {
        TransferEvent ev = base;
        ev.type = TransferEvent::Type::Started;
        ev.state = TransferState::Running;
        Emit(ev);
    }

    S3Client client(job.req.s3);
    client.SetCancelFlag(&job.cancelled);

    // Throttled progress relay: at most ~10 events/sec.
    ULONGLONG lastTick = 0;
    ProgressFn progress = [&](uint64_t transferred, uint64_t total) -> bool {
        if (job.cancelled.load(std::memory_order_relaxed))
            return false;
        ULONGLONG now = ::GetTickCount64();
        if (now - lastTick >= 100 || transferred == total)
        {
            lastTick = now;
            TransferEvent ev = base;
            ev.type = TransferEvent::Type::Progress;
            ev.state = TransferState::Running;
            ev.transferred = transferred;
            ev.total = total;
            Emit(ev);
        }
        return true;
    };

    auto result = std::make_shared<TransferResultData>();
    StorageError error;
    bool ok = false;

    switch (job.req.op)
    {
    case TransferOp::ListBuckets:
    {
        auto r = client.ListBuckets();
        ok = r.ok;
        error = r.error;
        result->buckets = std::move(r.value);
        break;
    }
    case TransferOp::List:
    {
        auto r = client.ListObjects(job.req.bucket, job.req.prefix, job.req.delimiter,
                                    job.req.continuationToken, job.req.maxKeys);
        ok = r.ok;
        error = r.error;
        result->listing = std::move(r.value);
        break;
    }
    case TransferOp::Head:
    {
        auto r = client.HeadObject(job.req.bucket, job.req.key);
        ok = r.ok;
        error = r.error;
        result->metadata = std::move(r.value);
        break;
    }
    case TransferOp::Download:
    {
        auto r = client.DownloadObject(job.req.bucket, job.req.key, job.req.localPath, progress);
        ok = r.ok;
        error = r.error;
        result->metadata = std::move(r.value);
        break;
    }
    case TransferOp::Upload:
    {
        auto r = client.PutObject(job.req.bucket, job.req.key, job.req.localPath,
                                  job.req.contentType, progress);
        ok = r.ok;
        error = r.error;
        result->put = std::move(r.value);
        break;
    }
    case TransferOp::UploadBytes:
    {
        auto r = client.PutObjectBytes(job.req.bucket, job.req.key, job.req.data,
                                       job.req.contentType);
        ok = r.ok;
        error = r.error;
        result->put = std::move(r.value);
        break;
    }
    case TransferOp::Delete:
    {
        auto r = client.DeleteObject(job.req.bucket, job.req.key);
        ok = r.ok;
        error = r.error;
        break;
    }
    case TransferOp::Copy:
    {
        auto r = client.CopyObject(job.req.bucket, job.req.key,
                                   job.req.dstBucket, job.req.dstKey);
        ok = r.ok;
        error = r.error;
        result->put = std::move(r.value);
        break;
    }
    case TransferOp::TestConnection:
    {
        auto r = client.TestConnection(job.req.bucket);
        ok = r.ok;
        error = r.error;
        break;
    }
    }

    TransferEvent ev = base;
    ev.type = TransferEvent::Type::Finished;
    if (ok)
    {
        ev.state = TransferState::Completed;
        ev.result = std::move(result);
    }
    else if (error.kind == ErrorKind::Cancelled || job.cancelled.load())
    {
        ev.state = TransferState::Cancelled;
    }
    else
    {
        ev.state = TransferState::Failed;
        ev.error = std::move(error);
    }
    Emit(ev);
}

} // namespace npps3
