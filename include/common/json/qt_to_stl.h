#pragma once
#include <qmap.h>
#include <QStringList>

#include "nlohmann/json.hpp"
using json = nlohmann::json;

namespace Common
{
    inline std::vector<std::string> QStringList2Vector(const QStringList& list)
    {
        auto res = std::vector<std::string>();
        for (const auto& QStr : list)
        {
            res.push_back(QStr.toStdString());
        }

        return res;
    }

    inline std::vector<int> QIntList2Vector(const QList<int>& list)
    {
        auto res = std::vector<int>();
        for (const auto& num : list)
        {
            res.push_back(num);
        }

        return res;
    }

    inline std::map<std::string, std::string> QMapStrStr2Map(const QMap<QString, QString>& m)
    {
        auto res = std::map<std::string, std::string>();
        for (const auto& key : m)
        {
            res.insert(key.toStdString(), m[key].toStdString());
        }

        return res;
    }
}
