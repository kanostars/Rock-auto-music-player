#include "time_seek_edit.h"
#include "theme.h"
#include <QDynamicPropertyChangeEvent>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <optional>

namespace rock {
namespace {
QString timeText(double seconds){
    const double whole=std::floor(std::max(0.0,seconds));
    const double minutes=std::floor(whole/60);
    const int second=static_cast<int>(std::fmod(whole,60));
    const int fraction=std::clamp(static_cast<int>(std::floor((seconds-whole)*100+1e-7)),0,99);
    return QString("%1:%2.%3").arg(QString::number(minutes,'f',0).rightJustified(2,QChar('0')))
        .arg(second,2,10,QChar('0')).arg(fraction,2,10,QChar('0'));
}
std::optional<double> parseTime(const QString& text){
    static const QRegularExpression secondsPattern("\\A[0-9]+(?:\\.[0-9]+)?\\z");
    static const QRegularExpression minutesPattern("\\A[0-9]+:[0-9]{1,2}(?:\\.[0-9]{1,3})?\\z");
    static const QRegularExpression hoursPattern("\\A[0-9]+:[0-9]{1,2}:[0-9]{1,2}(?:\\.[0-9]{1,3})?\\z");
    double value{};bool ok=false;
    if(secondsPattern.match(text).hasMatch())value=text.toDouble(&ok);
    else if(minutesPattern.match(text).hasMatch()){
        const auto parts=text.split(':');
        const double minute=parts[0].toDouble(&ok);if(!ok)return {};
        const double second=parts[1].toDouble(&ok);if(!ok||second>=60)return {};
        value=minute*60+second;
    }else if(hoursPattern.match(text).hasMatch()){
        const auto parts=text.split(':');
        const double hour=parts[0].toDouble(&ok);if(!ok)return {};
        const double minute=parts[1].toDouble(&ok);if(!ok||minute>=60)return {};
        const double second=parts[2].toDouble(&ok);if(!ok||second>=60)return {};
        value=hour*3600+minute*60+second;
    }else return {};
    if(!ok||!std::isfinite(value)||value<0)return {};
    return value;
}
const QString inputHint=QString::fromUtf8("输入秒数、分:秒或时:分:秒，按回车跳转；Esc 取消。");
}

TimeSeekEdit::TimeSeekEdit(QWidget* parent):QWidget(parent){
    setProperty("role","section");
    setSizePolicy(QSizePolicy::Maximum,QSizePolicy::Preferred);
    auto* row=new QHBoxLayout(this);row->setContentsMargins(0,0,0,0);row->setSpacing(2);
    input_=new QLineEdit(this);input_->setObjectName("timeSeekInput");input_->setMaxLength(32);
    input_->setAlignment(Qt::AlignCenter);input_->setAccessibleName("当前播放时间");input_->installEventFilter(this);
    durationLabel_=new QLabel(this);durationLabel_->setObjectName("timeSeekDuration");durationLabel_->setAccessibleName("歌曲总时长");
    row->addWidget(input_);row->addWidget(durationLabel_);
    setFocusProxy(input_);
    connect(input_,&QLineEdit::textEdited,this,[this]{error_.clear();updateAppearance();});
    setPosition(0,0);updateAppearance();
}

void TimeSeekEdit::setPosition(double seconds,double duration){
    const double nextDuration=std::isfinite(duration)&&duration>0?duration:0;
    const bool changed=nextDuration!=duration_;
    duration_=nextDuration;
    position_=std::isfinite(seconds)?std::clamp(seconds,0.0,duration_):0;
    if(changed)cancelEditing();
    input_->setEnabled(duration_>0);
    durationLabel_->setText("/ "+timeText(duration_));
    if(!editing_)refreshText();
}

void TimeSeekEdit::cancelEditing(){
    editing_=false;error_.clear();refreshText();updateAppearance();
    input_->clearFocus();
}

void TimeSeekEdit::refreshText(){input_->setText(timeText(position_));}

void TimeSeekEdit::updateAppearance(){
    if(!input_)return;
    const bool muted=property("role").toString()=="muted";
    const QString color=muted?"#8397a3":"#203d4e";
    const QString font=muted?"font-size:11px;font-weight:400;":"font-size:13px;font-weight:700;";
    const QString border=error_.isEmpty()?"transparent":"#b64957";
    Theme::setStyle(this,QString("QLineEdit#timeSeekInput {background:transparent;color:%1;border:1px solid %2;border-radius:4px;padding:2px 3px;min-height:0px;%3}"
        "QLineEdit#timeSeekInput:hover, QLineEdit#timeSeekInput:focus {border-color:%4;}"
        "QLineEdit#timeSeekInput:disabled {color:#afbdc5;border-color:transparent;}"
        "QLabel#timeSeekDuration {color:%1;%3}").arg(color,border,font,error_.isEmpty()?"#178e80":"#b64957"));
    input_->setToolTip(error_.isEmpty()?(duration_>0?inputHint:QString::fromUtf8("导入 MIDI 后可以输入时间跳转。")):error_);
    updateWidth();
}

void TimeSeekEdit::updateWidth(){
    if(!input_)return;
    input_->ensurePolished();
    input_->setFixedWidth(input_->fontMetrics().horizontalAdvance(timeText(duration_))+10);
}

void TimeSeekEdit::submit(bool leaving){
    if(!editing_||submitting_)return;
    if(!input_->isModified()){
        editing_=false;error_.clear();refreshText();updateAppearance();
        if(!leaving)input_->clearFocus();
        return;
    }
    const auto seconds=parseTime(input_->text());
    if(!seconds||*seconds>duration_||duration_<=0){
        error_=!seconds?QString::fromUtf8("时间格式不正确，请输入秒数、分:秒或时:分:秒，分隔后的秒数必须小于 60。")
            :QString::fromUtf8("时间超出范围，请输入 00:00.00 至 %1 之间的时间。").arg(timeText(duration_));
        if(leaving){editing_=false;refreshText();}
        updateAppearance();return;
    }
    editing_=false;error_.clear();submitting_=true;
    emit seekRequested(*seconds);
    submitting_=false;refreshText();updateAppearance();
    if(!leaving)input_->clearFocus();
}

bool TimeSeekEdit::event(QEvent* event){
    const bool result=QWidget::event(event);
    if(event->type()==QEvent::DynamicPropertyChange&&static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName()=="role")updateAppearance();
    if(event->type()==QEvent::EnabledChange&&!isEnabled()&&input_)cancelEditing();
    return result;
}

bool TimeSeekEdit::eventFilter(QObject* object,QEvent* event){
    if(object!=input_)return QWidget::eventFilter(object,event);
    if(event->type()==QEvent::FocusIn){
        if(!editing_){
            editing_=true;error_.clear();refreshText();updateAppearance();
            QTimer::singleShot(0,this,[this]{if(editing_&&input_->hasFocus())input_->selectAll();});
        }
    }else if(event->type()==QEvent::FocusOut){
        if(!input_->isEnabled())cancelEditing();
        else if(static_cast<QFocusEvent*>(event)->reason()!=Qt::PopupFocusReason)submit(true);
    }else if(event->type()==QEvent::KeyPress){
        auto* key=static_cast<QKeyEvent*>(event);
        if(key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter){submit(false);key->accept();return true;}
        if(key->key()==Qt::Key_Escape){cancelEditing();key->accept();return true;}
    }else if(event->type()==QEvent::EnabledChange&&!input_->isEnabled())cancelEditing();
    else if(event->type()==QEvent::FontChange)updateWidth();
    return QWidget::eventFilter(object,event);
}

void TimeSeekEdit::hideEvent(QHideEvent* event){cancelEditing();QWidget::hideEvent(event);}
}
