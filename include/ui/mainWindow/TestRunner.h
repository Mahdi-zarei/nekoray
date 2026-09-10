#pragma once

#include <QHash>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QPair>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>

#ifndef Q_MOC_RUN
#include <core/server/gen/libcore.pb.h>
#endif

#include "include/database/entities/Profile.h"

class MainWindow;

// Not a QObject: queued signals would reorder the synchronous progress path.
class TestRunner {
public:
    explicit TestRunner(MainWindow* mw) : mw_(mw) {}

    TestRunner(const TestRunner&) = delete;
    TestRunner& operator=(const TestRunner&) = delete;

    void runUrlTests(const QList<int>& profileIDs, const std::function<void()>& onFinished = {});

    void runIpTests(const QList<int>& profileIDs);

    void runSpeedTests(const QList<int>& profileIDs, bool testCurrent = false);

    void stop();

    bool isRunning();

    bool isTestingCurrent() const { return testingCurrent_.load(); }

private:
    enum class LatencyKind { Url, Ip };

    struct Target {
        QString coreConfig;
        QString xrayConfig;
        QStringList xrayFullConfigs;
        QStringList outboundTags;
        QMap<QString, int> tag2entID;
        QString xrayDnsStrategy;
        int entID = -1;
        // Not derivable from an empty outboundTags: a test-current run leaves both empty but wants "proxy".
        bool useDefaultOutbound = false;
        bool testCurrent = false;
    };

    void runLatencyGroup(LatencyKind kind, const QList<int>& requestedIDs,
                         const std::function<void()>& onFinished);

    void runUrlProbe(const Target& target);

    void runIpProbe(const Target& target);

    void runSpeedProbe(const Target& target);

    void applyUrlResult(const std::shared_ptr<Configs::Profile>& ent, const libcore::URLTestResp& res,
                        const QHash<QString, bool>* vpnConnected = nullptr);

    void applyIpResult(const std::shared_ptr<Configs::Profile>& ent, const libcore::IPTestRes& res);

    QString contextName(int entID) const;

    bool staleGen(quint64 gen) const { return sessionGen_.load() != gen; }

    void pollSpeedTest(const QMap<QString, int>& tag2entID, bool testCurrent, quint64 gen);

    void pollCountryTest(const QMap<QString, int>& tag2entID, bool testCurrent, quint64 gen);

    void creditTraffic(const std::shared_ptr<Configs::Profile>& profile, const QString& tag,
                       qint64 curUp, qint64 curDown);

    MainWindow* mw_;

    // Held for a whole sweep, so it must never double as a per-batch latch.
    QMutex session_;
    // A poll thread is not joined, so a late tick must not drain the next batch.
    std::atomic<quint64> sessionGen_ = 0;
    std::atomic<bool> stopRequested_ = false;
    std::atomic<bool> testingCurrent_ = false;

    // Tests bypass the clash tracker, so their bytes are counted only here, diffed per tag.
    QMutex creditMu_;
    QHash<QString, QPair<qint64, qint64>> credited_;
};
