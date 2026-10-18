#include "guide_overlay.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace rock {
namespace {
QLabel* words(const char* name) {
    auto* value=new QLabel;
    value->setObjectName(name);
    value->setTextFormat(Qt::PlainText);
    value->setWordWrap(true);
    value->setMinimumWidth(0);
    return value;
}

QPushButton* action(const QString& caption,const char* name) {
    auto* button=new QPushButton(caption);
    button->setObjectName(name);
    button->setAutoDefault(false);
    button->setDefault(false);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::StrongFocus);
    return button;
}

bool pointerInput(QEvent::Type type) {
    switch(type) {
    case QEvent::MouseButtonPress:case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:case QEvent::MouseMove:
    case QEvent::Wheel:case QEvent::ContextMenu:
    case QEvent::TouchBegin:case QEvent::TouchUpdate:case QEvent::TouchEnd:case QEvent::TouchCancel:
    case QEvent::TabletPress:case QEvent::TabletMove:case QEvent::TabletRelease:
    case QEvent::NativeGesture:return true;
    default:return false;
    }
}
}

GuideOverlay::GuideOverlay(QWidget* host,bool detached)
    :QWidget(host,detached?Qt::WindowFlags(Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint):Qt::WindowFlags(Qt::Widget)),
     host_(host),detached_(detached) {
    setObjectName("guideOverlay");
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_StyledBackground,false);
    setAttribute(Qt::WA_AcceptTouchEvents);
    setAttribute(Qt::WA_QuitOnClose,false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    hide();
    if(host)connect(host,&QObject::destroyed,this,&GuideOverlay::requestExit);

    card_=new QFrame(this);
    card_->setObjectName("guideCard");
    card_->setAttribute(Qt::WA_StyledBackground);
    // The guide keeps its own contrast even over an image or a light theme.
    card_->setStyleSheet(R"(
        QFrame#guideCard {background:#142035;border:1px solid #478df0;border-radius:16px;}
        QFrame#guideCard QLabel {background:transparent;border:none;font-family:'Microsoft YaHei UI';}
        QLabel#guideStepTitle {color:#f4f7ff;font-size:18px;font-weight:700;}
        QLabel#guideProgress {color:#9bbcff;font-size:12px;}
        QLabel#guideStepBody {color:#cbd8ec;font-size:14px;}
        QScrollArea#guideBodyScroll, QScrollArea#guideBodyScroll > QWidget {background:transparent;border:none;}
        QScrollArea#guideBodyScroll QScrollBar:vertical {background:#18283f;width:6px;}
        QScrollArea#guideBodyScroll QScrollBar::handle:vertical {background:#526b92;min-height:22px;border-radius:3px;}
        QScrollArea#guideBodyScroll QScrollBar::add-line:vertical, QScrollArea#guideBodyScroll QScrollBar::sub-line:vertical {height:0;}
        QFrame#guideCard QPushButton {background:#20324d;color:#e1eaff;border:1px solid #3a5379;border-radius:7px;padding:9px 14px;font-family:'Microsoft YaHei UI';font-size:14px;font-weight:600;}
        QFrame#guideCard QPushButton:hover {background:#2c4263;border-color:#6ca6ff;}
        QFrame#guideCard QPushButton:focus {border:2px solid #9bc3ff;padding:8px 13px;}
        QFrame#guideCard QPushButton:disabled {background:#18253b;color:#61728e;border-color:#2a3c57;}
        QFrame#guideCard QPushButton#guideNextButton {background:#3b82f6;color:#ffffff;border-color:#3b82f6;}
        QFrame#guideCard QPushButton#guideNextButton:hover {background:#5292fb;border-color:#8cbaff;}
        QFrame#guideCard QPushButton#guideNextButton:focus {border-color:#c0d8ff;}
    )");
    auto* layout=new QVBoxLayout(card_);
    layout->setContentsMargins(18,16,18,16);
    layout->setSpacing(13);
    auto* heading=new QHBoxLayout;
    heading->setSpacing(14);
    title_=words("guideStepTitle");
    heading->addWidget(title_,1);
    progress_=words("guideProgress");
    progress_->setWordWrap(false);
    progress_->setAlignment(Qt::AlignRight|Qt::AlignTop);
    heading->addWidget(progress_);
    layout->addLayout(heading);

    bodyScroll_=new QScrollArea;
    bodyScroll_->setObjectName("guideBodyScroll");
    bodyScroll_->setWidgetResizable(true);
    bodyScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bodyScroll_->setFrameShape(QFrame::NoFrame);
    body_=words("guideStepBody");
    body_->setAlignment(Qt::AlignLeft|Qt::AlignTop);
    bodyScroll_->setWidget(body_);
    body_->setAutoFillBackground(false);
    bodyScroll_->viewport()->setAutoFillBackground(false);
    layout->addWidget(bodyScroll_);

    auto* footer=new QHBoxLayout;
    footer->setSpacing(9);
    exit_=action("退出引导","guideExitButton");
    previous_=action("上一步","guidePreviousButton");
    next_=action("下一步","guideNextButton");
    footer->addWidget(exit_);
    footer->addStretch();
    footer->addWidget(previous_);
    footer->addWidget(next_);
    layout->addLayout(footer);
    connect(exit_,&QPushButton::clicked,this,&GuideOverlay::requestExit);
    connect(previous_,&QPushButton::clicked,this,&GuideOverlay::previousRequested);
    connect(next_,&QPushButton::clicked,this,&GuideOverlay::nextRequested);
}

