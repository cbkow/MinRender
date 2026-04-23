#pragma once

#include "core/job_types.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>

#include <vector>

namespace MR {

// JobTemplate list projection. Templates change rarely (SubmissionWatcher
// picks up filesystem edits), so the same fast-path / slow-path update
// pattern as JobsModel is overkill here — setTemplates does a full reset
// whenever anything changes.
class TemplatesModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        TemplateIdRole = Qt::UserRole + 1,
        NameRole,
        DccRole,          // template_id today; Phase 4 may derive a cleaner label
        PathRole,         // DCC executable path for the current OS
        FlagCountRole,
        IsValidRole,
        ValidationErrorRole,
        IsExampleRole,
    };

    explicit TemplatesModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setTemplates(const std::vector<JobTemplate>& templates);

private:
    std::vector<JobTemplate> m_templates;
};

} // namespace MR
