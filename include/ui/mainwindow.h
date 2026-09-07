#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include "include/ui/widget/SubscriptionInfoCard.hpp"

#include <include/global/HTTPRequestHelper.hpp>
#ifndef Q_MOC_RUN
#include <core/server/gen/libcore.pb.h>
#endif

#include "include/global/Configs.hpp"
#include "include/stats/connections/connectionLister.hpp"
#include "3rdparty/qv2ray/v2/ui/widgets/speedchart/SpeedWidget.hpp"
#include "include/database/entities/Profile.h"
#ifdef Q_OS_LINUX
#include <QtDBus>
#endif

#ifndef MW_INTERFACE

#include <optional>
#include <QKeyEvent>
#include <QSystemTrayIcon>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
#include <QQueue>
#include <QWaitCondition>
#include <QProcess>
#include <QTextDocument>
#include <QShortcut>
#include <QKeySequence>
#include <QSet>
#include <QHash>
#include <QIcon>
#include <QPixmap>
#include <QToolButton>
#include <QCheckBox>
#include <QSemaphore>
#include <QMutex>
#include <QThreadPool>
#include <QLocalServer>
#include <QLocalSocket>

#include "group/GroupSort.hpp"
#include "include/global/GuiUtils.hpp"
#include "include/ui/setting/Icon.hpp"
#include "include/ui/utils/DataViewHtmlGenerator.h"
#include "include/ui/utils/ProfilesFilterProxyModel.h"
#include "include/ui/utils/ProfilesTableModel.h"
#include "ui_mainwindow.h"

#endif

namespace Configs_sys {
    class CoreProcess;
}

class TrayProfileSelector;
class TrayOtpCodes;
class TestRunner;
class DialogVpnAuth;
struct VpnAuthChallenge;

struct VpnEndpointState {
    QString tag;
    QString state;
    QString error;
    bool connected = false;
    bool authFailed = false;
};

namespace Qv2ray::ui { class SyntaxHighlighter; }

QT_BEGIN_NAMESPACE
namespace Ui {
    class MainWindow;
}
QT_END_NAMESPACE

enum class RefreshAnchor {
    KeepPlace,
    Removal,
};

enum class ExitReason {
    None,
    RunUpdater,
    Restart,
    RestartWithTun,
    RestartWithDns,
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    ~MainWindow() override;

    qint64 GetCorePid();
    QString GetRunningConfigName();

    // The two live VPN queries below block on an RPC; never call them from the UI thread.
    static QString liveVpnConnectOkText();

    static QString liveVpnStateText(bool *connected = nullptr);

    void prepare_exit();

    void refresh_proxy_list(const QList<int> &ids = {}, bool mayNeedReset = false,
                            RefreshAnchor anchor = RefreshAnchor::KeepPlace);

    void show_group(int gid);

    void show_group_tab_menu(const QPoint &tabBarPos);

    void refresh_groups();

    void refresh_status(const QString &traffic_update = "");

    void update_traffic_graph(int proxyDl, int proxyUp, int directDl, int directUp);

    void profile_start(int _id = -1);

    void profile_stop(bool crash = false, bool block = false, bool manual = false);

    int get_profile_to_start();

    void set_spmode_system_proxy(bool enable, bool save = true);

    void toggle_system_proxy();

    void set_spmode_vpn(bool enable, bool save = true);

    bool get_elevated_permissions(ExitReason reason = ExitReason::RestartWithTun);

    void start_select_mode(QObject *context, const std::function<void(int)> &callback);

    void RegisterHotkey(bool unregister);

    bool StopVPNProcess();

    void RestartCore();

    // Takes a whole poll snapshot in the lister's order; row N is always its Nth entry. UI thread only.
    void UpdateConnectionList(const QList<Stats::ConnectionMetadata>& connections);

    void UpdateDataView(bool force = false);

    void refresh_auto_selector_view();

    class DialogAutoSelector *m_autoSelectorDialog = nullptr;

    void setDownloadReport(const DownloadProgressReport& report, bool show);

signals:

    void profile_selected(int id);

public slots:

