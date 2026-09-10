#include "include/ui/widget/json/JsonEditorDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

#include "include/global/Utils.hpp"
#include "include/ui/widget/json/JsonCodeEdit.h"
#include "include/ui/widget/json/JsonIssueList.h"

namespace JsonEdit {
    JsonEditorDialog::JsonEditorDialog(const QJsonObject& root, QWidget* parent) : QDialog(parent), m_original(root) {
        setWindowTitle(QObject::tr("JSON Editor"));
        setWindowModality(Qt::ApplicationModal);
        setModal(true);

        auto* layout = new QVBoxLayout(this);

        m_editor = new JsonCodeEdit(this);
        m_editor->setPlainText(root.isEmpty()
                                   ? QString()
                                   : QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented)));
        layout->addWidget(m_editor, 1);

        m_issues = new JsonIssueList(this);
        m_issues->attach(m_editor);
        layout->addWidget(m_issues);

        auto* bottom = new QHBoxLayout();
        m_status = new QLabel(this);
        bottom->addWidget(m_status);
        bottom->addStretch();

        auto* format = new QPushButton(QObject::tr("Format"), this);
        connect(format, &QPushButton::clicked, this, [this] {
            if (!m_editor->formatDocument()) {
                MessageBoxWarning(QObject::tr("Invalid JSON"), QObject::tr("Fix the errors before formatting the document."));
            }
        });
        bottom->addWidget(format);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        bottom->addWidget(buttons);
        layout->addLayout(bottom);

        connect(m_editor, &JsonCodeEdit::issuesChanged, this, [this] { m_status->setText(m_editor->statusText()); });
        m_status->setText(m_editor->statusText());

        resize(QSize(900, 600).boundedTo(screen()->availableGeometry().size()));
    }

    void JsonEditorDialog::SetValidator(std::shared_ptr<Validator> validator) {
        m_editor->setValidator(std::move(validator));
    }

    QJsonObject JsonEditorDialog::OpenEditor() {
        while (exec() == QDialog::Accepted) {
            const QString text = m_editor->toPlainText().trimmed();
            if (text.isEmpty()) return {};

            QJsonParseError error{};
            const auto document = QJsonDocument::fromJson(text.toUtf8(), &error);
            if (error.error == QJsonParseError::NoError && document.isObject()) return document.object();

            MessageBoxWarning(QObject::tr("Invalid JSON"), error.error == QJsonParseError::NoError
                                                      ? QObject::tr("The document must be a JSON object.")
                                                      : error.errorString());
        }
        return m_original;
    }
}
