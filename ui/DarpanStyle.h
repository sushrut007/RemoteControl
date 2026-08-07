#pragma once

#include <QString>

class QApplication;

class DarpanStyle
{
public:
    /// Load darpan.qss and apply app-wide. Returns false if the sheet could not be loaded.
    static bool apply(QApplication* app);

    static QString readStylesheet();
};
