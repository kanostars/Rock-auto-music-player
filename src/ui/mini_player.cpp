#include "mini_player.h"
#include "playback_icons.h"
#include <QCloseEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace rock {
namespace {
QString clockText(double seconds){const auto s=static_cast<qint64>(std::max(0.0,seconds));return QString("%1:%2").arg(s/60,2,10,QChar('0')).arg(s%60,2,10,QChar('0'));}
const QStringList modeNames{"顺序播放","单曲循环","随机播放","单曲播放完停止"};
const PlaylistIcon modeIcons[]{PlaylistIcon::Loop,PlaylistIcon::Single,PlaylistIcon::Shuffle,PlaylistIcon::Once};
QPushButton* action(PlaylistIcon icon,const QString& name,const char* id){
    auto* b=new QPushButton;b->setObjectName(id);b->setIcon(playlistIcon(icon));b->setIconSize(QSize(20,20));b->setFixedSize(32,32);
    b->setToolTip(name);b->setAccessibleName(name);b->setFocusPolicy(Qt::NoFocus);b->setCursor(Qt::PointingHandCursor);return b;
}
class SeekSlider:public QSlider {
public:SeekSlider():QSlider(Qt::Horizontal){setRange(0,1000000);setFocusPolicy(Qt::NoFocus);}
protected:
    void mousePressEvent(QMouseEvent* e) override{if(e->button()!=Qt::LeftButton){QSlider::mousePressEvent(e);return;}setSliderDown(true);seek(e);}
    void mouseMoveEvent(QMouseEvent* e) override{if(isSliderDown())seek(e);else QSlider::mouseMoveEvent(e);}
    void mouseReleaseEvent(QMouseEvent* e) override{if(e->button()==Qt::LeftButton&&isSliderDown()){seek(e);setSliderDown(false);e->accept();}else QSlider::mouseReleaseEvent(e);}
    void wheelEvent(QWheelEvent* e) override{e->ignore();}
private:void seek(QMouseEvent* e){setValue(QStyle::sliderValueFromPosition(minimum(),maximum(),std::clamp(qRound(e->position().x())-5,0,std::max(1,width()-10)),std::max(1,width()-10)));e->accept();}
};
class SongDelegate:public QStyledItemDelegate {
public:using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem&,const QModelIndex&) const override{return {200,46};}
    void paint(QPainter* p,const QStyleOptionViewItem& option,const QModelIndex& index) const override{
        p->save();p->setRenderHint(QPainter::Antialiasing);auto r=option.rect.adjusted(4,2,-4,-2);
        const bool selected=option.state&QStyle::State_Selected;
        if(selected||option.state&QStyle::State_MouseOver){p->setPen(Qt::NoPen);p->setBrush(QColor(selected?"#eaf5f1":"#f5f9fa"));p->drawRoundedRect(r,10,10);}
        p->setFont(option.font);p->setPen(QColor(selected?"#178e80":"#8397a3"));
        p->drawText(r.adjusted(9,0,0,0),Qt::AlignVCenter,selected?QString::fromUtf8("♪"):QString::number(index.row()+1).rightJustified(2,'0'));
        const auto title=index.data().toString().section('\n',0,0);p->setPen(QColor(selected?"#107566":"#526c7c"));
        const auto textRect=r.adjusted(40,0,-64,0);p->drawText(textRect,Qt::AlignVCenter,option.fontMetrics.elidedText(title,Qt::ElideRight,textRect.width()));
        p->setPen(QColor("#8397a3"));p->drawText(r.adjusted(0,0,-12,0),Qt::AlignRight|Qt::AlignVCenter,clockText(index.data(Qt::UserRole).toDouble()));p->restore();
    }
};
class ResizeHandle:public QWidget {
public:explicit ResizeHandle(QWidget* parent):QWidget(parent){setFixedSize(18,14);setCursor(Qt::SizeFDiagCursor);setToolTip("拖动调整胶囊宽高，大小会自动记住");setAccessibleName("调整胶囊大小");}
protected:void paintEvent(QPaintEvent*) override{QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(QColor("#a8bfc5"),1.3,Qt::SolidLine,Qt::RoundCap));p.drawLine(4,11,14,1);p.drawLine(9,11,14,6);}
};
}
MiniPlayer::MiniPlayer(QListWidget* library):QWidget(nullptr,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|Qt::WindowDoesNotAcceptFocus){
    setObjectName("miniPlayer");setWindowTitle("九键手碟 · 小窗播放器");setAttribute(Qt::WA_TranslucentBackground);setAttribute(Qt::WA_ShowWithoutActivating);setAttribute(Qt::WA_QuitOnClose,false);setFocusPolicy(Qt::NoFocus);
    setStyleSheet(R"(
        QWidget {font-family:'Microsoft YaHei UI';font-size:12px;color:#203d4e;}
        QFrame#miniCapsule {background:white;border:1px solid #dce6eb;border-radius:56px;}
        QFrame#miniQueue {background:white;border:1px solid #dce6eb;border-radius:20px;}
        QLabel {background:transparent;border:0;}
        QLabel#miniTitle {font-size:15px;font-weight:600;}
        QLabel#miniStatus,QLabel#miniElapsed,QLabel#miniDuration,QLabel#miniCount,QLabel#miniQueueMode {font-size:11px;color:#8397a3;}
        QPushButton {background:transparent;border:0;border-radius:16px;padding:0;}
        QPushButton:hover,QPushButton:checked {background:#e8f4f1;}
        QPushButton:disabled {background:transparent;}
        QPushButton#miniPlay {background:#178e80;border-radius:21px;}
        QPushButton#miniPlay:hover {background:#107566;}
        QPushButton#miniPlay:disabled {background:#c5dcd7;}
        QPushButton#miniQuit:hover,QPushButton#miniRemove:hover {background:#fbebed;}
        QListView {border:0;background:transparent;outline:0;}
        QSlider::groove:horizontal {height:3px;background:#e7eef0;border-radius:1px;}
        QSlider::sub-page:horizontal {background:#178e80;border-radius:1px;}
        QSlider::handle:horizontal {width:8px;height:8px;margin:-3px 0;border-radius:4px;background:#178e80;}
        QSlider::handle:horizontal:disabled {background:transparent;}
        QScrollBar:vertical {width:7px;background:transparent;}
        QScrollBar::handle:vertical {background:#c4d3da;border-radius:3px;min-height:20px;}
        QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical {height:0;}
        QToolTip {background:#203d4e;color:white;padding:6px;border:0;}
    )");
    layout_=new QVBoxLayout(this);layout_->setContentsMargins(10,10,10,10);layout_->setSpacing(10);layout_->setSizeConstraint(QLayout::SetNoConstraint);
    capsule_=new QFrame;capsule_->setObjectName("miniCapsule");capsule_->setFixedHeight(112);layout_->addWidget(capsule_);
    auto* capsuleLayout=new QVBoxLayout(capsule_);capsuleLayout->setContentsMargins(22,15,22,13);capsuleLayout->setSpacing(8);
    auto* top=new QHBoxLayout;top->setSpacing(6);capsuleLayout->addLayout(top);
    audition_=action(PlaylistIcon::Headphones,"本地试听","miniAudition");performance_=action(PlaylistIcon::Keyboard,"自动演奏","miniPerformance");
    for(auto* b:{audition_,performance_}){b->setCheckable(true);top->addWidget(b);}top->addSpacing(6);
    auto* song=new QVBoxLayout;song->setSpacing(4);title_=new QLabel;title_->setObjectName("miniTitle");title_->setMinimumWidth(0);title_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    status_=new QLabel;status_->setObjectName("miniStatus");status_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    for(auto* l:{title_,status_}){l->setTextFormat(Qt::PlainText);l->installEventFilter(this);l->setCursor(Qt::SizeAllCursor);song->addWidget(l);}top->addLayout(song,1);
    previous_=action(PlaylistIcon::Previous,"上一首","miniPrevious");play_=action(PlaylistIcon::Play,"播放","miniPlay");next_=action(PlaylistIcon::Next,"下一首","miniNext");
    play_->setFixedSize(42,42);play_->setIcon(playlistIcon(PlaylistIcon::Play,true));
    mode_=action(PlaylistIcon::Once,"单曲播放完停止","miniMode");list_=action(PlaylistIcon::List,"展开曲目列表","miniList");list_->setCheckable(true);
    for(auto* b:{previous_,play_,next_,mode_,list_})top->addWidget(b);
    auto* separator=new QFrame;separator->setFixedSize(1,26);separator->setStyleSheet("background:#e4ebef;");top->addSpacing(5);top->addWidget(separator);top->addSpacing(5);
    auto* restore=action(PlaylistIcon::Restore,"返回主窗口","miniRestore");auto* quit=action(PlaylistIcon::Power,"退出整个程序","miniQuit");top->addWidget(restore);top->addWidget(quit);
    auto* progressRow=new QHBoxLayout;progressRow->setSpacing(9);progressRow->setContentsMargins(76,0,10,0);capsuleLayout->addLayout(progressRow);
    elapsed_=new QLabel("00:00");elapsed_->setObjectName("miniElapsed");duration_=new QLabel("00:00");duration_->setObjectName("miniDuration");
    progress_=new SeekSlider;progress_->setObjectName("miniProgress");progress_->setAccessibleName("当前歌曲播放进度");progress_->setFixedHeight(14);progressRow->addWidget(elapsed_);progressRow->addWidget(progress_,1);progressRow->addWidget(duration_);
    volumeBox_=new QWidget;volumeBox_->setObjectName("miniVolumeBox");auto* volumeRow=new QHBoxLayout(volumeBox_);volumeRow->setContentsMargins(5,0,0,0);volumeRow->setSpacing(5);
    auto* speaker=new QLabel;speaker->setPixmap(playlistIcon(PlaylistIcon::Volume).pixmap(16,16));speaker->setToolTip("试听音量");volumeRow->addWidget(speaker);
    volume_=new QSlider(Qt::Horizontal);volume_->setObjectName("miniVolume");volume_->setAccessibleName("试听音量");volume_->setFocusPolicy(Qt::NoFocus);volume_->setRange(0,100);volume_->setFixedWidth(64);volumeRow->addWidget(volume_);
    volumeText_=new QLabel;volumeText_->setFixedWidth(30);volumeText_->setStyleSheet("font-size:11px;color:#8397a3;");volumeRow->addWidget(volumeText_);progressRow->addWidget(volumeBox_);volumeBox_->hide();
    queue_=new QFrame;queue_->setObjectName("miniQueue");auto* queueLayout=new QVBoxLayout(queue_);queueLayout->setContentsMargins(16,10,16,12);queueLayout->setSpacing(5);
    auto* head=new QHBoxLayout;head->addWidget(new QLabel("曲目列表"));count_=new QLabel;count_->setObjectName("miniCount");head->addWidget(count_);head->addStretch();
    up_=action(PlaylistIcon::Up,"上移当前曲目","miniUp");down_=action(PlaylistIcon::Down,"下移当前曲目","miniDown");remove_=action(PlaylistIcon::Remove,"移出共享曲目库，不删除原 MIDI 文件","miniRemove");for(auto* b:{up_,down_,remove_})head->addWidget(b);queueLayout->addLayout(head);
    songs_=new QListView;songs_->setObjectName("miniSongs");songs_->setModel(library->model());songs_->setSelectionModel(library->selectionModel());songs_->setItemDelegate(new SongDelegate(songs_));songs_->setUniformItemSizes(true);songs_->setMouseTracking(true);songs_->setSelectionMode(QAbstractItemView::SingleSelection);songs_->setEditTriggers(QAbstractItemView::NoEditTriggers);songs_->setFocusPolicy(Qt::NoFocus);songs_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);queueLayout->addWidget(songs_,1);
    queueMode_=new QLabel;queueMode_->setObjectName("miniQueueMode");queueLayout->addWidget(queueMode_);layout_->addWidget(queue_,0,Qt::AlignHCenter);queue_->hide();
    capsule_->installEventFilter(this);capsule_->setCursor(Qt::SizeAllCursor);resizeHandle_=new ResizeHandle(capsule_);resizeHandle_->setObjectName("miniResizeHandle");resizeHandle_->installEventFilter(this);
    connect(audition_,&QPushButton::clicked,this,[this]{emit sourceRequested(false);});connect(performance_,&QPushButton::clicked,this,[this]{emit sourceRequested(true);});
    connect(play_,&QPushButton::clicked,this,&MiniPlayer::playRequested);connect(previous_,&QPushButton::clicked,this,[this]{emit navigateRequested(true);});connect(next_,&QPushButton::clicked,this,[this]{emit navigateRequested(false);});
    connect(mode_,&QPushButton::clicked,this,&MiniPlayer::modeRequested);connect(list_,&QPushButton::clicked,this,&MiniPlayer::toggleList);connect(restore,&QPushButton::clicked,this,&MiniPlayer::restoreRequested);connect(quit,&QPushButton::clicked,this,&MiniPlayer::quitRequested);
    connect(progress_,&QSlider::sliderPressed,this,[this]{if(state_.seekEnabled)emit seekStarted();});
    connect(progress_,&QSlider::valueChanged,this,[this](int value){if(progress_->isSliderDown())elapsed_->setText(clockText(state_.duration*value/1000000.0));});
    connect(progress_,&QSlider::sliderReleased,this,[this]{if(state_.seekEnabled)emit seekRequested(state_.duration*progress_->value()/1000000.0);});
    connect(songs_,&QListView::clicked,this,[this](const QModelIndex& index){if(!state_.busy&&index.isValid())emit songPlayRequested(index.row());});
    connect(volume_,&QSlider::valueChanged,this,[this](int value){volumeText_->setText(QString::number(value)+"%");volume_->setToolTip(QString("试听音量 %1%（0 为静音）").arg(value));emit volumeRequested(value);});
    connect(up_,&QPushButton::clicked,this,[this]{const int row=songs_->currentIndex().row();emit moveRequested(row,row-1);});connect(down_,&QPushButton::clicked,this,[this]{const int row=songs_->currentIndex().row();emit moveRequested(row,row+1);});connect(remove_,&QPushButton::clicked,this,[this]{emit removeRequested(songs_->currentIndex().row());});
}
void MiniPlayer::setState(const MiniPlayerState& s){
    const bool playingChanged=!initialized_||s.playing!=state_.playing;
    const bool modeChanged=!initialized_||s.mode!=state_.mode;state_=s;initialized_=true;
    title_->setText(title_->fontMetrics().elidedText(s.title,Qt::ElideRight,std::max(0,title_->width())));title_->setToolTip(s.title);
    status_->setText(status_->fontMetrics().elidedText(s.status,Qt::ElideRight,std::max(0,status_->width())));status_->setToolTip(s.detail);
    audition_->setChecked(!s.performance);performance_->setChecked(s.performance);audition_->setEnabled(!s.busy);performance_->setEnabled(!s.busy);
    if(playingChanged){play_->setIcon(playlistIcon(s.playing?PlaylistIcon::Pause:PlaylistIcon::Play,true));play_->setToolTip(s.playing?"暂停":"播放 / 继续");play_->setAccessibleName(play_->toolTip());}
    play_->setEnabled(s.canPlay&&!s.busy);mode_->setEnabled(s.modeEnabled&&!s.busy);
    if(modeChanged){mode_->setIcon(playlistIcon(modeIcons[s.mode]));mode_->setToolTip(modeNames[s.mode]+" · 点击切换");mode_->setAccessibleName(mode_->toolTip());}
    const int count=songs_->model()->rowCount(),row=songs_->currentIndex().row();songs_->setEnabled(!s.busy);count_->setText(QString("%1 首").arg(count));
    if(count!=lastCount_){lastCount_=count;if(expanded_&&!dragging_&&!resizing_)arrange(capsule_->mapToGlobal(QPoint()));}
    queueMode_->setText(count?"共用主窗口曲目库 · "+modeNames[s.mode]:"曲目库为空，请返回主窗口导入 MIDI");
    previous_->setEnabled(count>1&&!s.busy);next_->setEnabled(count>1&&!s.busy);up_->setEnabled(row>0&&!s.busy);down_->setEnabled(row>=0&&row<count-1&&!s.busy);remove_->setEnabled(row>=0&&!s.busy);
    if(!progress_->isSliderDown())elapsed_->setText(clockText(s.position));duration_->setText(clockText(s.duration));progress_->setEnabled(s.seekEnabled&&!s.busy&&s.duration>0);
    progress_->setToolTip(QString("播放区间 %1 — %2 · %3").arg(clockText(s.rangeFirst),clockText(s.rangeLast),s.performance?"区间内定位，松开后继续演奏，原已暂停则保持暂停":"松开后从新位置继续播放，原已暂停则保持暂停"));
    if(!progress_->isSliderDown()){const QSignalBlocker blocker(progress_);progress_->setValue(s.duration>0?qRound(std::clamp(s.position/s.duration,0.0,1.0)*1000000):0);}
    volumeBox_->setVisible(!s.performance);
    if(!volume_->isSliderDown()){const QSignalBlocker blocker(volume_);volume_->setValue(s.volume);}
    volumeText_->setText(QString::number(s.volume)+"%");volume_->setToolTip(QString("试听音量 %1%（0 为静音）").arg(s.volume));
}
void MiniPlayer::present(QScreen* preferred){
    auto* screen=preferred?preferred:QGuiApplication::primaryScreen();if(!screen)return;
    const auto available=screen->availableGeometry();QSettings settings;
    const auto savedSize=settings.value("miniPlayer/size",capsuleSize_).toSize();
    capsuleSize_={std::clamp(savedSize.width(),600,1200),std::clamp(savedSize.height(),96,220)};
    const auto origin=settings.value("miniPlayer/position",QPoint(available.right()-capsuleSize_.width()-20,available.top()+24)).toPoint();
    arrange(origin);show();constrain();
}
void MiniPlayer::arrange(const QPoint& capsuleOrigin,QScreen* preferred){
    auto* screen=preferred?preferred:QGuiApplication::screenAt(capsuleOrigin+QPoint(100,40));if(!screen)screen=QGuiApplication::primaryScreen();if(!screen)return;const auto area=screen->availableGeometry();
    const QSize capsuleExtent(std::min(capsuleSize_.width(),std::max(1,area.width()-20)),std::min(capsuleSize_.height(),std::max(1,area.height()-20)));
    const int requestedHeight=std::max(140,80+46*std::min(5,songs_->model()->rowCount()));
    const int queueHeight=std::min(requestedHeight,std::max(80,area.height()-capsuleExtent.height()-40));
    const bool nextAbove=resizing_?above_:expanded_&&capsuleOrigin.y()+capsuleExtent.height()+10+queueHeight+10>area.bottom();
    if(nextAbove!=above_){above_=nextAbove;layout_->removeWidget(capsule_);layout_->removeWidget(queue_);
        if(above_){layout_->addWidget(queue_,0,Qt::AlignHCenter);layout_->addWidget(capsule_);}else{layout_->addWidget(capsule_);layout_->addWidget(queue_,0,Qt::AlignHCenter);}}
    if(capsule_->size()!=capsuleExtent){capsule_->setFixedSize(capsuleExtent);capsule_->setStyleSheet(QString("QFrame#miniCapsule {border-radius:%1px;}").arg(capsuleExtent.height()/2));}
    queue_->setFixedSize(std::max(1,capsuleExtent.width()-44),queueHeight);queue_->setVisible(expanded_);
    const QSize extent(capsuleExtent.width()+20,capsuleExtent.height()+20+(expanded_?queueHeight+10:0));
    const auto origin=capsuleOrigin-QPoint(10,10+(above_?queueHeight+10:0));
    const QRect geometry(boundedPosition(origin,screen,extent),extent);if(this->geometry()!=geometry)setGeometry(geometry);layout_->activate();
    resizeHandle_->move(capsule_->width()-capsule_->height()/2-8,capsule_->height()-18);resizeHandle_->raise();
}
void MiniPlayer::toggleList(){const auto origin=capsule_->mapToGlobal(QPoint());expanded_=!expanded_;list_->setChecked(expanded_);list_->setToolTip(expanded_?"收起曲目列表":"展开曲目列表");list_->setAccessibleName(list_->toolTip());arrange(origin);savePosition();}
QPoint MiniPlayer::boundedPosition(const QPoint& requested,QScreen* screen,QSize extent) const{
    if(!screen)return requested;if(!extent.isValid())extent=size();const auto r=screen->availableGeometry();
    return {std::clamp(requested.x(),r.left(),std::max(r.left(),r.right()-extent.width()+1)),std::clamp(requested.y(),r.top(),std::max(r.top(),r.bottom()-extent.height()+1))};
}
void MiniPlayer::constrain(){auto* screen=QGuiApplication::screenAt(frameGeometry().center());if(!screen)screen=QGuiApplication::primaryScreen();const auto target=boundedPosition(pos(),screen);if(target!=pos())move(target);}
void MiniPlayer::savePosition(){QSettings settings;settings.setValue("miniPlayer/position",capsule_->mapToGlobal(QPoint()));settings.setValue("miniPlayer/size",capsuleSize_);}
bool MiniPlayer::eventFilter(QObject* watched,QEvent* event){
    if(watched==resizeHandle_){
        if(event->type()==QEvent::MouseButtonPress){auto* e=static_cast<QMouseEvent*>(event);if(e->button()==Qt::LeftButton){
            resizing_=true;resizeStartPoint_=e->globalPosition().toPoint();resizeStartSize_=capsule_->size();resizeOrigin_=capsule_->mapToGlobal(QPoint());gestureScreen_=QGuiApplication::screenAt(resizeOrigin_+capsule_->rect().center());return true;}}
        else if(event->type()==QEvent::MouseMove&&resizing_){
            const auto delta=static_cast<QMouseEvent*>(event)->globalPosition().toPoint()-resizeStartPoint_;
            capsuleSize_={std::clamp(resizeStartSize_.width()+delta.x(),600,1200),std::clamp(resizeStartSize_.height()+delta.y(),96,220)};arrange(resizeOrigin_,gestureScreen_);return true;
        }else if(event->type()==QEvent::MouseButtonRelease&&resizing_){resizing_=false;arrange(capsule_->mapToGlobal(QPoint()),gestureScreen_);savePosition();gestureScreen_.clear();return true;}
    }
    if(watched==capsule_||watched==title_||watched==status_){
        if(event->type()==QEvent::MouseButtonPress){auto* e=static_cast<QMouseEvent*>(event);if(e->button()==Qt::LeftButton){dragging_=true;dragOffset_=e->globalPosition().toPoint()-pos();gestureScreen_=QGuiApplication::screenAt(e->globalPosition().toPoint());return true;}}
        else if(event->type()==QEvent::MouseMove&&dragging_){
            const auto cursor=static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
            if(auto* screen=QGuiApplication::screenAt(cursor))gestureScreen_=screen;
            // Clamp before moving: never place a layered window outside the screen
            // and pull it back in a second move during the same mouse event.
            const auto target=boundedPosition(cursor-dragOffset_,gestureScreen_);if(target!=pos())move(target);return true;
        }else if(event->type()==QEvent::MouseButtonRelease&&dragging_){dragging_=false;savePosition();gestureScreen_.clear();return true;}
    }return QWidget::eventFilter(watched,event);
}
void MiniPlayer::resizeEvent(QResizeEvent* e){QWidget::resizeEvent(e);if(resizeHandle_){resizeHandle_->move(capsule_->width()-capsule_->height()/2-8,capsule_->height()-18);resizeHandle_->raise();}}
void MiniPlayer::showEvent(QShowEvent* e){QWidget::showEvent(e);
#ifdef Q_OS_WIN
    const auto hwnd=reinterpret_cast<HWND>(winId());SetWindowLongPtrW(hwnd,GWL_EXSTYLE,GetWindowLongPtrW(hwnd,GWL_EXSTYLE)|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW);
    SetWindowPos(hwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
#endif
}
bool MiniPlayer::nativeEvent(const QByteArray& type,void* message,qintptr* result){
#ifdef Q_OS_WIN
    if(static_cast<MSG*>(message)->message==WM_MOUSEACTIVATE){*result=MA_NOACTIVATE;return true;}
#endif
    return QWidget::nativeEvent(type,message,result);
}
void MiniPlayer::closeEvent(QCloseEvent* e){e->ignore();emit restoreRequested();}
}
