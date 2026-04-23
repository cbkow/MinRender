#include "ui/models/templates_model.h"

#include <QString>

namespace MR {

namespace {

bool templatesEquivalent(const std::vector<JobTemplate>& a,
                         const std::vector<JobTemplate>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].template_id     != b[i].template_id)     return false;
        if (a[i].name            != b[i].name)            return false;
        if (a[i].valid           != b[i].valid)           return false;
        if (a[i].validation_error!= b[i].validation_error)return false;
        if (a[i].flags.size()    != b[i].flags.size())    return false;
    }
    return true;
}

QString cmdPathForHost(const TemplateCmd& cmd)
{
#ifdef _WIN32
    return QString::fromStdString(cmd.os_windows);
#elif defined(__APPLE__)
    return QString::fromStdString(cmd.os_macos);
#else
    return QString::fromStdString(cmd.os_linux);
#endif
}

} // namespace

TemplatesModel::TemplatesModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int TemplatesModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_templates.size());
}

QVariant TemplatesModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= static_cast<int>(m_templates.size()))
        return {};

    const JobTemplate& t = m_templates[static_cast<size_t>(index.row())];

    switch (role)
    {
    case TemplateIdRole:      return QString::fromStdString(t.template_id);
    case NameRole:            return QString::fromStdString(t.name);
    case DccRole:
        // No dedicated DCC field — template_id is the de-facto id
        // ("blender", "ae", "maya-arnold"…). Phase 4 can extract a
        // friendlier label if designs call for it.
        return QString::fromStdString(t.template_id);
    case PathRole:            return cmdPathForHost(t.cmd);
    case FlagCountRole:       return static_cast<int>(t.flags.size());
    case IsValidRole:         return t.valid;
    case ValidationErrorRole: return QString::fromStdString(t.validation_error);
    case IsExampleRole:       return t.isExample;
    }
    return {};
}

QHash<int, QByteArray> TemplatesModel::roleNames() const
{
    return {
        { TemplateIdRole,      "templateId" },
        { NameRole,            "name" },
        { DccRole,             "dcc" },
        { PathRole,            "path" },
        { FlagCountRole,       "flagCount" },
        { IsValidRole,         "isValid" },
        { ValidationErrorRole, "validationError" },
        { IsExampleRole,       "isExample" },
    };
}

void TemplatesModel::setTemplates(const std::vector<JobTemplate>& incoming)
{
    if (templatesEquivalent(m_templates, incoming))
    {
        // Exact same structural shape and identity — don't churn the UI.
        // A fine-grained dataChanged pass on e.g. validation_error flips
        // would be nice, but template list edits are rare (SubmissionWatcher
        // fires at filesystem cadence) — full reset is fine.
        return;
    }

    beginResetModel();
    m_templates = incoming;
    endResetModel();
}

} // namespace MR