    void on_commitDataRequest();

    void on_menu_exit_triggered();

#ifndef MW_INTERFACE

private slots:

    void on_masterLogBrowser_customContextMenuRequested(const QPoint &pos);

    void on_menu_basic_settings_triggered();

    void on_menu_routing_settings_triggered();

    void on_menu_vpn_settings_triggered();

    void on_menu_preset_settings_triggered();

    void on_menu_otp_manager_triggered();

    void on_menu_hotkey_settings_triggered();

    void on_menu_add_from_input_triggered();

    void on_menu_add_from_clipboard_triggered();

    void on_menu_clone_triggered();

    void on_menu_delete_repeat_triggered();

    void on_menu_delete_triggered();

    void on_menu_reset_traffic_triggered();

    void on_menu_copy_links_triggered();

    void on_menu_copy_links_nkr_triggered();

    void on_menu_export_config_triggered();

    void display_qr_link(bool nkrFormat = false);

    void on_menu_scan_qr_triggered();

    void on_menu_clear_test_result_triggered();

    void on_menu_manage_groups_triggered();

    void on_menu_select_all_triggered();

    void on_menu_remove_unavailable_triggered();

    void on_menu_remove_invalid_triggered();

    void on_menu_remove_insecure_triggered();

    void on_menu_resolve_selected_triggered();

    void on_menu_resolve_domain_triggered();

    void on_menu_update_subscription_triggered();

    void on_profilesTableView_doubleClicked(const QModelIndex &index);

    void on_profilesTableView_customContextMenuRequested(const QPoint &pos);

    void on_tabWidget_currentChanged(int index);

    void on_tabWidget_customContextMenuRequested(const QPoint& p);

private:
    Ui::MainWindow *ui;
    QElapsedTimer sinceWindowDeactivated;
    ProfilesTableModel *profilesTableModel = nullptr;

    ProfilesFilterProxyModel *profilesFilterModel = nullptr;
    QSystemTrayIcon *tray;
    QMenu *trayMenu = nullptr;
    QPointer<TrayProfileSelector> traySelector;
    void openTraySelector(bool routing);
    QPointer<TrayOtpCodes> trayOtpCodes;
    void openTrayOtpCodes();
    QShortcut *shortcut_esc = new QShortcut(QKeySequence::Cancel, this);
    QThreadPool *parallelCoreCallPool = new QThreadPool(this);
    std::unique_ptr<TestRunner> testRunner;
    Configs_sys::CoreProcess *core_process = nullptr;
    QMutex coreProcessMutex;
    QLocalServer *core_server = nullptr;
    bool rpc_started = false;
    qint64 vpn_pid = 0;
    QTextDocument *qvLogDocument = new QTextDocument(this);
    QString title_error;
    std::optional<Icon::TrayIconStatus> icon_status;
    std::shared_ptr<Configs::Profile> running;
    int last_running_profile_id = -1;
    bool m_profileConnecting = false;
    bool m_profileDisconnecting = false;
    bool m_xrayGeoAssetBusy = false;
    QString traffic_update_cache;
    qint64 last_test_time = 0;
    int proxy_last_order = -1;
    bool select_mode = false;
    QMutex mu_starting;
    QMutex mu_stopping;
    QMutex mu_exit;
    ExitReason exit_reason = ExitReason::None;
    QMutex mu_download_update;
    QMutex mu_download_dashboard;
    class ConnectionsTableModel *connectionsModel = nullptr;
    class ConnectionsFilterProxyModel *connectionsFilterModel = nullptr;
    class ConnectionCloseDelegate *connectionCloseDelegate = nullptr;
    class ConnectionsFilterHeader *connectionFilterHeader = nullptr;
    QTimer *connectionFilterDebounce = nullptr;
    QToolButton *connectionCloseAllButton = nullptr;
    QIcon connectionCloseIcon;
    int toolTipID;
    SpeedWidget *speedChartWidget;
    std::atomic<qint64> lastUpdatedMs = QDateTime::currentMSecsSinceEpoch();
    DataViewHtmlGenerator dataViewHtmlGenerator_;

