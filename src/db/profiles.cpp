#include <QDir>
#include <include/db/profiles.h>

#include "include/db/parse.h"
#include "include/proxy/base.h"

namespace Database
{
    ProfileStore::ProfileStore()
    {
        QDir().mkdir(Constants::DefaultPaths::ProfilesPath);
        path = Constants::DefaultPaths::ProfilesPath;
    }

    void ProfileStore::LoadProfiles(QString* error)
    {
        auto dir = QDir(path);
        if (!dir.exists())
        {
            *error = "Dir not found";
            return;
        }
        for (const auto& entry : dir.entryList(QDir::Files))
        {
            auto f = QFile(entry);
            auto data = f.readAll();
            json j = json::parse(data);
            if (!j.contains("id"))
            {
                *error += "No id in json\n";
                continue;
            }
            proxies[QString(j["id"].get<std::string>().c_str())] = ParseFromJson(j);
        }
    }

    bool ProfileStore::SaveProfile(const std::shared_ptr<Proxy::BaseProxy>& proxy, QString* error)
    {
        if (proxy->id.isEmpty())
        {
            *error = "No id in proxy";
            return false;
        }
        auto fileName = proxy->id + ".json";
        QFile file(path + "/" +fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            *error = "Failed to open file:" + file.errorString();
            return false;
        }

        auto j = MarshallToJson(proxy);
        file.write(std::string(j).c_str());

        file.close();
        proxies[proxy->id] = proxy;
        return true;
    }

    std::shared_ptr<Proxy::BaseProxy> ProfileStore::GetProfile(const QString& ID)
    {
        if (!proxies.contains(ID)) return nullptr;
        return proxies[ID];
    }

    bool ProfileStore::DeleteProfile(QString* error, const QString& ID)
    {
        if (!proxies.contains(ID))
        {
            *error = "Invalid ID";
            return false;
        }
        if (!QFile::remove(path + "/" + ID + ".json"))
        {
            *error = QString("Failed to delete file:" + path + "/" + ID + ".json");
            return false;
        }
        proxies.remove(ID);
        return true;
    }

}
