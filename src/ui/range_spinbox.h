#pragma once
#include <QDoubleSpinBox>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QSpinBox>
#include <cmath>
#include <functional>
#include <type_traits>

namespace rock {
// Keep the complete draft (including an out-of-range paste) visible until commit.
// Qt's default validator otherwise discards digits before we can explain the error.
template<class Base> class RangeSpinBox : public Base {
public:
    explicit RangeSpinBox(QString name):name_(std::move(name)){this->setKeyboardTracking(false);}
    std::function<void(const QString&)> rejected;
    bool commitInput() {
        if(!this->isEnabled())return true;
        const QString raw=this->cleanText();bool ok=false;
        double value=this->locale().toDouble(raw,&ok);
        if constexpr(std::is_same_v<Base,QSpinBox>)ok=ok&&std::floor(value)==value;
        if(!ok||!std::isfinite(value)||value<this->minimum()||value>this->maximum()) {
            QString range=QString("%1～%2%3").arg(this->minimum()).arg(this->maximum()).arg(this->suffix());
            this->lineEdit()->setText(this->prefix()+this->textFromValue(this->value())+this->suffix());
            if(rejected)rejected(QString("%1的有效范围是 %2。\n输入“%3”无效，已恢复上一个有效值。")
                .arg(name_,range,raw.left(80)));
            return false;
        }
        this->setValue(value);return true;
    }
protected:
    QValidator::State validate(QString& input,int& pos) const override {
        auto state=Base::validate(input,pos);
        return state==QValidator::Invalid?QValidator::Intermediate:state;
    }
    void focusOutEvent(QFocusEvent* event) override {
        commitInput();Base::focusOutEvent(event);
    }
    void keyPressEvent(QKeyEvent* event) override {
        if(event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter) {
            if(!commitInput()){event->accept();return;}
        }
        Base::keyPressEvent(event);
    }
    void stepBy(int steps) override {if(commitInput())Base::stepBy(steps);}
private:
    QString name_;
};
using RangeDoubleSpinBox=RangeSpinBox<QDoubleSpinBox>;
using RangeIntSpinBox=RangeSpinBox<QSpinBox>;
}
