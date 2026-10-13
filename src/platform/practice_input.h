#pragma once
#include <QObject>
#include <QString>
#include <array>
#include <memory>

namespace rock {
// Observe practice keys without taking them away from the foreground game.
class PracticeInput final:public QObject {
    Q_OBJECT
public:
    explicit PracticeInput(QObject* parent=nullptr);
    ~PracticeInput() override;
    bool start(const std::array<int,9>& keys,QString& error);
    void stop();
signals:
    void keyChanged(int target,bool down);
private:
    struct Private;
    std::unique_ptr<Private> d_;
};
}
