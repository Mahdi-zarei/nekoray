#pragma once

#include <QString>

namespace LocalNetwork {
    // True while the mixed inbound is bound somewhere other than loopback, i.e. reachable from the LAN.
    bool LanInboundEnabled();

    // True only for a wildcard bind; an explicit bind address is already the one clients dial.
    bool LanInboundIsWildcard();

    // Address of the default route's interface, never our own tun; empty when none can be determined.
    QString LanAddress();

    // True for loopback and for every address this machine currently holds, tun addresses included.
    bool IsOwnAddress(const QString &host);
}
