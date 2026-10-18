#pragma once

#include <QDialog>

namespace rock {
class AboutDialog final : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent=nullptr);
};
}
