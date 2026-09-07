#include "include/ui/profile/edit_autoselector.h"

#include "include/configs/AutoSelectorPlan.h"
#include "include/database/DatabaseManager.h"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/entities/Group.h"
#include "include/database/entities/Profile.h"
#include "include/stats/autoselector/AutoSelectorMonitor.hpp"
#include "include/ui/mainwindow_interface.h"

#include <QFormLayout>
#include <QGridLayout>
#include <QGuiApplication>
#include <QScreen>

EditAutoSelector::EditAutoSelector(QWidget *parent) : QWidget(parent), ui(new Ui::EditAutoSelector) {
    ui->setupUi(this);

    ui->balance_mode->addItem(tr("Rotate on a timer (keeps sessions stable)"), "rotate");
    ui->balance_mode->addItem(tr("Per connection (widest spread)"), "connection");

    ui->advanced_area->setVisible(false);
    connect(ui->advanced_toggle, &QPushButton::toggled, this, [this](bool on) {
        ui->advanced_area->setVisible(on);
        ui->advanced_toggle->setText(on ? tr("Advanced ▴") : tr("Advanced…"));
        resizeDialogToContent();
    });

    ui->advanced_layout->setColumnStretch(0, 1);
    ui->advanced_layout->setColumnStretch(1, 1);

    mirrorTooltipsToLabels();

    // Unlike the rest of this form, releasing the pin is written immediately, not in onEnd().
    connect(ui->pinned_clear, &QPushButton::clicked, this, [this] {
        m_pinnedID = -1;
        refreshPinnedRow();
        if (ent != nullptr) {
            if (auto outbound = ent->AutoSelector(); outbound != nullptr) {
                outbound->pinnedID = -1;
                Configs::dataManager->profilesRepo->Save(ent);
            }
        }
        // The running core holds its own copy of the pin.
        if (Stats::autoSelectorMonitor->Active()) {
            (void) Stats::autoSelectorMonitor->RequestSelect({});
        }
    });

    connect(ui->balance, &QCheckBox::toggled, this, [this] { updateBalanceEnabled(); });
    connect(ui->balance_mode, &QComboBox::currentIndexChanged, this, [this] { updateBalanceEnabled(); });

    connect(ui->group, &QComboBox::currentIndexChanged, this, [this] { refreshPlanSummary(); });
    connect(ui->name_filter, &QLineEdit::textChanged, this, [this] { refreshPlanSummary(); });
    connect(ui->country_filter, &QLineEdit::textChanged, this, [this] { refreshPlanSummary(); });
    connect(ui->exclude_unavailable, &QCheckBox::toggled, this, [this] { refreshPlanSummary(); });
    connect(ui->pool_cap, &QSpinBox::valueChanged, this, [this] { refreshPlanSummary(); });
    connect(ui->build_limit, &QSpinBox::valueChanged, this, [this] { refreshPlanSummary(); });
    connect(ui->result_validity, &QSpinBox::valueChanged, this, [this] { refreshPlanSummary(); });
}

EditAutoSelector::~EditAutoSelector() {
    delete ui;
}

// Hiding a child never shrinks the dialog: parent layouts cache their last size, so the resize is only correct one event-loop pass after the invalidation.
void EditAutoSelector::resizeDialogToContent() {
    auto *dialog = get_edit_dialog ? get_edit_dialog() : nullptr;
    if (dialog == nullptr) return;
    if (layout() != nullptr) layout()->invalidate();
    updateGeometry();

    runOnThread([dialog] {
        if (auto *dialogLayout = dialog->layout()) dialogLayout->activate();
        // adjustSize() would clamp to 2/3 of the screen, so bound it here instead.
        const auto available = QGuiApplication::primaryScreen()->availableGeometry().size();
        dialog->resize(dialog->sizeHint().boundedTo(available));
    }, dialog);
}

void EditAutoSelector::mirrorTooltipsToLabels() const {
    for (auto *form : findChildren<QFormLayout *>()) {
        for (int row = 0; row < form->rowCount(); row++) {
            auto *labelItem = form->itemAt(row, QFormLayout::LabelRole);
            auto *fieldItem = form->itemAt(row, QFormLayout::FieldRole);
            if (labelItem == nullptr || fieldItem == nullptr) continue;
            auto *label = labelItem->widget();
            auto *field = fieldItem->widget();
            if (label == nullptr || field == nullptr) continue;
            if (const auto tip = field->toolTip(); !tip.isEmpty()) label->setToolTip(tip);
        }
    }
}

