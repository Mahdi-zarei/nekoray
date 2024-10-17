#pragma once
#include <QString>
#include <QStringList>

namespace NekoGui {
    namespace DomainMatcher {
        enum DomainMatcher {
            DEFAULT,
            MPH,
        };
    }

    namespace SniffingMode {
        enum SniffingMode {
            DISABLE,
            FOR_ROUTING,
            FOR_DESTINATION,
        };
    }

    namespace CoreType {
        enum CoreType {
            SING_BOX,
        };
    }

    namespace Information {
        inline QString HijackInfo = "Listens on the given addr:port (on Windows, port is always 53) and redirects the requests to the DNS module. Domains that match the rules will have their requests hijacked and the A and AAAA queries will be responded with the Inet4 response and Inet6 response respectively.\nThe Redirect settings sets up an inbound that listens on the given addr:port, sniffs the destination if possible and redirects the requests to their true destination.\nThe use case of these settings is apps that do not respect the system proxy for resolving their DNS requests (one such example is discord), You can hijack their DNS requests to 127.0.0.1 and then route them through the Nekoray tunnel. The same effect could be achieved using Tun mode, but one may not want to tunnel the whole device (For example when Gaming), this is where DNS hijack can transparently handle things.\n\nCurrently you can Automatically set the System DNS in windows.";
    }

namespace GeoAssets {
    inline QStringList GeoIPURLs = {"https://github.com/SagerNet/sing-geoip/releases/latest/download/geoip.db", "https://raw.githubusercontent.com/Chocolate4U/Iran-sing-box-rules/release/geoip.db"};
    inline QStringList GeoSiteURLs = {"https://github.com/SagerNet/sing-geosite/releases/latest/download/geosite.db", "https://raw.githubusercontent.com/Chocolate4U/Iran-sing-box-rules/release/geosite.db"};
    }
} // namespace NekoGui
