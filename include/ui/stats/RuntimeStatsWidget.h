#pragma once

#include <QWidget>
#include <QList>
#include <QPointer>
#include <QStringList>

#include <atomic>

#include "ui_RuntimeStatsWidget.h"

#include "include/sys/ProcessMetrics.hpp"
#include "include/ui/stats/dialog_endpoint_details.h"

QT_BEGIN_NAMESPACE
namespace Ui {
    class RuntimeStatsWidget;
}
QT_END_NAMESPACE

class QTimer;

class RuntimeStatsWidget : public QWidget {
    Q_OBJECT

public:
    explicit RuntimeStatsWidget(QWidget* parent = nullptr);
    ~RuntimeStatsWidget() override;

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void refreshLive();
    void probeEgress();

    void applyEndpoints(const QList<Stats::VpnEndpointView>& views);
    void fitEndpointTable();
    void openEndpointDetails(const QString& tag);
    [[nodiscard]] QString selectedEndpointTag() const;
    [[nodiscard]] bool panelActive() const;

    Ui::RuntimeStatsWidget* ui;
    QTimer* timer_ = nullptr;
    Sys::ProcessMetrics metrics_;
    std::atomic<bool> probing_{false};
    std::atomic<bool> connBusy_{false};
    std::atomic<bool> vpnBusy_{false};

    QString lastProbedConfig_;
    qint64 lastProbeSecs_ = 0;
    bool egressSnapshotDone_ = false;

    QPointer<DialogEndpointDetails> details_;
    QList<Stats::VpnEndpointView> endpointViews_;
    QStringList endpointTags_;
};
