#pragma once
#include <qmap.h>
#include <QString>

#include "include/global.h"

namespace Proxy
{
    class BaseProxy;
}

namespace Database
{
    class ProfileStore
    {
        QMap<QString, std::shared_ptr<Proxy::BaseProxy>> proxies;

        int lastID = 0;

        QString path = Constants::DefaultPaths::ProfilesPath;

    public:
        ProfileStore();

        void LoadProfiles(QString* error);

        bool SaveProfile(const std::shared_ptr<Proxy::BaseProxy>& proxy, QString* error);

        std::shared_ptr<Proxy::BaseProxy> GetProfile(const QString& ID);

        bool DeleteProfile(QString* error, const QString& ID);
    };
}
