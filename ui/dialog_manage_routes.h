#pragma once

#include <QDialog>
#include <QMenu>

#include "3rdparty/qv2ray/v2/ui/QvAutoCompleteTextEdit.hpp"
#include "main/NekoGui.hpp"
#include "widget/RouteItem.h"

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

    QList<std::shared_ptr<NekoGui::RoutingChain>> chainList;

    int currentRouteProfileID = -1;

    void set_dns_hijack_enability(bool enable) const;

    static bool validate_dns_rules(const QString &rawString);

    QShortcut* deleteShortcut;
public slots:
    void accept() override;

    void updateCurrentRouteProfile(int idx);

    void on_new_route_clicked();

    void on_clone_route_clicked();

    void on_edit_route_clicked();

    void on_delete_route_clicked();
};
