#include "include/ui/widget/SubscriptionInfoCard.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QToolButton>
#include <QDesktopServices>
#include <QUrl>
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QResizeEvent>

#include "include/database/entities/Group.h"
#include "include/database/GroupsRepo.h"
#include "include/configs/sub/GroupUpdater.hpp"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/global/GuiUtils.hpp"
#include "include/global/Utils.hpp"
#include "include/global/Configs.hpp"

namespace
{
    QPixmap renderVectorPixmap(const QString &kind, const QColor &color, int size = 14)
    {
        QPixmap pix(size, size);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(color, 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        if (kind == "globe")
        {
            p.drawEllipse(QRectF(1.2, 1.2, 11.6, 11.6));
            p.drawLine(QPointF(1.5, 7.0), QPointF(12.5, 7.0));
            p.drawEllipse(QRectF(4.4, 1.2, 5.2, 11.6));
        }
        else if (kind == "chat")
        {
            QPainterPath path;
            path.addRoundedRect(QRectF(1.2, 1.5, 11.6, 8.5), 2.0, 2.0);
            path.moveTo(3.5, 10.0);
            path.lineTo(2.0, 12.8);
            path.lineTo(6.5, 10.0);
            p.drawPath(path);
            p.drawLine(QPointF(3.8, 4.5), QPointF(10.2, 4.5));
            p.drawLine(QPointF(3.8, 7.0), QPointF(8.2, 7.0));
        }
        else if (kind == "hourglass")
        {
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawRoundedRect(QRectF(2.0, 1.0, 10.0, 1.6), 0.6, 0.6);
            p.drawRoundedRect(QRectF(2.0, 11.4, 10.0, 1.6), 0.6, 0.6);

            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            QPainterPath glass;
            glass.moveTo(3.4, 2.6);
            glass.lineTo(10.6, 2.6);
            glass.cubicTo(QPointF(10.0, 4.8), QPointF(8.2, 5.8), QPointF(8.0, 7.0));
            glass.cubicTo(QPointF(8.2, 8.2), QPointF(10.0, 9.2), QPointF(10.6, 11.4));
            glass.lineTo(3.4, 11.4);
            glass.cubicTo(QPointF(4.0, 9.2), QPointF(5.8, 8.2), QPointF(6.0, 7.0));
            glass.cubicTo(QPointF(5.8, 5.8), QPointF(4.0, 4.8), QPointF(3.4, 2.6));
            p.drawPath(glass);

            p.setPen(Qt::NoPen);
            p.setBrush(color);
            QPainterPath sand;
            sand.moveTo(4.6, 10.8);
            sand.lineTo(9.4, 10.8);
            sand.lineTo(8.2, 8.4);
            sand.lineTo(5.8, 8.4);
            sand.closeSubpath();
            p.drawPath(sand);

            p.setPen(pen);
            p.drawLine(QPointF(7.0, 6.2), QPointF(7.0, 8.4));
        }
        else if (kind == "warn")
        {
            QPolygonF tri;
            tri << QPointF(7.0, 1.2) << QPointF(13.2, 12.5) << QPointF(0.8, 12.5);
            p.drawPolygon(tri);
            p.drawLine(QPointF(7.0, 4.8), QPointF(7.0, 8.2));
            p.drawPoint(QPointF(7.0, 10.5));
        }
        else if (kind == "info")
        {
            p.drawEllipse(QRectF(1.5, 1.5, 11.0, 11.0));
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawEllipse(QRectF(6.1, 3.5, 1.8, 1.8));
            p.drawRoundedRect(QRectF(6.2, 6.2, 1.6, 4.0), 0.6, 0.6);
        }

        p.end();
        return pix;
    }
}

SubscriptionInfoCard::SubscriptionInfoCard(QWidget *parent)
    : QFrame(parent)
{
    setupUi();
    applyTheme();

    connect(themeManager(), &ThemeManager::themeChanged, this, [this](const QString &)
            {
        applyTheme();
        updateData(); });

    connect(Subscription::updater(), &Subscription::GroupUpdater::asyncUpdateCallback, this, [this](int gid)
            {
        if (m_group && (gid < 0 || m_group->id == gid)) {
            auto freshGroup = Configs::dataManager->groupsRepo->GetGroup(m_group->id);
            if (freshGroup != nullptr) {
                setGroup(freshGroup);
            }
        } });
}

void SubscriptionInfoCard::setupUi()
{
    setObjectName(QStringLiteral("SubscriptionInfoCard"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(4, 1, 4, 1);
    rootLayout->setSpacing(2);

    auto *row1 = new QHBoxLayout();
    row1->setContentsMargins(0, 0, 0, 0);
    row1->setSpacing(3);
    row1->setAlignment(Qt::AlignVCenter);

    m_titleLabel = new QLabel(this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(8);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    row1->addWidget(m_titleLabel, 1);

    m_expiryIcon = new QLabel(this);
    m_expiryIcon->setFixedSize(13, 13);
    m_expiryLabel = new QLabel(this);
    QFont expFont = m_expiryLabel->font();
    expFont.setPointSize(8);
    m_expiryLabel->setFont(expFont);
    m_expiryLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    row1->addWidget(m_expiryIcon);
    row1->addWidget(m_expiryLabel);
    row1->addSpacing(10);

    auto createButton = [this](const QString &tooltip) -> QToolButton *
    {
        auto *btn = new QToolButton(this);
        btn->setFixedSize(22, 18);
        btn->setToolTip(tooltip);
        btn->setCursor(Qt::PointingHandCursor);
        return btn;
    };

    m_btnPortal = createButton(tr("Website / Portal"));
    connect(m_btnPortal, &QToolButton::clicked, this, [this]
            {
        if (m_group) {
            auto sub = m_group->GetSubUserInfo();
            if (!sub.web_url.isEmpty()) QDesktopServices::openUrl(QUrl(sub.web_url));
        } });
    m_btnPortal->hide();
    row1->addWidget(m_btnPortal);

    m_btnSupport = createButton(tr("Technical Support"));
    connect(m_btnSupport, &QToolButton::clicked, this, [this]
            {
        if (m_group) {
            auto sub = m_group->GetSubUserInfo();
            if (!sub.support_url.isEmpty()) QDesktopServices::openUrl(QUrl(sub.support_url));
        } });
    m_btnSupport->hide();
    row1->addWidget(m_btnSupport);

    rootLayout->addLayout(row1);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setFixedHeight(15);
    m_progressBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_progressBar->setTextVisible(true);
    m_progressBar->setAlignment(Qt::AlignCenter);
    rootLayout->addWidget(m_progressBar);

    m_bottomRow = new QFrame(this);
    m_bottomRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *row3 = new QHBoxLayout(m_bottomRow);
    row3->setContentsMargins(0, 0, 0, 0);
    row3->setSpacing(6);
    row3->setAlignment(Qt::AlignVCenter);

    m_bottomLabel = new QLabel(m_bottomRow);
    QFont btmFont = m_bottomLabel->font();
    btmFont.setPointSize(8);
    m_bottomLabel->setFont(btmFont);
    m_bottomLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    row3->addWidget(m_bottomLabel, 1);

    m_btnAnnounceMore = createButton(tr("View full announcement"));
    m_btnAnnounceMore->setFixedSize(18, 16);
    connect(m_btnAnnounceMore, &QToolButton::clicked, this, [this]
            {
        if (m_group) {
            auto sub = m_group->GetSubUserInfo();
            if (!sub.announce.isEmpty()) {
                QString title = !sub.title.isEmpty() ? sub.title : m_group->name;
                MessageBoxScrollable(tr("Announcement - %1").arg(title), sub.announce);
            }
        } });
    m_btnAnnounceMore->hide();
    row3->addWidget(m_btnAnnounceMore);

    rootLayout->addWidget(m_bottomRow);
}

bool SubscriptionInfoCard::hasSubscription() const
{
    return m_group != nullptr && !m_group->url.isEmpty();
}

void SubscriptionInfoCard::setGroup(const std::shared_ptr<Configs::Group> &group)
{
    m_group = group;
    updateData();
}

void SubscriptionInfoCard::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    updateAnnouncementLayout();
}

void SubscriptionInfoCard::updateAnnouncementLayout()
{
    if (m_fullAnnounce.isEmpty())
    {
        m_bottomRow->hide();
        return;
    }

    m_bottomRow->show();
    QFontMetrics fm(m_bottomLabel->font());

    int availWidth = m_bottomRow->width() - 16;
    if (availWidth <= 50)
        return;

    QString fullTextWithBullet = QStringLiteral("• %1").arg(m_fullAnnounce);
    int fullWidth = fm.horizontalAdvance(fullTextWithBullet);

    if (fullWidth <= availWidth)
    {
        m_bottomLabel->setText(fullTextWithBullet);
        m_btnAnnounceMore->hide();
    }
    else
    {
        m_btnAnnounceMore->show();
        int btnWidth = m_btnAnnounceMore->width() > 0 ? m_btnAnnounceMore->width() : 18;
        int textAvailWidth = availWidth - btnWidth - 10;

        QString elided = fm.elidedText(m_fullAnnounce, Qt::ElideRight, textAvailWidth);
        m_bottomLabel->setText(QStringLiteral("• %1").arg(elided));
    }
}

void SubscriptionInfoCard::updateData()
{
    if (!hasSubscription())
    {
        hide();
        return;
    }

    auto sub = m_group->GetSubUserInfo();
    const auto &tk = themeManager()->tokens;
    const QColor winBg = qApp->palette().color(QPalette::Active, QPalette::Window);
    const bool isDark = (winBg.lightness() <= 128);

    const QString textPrimary = isDark ? QStringLiteral("#F3F4F6") : QStringLiteral("#111827");
    const QString textSecondary = isDark ? QStringLiteral("#D1D5DB") : QStringLiteral("#374151");
    const QString trackColor = isDark ? QStringLiteral("#1E2630") : QStringLiteral("#E9ECEF");
    const QString borderColor = isDark ? QStringLiteral("#455364") : QStringLiteral("#CBD5E1");

    QString title = !sub.title.isEmpty() ? sub.title : m_group->name;
    m_titleLabel->setText(title);
    m_titleLabel->setStyleSheet(QStringLiteral("color: %1;").arg(textPrimary));

    if (sub.expire > 0)
    {
        qint64 now = QDateTime::currentSecsSinceEpoch();
        qint64 diffSecs = sub.expire - now;
        qint64 diffDays = diffSecs / 86400;

        if (sub.isExpired())
        {
            m_expiryIcon->setPixmap(renderVectorPixmap("warn", tk.danger, 13));
            m_expiryLabel->setText(tr("Expired"));
            m_expiryLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;").arg(tk.danger.name()));
        }
        else if (diffSecs < 86400)
        {
            qint64 hours = std::max<qint64>(1, diffSecs / 3600);
            m_expiryIcon->setPixmap(renderVectorPixmap("warn", tk.danger, 13));
            m_expiryLabel->setText(QStringLiteral("%1h left").arg(hours));
            m_expiryLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;").arg(tk.danger.name()));
        }
        else if (diffDays <= 3)
        {
            m_expiryIcon->setPixmap(renderVectorPixmap("warn", tk.danger, 13));
            m_expiryLabel->setText(QStringLiteral("%1d left").arg(diffDays));
            m_expiryLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;").arg(tk.danger.name()));
        }
        else
        {
            m_expiryIcon->setPixmap(renderVectorPixmap("hourglass", QColor(textSecondary), 13));
            m_expiryLabel->setText(QStringLiteral("%1d left").arg(diffDays));
            m_expiryLabel->setStyleSheet(QStringLiteral("color: %1;").arg(textSecondary));
        }
        m_expiryIcon->show();
        m_expiryLabel->show();
    }
    else
    {
        m_expiryIcon->setPixmap(renderVectorPixmap("hourglass", QColor(textSecondary), 13));
        m_expiryLabel->setText(QStringLiteral("∞ left"));
        m_expiryLabel->setStyleSheet(QStringLiteral("color: %1;").arg(textSecondary));
        m_expiryIcon->show();
        m_expiryLabel->show();
    }

    const bool hasWeb = !sub.web_url.isEmpty() && (sub.web_url.startsWith("http://", Qt::CaseInsensitive) || sub.web_url.startsWith("https://", Qt::CaseInsensitive));
    const bool hasSupport = !sub.support_url.isEmpty() && (sub.support_url.startsWith("http://", Qt::CaseInsensitive) || sub.support_url.startsWith("https://", Qt::CaseInsensitive));
    m_btnPortal->setVisible(hasWeb);
    m_btnSupport->setVisible(hasSupport);

    QString usedStr = ReadableSize(sub.used());
    QString totalStr = (sub.total > 0) ? ReadableSize(sub.total) : QString::fromUtf8("∞");
    double pct = sub.percentUsed();

    QString barColor;
    if (sub.isExpired() || pct >= 90.0)
        barColor = tk.danger.name();
    else if (pct >= 75.0)
        barColor = tk.tag.name();
    else if (pct >= 50.0)
        barColor = tk.accent.name();
    else
        barColor = tk.success.name();

    if (sub.total > 0)
    {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(static_cast<int>(pct));
        m_progressBar->setFormat(QStringLiteral("%1 / %2 (%3%)").arg(usedStr, totalStr, QString::number(static_cast<int>(pct))));
    }
    else
    {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(100);
        m_progressBar->setFormat(QStringLiteral("%1 / ∞").arg(usedStr));
    }

    m_progressBar->setStyleSheet(QStringLiteral(
                                     "QProgressBar {"
                                     "  border: 1px solid %1;"
                                     "  border-radius: 4px;"
                                     "  text-align: center;"
                                     "  font-weight: bold;"
                                     "  font-size: 7.5pt;"
                                     "  line-height: 1;"
                                     "  color: %2;"
                                     "  background-color: %3;"
                                     "}"
                                     "QProgressBar::chunk {"
                                     "  background-color: %4;"
                                     "  border-radius: 3px;"
                                     "}")
                                     .arg(borderColor, (pct >= 50.0 && sub.total > 0) ? QStringLiteral("#FFFFFF") : textPrimary, trackColor, barColor));
    const QString cleanAnnounce = sub.announce.trimmed();
    const bool hasAnnounce = !cleanAnnounce.isEmpty() && cleanAnnounce.compare("base64:", Qt::CaseInsensitive) != 0;

    if (hasAnnounce)
    {
        m_fullAnnounce = cleanAnnounce;
        m_bottomLabel->setToolTip(cleanAnnounce);
        m_bottomLabel->setStyleSheet(QStringLiteral("color: %1;").arg(textSecondary));
        updateAnnouncementLayout();
    }
    else
    {
        m_fullAnnounce.clear();
        m_btnAnnounceMore->hide();
        if (m_group->sub_update_interval > 0)
        {
            m_bottomLabel->setText(tr("Auto-update: %1h").arg(m_group->sub_update_interval));
            m_bottomLabel->setToolTip(QString());
            m_bottomLabel->setStyleSheet(QStringLiteral("color: %1;").arg(textSecondary));
            m_bottomRow->show();
        }
        else if (m_group->sub_last_update > 0)
        {
            m_bottomLabel->setText(tr("Updated: %1").arg(DisplayTime(m_group->sub_last_update, QLocale::ShortFormat)));
            m_bottomLabel->setToolTip(QString());
            m_bottomLabel->setStyleSheet(QStringLiteral("color: %1;").arg(textSecondary));
            m_bottomRow->show();
        }
        else
        {
            m_bottomRow->hide();
        }
    }

    show();
}

void SubscriptionInfoCard::applyTheme()
{
    const auto &tk = themeManager()->tokens;
    const QColor winBg = qApp->palette().color(QPalette::Active, QPalette::Window);
    const bool isDark = (winBg.lightness() <= 128);

    const QString chipBg = isDark ? QStringLiteral("#1E293B") : QStringLiteral("#F1F5F9");
    const QString chipBorder = isDark ? QStringLiteral("#334155") : QStringLiteral("#CBD5E1");

    setStyleSheet(QStringLiteral(
                      "QFrame#SubscriptionInfoCard { background: transparent; border: none; }"
                      "QToolButton {"
                      "  background-color: %1;"
                      "  border: 1px solid %2;"
                      "  border-radius: 4px;"
                      "  padding: 1px;"
                      "}"
                      "QToolButton:hover {"
                      "  background-color: %3;"
                      "  border-color: %4;"
                      "}")
                      .arg(chipBg, chipBorder, tk.hoverFill.name(), tk.accent.name()));

    m_btnPortal->setIcon(QIcon(renderVectorPixmap("globe", tk.onSurface, 13)));
    m_btnSupport->setIcon(QIcon(renderVectorPixmap("chat", tk.onSurface, 13)));
    m_btnAnnounceMore->setIcon(QIcon(renderVectorPixmap("info", tk.onSurface, 14)));
}
