#include "BenchmarkProtobufSerializer.h"
#include "../../logging/Logger.h"
#include <QVariantMap>
#include <QVariantList>
#include <algorithm>

// Generated headers
#include "benchmark_public.pb.h"
#include "benchmark_upload.pb.h"
#include "benchmark_full.pb.h"
#include "benchmark_common.pb.h"

// Minimal stub: passthrough for QByteArray and simple map detection placeholder.

SerializationResult BenchmarkProtobufSerializer::serialize(const QVariant& data) {
    SerializationResult r;
    // In this initial version, only support already-encoded QByteArray (like BinarySerializer),
    // or reject with a clear message so callers use BinarySerializer for now.
    if (data.canConvert<QByteArray>()) {
        r.data = data.toByteArray();
        r.success = true;
        return r;
    }
    r.error = QStringLiteral("BenchmarkProtobufSerializer expects QByteArray for now (mapper pending)");
    return r;
}

DeserializationResult BenchmarkProtobufSerializer::deserialize(const QByteArray& data,
                                                                const QString&) {
    DeserializationResult r;
    LOG_INFO << "BenchmarkProtobufSerializer::deserialize starting - data size: " << data.size() << " bytes";
    LOG_INFO << "Raw data preview (first 100 bytes hex): " << data.left(100).toHex(' ').toStdString();
    
    // Try to parse the most common response messages first - LeaderboardResponse is likely most common
    try {
        LOG_INFO << "Trying to parse as LeaderboardResponse...";
        checkmark::benchmarks::LeaderboardResponse lr;
        if (lr.ParseFromArray(data.constData(), data.size())) {
            LOG_INFO << "Successfully parsed as LeaderboardResponse with " << lr.runs_size() << " runs";
            QVariantMap out;
            // runs
            QVariantList runs;
            runs.reserve(lr.runs_size());
            for (const auto& run : lr.runs()) {
                // Reuse the mapping by serializing nested PublicRunResponse to bytes then re-deserializing
                std::string tmp;
                run.SerializeToString(&tmp);
                DeserializationResult dr = deserialize(QByteArray::fromStdString(tmp));
                if (dr.success && dr.data.type() == QVariant::Map) {
                    runs.push_back(dr.data);
                } else {
                    // Fallback: minimal map
                    QVariantMap m; m.insert("summary.avg_fps", run.summary().avg_fps()); runs.push_back(m);
                }
            }
            out.insert("runs", runs);
            // meta
            QVariantMap meta;
            if (lr.has_meta()) {
                const auto& m = lr.meta();
                meta.insert("total_matches", static_cast<uint>(m.total_matches()));
                meta.insert("selection_policy", QString::fromStdString(m.selection_policy()));
                QVariantList applied;
                for (const auto& f : m.applied_filters()) {
                    QVariantMap fm; fm.insert("key", QString::fromStdString(f.key())); fm.insert("value", QString::fromStdString(f.value())); applied.push_back(fm);
                }
                meta.insert("applied_filters", applied);
                if (!m.reason().empty()) meta.insert("reason", QString::fromStdString(m.reason()));
            }
            out.insert("meta", meta);
            r.data = out; r.success = true; return r;
        }
        LOG_INFO << "Failed to parse as LeaderboardResponse";
    } catch (const std::exception& e) {
        LOG_WARN << "Exception parsing as LeaderboardResponse: " << e.what();
    } catch (...) {
        LOG_WARN << "Unknown exception parsing as LeaderboardResponse";
    }
    // Note: Upload response parsing moved later to reduce false positives
    // Try BenchmarkMenuResponse first (before PublicRunResponse) to avoid false positives when parsing menu bytes
    try {
        LOG_INFO << "Trying to parse as BenchmarkMenuResponse...";
        checkmark::benchmarks::BenchmarkMenuResponse mm;
        if (mm.ParseFromArray(data.constData(), data.size())) {
            bool hasItems = mm.items_size() > 0;
            bool hasCats = mm.has_categories() && (
                mm.categories().cpu_models_size() > 0 ||
                mm.categories().gpu_primary_models_size() > 0 ||
                mm.categories().memory_clocks_size() > 0 ||
                mm.categories().memory_total_physicals_size() > 0);
            if (hasItems || hasCats) {
                LOG_INFO << "Successfully parsed as BenchmarkMenuResponse";
                QVariantList items; items.reserve(mm.items_size());
                for (const auto& it : mm.items()) {
                    QVariantMap im; im.insert("run_id", QString::fromStdString(it.run_id())); im.insert("label", QString::fromStdString(it.label())); im.insert("game", QString::fromStdString(it.game())); im.insert("map", QString::fromStdString(it.map())); items.push_back(im);
                }
                QVariantMap out; out.insert("items", items);
                if (mm.has_categories()) {
                    const auto& c = mm.categories();
                    QVariantMap cat;
                    QVariantList cpus; for (const auto& v : c.cpu_models()) cpus.push_back(QString::fromStdString(v));
                    QVariantList gpus; for (const auto& v : c.gpu_primary_models()) gpus.push_back(QString::fromStdString(v));
                    QVariantList mclocks; for (const auto& v : c.memory_clocks()) mclocks.push_back(QString::fromStdString(v));
                    QVariantList mtotals; for (const auto& v : c.memory_total_physicals()) mtotals.push_back(QString::fromStdString(v));
                    cat.insert("cpu_models", cpus);
                    cat.insert("gpu_primary_models", gpus);
                    cat.insert("memory_clocks", mclocks);
                    cat.insert("memory_total_physicals", mtotals);
                    // Additional debug logging to show what was parsed
                    LOG_INFO << "BenchmarkProtobufSerializer: categories parsed: cpu_models=" << cpus.size()
                             << ", gpu_primary_models=" << gpus.size()
                             << ", memory_clocks=" << mclocks.size()
                             << ", memory_total_physicals=" << mtotals.size();
                    auto logSampleList = [](const QVariantList& lst, const char* name) {
                        int n = lst.size();
                        QString sample;
                        int show = std::min(6, n);
                        for (int i = 0; i < show; ++i) {
                            if (!sample.isEmpty()) sample += ", ";
                            sample += lst[i].toString();
                        }
                        if (n > 0) LOG_INFO << "BenchmarkProtobufSerializer: sample " << name << "[0.." << show << "] = '" << sample.toStdString() << "'";
                    };
                    logSampleList(cpus, "cpu_models");
                    logSampleList(gpus, "gpu_primary_models");
                    logSampleList(mclocks, "memory_clocks");
                    logSampleList(mtotals, "memory_total_physicals");
                    out.insert("categories", cat);
                }
                r.data = out; r.success = true; return r;
            } else {
                LOG_INFO << "Parsed BenchmarkMenuResponse but it appears empty (no items/categories) - continuing";
            }
        }
        LOG_INFO << "Failed to parse as BenchmarkMenuResponse";
    } catch (const std::exception& e) {
        LOG_WARN << "Exception parsing as BenchmarkMenuResponse: " << e.what();
    } catch (...) {
        LOG_WARN << "Unknown exception parsing as BenchmarkMenuResponse";
    }
    try {
        LOG_INFO << "Trying to parse as PublicRunResponse...";
        checkmark::benchmarks::PublicRunResponse pr;
        if (pr.ParseFromArray(data.constData(), data.size())) {
            // Heuristic validation to avoid false positives on unrelated messages
            bool hasSamples = pr.samples_size() > 0;
            bool hasSummary = pr.has_summary();
            bool summaryHasData = false;
            if (hasSummary) {
                const auto& s = pr.summary();
                summaryHasData = (s.avg_fps() != 0.0) || (s.avg_frame_time_ms() != 0.0) ||
                                 !s.cpu_model().empty() || !s.gpu_primary_model().empty() ||
                                 !s.memory_clock().empty() || !s.memory_total_physical().empty();
            }
            if (!(hasSamples || summaryHasData)) {
                LOG_INFO << "PublicRunResponse parsed but lacks samples and summary data - likely false positive, continuing";
            } else {
                LOG_INFO << "Successfully parsed as PublicRunResponse";
                // Map PublicRunResponse -> QVariant
                QVariantMap out;
                // meta
                QVariantMap meta;
                if (pr.has_meta()) {
                    const auto& mm = pr.meta();
                    meta.insert("run_id", QString::fromStdString(mm.run_id()));
                    meta.insert("timestamp_utc", QString::fromStdString(mm.timestamp_utc()));
                    meta.insert("user_system_id", QString::fromStdString(mm.user_system_id()));
                    meta.insert("display_width", static_cast<int>(mm.display_width()));
                    meta.insert("display_height", static_cast<int>(mm.display_height()));
                }
                out.insert("meta", meta);

                // summary
                QVariantMap summary;
                if (hasSummary) {
                    const auto& s = pr.summary();
                    summary.insert("avg_fps", s.avg_fps());
                    summary.insert("avg_frame_time_ms", s.avg_frame_time_ms());
                    summary.insert("avg_gpu_usage_pct", s.avg_gpu_usage_pct());
                    summary.insert("avg_memory_load_pct", s.avg_memory_load_pct());
                    summary.insert("p1_low_fps_cumulative", s.p1_low_fps_cumulative());
                    summary.insert("p5_low_fps_cumulative", s.p5_low_fps_cumulative());
                    summary.insert("highest_frame_time_ms", s.highest_frame_time_ms());
                    summary.insert("cpu_model", QString::fromStdString(s.cpu_model()));
                    summary.insert("memory_total_physical", QString::fromStdString(s.memory_total_physical()));
                    summary.insert("memory_clock", QString::fromStdString(s.memory_clock()));
                    summary.insert("gpu_primary_model", QString::fromStdString(s.gpu_primary_model()));
                    summary.insert("graphics_resolution", QString::fromStdString(s.graphics_resolution()));
                }
                out.insert("summary", summary);

                // samples
                QVariantList samples;
                samples.reserve(pr.samples_size());
                for (const auto& sm : pr.samples()) {
                    QVariantMap row;
                    row.insert("time", static_cast<uint>(sm.time()));
                    row.insert("fps", sm.fps());
                    row.insert("frame_time_ms", sm.frame_time_ms());
                    row.insert("frame_time_variance", sm.frame_time_variance());
                    row.insert("highest_frame_time_ms", sm.highest_frame_time_ms());
                    row.insert("p1_high_frame_time_ms", sm.p1_high_frame_time_ms());
                    row.insert("p5_high_frame_time_ms", sm.p5_high_frame_time_ms());
                    row.insert("gpu_util_pct", sm.gpu_util_pct());
                    row.insert("gpu_usage_pct", sm.gpu_usage_pct());
                    row.insert("memory_load_pct", sm.memory_load_pct());
                    row.insert("memory_usage_mb", sm.memory_usage_mb());
                    row.insert("gpu_mem_used_bytes", static_cast<qulonglong>(sm.gpu_mem_used_bytes()));
                    row.insert("gpu_mem_total_bytes", static_cast<qulonglong>(sm.gpu_mem_total_bytes()));

                    // core usages
                    QVariantList cores;
                    cores.reserve(sm.core_usages_size());
                    for (const auto& cu : sm.core_usages()) {
                        QVariantMap c; c.insert("core_index", cu.core_index()); c.insert("usage_pct", cu.usage_pct()); cores.push_back(c);
                    }
                    if (!cores.isEmpty()) row.insert("core_usages", cores);
                    samples.push_back(row);
                }
                out.insert("samples", samples);
                r.data = out; r.success = true; return r;
            }
        }
        LOG_INFO << "Failed to parse as PublicRunResponse";
    } catch (const std::exception& e) {
        LOG_WARN << "Exception parsing as PublicRunResponse: " << e.what();
    } catch (...) {
        LOG_WARN << "Unknown exception parsing as PublicRunResponse";
    }
    try {
        LOG_INFO << "Trying to parse as BenchmarkMenuResponse...";
        checkmark::benchmarks::BenchmarkMenuResponse mm;
        if (mm.ParseFromArray(data.constData(), data.size())) {
            LOG_INFO << "Successfully parsed as BenchmarkMenuResponse";
            QVariantList items; items.reserve(mm.items_size());
            for (const auto& it : mm.items()) {
                QVariantMap im; im.insert("run_id", QString::fromStdString(it.run_id())); im.insert("label", QString::fromStdString(it.label())); im.insert("game", QString::fromStdString(it.game())); im.insert("map", QString::fromStdString(it.map())); items.push_back(im);
            }
            QVariantMap out; out.insert("items", items);
            // categories (if present)
            if (mm.has_categories()) {
                const auto& c = mm.categories();
                QVariantMap cat;
                QVariantList cpus; for (const auto& v : c.cpu_models()) cpus.push_back(QString::fromStdString(v));
                QVariantList gpus; for (const auto& v : c.gpu_primary_models()) gpus.push_back(QString::fromStdString(v));
                QVariantList mclocks; for (const auto& v : c.memory_clocks()) mclocks.push_back(QString::fromStdString(v));
                QVariantList mtotals; for (const auto& v : c.memory_total_physicals()) mtotals.push_back(QString::fromStdString(v));
                cat.insert("cpu_models", cpus);
                cat.insert("gpu_primary_models", gpus);
                cat.insert("memory_clocks", mclocks);
                cat.insert("memory_total_physicals", mtotals);
                // Additional debug logging to show what was parsed
                LOG_INFO << "BenchmarkProtobufSerializer: categories parsed: cpu_models=" << cpus.size()
                         << ", gpu_primary_models=" << gpus.size()
                         << ", memory_clocks=" << mclocks.size()
                         << ", memory_total_physicals=" << mtotals.size();
                auto logSampleList = [](const QVariantList& lst, const char* name) {
                    int n = lst.size();
                    QString sample;
                    int show = std::min(6, n);
                    for (int i = 0; i < show; ++i) {
                        if (!sample.isEmpty()) sample += ", ";
                        sample += lst[i].toString();
                    }
                    if (n > 0) LOG_INFO << "BenchmarkProtobufSerializer: sample " << name << "[0.." << show << "] = '" << sample.toStdString() << "'";
                };
                logSampleList(cpus, "cpu_models");
                logSampleList(gpus, "gpu_primary_models");
                logSampleList(mclocks, "memory_clocks");
                logSampleList(mtotals, "memory_total_physicals");
                out.insert("categories", cat);
            }
            r.data = out; r.success = true; return r;
        }
        LOG_INFO << "Failed to parse as BenchmarkMenuResponse";
    } catch (const std::exception& e) {
        LOG_WARN << "Exception parsing as BenchmarkMenuResponse: " << e.what();
    } catch (...) {
        LOG_WARN << "Unknown exception parsing as BenchmarkMenuResponse";
    }
    // (Removed duplicate LeaderboardResponse parsing block)

    // Try upload response last to minimize false positives across other message types
    try {
        LOG_INFO << "Trying to parse as BenchmarkUploadResponse...";
        checkmark::benchmarks::BenchmarkUploadResponse up;
        if (up.ParseFromArray(data.constData(), data.size())) {
            LOG_INFO << "Successfully parsed as BenchmarkUploadResponse";
            QVariantMap m; m.insert("accepted", up.accepted()); m.insert("run_id", QString::fromStdString(up.run_id())); m.insert("message", QString::fromStdString(up.message()));
            r.data = m; r.success = true; return r;
        }
        LOG_INFO << "Failed to parse as BenchmarkUploadResponse";
    } catch (const std::exception& e) {
        LOG_WARN << "Exception parsing as BenchmarkUploadResponse: " << e.what();
    } catch (...) {
        LOG_WARN << "Unknown exception parsing as BenchmarkUploadResponse";
    }

    // Fallback: raw bytes so caller can inspect/log
    LOG_WARN << "Failed to parse data as any known protobuf message type - returning raw bytes";
    r.success = true; r.data = data; return r;
}

bool BenchmarkProtobufSerializer::canSerialize(const QVariant& data) const {
    return data.canConvert<QByteArray>();
}