GuideOverlay::~GuideOverlay(){if(qApp)qApp->removeEventFilter(this);}

void GuideOverlay::present(QWidget* target,const QString& topic,const QString& title,const QString& body,int index,int total) {
    dismiss();
    if(!host_||!target||!host_->isVisible()||target->window()!=host_->window()||
       (target!=host_&&!host_->isAncestorOf(target))||!target->isVisibleTo(host_)) {
        emit exitRequested();return;
    }
    previousFocus_=QApplication::focusWidget();
    target_=target;
    title_->setText(title);
    body_->setText(body);
    const int count=std::max(1,total),step=std::clamp(index,0,count-1);
    progress_->setText(QString("%1 · %2 / %3").arg(topic).arg(step+1).arg(count));
    previous_->setEnabled(step>0);
    next_->setText(step==count-1?"完成":"下一步");
    active_=true;
    targetDestroyed_=connect(target,&QObject::destroyed,this,&GuideOverlay::requestExit);
    if(qApp)qApp->installEventFilter(this);
    refreshGeometry();
    if(!active_)return;
    show();raise();card_->show();card_->raise();
    if(detached_)activateWindow();
    next_->setFocus(Qt::OtherFocusReason);
    update();
}

void GuideOverlay::dismiss() {
    const bool wasActive=active_;
    active_=false;geometryPending_=false;
    if(qApp)qApp->removeEventFilter(this);
    disconnect(targetDestroyed_);targetDestroyed_={};
    hide();
    target_.clear();ancestors_.clear();spotlight_={};
    if(wasActive&&previousFocus_&&previousFocus_->isVisible())previousFocus_->setFocus(Qt::OtherFocusReason);
    previousFocus_.clear();
}

void GuideOverlay::requestExit() {
    if(!active_)return;
    dismiss();emit exitRequested();
}

bool GuideOverlay::containsObject(QObject* object)const {
    const auto* widget=qobject_cast<QWidget*>(object);
    return widget&&(widget==this||isAncestorOf(widget));
}

bool GuideOverlay::watchesObject(QObject* object)const {
    return std::any_of(ancestors_.begin(),ancestors_.end(),[object](const auto& widget){return widget&&widget==object;});
}

void GuideOverlay::scheduleGeometry() {
    if(geometryPending_||!active_)return;
    geometryPending_=true;
    QTimer::singleShot(0,this,[this]{geometryPending_=false;if(active_)refreshGeometry();});
}

