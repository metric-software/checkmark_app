#include "BenchmarkApiClient.h"
#include "../serialization/BenchmarkProtobufSerializer.h"
#include "../serialization/BinarySerializer.h"
#include "../serialization/JsonSerializer.h"
#include "../utils/RequestBuilder.h"
#include "../../ApplicationSettings.h"
#include "../../logging/Logger.h"
#include <QCryptographicHash>
// Generated protobufs
#include "benchmark_public.pb.h"

// NOTE: Endpoints are placeholders; align with server routing when available.
static const char* kUploadPath = "/pb/benchmarks/upload";
static const char* kPublicRunPath = "/pb/benchmarks/run";      // GET ?id=<run_id>
static const char* kMenuPath = "/pb/benchmarks/menu";          // GET
static const char* kLeaderboardPath = "/pb/benchmarks/leaderboard"; // POST
static const char* kAggregatesPath = "/pb/benchmarks/aggregates"; // GET aggregated summaries

BenchmarkApiClient::BenchmarkApiClient(QObject* parent)
    : BaseApiClient(parent) {
    // Force benchmark-only protobuf serializer for these endpoints
    setSerializer(std::make_shared<BenchmarkProtobufSerializer>());
}

void BenchmarkApiClient::uploadBenchmark(const QVariant& uploadRequestVariant, BenchUploadCb cb) {
    if (!ApplicationSettings::getInstance().getEffectiveAutomaticDataUploadEnabled()) {
        QString error = ApplicationSettings::getInstance().isOfflineModeEnabled()
            ? QStringLiteral("Offline mode is enabled")
            : QStringLiteral("Data collection/upload is disabled");
        LOG_INFO << "BenchmarkApiClient: Upload blocked: " << error.toStdString();
        cb(false, error, QString());
        return;
    }
    // If QVariant holds QByteArray (already-encoded protobuf), switch serializer
    if (uploadRequestVariant.canConvert<QByteArray>()) {
        setSerializer(std::make_shared<BinarySerializer>());
        LOG_INFO << "BenchmarkApiClient: using BinarySerializer for upload";
    } else {
        setSerializer(std::make_shared<BenchmarkProtobufSerializer>());
        LOG_INFO << "BenchmarkApiClient: using ProtobufSerializer for upload";
    }
    post(QString::fromLatin1(kUploadPath), uploadRequestVariant,
         [cb](const ApiResponse& resp) {
             if (!resp.success) { cb(false, resp.error, QString()); return; }
             QString runId;
             if (resp.data.type() == QVariant::Map) {
                 QVariantMap m = resp.data.toMap();
                 runId = m.value(QStringLiteral("run_id")).toString();
             }
             cb(true, QString(), runId);
         });
}

void BenchmarkApiClient::getPublicRun(const QString& runId, PublicRunCb cb) {
    if (!ApplicationSettings::getInstance().getEffectiveAllowDataCollection()) {
        QString error = ApplicationSettings::getInstance().isOfflineModeEnabled()
            ? QStringLiteral("Offline mode is enabled")
            : QStringLiteral("Data collection is disabled");
        LOG_INFO << "BenchmarkApiClient: Public run fetch blocked: " << error.toStdString();
        cb(false, QVariant(), error);
        return;
    }
    // GET /pb/benchmarks/run?id=<run_id>
    RequestBuilder b;
    b.setMethod(HttpMethod::GET).setPath(QString::fromLatin1(kPublicRunPath)).addQueryParam("id", runId);
    // cache by path+id
    QString cacheKey = QString("%1?id=%2").arg(QString::fromLatin1(kPublicRunPath)).arg(runId);
    // cache for 5 minutes
    constexpr int kTTL = 300;
    sendRequest(b, QVariant(), [cb](const ApiResponse& resp) {
        if (!resp.success) { cb(false, QVariant(), resp.error); return; }
        cb(true, resp.data, QString());
    }, /*useCache=*/true, cacheKey, kTTL);
}

