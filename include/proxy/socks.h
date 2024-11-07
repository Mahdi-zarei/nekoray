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
}
