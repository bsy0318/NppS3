// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../storage/S3Client.h"

#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace npps3 {

enum class TransferOp
{
    ListBuckets,
    List,
    Head,
    Download,
    Upload,
    UploadBytes,
    Delete,
    Copy,
    TestConnection,
};

enum class TransferState
{
    Pending,
    Running,
    Completed,
    Failed,
    Cancelled,
};

const wchar_t* TransferOpName(TransferOp op);
const wchar_t* TransferStateName(TransferState st);

struct TransferRequest
{
    TransferOp op = TransferOp::List;
    S3Config s3;               // per-job snapshot; secret stays in memory only
    std::string bucket;
    std::string key;
    std::string dstBucket;     // Copy
    std::string dstKey;        // Copy
    std::wstring localPath;    // Download/Upload
    std::string contentType;   // Upload
    std::string data;          // UploadBytes payload
    std::string prefix;        // List
    std::string delimiter;     // List
    std::string continuationToken;
    int maxKeys = 1000;
    unsigned long long context = 0; // caller correlation token (e.g. tree item)
    // Human-readable label for the transfer list. Set by the caller so the
    // UI never has to show a raw endpoint (which embeds the account id).
    std::wstring displayLabel;
};

struct TransferResultData
{
    std::vector<BucketInfo> buckets;
    ListObjectsResult listing;
    ObjectMetadata metadata;
    PutObjectResult put;
};

struct TransferEvent
{
    enum class Type { Started, Progress, Finished };

    Type type = Type::Started;
    unsigned long long id = 0;
    TransferOp op = TransferOp::List;
    TransferState state = TransferState::Pending;
    unsigned long long transferred = 0;
    unsigned long long total = 0;
    StorageError error;                          // valid when state == Failed
    std::shared_ptr<TransferResultData> result;  // valid when state == Completed
    std::wstring label;                          // display text (bucket/key)
    unsigned long long context = 0;
    // Snapshot of request fields completion handlers need:
    std::string bucket;
    std::string key;
    std::wstring localPath;
};

// Invoked on a WORKER thread. Implementations must marshal to the UI thread
// themselves (e.g. PostMessage with a heap-allocated copy).
class ITransferObserver
{
public:
    virtual ~ITransferObserver() = default;
    virtual void OnTransferEvent(const TransferEvent& ev) = 0;
};

// Two single-worker lanes: "control" (list/head/delete/copy/test) and "data"
// (upload/download), so browsing stays responsive during long transfers.
class TransferManager
{
public:
    explicit TransferManager(ITransferObserver* observer);
    ~TransferManager();
    TransferManager(const TransferManager&) = delete;
    TransferManager& operator=(const TransferManager&) = delete;

    void Start();
    // Prevents new work, cancels queued+running jobs, joins workers.
    // Safe to call more than once. Must be called before the observer dies.
    void Shutdown();

    unsigned long long Enqueue(TransferRequest req);
    void Cancel(unsigned long long id);
    bool HasActiveWork() const;

private:
    struct Job
    {
        unsigned long long id = 0;
        TransferRequest req;
        std::atomic<bool> cancelled{false};
        ~Job()
        {
            // Do not leave the secret in freed heap memory.
            if (!req.s3.secretAccessKey.empty())
                ::memset(req.s3.secretAccessKey.data(), 0, req.s3.secretAccessKey.size());
        }
    };
    using JobPtr = std::shared_ptr<Job>;

    static int LaneFor(TransferOp op);
    void WorkerLoop(int lane);
    void Execute(Job& job);
    void Emit(const TransferEvent& ev);
    static std::wstring LabelFor(const TransferRequest& req);

    ITransferObserver* m_observer;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<JobPtr> m_queue[2];
    JobPtr m_active[2];
    std::thread m_workers[2];
    bool m_started = false;
    bool m_stopping = false;
    unsigned long long m_nextId = 1;
};

} // namespace npps3
