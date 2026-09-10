#pragma once

#include <QSet>

namespace Configs
{
    // Every profile the running config was built from: the started one, its chain hops, the group's
    // front/landing proxies, route-profile outbounds and endpoint hops, auto-selector members.
    void SetRunningProfiles(const QSet<int> &profileIDs);

    void ClearRunningProfiles();

    bool RunningUsesProfile(int profileID);
}
