#include <QThread>
#include <QDateTime>
#include <core/server/gen/libcore.pb.h>
#include <include/api/RPC.h>
#include "include/ui/mainwindow_interface.h"
#include <include/stats/connections/connectionLister.hpp>
#include "include/stats/traffic/TrafficStatsManager.hpp"
#include "include/global/LocalNetwork.hpp"

#include <algorithm>



namespace Stats
{
    // Guards out-of-band ForceUpdate() polls: a tiny byte delta over a tiny interval reads as a spike.
    static constexpr qint64 kSpeedSampleMinMs = 500;

    // Off-view polls still run: they keep the per-app traffic stats sampled.
    static constexpr unsigned long kActivePollMs = 1000;
    static constexpr unsigned long kRelaxedPollMs = 5000;

    ConnectionLister* connection_lister = new ConnectionLister();

    void ConnectionLister::ForceUpdate()
    {
        QMutexLocker wlk(&waitMu_);
        forced_ = true;
        waitCond_.wakeAll();
    }


    void ConnectionLister::Loop()
    {
        while (true)
        {
            if (stop) return;

            bool forced = false;
            {
                QMutexLocker wlk(&waitMu_);
                if (!forced_) waitCond_.wait(&waitMu_, inView_.load() ? kActivePollMs : kRelaxedPollMs);
                forced = forced_;
                forced_ = false;
            }

            if (stop) return;
            // A forced poll is a user action on the table, so it runs even while stats are off.
            if (!forced && (suspend || !Configs::dataManager->settingsRepo->enable_stats)) continue;

            mu.lock();
            update(forced || inView_.load());
            mu.unlock();
        }
    }

    void ConnectionLister::SetInView(bool inView)
    {
        const bool was = inView_.exchange(inView);
        if (inView && !was)
        {
            QMutexLocker wlk(&waitMu_);
            waitCond_.wakeAll();
        }
    }

    // The core sends M.Socksaddr.String(): "1.2.3.4:5678" or "[fe80::1]:5678", so an unbracketed form must split at the last colon.
    static QString endpointHost(const QString& endpoint)
    {
        if (endpoint.startsWith('['))
        {
            const auto close = endpoint.indexOf(']');
            return close > 0 ? endpoint.mid(1, close - 1) : endpoint.mid(1);
        }
        const auto colon = endpoint.lastIndexOf(':');
        return colon < 0 ? endpoint : endpoint.left(colon);
    }

    static ConnectionMetadata metaFromProto(const libcore::ConnectionMetaData& conn, const QString& localLabel)
    {
        ConnectionMetadata c;
        c.id = QString::fromStdString(conn.id.value());
        c.createdAtMs = conn.created_at.value();
        c.dest = QString::fromStdString(conn.dest.value());
        c.upload = conn.upload.value();
        c.download = conn.download.value();
        c.domain = QString::fromStdString(conn.domain.value());
        c.network = QString::fromStdString(conn.network.value());
        c.outbound = QString::fromStdString(conn.outbound.value());
        c.process = QString::fromStdString(conn.process.value());
        c.processPath = QString::fromStdString(conn.process_path.value());
        c.protocol = QString::fromStdString(conn.protocol.value());
        c.closedAtMs = conn.closed_at.value();
        c.source = QString::fromStdString(conn.source.value());
        // In tun mode our own traffic enters with the tun's (or this machine's LAN) address, so a loopback-only test would label it a LAN client.
        const QString host = endpointHost(c.source);
        c.sourceDisplay = host.isEmpty() || LocalNetwork::IsOwnAddress(host) ? localLabel : host;
        return c;
    }

    namespace
    {
        // The core iterates a map, so orderings must be total or equal keys reshuffle every poll.
        template <typename Key>
        void sortLargestFirst(QList<ConnectionMetadata>& list, bool asc, Key key)
        {
            std::sort(list.begin(), list.end(), [&](const ConnectionMetadata& a, const ConnectionMetadata& b)
            {
                const auto& ka = key(a);
                const auto& kb = key(b);
                if (ka == kb) return asc ? a.id > b.id : a.id < b.id;
                return asc ? ka < kb : ka > kb;
            });
        }

        // Text columns read the other way round: unflipped means A→Z, not Z→A.
        template <typename Key>
        void sortSmallestFirst(QList<ConnectionMetadata>& list, bool asc, Key key)
        {
            std::sort(list.begin(), list.end(), [&](const ConnectionMetadata& a, const ConnectionMetadata& b)
            {
                const auto& ka = key(a);
                const auto& kb = key(b);
                if (ka == kb) return asc ? a.id > b.id : a.id < b.id;
                return asc ? ka > kb : ka < kb;
            });
        }
    }