void EditAutoSelector::onStart(std::shared_ptr<Configs::Profile> _ent) {
    this->ent = _ent;
    auto outbound = this->ent->AutoSelector();
    if (outbound == nullptr) return;

    // Every setValue below fires valueChanged, which would replan against a half-filled form.
    m_loading = true;

    for (int gid : Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
        auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
        if (group == nullptr || group->archive) continue;
        ui->group->addItem(group->name, group->id);
    }
    const int trackedGid = outbound->gid >= 0 ? outbound->gid : this->ent->gid;
    if (const int idx = ui->group->findData(trackedGid); idx >= 0) ui->group->setCurrentIndex(idx);

    ui->name_filter->setText(outbound->nameFilter);
    ui->country_filter->setText(outbound->countryFilter);
    ui->exclude_unavailable->setChecked(outbound->excludeUnavailable);
    ui->pool_cap->setMaximum(Configs::autoSelector::kMaxPoolCap);
    ui->pool_cap->setValue(outbound->poolCap);
    ui->build_limit->setMaximum(Configs::autoSelector::kMaxBuildLimit);
    ui->build_limit->setValue(outbound->buildLimit);
    ui->result_validity->setValue(outbound->resultValidityMins);

    ui->test_url->setText(outbound->testURL);
    ui->connectivity_url->setText(outbound->connectivityURL);
    ui->interval->setValue(outbound->intervalSec);
    m_pinnedID = outbound->pinnedID;
    refreshPinnedRow();
    ui->bench_interval->setValue(outbound->benchIntervalSec);
    ui->watch_interval->setValue(outbound->watchIntervalSec);
    ui->active_size->setValue(outbound->activeSize);
    ui->sampling->setValue(outbound->sampling);
    ui->tolerance->setValue(outbound->toleranceMs);
    ui->max_rtt->setValue(outbound->maxRTTms);
    ui->dial_retries->setValue(outbound->dialRetries);
    ui->interrupt_on_switch->setChecked(outbound->interruptOnSwitch);

    ui->balance->setChecked(outbound->balance);
    ui->expected->setValue(outbound->expected);
    if (const int idx = ui->balance_mode->findData(outbound->balanceMode); idx >= 0)
        ui->balance_mode->setCurrentIndex(idx);
    ui->balance_interval->setValue(outbound->balanceIntervalSec);

    m_loading = false;
    updateBalanceEnabled();
    refreshPlanSummary();
}

bool EditAutoSelector::onEnd() {
    if (get_edit_text_name().isEmpty()) {
        MessageBoxWarning(software_name, tr("Name cannot be empty."));
        return false;
    }
    auto outbound = this->ent->AutoSelector();
    if (outbound == nullptr) return false;

    const int gid = ui->group->currentData().toInt();
    if (ui->group->currentIndex() < 0) {
        MessageBoxWarning(software_name, tr("Select the group this auto selector should track."));
        return false;
    }
    if (!ui->name_filter->text().trimmed().isEmpty()) {
        if (const QRegularExpression re(ui->name_filter->text()); !re.isValid()) {
            MessageBoxWarning(software_name, tr("The name filter is not a valid regular expression: %1")
                                                 .arg(re.errorString()));
            return false;
        }
    }

    outbound->gid = gid;
    outbound->nameFilter = ui->name_filter->text().trimmed();
    outbound->countryFilter = ui->country_filter->text().trimmed();
    outbound->excludeUnavailable = ui->exclude_unavailable->isChecked();
    outbound->poolCap = ui->pool_cap->value();
    outbound->buildLimit = ui->build_limit->value();
    outbound->resultValidityMins = ui->result_validity->value();

    outbound->testURL = ui->test_url->text().trimmed();
    outbound->connectivityURL = ui->connectivity_url->text().trimmed();
    outbound->intervalSec = ui->interval->value();
    outbound->pinnedID = m_pinnedID;
    outbound->benchIntervalSec = ui->bench_interval->value();
    outbound->watchIntervalSec = ui->watch_interval->value();
    outbound->activeSize = ui->active_size->value();
    outbound->sampling = ui->sampling->value();
    outbound->toleranceMs = ui->tolerance->value();
    outbound->maxRTTms = ui->max_rtt->value();
    outbound->dialRetries = ui->dial_retries->value();
    outbound->interruptOnSwitch = ui->interrupt_on_switch->isChecked();

    outbound->balance = ui->balance->isChecked();
    outbound->expected = ui->expected->value();
    outbound->balanceMode = ui->balance_mode->currentData().toString();
    outbound->balanceIntervalSec = ui->balance_interval->value();
    outbound->Normalize();

    // The pool is a ranking, so a filter change can leave entries that no longer qualify.
    const auto candidates = Configs::AutoSelectorRankingCandidates(this->ent);
    const QSet<int> eligible(candidates.begin(), candidates.end());
    QList<int> pool;
    for (int id : outbound->pool) {
        if (eligible.contains(id)) pool << id;
    }
    outbound->pool = pool;
    return true;
}

