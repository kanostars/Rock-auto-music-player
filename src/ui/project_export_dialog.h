#pragma once
#include "app/project.h"
#include <QDialog>
#include <QStringList>
class QComboBox;class QLineEdit;
namespace rock {
class ProjectExportDialog final:public QDialog {
public:
    ProjectExportDialog(const ProjectState&,QWidget* parent=nullptr);
    ProjectStorage storage() const;
    QStringList songPaths() const;
private:
    QComboBox* storage_{};
    std::vector<QLineEdit*> paths_;
};
}
