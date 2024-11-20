#pragma once

#include "base.h"

namespace Proxy
{
    class SocksProxy : public BaseProxy
    {
    public:
        int version = 5;
        bool udpOverTcp = false;

        // Auth
        QString username;
        QString password;

        QString getExportLink() override;

        QJsonObject buildProxy(QString const& tag) override;
    };

    inline void to_json(json &j, const SocksProxy& prx)
    {
        to_json(j, static_cast<BaseProxy>(prx));
        j.push_back({"version", prx.version});
        j.push_back({"username", prx.username.toStdString()});
        j.push_back({"password", prx.password.toStdString()});
        j.push_back({"udp_over_tcp", prx.udpOverTcp});
    }

    inline void from_json(const json& j, SocksProxy& prx)
    {
        from_json(j, static_cast<BaseProxy&>(prx));
        prx.version = j.at("version").get<int>();
        prx.username = j.at("username").get<std::string>().c_str();
        prx.password = j.at("password").get<std::string>().c_str();
        prx.udpOverTcp = j.at("udp_over_tcp").get<bool>();
    }
}
