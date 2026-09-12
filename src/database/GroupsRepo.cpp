#include "include/database/entities/Group.h"
#include "include/database/GroupsRepo.h"
#include "include/global/Utils.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include <QMutexLocker>
#include <QSet>

#include "include/database/ProfilesRepo.h"
#include "include/global/Configs.hpp"
#include "include/ui/mainwindow.h"


namespace Configs {
    GroupsRepo::GroupsRepo(Database& database) : db(database) {
        createTables();
    }

void GroupsRepo::createTables() const {
        db.exec(R"(
            CREATE TABLE IF NOT EXISTS groups (
                id INTEGER PRIMARY KEY,
                archive INTEGER NOT NULL DEFAULT 0,
                skip_auto_update INTEGER NOT NULL DEFAULT 0,
                name TEXT NOT NULL DEFAULT '',
                url TEXT,
                info TEXT,
                sub_last_update INTEGER NOT NULL DEFAULT 0,
                sub_update_interval INTEGER NOT NULL DEFAULT 0,
                front_proxy_id INTEGER NOT NULL DEFAULT -1,
                landing_proxy_id INTEGER NOT NULL DEFAULT -1,
                column_width_json TEXT,
                profiles_json TEXT NOT NULL DEFAULT '[]',
                scroll_last_profile INTEGER NOT NULL DEFAULT -1,
                auto_clear_unavailable INTEGER NOT NULL DEFAULT 0,
                test_sort_by INTEGER NOT NULL DEFAULT 0,
                traffic_sort_by INTEGER NOT NULL DEFAULT 0,
                test_items_to_show INTEGER NOT NULL DEFAULT 0,
                type_sort_by INTEGER NOT NULL DEFAULT 0,
                created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
                updated_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
            )
        )");
        // Migrate existing databases created before type_sort_by or sub_update_interval was added.
        if (!groupsColumnExists("type_sort_by"))
            db.exec("ALTER TABLE groups ADD COLUMN type_sort_by INTEGER NOT NULL DEFAULT 0");
        if (!groupsColumnExists("sub_update_interval"))
            db.exec("ALTER TABLE groups ADD COLUMN sub_update_interval INTEGER NOT NULL DEFAULT 0");

        db.exec(R"(
            CREATE TABLE IF NOT EXISTS groups_order (
                group_id INTEGER NOT NULL PRIMARY KEY,
                display_order INTEGER NOT NULL
            )
        )");
    }

    bool GroupsRepo::groupsColumnExists(const char* columnName) const {
        auto pragma = db.query("PRAGMA table_info(groups)");
        if (!pragma) return false;
        while (pragma->executeStep()) {
            if (pragma->getColumn(1).getText() == std::string(columnName)) return true;
        }
        return false;
    }

    QJsonObject GroupsRepo::groupToJson(const Group* group) const {
        QJsonObject json;
        
        json["id"] = group->id;
        json["archive"] = group->archive;
        json["skip_auto_update"] = group->skip_auto_update;
        json["auto_clear_unavailable"] = group->auto_clear_unavailable;
        json["name"] = group->name;
        json["url"] = group->url;
        json["info"] = group->info;
        json["sub_last_update"] = static_cast<qint64>(group->sub_last_update);
        json["sub_update_interval"] = group->sub_update_interval;
        json["front_proxy_id"] = group->front_proxy_id;
        json["landing_proxy_id"] = group->landing_proxy_id;
        json["column_width"] = QListInt2QJsonArray(group->column_width);
        json["profiles"] = QListInt2QJsonArray(group->profiles);
        json["scroll_last_profile"] = group->scroll_last_profile;
        json["test_sort_by"] = static_cast<int>(group->test_sort_by);
        json["traffic_sort_by"] = static_cast<int>(group->traffic_sort_by);
        json["type_sort_by"] = static_cast<int>(group->type_sort_by);
        json["test_items_to_show"] = static_cast<int>(group->test_items_to_show);

        return json;
    }

    std::shared_ptr<Group> GroupsRepo::groupFromJson(const QJsonObject& json) const {
        auto group = std::make_shared<Group>();
        
        group->id = json["id"].toInt();
        group->archive = json["archive"].toBool();
        group->skip_auto_update = json["skip_auto_update"].toBool();
        group->auto_clear_unavailable = json["auto_clear_unavailable"].toBool();
        group->name = json["name"].toString();
        group->url = json["url"].toString();
        group->info = json["info"].toString();
        group->sub_last_update = json["sub_last_update"].toVariant().toLongLong();
        group->sub_update_interval = json["sub_update_interval"].toInt(0);
        group->front_proxy_id = json["front_proxy_id"].toInt();
        group->landing_proxy_id = json["landing_proxy_id"].toInt();
        group->column_width = QJsonArray2QListInt(json["column_width"].toArray());
        group->profiles = QJsonArray2QListInt(json["profiles"].toArray());
        group->scroll_last_profile = json["scroll_last_profile"].toInt(-1);
        group->test_sort_by = static_cast<testBy>(json["test_sort_by"].toInt(0));
        group->traffic_sort_by = static_cast<trafficBy>(json["traffic_sort_by"].toInt(0));
        group->type_sort_by = static_cast<typeBy>(json["type_sort_by"].toInt(0));
        group->test_items_to_show = static_cast<testShowItems>(json["test_items_to_show"].toInt(0));
        
        return group;
    }