void BenchmarkApiClient::getBenchmarkMenu(MenuCb cb) {
    if (!ApplicationSettings::getInstance().getEffectiveAllowDataCollection()) {
        QString error = ApplicationSettings::getInstance().isOfflineModeEnabled()
            ? QStringLiteral("Offline mode is enabled")
            : QStringLiteral("Data collection is disabled");
        LOG_INFO << "BenchmarkApiClient: Menu fetch blocked: " << error.toStdString();
        cb(false, QVariant(), error);
        return;
    }
    // cache menu briefly client-side (MenuManager has its own 5min refresh cadence); use 60s here
    RequestBuilder b; b.setMethod(HttpMethod::GET).setPath(QString::fromLatin1(kMenuPath));
    constexpr int kTTL = 60;
    sendRequest(b, QVariant(), [cb](const ApiResponse& resp) {
        if (!resp.success) { cb(false, QVariant(), resp.error); return; }
        cb(true, resp.data, QString());
    }, /*useCache=*/true, /*cacheKey=*/QString::fromLatin1(kMenuPath), kTTL);
}

void BenchmarkApiClient::getBenchmarkAggregates(PublicRunCb cb) {
    if (!ApplicationSettings::getInstance().getEffectiveAllowDataCollection()) {
        QString error = ApplicationSettings::getInstance().isOfflineModeEnabled()
            ? QStringLiteral("Offline mode is enabled")
            : QStringLiteral("Data collection is disabled");
        LOG_INFO << "BenchmarkApiClient: Aggregates fetch blocked: " << error.toStdString();
        cb(false, QVariant(), error);
        return;
    }
    // Aggregates endpoint is JSON-only
    setSerializer(std::make_shared<JsonSerializer>());
    RequestBuilder b;
    b.setMethod(HttpMethod::GET).setPath(QString::fromLatin1(kAggregatesPath));
    constexpr int kTTL = 60; // short cache since backend refreshes periodically
    sendRequest(b, QVariant(), [cb](const ApiResponse& resp) {
        cb(resp.success, resp.data, resp.error);
    }, /*useCache=*/true, /*cacheKey=*/QString::fromLatin1(kAggregatesPath), kTTL);
}

void BenchmarkApiClient::queryLeaderboard(const QVariantMap& query, LeaderboardCb cb) {
    if (!ApplicationSettings::getInstance().getEffectiveAllowDataCollection()) {
        QString error = ApplicationSettings::getInstance().isOfflineModeEnabled()
            ? QStringLiteral("Offline mode is enabled")
            : QStringLiteral("Data collection is disabled");
        LOG_INFO << "BenchmarkApiClient: Leaderboard query blocked: " << error.toStdString();
        cb(false, QVariant(), error);
        return;
    }
    // Build LeaderboardQuery protobuf from QVariantMap { mode: string, filters: [{key,value}] }
    checkmark::benchmarks::LeaderboardQuery qpb;
    QString modeStr = query.value(QStringLiteral("mode")).toString();
    if (modeStr == QLatin1String("FULL_TOP5")) {
        qpb.set_mode(checkmark::benchmarks::LeaderboardMode::FULL_TOP5);
    } else if (modeStr == QLatin1String("FULL_MEDIAN")) {
        qpb.set_mode(checkmark::benchmarks::LeaderboardMode::FULL_MEDIAN);
    } else if (modeStr == QLatin1String("FILTERED")) {
        qpb.set_mode(checkmark::benchmarks::LeaderboardMode::FILTERED);
    } else {
        // default safe mode
        qpb.set_mode(checkmark::benchmarks::LeaderboardMode::FULL_MEDIAN);
    }

    QVariantList filters = query.value(QStringLiteral("filters")).toList();
    for (const auto& fv : filters) {
        QVariantMap fm = fv.toMap();
        auto* f = qpb.add_filters();
        f->set_key(fm.value(QStringLiteral("key")).toString().toStdString());
        f->set_value(fm.value(QStringLiteral("value")).toString().toStdString());
    }

    std::string s;
    qpb.SerializeToString(&s);
    QByteArray body = QByteArray::fromStdString(s);

    // Prepare request (Content-Type comes from serializer; ensure it's protobuf)
    setSerializer(std::make_shared<BenchmarkProtobufSerializer>());
    RequestBuilder b; b.setMethod(HttpMethod::POST).setPath(QString::fromLatin1(kLeaderboardPath));

    // Cache key based on protobuf body, with short TTL as data is dynamic
    QString cacheKey = QString("%1|%2").arg(QString::fromLatin1(kLeaderboardPath))
                        .arg(QString::fromLatin1(QCryptographicHash::hash(body, QCryptographicHash::Md5).toHex()));
    constexpr int kTTL = 60; // 1 minute cache for leaderboard queries

    sendRequest(b, QVariant::fromValue(body), [cb](const ApiResponse& resp){
        cb(resp.success, resp.data, resp.error);
    }, /*useCache=*/true, cacheKey, kTTL);
}
