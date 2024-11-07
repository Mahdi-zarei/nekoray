#pragma once
#include <qmap.h>

namespace Common
{
    inline QMap<QString, QString> Map2QMapQStrQStr(const std::map<std::string, std::string>& m)
    {
        QMap<QString, QString> res;
        for (const auto& [key, val] : m)
        {
            res.insert(QString(key.c_str()), QString(val.c_str()));
        }
        return res;
    }

    inline QStringList Vector2QStringList(const std::vector<std::string>& v)
    {
        QStringList res;
        for (const auto& str : v)
        {
            res.append(str.c_str());
        }

        return res;
    }

    inline QList<int> Vector2QListInt(const std::vector<int>& v)
    {
        QList<int> res;
        for (const int num : v)
        {
            res.append(num);
        }

        return res;
    }
}
