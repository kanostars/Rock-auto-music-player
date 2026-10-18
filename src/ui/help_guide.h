#pragma once
#include <QDialog>
#include <QString>
#include <vector>

class QLabel;
class QPushButton;
namespace rock {
struct GuideStep {
    QString title;
    QString description;
    QString target;
};
struct GuideTopic {
    QString id;
    QString title;
    QString description;
    std::vector<GuideStep> steps;
};
const std::vector<GuideTopic>& guideTopics();

class HelpGuideDialog final:public QDialog {
    Q_OBJECT
public:
    explicit HelpGuideDialog(QWidget* parent=nullptr);
    void setStartEnabled(bool enabled,const QString& reason={});
signals:
    void topicRequested(const QString& id);
private:
    QLabel* status_{};
    std::vector<QPushButton*> topics_;
};
}
