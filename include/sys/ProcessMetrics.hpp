#pragma once

#include <QHash>
#include <QtGlobal>

namespace Sys {
    // CPU% is a rate normalized by the logical CPU count (100 = the whole machine); the first sample for a pid only seeds the baseline.
    class ProcessMetrics {
    public:
        struct Sample {
            bool ok = false;         // false when the pid could not be queried
            qint64 rssBytes = 0;     // bytes: Windows private working set, macOS phys_footprint, Linux RSS
            double cpuPercent = 0.0; // 0..100 of total machine CPU; 0 on first sample
        };

        // 0/negative pid -> not ok; a reused pid reads 0 for that tick via the non-negative-delta guard.
        Sample sample(qint64 pid);

        // Drops the CPU baseline; the next sample per pid reports 0% and re-seeds.
        void reset() { prior_.clear(); }

    private:
        struct Prior {
            quint64 cpuTimeNs = 0; // cumulative process CPU time (user+kernel), ns
            qint64 wallNs = 0;     // monotonic wall clock at that sample, ns
        };
        QHash<qint64, Prior> prior_;
    };
} // namespace Sys
