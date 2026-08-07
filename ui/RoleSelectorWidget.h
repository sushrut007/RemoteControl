#pragma once

#include <QWidget>
#include <QString>

class QButtonGroup;
class QRadioButton;

class RoleSelectorWidget : public QWidget
{
    Q_OBJECT
public:
    explicit RoleSelectorWidget(QWidget* parent = nullptr);

    QString selectedRole() const;
    void setSelectedRole(const QString& role);

signals:
    void roleChanged(const QString& role);

private:
    void buildUi();
    void styleOption(QRadioButton* btn, const QString& roleColor);

    QButtonGroup* m_group{ nullptr };
    QRadioButton* m_hostBtn{ nullptr };
    QRadioButton* m_viewerBtn{ nullptr };
    QRadioButton* m_controllerBtn{ nullptr };
};