void EditAutoSelector::refreshPinnedRow() const
{
    const bool pinned = m_pinnedID >= 0;
    ui->pinned_l->setVisible(pinned);
    ui->pinned_name->setVisible(pinned);
    ui->pinned_clear->setVisible(pinned);
    if (!pinned) return;

    auto member = Configs::dataManager->profilesRepo->GetProfile(m_pinnedID);
    const auto name = member != nullptr && member->outbound != nullptr
                          ? member->outbound->DisplayName()
                          : QObject::tr("a profile that no longer exists");
    ui->pinned_name->setText(tr("%1 — chosen by you, so the selector stays on it while it works").arg(name));
}

void EditAutoSelector::updateBalanceEnabled() const {
    const bool on = ui->balance->isChecked();
    ui->balance_mode->setEnabled(on);
    ui->balance_mode_l->setEnabled(on);
    const bool rotating = on && ui->balance_mode->currentData().toString() == "rotate";
    ui->balance_interval->setEnabled(rotating);
    ui->balance_interval_l->setEnabled(rotating);

    if (!on) {
        ui->balance_hint->setText(tr("The best profile carries all traffic; the other ready profiles are "
                                     "kept verified so one can take over instantly."));
        return;
    }
    if (rotating) {
        ui->balance_hint->setText(tr("New connections move to another good profile on each rotation; open ones are "
                                     "left alone and finish where they started. Traffic is counted exactly."));
    } else {
        ui->balance_hint->setText(tr("Every new connection may take a different profile. Widest spread, but your "
                                     "exit IP changes mid-session and per-profile traffic becomes approximate."));
    }
}

void EditAutoSelector::refreshPlanSummary() {
    if (m_loading) return;
    auto outbound = this->ent == nullptr ? nullptr : this->ent->AutoSelector();
    if (outbound == nullptr) return;

    // Throwaway copy: PlanAutoSelector normalises, which would clamp fields the form hasn't written yet.
    auto probe = std::make_shared<Configs::Profile>(new Configs::autoSelector(*outbound),
                                                    QStringLiteral("autoselector"));
    probe->id = this->ent->id;
    probe->gid = this->ent->gid;
    auto *preview = probe->AutoSelector();
    preview->gid = ui->group->currentData().toInt();
    preview->nameFilter = ui->name_filter->text().trimmed();
    preview->countryFilter = ui->country_filter->text().trimmed();
    preview->excludeUnavailable = ui->exclude_unavailable->isChecked();
    preview->poolCap = ui->pool_cap->value();
    preview->buildLimit = ui->build_limit->value();
    preview->resultValidityMins = ui->result_validity->value();

    const auto plan = Configs::PlanAutoSelector(probe);

    if (!plan.error.isEmpty()) {
        ui->plan_summary->setText(plan.error);
        return;
    }

    QStringList lines;
    lines << tr("%1 of %2 profiles in the group can be used; %3 would run.")
                 .arg(plan.eligible)
                 .arg(plan.membersInGroup)
                 .arg(plan.build.size());
    if (!plan.skipped.isEmpty()) {
        QStringList reasons;
        for (const auto &[skip, count] : plan.skipped) {
            reasons << tr("%1 %2").arg(count).arg(Configs::AutoSelectorSkipReason(skip));
        }
        lines << tr("Skipped: %1.").arg(reasons.join(tr(", ")));
    }
    if (plan.keptUnavailable > 0) {
        lines << tr("Every profile's last test failed, which usually means the network was down "
                    "rather than the servers, so all %1 are kept and will be re-checked.")
                     .arg(plan.keptUnavailable);
    }
    if (plan.truncated) {
        lines << tr("More than %1 profiles match, so only the best-ranked ones are kept.").arg(plan.poolCapUsed);
    }
    if (plan.rankedByTest > 0) {
        lines << tr("%1 have a recent test result that will be reused.").arg(plan.rankedByTest);
    }
    if (plan.needsRanking) {
        lines << tr("The rest will be measured before the selector starts.");
    }
    ui->plan_summary->setText(lines.join(" "));
}
