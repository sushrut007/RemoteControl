#include "RoleSelectorWidget.h"

#include "DarpanTheme.h"

#include <QVBoxLayout>
#include <QButtonGroup>
#include <QRadioButton>

namespace {

QRadioButton* makeRoleOption(const QString& title,
    const QString& subtitle,
    const QString& roleValue,
    const QString& dotColor,
    QWidget* parent)
{
    auto* btn = new QRadioButton(parent);
    btn->setObjectName(QStringLiteral("RoleOption"));
    btn->setProperty("roleValue", roleValue);
    btn->setProperty("dotColor", dotColor);
    btn->setText(QStringLiteral("%1\n%2").arg(title, subtitle));
    return btn;
}

} // namespace

RoleSelectorWidget::RoleSelectorWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
}

void RoleSelectorWidget::buildUi()
{
    setObjectName(QStringLiteral("RoleSelectorFrame"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_group = new QButtonGroup(this);

    m_hostBtn = makeRoleOption(tr("Host"), tr("Share my screen"),
        QStringLiteral("host"), DarpanTheme::kRoleHost, this);
    m_viewerBtn = makeRoleOption(tr("Viewer"), tr("Watch only"),
        QStringLiteral("viewer"), DarpanTheme::kRoleViewer, this);
    m_controllerBtn = makeRoleOption(tr("Controller"), tr("Watch and control"),
        QStringLiteral("controller"), DarpanTheme::kRoleController, this);

    for (auto* btn : { m_hostBtn, m_viewerBtn, m_controllerBtn }) {
        m_group->addButton(btn);
        layout->addWidget(btn);
    }

    m_controllerBtn->setChecked(true);

    QObject::connect(m_group, &QButtonGroup::idClicked, this, [this](int) {
        emit roleChanged(selectedRole());
    });
}

QString RoleSelectorWidget::selectedRole() const
{
    if (m_hostBtn->isChecked())       return QStringLiteral("host");
    if (m_viewerBtn->isChecked())     return QStringLiteral("viewer");
    return QStringLiteral("controller");
}

void RoleSelectorWidget::setSelectedRole(const QString& role)
{
    if (role == QLatin1String("host")) {
        m_hostBtn->setChecked(true);
    }
    else if (role == QLatin1String("viewer")) {
        m_viewerBtn->setChecked(true);
    }
    else {
        m_controllerBtn->setChecked(true);
    }
}

void RoleSelectorWidget::styleOption(QRadioButton*, const QString&)
{
}
