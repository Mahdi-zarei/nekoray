#pragma once

#include <QDialog>
#include <QMenu>

#include <atomic>

#include "3rdparty/qv2ray/v2/ui/QvAutoCompleteTextEdit.hpp"
#include "include/global/Configs.hpp"
#include "include/ui/setting/RouteItem.h"
#include "ui_dialog_manage_routes.h"
#include "include/database/entities/RouteProfile.h"

QT_BEGIN_NAMESPACE
namespace Ui {
    class DialogManageRoutes;
}
QT_END_NAMESPACE

class DialogManageRoutes : public QDialog {
    Q_OBJECT

public:
    explicit DialogManageRoutes(QWidget *parent = nullptr);

    ~DialogManageRoutes() override;

private:
    Ui::DialogManageRoutes *ui;

    RouteItem* routeChainWidget;

    void reloadProfileItems();

    void applyImportedProfile(const std::shared_ptr<Configs::RouteProfile>& profile, bool wasOldArray);

    bool tryImportRemoteRoutesLink(const QString& text);

    void updateRemoteProfiles(const QList<std::shared_ptr<Configs::RouteProfile>>& profiles);

    // routeUpdateRunning is UI-thread only; routeUpdateCancel is set there and polled by the worker.
    bool routeUpdateRunning = false;
    std::atomic<bool> routeUpdateCancel{false};

    QList<std::shared_ptr<Configs::RouteProfile>> chainList;

    std::shared_ptr<Configs::RouteProfile> currentRoute;

    int tooltipID = 0;

    void set_dns_hijack_enability(bool enable) const;

    static bool validate_dns_rules(const QString &rawString);

    void show_predefined_dns_editor();

    void show_dns_advanced_editor();

    void show_dns_object_editor();

    struct DnsAdvancedDraft {
        int cache_capacity;
        bool disable_cache;
        bool disable_expire;
        bool persist_cache;
        bool reverse_mapping;
        bool optimistic;
        QString optimistic_timeout;
        QString query_timeout;
    };

    // Held until accept() so the popups can be cancelled without touching the settings.
    bool predefined_dns_enabled = true;
    QString predefined_dns_text;
    DnsAdvancedDraft dns_advanced{};
    QString dns_object_text;

    QShortcut* deleteShortcut;

    AutoCompleteTextEdit* rule_editor;
public slots:
    void accept() override;

    void updateCurrentRouteProfile(int idx);

    void on_new_route_clicked();

    void on_export_route_clicked();

    void on_import_route_clicked();

    void on_clone_route_clicked();

    void on_edit_route_clicked();

    void on_delete_route_clicked();

    void on_update_route_clicked();
};
