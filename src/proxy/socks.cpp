#include <include/proxy/socks.h>

namespace Proxy
{

    void to_json(json &j, const SocksProxy& prx)
    {
        to_json(j, static_cast<BaseProxy>(prx));
        j.push_back({"version", prx.version});
        j.push_back({"username", prx.username.toStdString()});
        j.push_back({"password", prx.password.toStdString()});
        j.push_back({"udp_over_tcp", prx.udpOverTcp});
    }

    void from_json(const json& j, SocksProxy& prx)
    {
        from_json(j, static_cast<BaseProxy&>(prx));
        prx.version = j.at("version").get<int>();
        prx.username = j.at("username").get<std::string>().c_str();
        prx.password = j.at("password").get<std::string>().c_str();
        prx.udpOverTcp = j.at("udp_over_tcp").get<bool>();
    }

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