    void GroupsRepo::saveToDatabase(const Group* group, int id) const {
        QJsonArray columnWidthArray = QListInt2QJsonArray(group->column_width);
        QJsonArray profilesArray = QListInt2QJsonArray(group->profiles);
        
        QJsonDocument columnWidthDoc(columnWidthArray);
        QJsonDocument profilesDoc(profilesArray);
        
        QString columnWidthJson = QString::fromUtf8(columnWidthDoc.toJson(QJsonDocument::Compact));
        QString profilesJson = QString::fromUtf8(profilesDoc.toJson(QJsonDocument::Compact));
        
        db.exec(R"(
            INSERT INTO groups
            (id, archive, skip_auto_update, auto_clear_unavailable, name, url, info, sub_last_update, sub_update_interval,
             front_proxy_id, landing_proxy_id,
             column_width_json, profiles_json, scroll_last_profile, test_sort_by, traffic_sort_by, test_items_to_show,
             type_sort_by)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(id) DO UPDATE SET
                archive = excluded.archive, skip_auto_update = excluded.skip_auto_update,
                auto_clear_unavailable = excluded.auto_clear_unavailable, name = excluded.name,
                url = excluded.url, info = excluded.info, sub_last_update = excluded.sub_last_update,
                sub_update_interval = excluded.sub_update_interval,
                front_proxy_id = excluded.front_proxy_id, landing_proxy_id = excluded.landing_proxy_id,
                column_width_json = excluded.column_width_json, profiles_json = excluded.profiles_json,
                scroll_last_profile = excluded.scroll_last_profile, test_sort_by = excluded.test_sort_by,
                traffic_sort_by = excluded.traffic_sort_by, test_items_to_show = excluded.test_items_to_show,
                type_sort_by = excluded.type_sort_by,
                updated_at = strftime('%s', 'now')
        )",
            id,
            group->archive ? 1 : 0,
            group->skip_auto_update ? 1 : 0,
            group->auto_clear_unavailable ? 1 : 0,
            group->name.toStdString(),
            group->url.toStdString(),
            group->info.toStdString(),
            static_cast<long long>(group->sub_last_update),
            group->sub_update_interval,
            group->front_proxy_id,
            group->landing_proxy_id,
            columnWidthJson.toStdString(),
            profilesJson.toStdString(),
            group->scroll_last_profile,
            static_cast<int>(group->test_sort_by),
            static_cast<int>(group->traffic_sort_by),
            static_cast<int>(group->test_items_to_show),
            static_cast<int>(group->type_sort_by)
        );
    }

    std::shared_ptr<Group> GroupsRepo::loadFromDatabase(int id) const {
        auto query = db.query(R"(
            SELECT id, archive, skip_auto_update, auto_clear_unavailable, name, url, info, sub_last_update, sub_update_interval,
                   front_proxy_id, landing_proxy_id,
                   column_width_json, profiles_json, scroll_last_profile, test_sort_by, traffic_sort_by, test_items_to_show,
                   type_sort_by
            FROM groups WHERE id = ?
        )", id);
        if (!query || !query->executeStep()) {
            return nullptr;
        }
        
        QJsonObject json;
        json["id"] = query->getColumn(0).getInt();
        json["archive"] = query->getColumn(1).getInt() != 0;
        json["skip_auto_update"] = query->getColumn(2).getInt() != 0;
        json["auto_clear_unavailable"] = query->getColumn(3).getInt() != 0;
        json["name"] = QString::fromStdString(query->getColumn(4).getText());
        json["url"] = QString::fromStdString(query->getColumn(5).getText());
        json["info"] = QString::fromStdString(query->getColumn(6).getText());
        json["sub_last_update"] = static_cast<qint64>(query->getColumn(7).getInt64());
        json["sub_update_interval"] = query->getColumn(8).getInt();
        json["front_proxy_id"] = query->getColumn(9).getInt();
        json["landing_proxy_id"] = query->getColumn(10).getInt();

        QString columnWidthJsonStr = QString::fromStdString(query->getColumn(11).getText());
        if (!columnWidthJsonStr.isEmpty()) {
            QJsonDocument columnWidthDoc = QJsonDocument::fromJson(columnWidthJsonStr.toUtf8());
            if (!columnWidthDoc.isNull() && columnWidthDoc.isArray()) {
                json["column_width"] = columnWidthDoc.array();
            }
        }
        
        QString profilesJsonStr = QString::fromStdString(query->getColumn(12).getText());
        if (!profilesJsonStr.isEmpty()) {
            QJsonDocument profilesDoc = QJsonDocument::fromJson(profilesJsonStr.toUtf8());
            if (!profilesDoc.isNull() && profilesDoc.isArray()) {
                json["profiles"] = profilesDoc.array();
            }
        }

        json["scroll_last_profile"] = query->getColumn(13).getInt();
        json["test_sort_by"] = query->getColumn(14).getInt();
        json["traffic_sort_by"] = query->getColumn(15).getInt();
        json["test_items_to_show"] = query->getColumn(16).getInt();
        json["type_sort_by"] = query->getColumn(17).getInt();

        auto group = groupFromJson(json);
        // Refreshes could map several identical servers onto one id, leaving it in the persisted list once per server (#1775).
        QSet<int> seen;
        QList<int> unique;
        unique.reserve(group->profiles.size());
        for (int pid : group->profiles) {
            if (seen.contains(pid)) continue;
            seen.insert(pid);
            unique.append(pid);
        }
        if (unique.size() != group->profiles.size()) {
            group->profiles = std::move(unique);
            // The caller (GetGroup) already holds the mutex, so write straight through.
            saveToDatabase(group.get(), group->id);
        }
        return group;
    }

    std::shared_ptr<Group> GroupsRepo::NewGroup() {
        return std::make_shared<Group>();
    }

    bool GroupsRepo::AddGroup(std::shared_ptr<Group>& group) {
        QMutexLocker locker(&mutex);
        if (group->id >= 0) return false;
        int newId = NewGroupID();
        group->id = newId;
        memMap[newId] = group;
        saveToDatabase(group.get(), group->id);
        int maxOrder = -1;
        auto maxOrderQuery = db.query("SELECT MAX(display_order) FROM groups_order");
        if (maxOrderQuery && maxOrderQuery->executeStep()) {
            maxOrder = maxOrderQuery->getColumn(0).getInt();
        }
        db.exec("INSERT INTO groups_order (group_id, display_order) VALUES (?, ?)",
            group->id,
            maxOrder + 1
        );
        return true;
    }

    std::shared_ptr<Group> GroupsRepo::GetGroup(int id) const {
        QMutexLocker locker(&mutex);
        if (auto it = memMap.find(id); it != memMap.end()) {
            return it->second;
        }
        auto group = loadFromDatabase(id);
        if (!group) return nullptr;
        memMap[id] = group;
        return group;
    }

    std::shared_ptr<Group> GroupsRepo::CurrentGroup() const {
        if (!Configs::dataManager || !Configs::dataManager->settingsRepo) {
            return nullptr;
        }
        
        int currentGroupId = Configs::dataManager->settingsRepo->current_group;
        
        return GetGroup(currentGroupId);
    }

    void GroupsRepo::DeleteGroup(int id) {
        QMutexLocker locker(&mutex);
        memMap.erase(id);
        db.exec("DELETE FROM groups_order WHERE group_id = ?", id);
        db.exec("DELETE FROM groups WHERE id = ?", id);
    }

    QList<int> GroupsRepo::GetAllGroupIds() const {
        QList<int> ids;
        auto query = db.query("SELECT id FROM groups ORDER BY id");
        if (query) {
            while (query->executeStep()) {
                ids.append(query->getColumn(0).getInt());
            }
        }
        return ids;
    }

    int GroupsRepo::NewGroupID() const {
        // Callers already hold the mutex.
        auto query = db.query("UPDATE entity_ids SET group_last_id = group_last_id + 1 RETURNING group_last_id");
        if (query && query->executeStep()) {
            return query->getColumn(0).getInt();
        }
        
        return 0;
    }

    QList<int> GroupsRepo::GetGroupsTabOrder() const {
        QList<int> order;
        auto query = db.query("SELECT group_id FROM groups_order ORDER BY display_order");
        if (query) {
            while (query->executeStep()) {
                order.append(query->getColumn(0).getInt());
            }
        }
        return order;
    }

    void GroupsRepo::SetGroupsTabOrder(const QList<int>& order) {
        db.exec("DELETE FROM groups_order");
        if (!order.isEmpty()) {
            std::vector<int> pairs;
            pairs.reserve(order.size() * 2);
            for (int i = 0; i < order.size(); ++i) {
                pairs.push_back(order[i]);
                pairs.push_back(i);
            }
            db.execBatchInsertIntPairs("groups_order", "group_id", "display_order", pairs);
        }
    }

    bool GroupsRepo::Save(const std::shared_ptr<Group>& group) {
        if (!group) {
            return false;
        }
        
        if (group->id < 0) {
            return false;
        }
        
        QMutexLocker locker(&mutex);
        saveToDatabase(group.get(), group->id);
        memMap[group->id] = group;
        
        return true;
    }
}