    void ConnectionLister::update(const bool pushToUi)
    {
        libcore::QueryConnectionsResp resp = API::defaultClient->QueryConnections();
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const QString localLabel = QObject::tr("Local");

        QList<ConnectionMetadata> rows;
        rows.reserve(static_cast<qsizetype>(resp.active.size()));
        QHash<QString, SpeedSample> newSamples;
        newSamples.reserve(static_cast<qsizetype>(resp.active.size()));

        for (const auto& conn : resp.active)
        {
            auto c = metaFromProto(conn, localLabel);

            SpeedSample s;
            if (const auto it = speedSamples_.constFind(c.id); it != speedSamples_.constEnd())
            {
                const qint64 dt = nowMs - it->sampledAtMs;
                if (dt >= kSpeedSampleMinMs)
                {
                    qint64 dUp = c.upload - it->upload;
                    qint64 dDown = c.download - it->download;
                    if (dUp < 0) dUp = 0; // counters only grow; guard against any reset
                    if (dDown < 0) dDown = 0;
                    s.upload = c.upload;
                    s.download = c.download;
                    s.sampledAtMs = nowMs;
                    s.upSpeed = dUp * 1000 / dt;
                    s.downSpeed = dDown * 1000 / dt;
                }
                else
                {
                    s = *it; // window too short: keep last rate and baseline
                }
            }
            else
            {
                s.upload = c.upload; // first sighting: seed baseline, no rate yet
                s.download = c.download;
                s.sampledAtMs = nowMs;
            }
            newSamples.insert(c.id, s);
            c.uploadSpeed = s.upSpeed;
            c.downloadSpeed = s.downSpeed;

            rows.append(std::move(c));
        }
        speedSamples_ = newSamples; // drop ids for connections that have closed

        // Credits the live set plus the recently-closed ring (deduped by id), so a connection that opened and closed between polls still counts.
        if (!Configs::dataManager->settingsRepo->disable_traffic_stats)
        {
            QHash<QString, QPair<qint64, qint64>> newLast;
            QSet<QString> currentClosed;

            auto credit = [&](const libcore::ConnectionMetaData& cm, qint64 curUp, qint64 curDown)
            {
                const QString id = QString::fromStdString(cm.id.value());
                qint64 baseUp = 0, baseDown = 0;
                if (const auto it = lastBytes_.constFind(id); it != lastBytes_.constEnd())
                {
                    baseUp = it->first;
                    baseDown = it->second;
                }
                qint64 dUp = curUp - baseUp;
                qint64 dDown = curDown - baseDown;
                if (dUp < 0) dUp = 0;
                if (dDown < 0) dDown = 0;
                if (dUp == 0 && dDown == 0) return;
                QString name = QString::fromStdString(cm.process.value());
                if (name.isEmpty()) name = "Unknown";
                trafficStatsManager->AddAppDelta(name, QString::fromStdString(cm.process_path.value()), dUp, dDown);
            };

            for (const auto& cm : resp.active)
            {
                const qint64 up = cm.upload.value();
                const qint64 down = cm.download.value();
                credit(cm, up, down);
                newLast.insert(QString::fromStdString(cm.id.value()), {up, down});
            }
            for (const auto& cm : resp.closed)
            {
                const QString id = QString::fromStdString(cm.id.value());
                currentClosed.insert(id);
                if (accountedClosed_.contains(id)) continue;
                credit(cm, cm.upload.value(), cm.download.value());
            }
            lastBytes_ = newLast;             // drop evicted / now-closed live ids
            accountedClosed_ = currentClosed; // everything in the ring is accounted
        }

        if (!pushToUi) return;

        switch (sort)
        {
        // Oldest first, matching how rows used to accumulate. `asc` is ignored: this means "unsorted".
        case Default:
            sortSmallestFirst(rows, false, [](const ConnectionMetadata& c) { return c.createdAtMs; });
            break;
        case ByDownload:
            sortLargestFirst(rows, asc, [](const ConnectionMetadata& c) { return c.download; });
            break;
        case ByUpload:
            sortLargestFirst(rows, asc, [](const ConnectionMetadata& c) { return c.upload; });
            break;
        case ByTraffic:
            sortLargestFirst(rows, asc, [](const ConnectionMetadata& c) { return c.upload + c.download; });
            break;
        case ByDownloadSpeed:
            sortLargestFirst(rows, asc, [](const ConnectionMetadata& c) { return c.downloadSpeed; });
            break;
        case ByUploadSpeed:
            sortLargestFirst(rows, asc, [](const ConnectionMetadata& c) { return c.uploadSpeed; });
            break;
        case BySpeed:
            sortLargestFirst(rows, asc, [](const ConnectionMetadata& c) { return c.uploadSpeed + c.downloadSpeed; });
            break;
        case ByProcess:
            sortSmallestFirst(rows, asc, [](const ConnectionMetadata& c) -> const QString& { return c.process; });
            break;
        case ByOutbound:
            sortSmallestFirst(rows, asc, [](const ConnectionMetadata& c) -> const QString& { return c.outbound; });
            break;
        case ByProtocol:
            sortSmallestFirst(rows, asc, [](const ConnectionMetadata& c) -> const QString& { return c.protocol; });
            break;
        case BySource:
            sortSmallestFirst(rows, asc, [](const ConnectionMetadata& c) -> const QString& { return c.sourceDisplay; });
            break;
        }

        runOnUiThread([rows = std::move(rows)] {
            if (auto* m = GetMainWindow()) m->UpdateConnectionList(rows);
        });
    }

    void ConnectionLister::stopLoop()
    {
        stop = true;
        QMutexLocker wlk(&waitMu_);
        waitCond_.wakeAll();
    }

    void ConnectionLister::setSort(const ConnectionSort newSort)
    {
        if (sort == newSort) asc = !asc;
        else
        {
            sort = newSort;
            asc = false;
        }
    }

    void ConnectionLister::restoreSort(const ConnectionSort newSort, const bool ascending)
    {
        sort = newSort;
        asc = ascending;
    }

}
