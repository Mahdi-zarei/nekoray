#include <include/proxy/socks.h>

namespace Proxy
{
    QString SocksProxy::getExportLink()
    {
        // todo
        return "";
    }

    QJsonObject SocksProxy::buildProxy(QString const& tag)
    {
        QJsonObject res = {
            {"tag", tag},
            {"type", type},
            {"server", serverAddr},
            {"server_port", serverPort},
            {"version", version}
        };
        if (!username.isEmpty()) res["username"] = username;
        if (!password.isEmpty()) res["password"] = password;
        if (udpOverTcp) res["udp_over_tcp"] = udpOverTcp;
        if (tcpFastOpen) res["tcp_fast_open"] = tcpFastOpen;
        if (!domainStrategy.isEmpty()) res["domain_strategy"] = domainStrategy;

        return res;
    }
}
