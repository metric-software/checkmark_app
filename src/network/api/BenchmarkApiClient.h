#ifndef BENCHMARKAPICLIENT_H
#define BENCHMARKAPICLIENT_H

// BenchmarkApiClient - Protobuf endpoints for benchmark uploads and public data fetches
// Purpose: Upload public samples + summary + attachments; fetch public run and menu
// Notes: Template only; implement after server schema finalization.

#include "BaseApiClient.h"

using BenchUploadCb = std::function<void(bool success, const QString& error, QString runId)>;
using PublicRunCb = std::function<void(bool success, const QVariant& data, const QString& error)>;
using MenuCb = std::function<void(bool success, const QVariant& data, const QString& error)>;
using LeaderboardCb = std::function<void(bool success, const QVariant& data, const QString& error)>;

class BenchmarkApiClient : public BaseApiClient {
    Q_OBJECT
public:
    explicit BenchmarkApiClient(QObject* parent = nullptr);

    // Upload a benchmark in protobuf (BenchmarkUploadRequest)
    void uploadBenchmark(const QVariant& uploadRequestVariant, BenchUploadCb cb);

    // GET a public run (PublicRunResponse) by run_id
    void getPublicRun(const QString& runId, PublicRunCb cb);

    // GET a benchmark menu listing (BenchmarkMenuResponse)
    void getBenchmarkMenu(MenuCb cb);

    // GET aggregated benchmark summaries (overall + per-component)
    void getBenchmarkAggregates(PublicRunCb cb);

    // POST leaderboard query (LeaderboardQuery) -> LeaderboardResponse
    void queryLeaderboard(const QVariantMap& query, LeaderboardCb cb);
};

#endif // BENCHMARKAPICLIENT_H
