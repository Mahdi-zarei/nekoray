#include "include/global/RunningProfiles.hpp"

#include <QMutex>
#include <QMutexLocker>

namespace Configs {
    namespace {
        QMutex runningProfilesMu;
        QSet<int> runningProfiles;
    }

    void SetRunningProfiles(const QSet<int> &profileIDs)
    {
        QMutexLocker lk(&runningProfilesMu);
        runningProfiles.clear();
        for (const auto id : profileIDs) {
            if (id < 0) continue;
            runningProfiles.insert(id);
        }
    }

    void ClearRunningProfiles()
    {
        QMutexLocker lk(&runningProfilesMu);
        runningProfiles.clear();
    }

    bool RunningUsesProfile(int profileID)
    {
        if (profileID < 0) return false;
        QMutexLocker lk(&runningProfilesMu);
        return runningProfiles.contains(profileID);
    }
}
