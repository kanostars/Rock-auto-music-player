#include "handpan_test.h"
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <algorithm>

namespace rock {
namespace {
const std::array<QPointF,9> centers{{{698,593},{172,376},{435,376},{698,376},{961,376},{1225,376},{435,160},{698,160},{961,160}}};
void polygon(QPainter& p,const QColor& color,std::initializer_list<QPointF> points){p.setBrush(color);p.drawPolygon(QPolygonF(QVector<QPointF>(points)));}
void numeral(QPainter& p,int number) {
    const QColor ink("#282a25");p.setPen(Qt::NoPen);
    switch(number) {
    case 1:polygon(p,QColor("#278c51"),{{-16,-27},{9,-27},{9,28},{-16,48}});break;
    case 2:{
        p.setBrush(QColor("#a340d0"));QPainterPath dome;dome.moveTo(-33,-3);dome.arcTo(QRectF(-33,-32,58,58),180,-270);dome.lineTo(-4,-3);dome.closeSubpath();p.drawPath(dome);
        polygon(p,ink,{{-33,26},{34,26},{16,51},{-33,51}});break;
    }
    case 3:{
        p.setBrush(QColor("#82ac40"));QPainterPath lower;lower.moveTo(-5,-2);lower.arcTo(QRectF(-32,-2,54,54),90,-270);lower.lineTo(-32,25);lower.lineTo(-5,25);lower.closeSubpath();p.drawPath(lower);
        polygon(p,ink,{{-31,-27},{25,-27},{8,-2},{-31,-2}});break;
    }
    case 4:{
        p.setBrush(QColor("#cc409b"));QPainterPath half;half.moveTo(-4,-36);half.arcTo(QRectF(-33,-36,58,58),90,180);half.closeSubpath();p.drawPath(half);
        polygon(p,ink,{{5,-36},{26,-36},{26,23},{5,42},{5,22},{-4,22},{-4,-3},{5,-3}});break;
    }
    case 5:{
        polygon(p,ink,{{-32,-35},{23,-35},{7,-15},{-11,-15},{-11,13},{-32,13}});
        p.setBrush(QColor("#ec8b00"));QPainterPath half;half.moveTo(-11,-15);half.arcTo(QRectF(-39,-15,56,56),90,-180);half.closeSubpath();p.drawPath(half);break;
    }
    case 6:
        p.setBrush(QColor("#508ff5"));p.drawEllipse(QRectF(-30,-9,54,54));polygon(p,ink,{{-30,-16},{-3,-36},{-3,18},{-30,18}});break;
    case 7:
        polygon(p,QColor("#fa6513"),{{-7,-13},{18,-13},{-1,43},{-25,43}});polygon(p,ink,{{-32,-36},{21,-36},{14,-11},{-32,-11}});break;
    }
}
}
HandpanBoard::HandpanBoard(QWidget* parent):QWidget(parent){setObjectName("handpanBoard");setMinimumSize(650,345);setFocusPolicy(Qt::StrongFocus);setAccessibleName("九键手碟，T Y U / F G H J K / B");}
QTransform HandpanBoard::boardTransform() const {
    double scale=std::min(width()/1397.0,height()/740.0);QTransform transform;transform.translate((width()-1397*scale)/2,(height()-740*scale)/2);transform.scale(scale,scale);return transform;
}
QPoint HandpanBoard::padCenter(int target) const{return boardTransform().map(centers.at(target)).toPoint();}
void HandpanBoard::setHighlighted(int target,bool value){if(target>=0&&target<9){held_[target]=value;update();}}
void HandpanBoard::paintEvent(QPaintEvent*) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setTransform(boardTransform());
    QLinearGradient background(0,0,1397,740);background.setColorAt(0,QColor("#22213f"));background.setColorAt(.65,QColor("#2d294b"));background.setColorAt(1,QColor("#23213e"));
    p.setPen(Qt::NoPen);p.setBrush(background);p.drawRoundedRect(QRectF(0,0,1397,740),24,24);
    p.setPen(QPen(QColor(240,232,202,40),2.4));for(int y:{121,337,553})for(int i=0;i<5;++i)p.drawLine(QPointF(40,y+i*16),QPointF(1357,y+i*16));
    p.setPen(Qt::NoPen);p.setBrush(QColor(240,232,202,65));
    for(int i=0;i<32;++i){double x=42+(i*173)%1310,y=30+(i*97)%680;p.drawEllipse(QPointF(x,y),2.6,2.6);}
    const std::array<int,9> numbers{6,3,4,5,6,7,1,2,3};
    for(int target=0;target<9;++target) {
        p.save();p.translate(centers[target]);
        if(held_[target]){p.setBrush(QColor(255,222,141,45));p.drawEllipse(QPointF(0,0),79,79);p.setBrush(QColor("#ffcf6e"));p.drawEllipse(QPointF(0,0),71,71);}
        p.setBrush(held_[target]?QColor("#fffced"):QColor("#f7f1dc"));p.drawEllipse(QPointF(0,0),66,66);numeral(p,numbers[target]);
        p.setBrush(QColor("#282a25"));if(target>=6)p.drawEllipse(QPointF(0,-43),6.5,6.5);if(target==0)p.drawEllipse(QPointF(0,54),6.5,6.5);
        p.setBrush(held_[target]?QColor("#ffcf6e"):QColor("#f7f1dc"));p.drawEllipse(QPointF(0,94),16.5,16.5);
        p.setPen(QColor("#282a25"));QFont font("Georgia");font.setPixelSize(18);p.setFont(font);p.drawText(QRectF(-16,78,32,32),Qt::AlignCenter,QString(QChar(keys[target])));p.restore();
    }
}
void HandpanBoard::mousePressEvent(QMouseEvent* event) {
    if(event->button()!=Qt::LeftButton)return;setFocus();const auto position=boardTransform().inverted().map(event->position());
    for(int target=0;target<9;++target){auto d=position-centers[target];if(d.x()*d.x()+d.y()*d.y()<=66*66){mouseTarget_=target;emit padPressed(target);event->accept();return;}}
}
void HandpanBoard::mouseReleaseEvent(QMouseEvent* event) {
    if(event->button()==Qt::LeftButton&&mouseTarget_>=0){int target=mouseTarget_;mouseTarget_=-1;emit padReleased(target);event->accept();}
}
HandpanTestDialog::HandpanTestDialog(QWidget* parent,AudioBackend backend):QDialog(parent),audio_(backend) {
    setObjectName("keyTestWindow");setWindowTitle("九键手碟测试");resize(960,680);setMinimumSize(730,540);
    setStyleSheet("QDialog#keyTestWindow {background:#eef3f5;} QLabel {background:transparent; color:#203d4e; font-family:'Microsoft YaHei UI';}");
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(18,16,18,14);layout->setSpacing(9);
    auto* title=new QLabel("九键手碟测试");title->setStyleSheet("font-size:22px; font-weight:700;");layout->addWidget(title);
    auto* instructions=new QLabel("按 T / Y / U、F / G / H / J / K、B，或用鼠标点击音键，即可高亮并试听；支持同时按下多个键。\n请保持当前小窗处于前台并获得焦点；最小化或焦点移到其他窗口时，立即暂停发声。重新聚焦后需再次按键。");
    instructions->setWordWrap(true);instructions->setStyleSheet("font-size:12px; color:#627c8a;");layout->addWidget(instructions);
    board_=new HandpanBoard;layout->addWidget(board_,1);status_=new QLabel("已暂停 · 请点击此窗口以启用试听");status_->setObjectName("handpanTestStatus");layout->addWidget(status_);
    connect(board_,&HandpanBoard::padPressed,this,[this](int target){press(target,true);});
    connect(board_,&HandpanBoard::padReleased,this,[this](int target){release(target,true);});
    qApp->installEventFilter(this);connect(qApp,&QGuiApplication::applicationStateChanged,this,[this]{synchronizeFocus();});
}
HandpanTestDialog::~HandpanTestDialog(){qApp->removeEventFilter(this);audio_.stop();}
bool HandpanTestDialog::acceptsInput() const{return isVisible()&&!isMinimized()&&isActiveWindow()&&QGuiApplication::applicationState()==Qt::ApplicationActive;}
void HandpanTestDialog::silence(){audio_.stop();keyboardHeld_.fill(false);mouseHeld_.fill(false);for(int i=0;i<9;++i)board_->setHighlighted(i,false);}
void HandpanTestDialog::synchronizeFocus() {
    if(!board_)return;const bool active=acceptsInput();if(active==active_)return;active_=active;
    if(!active){silence();status_->setText("已暂停 · 请保持此窗口在前台并获得焦点");return;}
    QString error;if(!audio_.start(error)){status_->setText(error);return;}status_->setText("可以试听 · 按下高亮，松开后余音自然衰减");
}
void HandpanTestDialog::press(int target,bool mouse) {
    synchronizeFocus();if(!acceptsInput()||!audio_.running())return;
    auto& held=mouse?mouseHeld_:keyboardHeld_;if(held[target])return;
    if(!audio_.strike(target))return;held[target]=true;board_->setHighlighted(target,true);emit noteTriggered(target);
}
void HandpanTestDialog::release(int target,bool mouse){(mouse?mouseHeld_:keyboardHeld_)[target]=false;board_->setHighlighted(target,keyboardHeld_[target]||mouseHeld_[target]);}
bool HandpanTestDialog::event(QEvent* event) {
    if(board_&&(event->type()==QEvent::WindowDeactivate||event->type()==QEvent::Hide||event->type()==QEvent::Close)){
        active_=false;silence();status_->setText("已暂停 · 请保持此窗口在前台并获得焦点");
    }
    const bool result=QDialog::event(event);
    if(event->type()==QEvent::WindowActivate||event->type()==QEvent::WindowStateChange||event->type()==QEvent::Show)synchronizeFocus();
    return result;
}
bool HandpanTestDialog::eventFilter(QObject* object,QEvent* event) {
    auto* widget=qobject_cast<QWidget*>(object);
    if(!widget||widget->window()!=this||(event->type()!=QEvent::KeyPress&&event->type()!=QEvent::KeyRelease))return QDialog::eventFilter(object,event);
    auto* key=static_cast<QKeyEvent*>(event);auto it=std::find(keys.begin(),keys.end(),key->key());if(it==keys.end())return false;
    if(!acceptsInput())return false;
    if(!key->isAutoRepeat()) {
        const int target=static_cast<int>(it-keys.begin());
        if(event->type()==QEvent::KeyRelease)release(target,false);
        else if(!(key->modifiers()&(Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier)))press(target,false);
    }
    key->accept();return true;
}
}
