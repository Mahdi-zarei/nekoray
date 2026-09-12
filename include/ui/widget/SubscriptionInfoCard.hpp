#pragma once

#include <QFrame>
#include <memory>

namespace Configs {
    class Group;
}

class QLabel;
class QProgressBar;
class QToolButton;
class QResizeEvent;

class SubscriptionInfoCard : public QFrame {
    Q_OBJECT
public:
    explicit SubscriptionInfoCard(QWidget *parent = nullptr);
    ~SubscriptionInfoCard() override = default;

    void setGroup(const std::shared_ptr<Configs::Group> &group);
    void applyTheme();
    [[nodiscard]] bool hasSubscription() const;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUi();
    void updateData();
    void updateAnnouncementLayout();

    std::shared_ptr<Configs::Group> m_group;
    QString m_fullAnnounce;

    // Line 1: Title & Expiry & Action buttons
    QLabel *m_titleLabel = nullptr;
    QLabel *m_expiryIcon = nullptr;
    QLabel *m_expiryLabel = nullptr;
    QToolButton *m_btnPortal = nullptr;
    QToolButton *m_btnSupport = nullptr;

    // Line 2: Progress bar 
    QProgressBar *m_progressBar = nullptr;

    // Line 3: Announcement footer
    QFrame *m_bottomRow = nullptr;
    QLabel *m_bottomLabel = nullptr;
    QToolButton *m_btnAnnounceMore = nullptr;
};