    QStackedWidget *m_topBarStack = nullptr;       
    SubscriptionInfoCard *m_subInfoCard = nullptr; 

    QList<QShortcut*> hiddenMenuShortcuts;

    QString addressFilterString;
    QString nameFilterString;
    QString typeFilterString;
    QString countryFilterString;

    QTimer *m_filterRefreshDebounce = nullptr;

    bool m_profilesTableHadFocus = false;
    int m_profilesScrollValue = 0;

    QStringList includeKeywords;
    QStringList excludeKeywords;
    QRegularExpression includeCombined;
    QRegularExpression excludeCombined;
    QMutex logMutex;
    QQueue<QString> logQueue;
    QWaitCondition logWaiter;
    Qv2ray::ui::SyntaxHighlighter *logHighlighter = nullptr;

    QMutex logPendingMutex;
    QString logPendingText;
    bool logFlushScheduled = false;

    struct LogFilter {
        bool enableInclude = false;
        bool enableExclude = false;
        QStringList includeKeywords;
        QStringList excludeKeywords;
        QRegularExpression includeCombined;
        QRegularExpression excludeCombined;
    };

    void append_log(const QString &log);

    void log_process_loop();

    // UI thread only.
    void flush_log_batch();

    bool should_print_log(const QString &log, const LogFilter &filter);

    void updateLogFilterFields();

    void setLogHighlighter(bool darkMode);

    void applyProfileFilters();

    QList<int> get_now_selected_list();
    void refresh_startstop_button();

    QList<int> get_selected_or_group();

    void set_system_proxy(bool enable);

    void saveProfileFocusState();

    void restoreProfileFocusState(RefreshAnchor anchor);

    void selectProfileRows(const QList<int> &rows);

    void focusProfilesTable(bool selectFirst);

    void clearUnavailableProfiles(bool confirm = true, QList<int> profileIDs = {});

    void dialog_message_impl(MwMessage cmd, const QStringList &args);

    void handle_deeplink_impl(const QString &url);

    void handle_addsub(const QString &url, const QString &name);

    void handle_import_route(const QString &url);

    void handle_add_remote_routes(const QString &url);

    void import_or_handle_deeplink(const QString &text);

    // A pasted url asks whether it is a subscription or a proxy link; everything else is imported as is.
    void import_text(const QString &text);

    void refresh_proxy_list_column_size();

    void refresh_proxy_list_impl(const QList<int> &ids = {}, bool mayNeedReset = false);

    void refresh_proxy_list_impl_refresh_data(const QList<int>& ids = {}, bool mayNeedReset = false);

    void parseQrImage(const QPixmap *image);

    void importFromFiles(const QStringList &paths);

    void trayClickEvent();

    void keyPressEvent(QKeyEvent *event) override;

    void closeEvent(QCloseEvent *event) override;

    void changeEvent(QEvent *event) override;

    void showEvent(QShowEvent *event) override;

    void hideEvent(QHideEvent *event) override;

    void resizeEvent(QResizeEvent *event) override;

    void syncConnectionViewState();

    void dragEnterEvent(QDragEnterEvent *event);

    void dropEvent(QDropEvent* event) override;

    void applyLogBrowserFont();

    void applyTopBarMetrics();
    bool usesTightLabels() const;

    QSize designMinimumSize;

    QTimer *m_proxyListRefreshDebounce = nullptr;
    void scheduleProxyListRefresh();

    bool m_adjustingColumns = false;

    void HotkeyEvent(const QString &key);

    void RegisterHiddenMenuShortcuts(bool unregister = false);
    void registerMenuShortcuts(QMenu *menu, QSet<QKeySequence> &claimed);
    void collectMenuShortcuts(QMenu *menu, QSet<QKeySequence> &out);

    void setActionsData();

    QList<QAction*> getActionsForShortcut();

    void loadShortcuts();

    void setup_rpc(QLocalSocket *socket);

    bool verify_core_pid(QLocalSocket *socket);

    void rank_auto_selector(const std::shared_ptr<Configs::Profile>& ent, const QList<int>& stale = {});