void GuideOverlay::refreshGeometry() {
    if(!active_)return;
    if(!host_||!target_||!host_->isVisible()||target_->window()!=host_->window()||
       (target_!=host_&&!host_->isAncestorOf(target_))||!target_->isVisibleTo(host_)) {
        requestExit();return;
    }
    if(detached_) {
        auto* hostWindow=host_->windowHandle();
        if(hostWindow_!=hostWindow) {
            disconnect(hostScreenChanged_);hostScreenChanged_={};hostWindow_=hostWindow;
            if(hostWindow)hostScreenChanged_=connect(hostWindow,&QWindow::screenChanged,this,[this]{scheduleGeometry();});
        }
        auto* screen=host_->screen();
        if(screen_!=screen) {
            disconnect(screenGeometryChanged_);disconnect(screenAvailableChanged_);
            screenGeometryChanged_={};screenAvailableChanged_={};screen_=screen;
            if(screen) {
                screenGeometryChanged_=connect(screen,&QScreen::geometryChanged,this,[this]{scheduleGeometry();});
                screenAvailableChanged_=connect(screen,&QScreen::availableGeometryChanged,this,[this]{scheduleGeometry();});
            }
        }
        if(!screen){requestExit();return;}
        setGeometry(screen->availableGeometry());
    } else setGeometry(host_->rect());
    ancestors_.clear();
    QRect visible(target_->mapToGlobal(QPoint(0,0)),target_->size());
    for(QWidget* widget=target_;widget;widget=widget->parentWidget()) {
        ancestors_.push_back(widget);
        visible=visible.intersected(QRect(widget->mapToGlobal(QPoint(0,0)),widget->size()));
        if(widget==host_)break;
    }
    visible=QRect(mapFromGlobal(visible.topLeft()),visible.size()).intersected(rect());
    if(visible.isEmpty()){requestExit();return;}
    spotlight_=visible.adjusted(-6,-6,6,6).intersected(rect().adjusted(4,4,-4,-4));
    if(spotlight_.isEmpty()){requestExit();return;}

    constexpr int margin=14,gap=14;
    const int cardWidth=std::min(480,std::max(1,width()-margin*2));
    card_->setFixedWidth(cardWidth);
    // Long descriptions remain scrollable instead of pushing the footer off a
    // short settings window. Text size is independent of the chosen app theme.
    const int textWidth=std::max(1,cardWidth-36-8);
    const int bodyHeight=std::max(body_->fontMetrics().height(),body_->heightForWidth(textWidth));
    const int headingHeight=std::max(title_->heightForWidth(std::max(1,textWidth-progress_->sizeHint().width()-14)),progress_->sizeHint().height());
    const int footerHeight=std::max({exit_->sizeHint().height(),previous_->sizeHint().height(),next_->sizeHint().height()});
    const int maxBodyHeight=std::max(20,height()-margin*2-32-26-headingHeight-footerHeight);
    bodyScroll_->setFixedHeight(std::min(bodyHeight,maxBodyHeight));
    card_->layout()->invalidate();card_->layout()->activate();
    int cardHeight=card_->layout()->totalHeightForWidth(cardWidth);
    if(cardHeight<0)cardHeight=card_->sizeHint().height();
    cardHeight=std::min(std::max(1,cardHeight),std::max(1,height()-margin*2));
    card_->resize(cardWidth,cardHeight);

    const int maxX=std::max(margin,width()-margin-cardWidth);
    const int maxY=std::max(margin,height()-margin-cardHeight);
    // A capsule is only a little taller than its controls. Keep the explanation
    // beside the entire capsule, rather than covering its neighbouring buttons.
    const QRect anchor=detached_?QRect(mapFromGlobal(host_->mapToGlobal(QPoint(0,0))),host_->size()).intersected(rect()).adjusted(-6,-6,6,6):spotlight_;
    const auto center=anchor.center();
    const std::array<QPoint,8> origins{
        QPoint(center.x()-cardWidth/2,anchor.bottom()+gap),
        QPoint(center.x()-cardWidth/2,anchor.top()-gap-cardHeight),
        QPoint(anchor.right()+gap,center.y()-cardHeight/2),
        QPoint(anchor.left()-gap-cardWidth,center.y()-cardHeight/2),
        QPoint(margin,margin),QPoint(maxX,margin),QPoint(margin,maxY),QPoint(maxX,maxY)
    };
    QRect best;
    double score=std::numeric_limits<double>::max();
    for(size_t i=0;i<origins.size();++i) {
        const QRect candidate(QPoint(std::clamp(origins[i].x(),margin,maxX),std::clamp(origins[i].y(),margin,maxY)),QSize(cardWidth,cardHeight));
        const auto overlap=candidate.intersected(anchor);
        const double area=overlap.isEmpty()?0:double(overlap.width())*overlap.height();
        const double distance=std::hypot(double(candidate.center().x()-center.x()),double(candidate.center().y()-center.y()));
        const double candidateScore=area*1000000.+distance+double(i);
        if(candidateScore<score){score=candidateScore;best=candidate;}
    }
    card_->setGeometry(best);raise();card_->raise();update();
}

