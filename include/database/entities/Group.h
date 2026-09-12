#pragma once
#include <QDateTime>
#include <QList>
#include <QMutex>
#include <QString>
#include <algorithm>

#include "include/ui/group/GroupSort.hpp"

namespace Configs
{
    struct SubUserInfo {
        bool valid = false;
        qint64 upload = 0;       // Bytes
        qint64 download = 0;     // Bytes
        qint64 total = 0;        // Bytes (0 = unlimited)
        qint64 expire = 0;       // Unix epoch seconds (0 = no expiry)
        QString title;           // Profile Title (e.g. "WindyDay")
        QString web_url;         // Website / Dashboard URL
        QString support_url;     // Support / Telegram URL
        QString announce;        // Announcement text

        [[nodiscard]] qint64 used() const { return upload + download; }
        [[nodiscard]] qint64 remaining() const { return (total > used()) ? (total - used()) : 0; }
        [[nodiscard]] double percentUsed() const {
            if (total <= 0) return 0.0;
            return std::clamp((static_cast<double>(used()) / static_cast<double>(total)) * 100.0, 0.0, 100.0);
        }
        [[nodiscard]] bool isExpired() const {
            if (expire <= 0) return false;
            return QDateTime::currentSecsSinceEpoch() > expire;
        }
    };

    SubUserInfo ParseSubUserInfo(const QString &info);

    enum class testBy : int {
        latency = 0,
        dlSpeed,
        ulSpeed,
        ipOut
    };

    enum class testShowItems : int {
        all = 0,
        none,
        ipOnly,
        speedOnly
    };

    enum class trafficBy : int {
        total = 0,
        dl,
        ul
    };

    enum class typeBy : int {
        byType = 0,
        bySecurity
    };

    class Group {
    public:
        QMutex mutex;
        int id = -1;
        bool archive = false;
        bool skip_auto_update = false;
        bool auto_clear_unavailable = false;
        QString name = "";
        QString url = "";
        QString info = "";
        qint64 sub_last_update = 0;
        int sub_update_interval = 0; // In hours (0 = Default/Auto, >0 = custom)
        int front_proxy_id = -1;
        int landing_proxy_id = -1;

        QList<int> column_width;
        QList<int> calculated_column_width; // memory only, no need to save to db
        QList<int> profiles;
        int scroll_last_profile = -1;
        testBy test_sort_by = testBy::latency;
        trafficBy traffic_sort_by = trafficBy::total;
        typeBy type_sort_by = typeBy::byType;
        testShowItems test_items_to_show = testShowItems::all;
        // Memory only. Pairs of (profileID, row as displayed).
        QList<std::pair<int, int>> selectedProfilesIdIdxPairs;

        Group() = default;

        [[nodiscard]] SubUserInfo GetSubUserInfo() const { return ParseSubUserInfo(info); }

        void clearCalculatedColumnWidth();

        [[nodiscard]] QList<int> Profiles() const;

        bool SortProfiles(GroupSortAction method);

        bool RemoveProfile(int ID);

        bool RemoveProfileBatch(const QList<int>& IDs);

        bool AddProfile(int ID);

        bool AddProfileBatch(const QList<int>& IDs);

        bool SwapProfiles(int idx1, int idx2);

        bool EmplaceProfile(int idx, int newIdx);

        [[nodiscard]] bool HasProfile(int ID) const;
    };
}// namespace Configs