    void on_auto_selector_exhausted(int profileID);

    void on_subscription_group_changed(int gid, const QList<int>& disturbed);

    bool auto_selector_ranked = false;

    bool handleXrayGeoAssetError(const QString& error, const QString& contextName);

    void url_test_current();

    static std::shared_ptr<Configs::Profile> vpn_exit_endpoint(const std::shared_ptr<Configs::Profile> &ent);

    static QString vpn_state_text(const QString &state, const QString &error);

    void start_vpn_challenge_poll();

    void stop_vpn_challenge_poll();

    void poll_vpn_challenges();

    void show_vpn_challenge(const VpnAuthChallenge &challenge);

    // True once the challenge is either submitted or deliberately held back for a fresher code.
    bool auto_answer_vpn_challenge(const VpnAuthChallenge &challenge);

    void submit_vpn_challenge_answer(const VpnAuthChallenge &challenge, const QString &username,
                                     const QString &password, const QString &secret,
                                     const QMap<QString, QString> &formValues);

    void show_vpn_auth_failure(const QString &endpointTag, const QString &error);

    bool auto_restart_for_vpn_auth(const QString &endpointTag, int profileID);

    void update_vpn_endpoint_states(const QList<VpnEndpointState> &states);

    void reset_vpn_endpoint_tracking();

    void clear_vpn_credential_overrides();

    QTimer *m_vpnChallengeTimer = nullptr;
    std::atomic<bool> m_vpnChallengeBusy{false};
    QSet<QString> m_vpnChallengeSeen;
    QPointer<DialogVpnAuth> m_vpnAuthDialog;
    QString m_vpnEndpointState;
    QString m_vpnTroubleSummary;
    QString m_vpnTroubleDetail;
    QHash<QString, QString> m_vpnEndpointLastState;
    QHash<QString, QString> m_vpnOtpLastCode;
    QHash<QString, int> m_vpnOtpRejects;
    QSet<QString> m_vpnChallengeAnswering;
    // Like m_vpnAuthPrompted, these outlive the restart they count; nothing else would end it.
    QHash<int, int> m_vpnAutoRestarts;
    qint64 m_vpnAutoRestartAt = 0;
    // Survives the restart the recovery itself triggers, so a rejected retry cannot loop.
    QHash<int, int> m_vpnAuthPrompted;
    int m_vpnAuthRestartID = -1;

    bool set_system_dns(bool set, bool save_set = true);

    void CheckUpdate();

    void OpenDashboard();

    void SeedDashboard();

    void setupConnectionList();

    void setupConnectionSortMenu();

    void setupConnectionFilter();

    void restoreConnectionSort();

    void applyConnectionSort(Stats::ConnectionSort sort);

    void applyConnectionFilters();

    void syncConnectionSourceColumn();

    // Rows are rewritten on every poll, so ids are read at click time, never captured.
    void closeConnections(const QStringList &ids);

    QStringList listedConnectionIds() const;

    void refreshConnectionCloseIcons();

    friend class TestRunner;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

#endif // MW_INTERFACE
};

inline MainWindow *GetMainWindow() {
    return qobject_cast<MainWindow *>(mainwindow);
}

void UI_InitMainWindow();

#ifdef Q_OS_LINUX
class OrgFreedesktopPortalRequestInterface : public QDBusAbstractInterface
{
    Q_OBJECT
public:
    OrgFreedesktopPortalRequestInterface(const QString& service,
                                         const QString& path,
                                         const QDBusConnection& connection,
                                         QObject* parent = nullptr);

    ~OrgFreedesktopPortalRequestInterface();

public Q_SLOTS:
    inline QDBusPendingReply<> Close()
    {
        QList<QVariant> argumentList;
        return asyncCallWithArgumentList(QStringLiteral("Close"), argumentList);
    }

Q_SIGNALS:
    void Response(uint response, QVariantMap results);
};

namespace org {
namespace freedesktop {
namespace portal {
typedef ::OrgFreedesktopPortalRequestInterface Request;
}
}
}
#endif