void GuideOverlay::paintEvent(QPaintEvent*) {
    if(!active_)return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath mask;mask.setFillRule(Qt::OddEvenFill);mask.addRect(QRectF(rect()));
    mask.addRoundedRect(QRectF(spotlight_),9,9);
    painter.fillPath(mask,QColor(2,8,23,177));
    painter.setBrush(Qt::NoBrush);painter.setPen(QPen(QColor("#66a6ff"),2));
    painter.drawRoundedRect(QRectF(spotlight_).adjusted(1,1,-1,-1),8,8);
}

bool GuideOverlay::event(QEvent* event) {
    if(active_&&(event->type()==QEvent::Close||event->type()==QEvent::Hide))requestExit();
    if(active_&&pointerInput(event->type())){event->accept();return true;}
    return QWidget::event(event);
}

void GuideOverlay::cycleFocus(bool backwards) {
    std::vector<QPushButton*> buttons{exit_};
    if(previous_->isEnabled())buttons.push_back(previous_);
    buttons.push_back(next_);
    const auto focus=QApplication::focusWidget();
    const auto found=std::find(buttons.begin(),buttons.end(),focus);
    int position=found==buttons.end()?0:int(std::distance(buttons.begin(),found));
    position=(position+(backwards?int(buttons.size())-1:1))%int(buttons.size());
    buttons[size_t(position)]->setFocus(backwards?Qt::BacktabFocusReason:Qt::TabFocusReason);
}

bool GuideOverlay::eventFilter(QObject* watched,QEvent* event) {
    if(!active_)return false;
    const auto type=event->type();
    if(type==QEvent::ShortcutOverride){event->accept();return true;}
    if(type==QEvent::Shortcut){event->accept();return true;}
    if(type==QEvent::KeyRelease){event->accept();return true;}
    if(type==QEvent::KeyPress) {
        auto* key=static_cast<QKeyEvent*>(event);
        switch(key->key()) {
        case Qt::Key_Escape:requestExit();return true;
        case Qt::Key_Left:case Qt::Key_Up:if(previous_->isEnabled())emit previousRequested();return true;
        case Qt::Key_Right:case Qt::Key_Down:emit nextRequested();return true;
        case Qt::Key_Tab:cycleFocus(key->modifiers().testFlag(Qt::ShiftModifier));return true;
        case Qt::Key_Backtab:cycleFocus(true);return true;
        case Qt::Key_Return:case Qt::Key_Enter:case Qt::Key_Space:
            if(auto* button=qobject_cast<QPushButton*>(QApplication::focusWidget());button&&isAncestorOf(button)&&button->isEnabled())button->click();
            return true;
        default:event->accept();return true;
        }
    }
    // Native mouse events reach QWidgetWindow first. Let Qt dispatch those
    // window events to their child widget before checking the guide boundary.
    if(pointerInput(type)&&qobject_cast<QWidget*>(watched)&&!containsObject(watched)){event->accept();return true;}
    if(watchesObject(watched)) {
        if(type==QEvent::Hide||type==QEvent::Close){requestExit();return false;}
        if(type==QEvent::Resize||type==QEvent::Move||type==QEvent::LayoutRequest||type==QEvent::ParentChange||type==QEvent::Show||type==QEvent::WindowStateChange||type==QEvent::ScreenChangeInternal)
            scheduleGeometry();
    }
    if(type==QEvent::FocusIn&&!containsObject(watched))
        QTimer::singleShot(0,this,[this]{if(active_)next_->setFocus(Qt::OtherFocusReason);});
    return QWidget::eventFilter(watched,event);
}
}
