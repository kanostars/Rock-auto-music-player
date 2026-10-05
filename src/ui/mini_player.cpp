#include "mini_player.h"
#include "theme.h"
#include "app/preferences.h"
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
#include <QRegularExpression>
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
constexpr int minimumCapsuleWidth=400,maximumCapsuleWidth=1200;
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
private:void seek(QMouseEvent* e){const int margin=qRound(5*property("miniScale").toDouble());const int span=std::max(1,width()-2*margin);setValue(QStyle::sliderValueFromPosition(minimum(),maximum(),std::clamp(qRound(e->position().x())-margin,0,span),span));e->accept();}
};
class SongDelegate:public QStyledItemDelegate {
public:using QStyledItemDelegate::QStyledItemDelegate;
    double scale() const{const double s=parent()->property("miniScale").toDouble();return s>0?s:1;}
    QSize sizeHint(const QStyleOptionViewItem&,const QModelIndex&) const override{return {qRound(200*scale()),qRound(46*scale())};}
    void paint(QPainter* p,const QStyleOptionViewItem& option,const QModelIndex& index) const override{
        const double s=scale();auto px=[s](int n){return qRound(n*s);};
        p->save();p->setRenderHint(QPainter::Antialiasing);auto r=option.rect.adjusted(px(4),px(2),-px(4),-px(2));
        const bool selected=option.state&QStyle::State_Selected;
        if(selected||option.state&QStyle::State_MouseOver){p->setPen(Qt::NoPen);p->setBrush(Theme::color(selected?"#eaf5f1":"#f5f9fa"));p->drawRoundedRect(r,10*s,10*s);}
        p->setFont(option.font);p->setPen(Theme::color(selected?"#178e80":"#8397a3"));
        p->drawText(r.adjusted(px(9),0,0,0),Qt::AlignVCenter,selected?QString::fromUtf8("♪"):QString::number(index.row()+1).rightJustified(2,'0'));
        const auto title=index.data().toString().section('\n',0,0);p->setPen(Theme::color(selected?"#107566":"#526c7c"));
        const auto textRect=r.adjusted(px(40),0,-px(64),0);p->drawText(textRect,Qt::AlignVCenter,option.fontMetrics.elidedText(title,Qt::ElideRight,textRect.width()));
        p->setPen(Theme::color("#8397a3"));p->drawText(r.adjusted(0,0,-px(12),0),Qt::AlignRight|Qt::AlignVCenter,clockText(index.data(Qt::UserRole).toDouble()));p->restore();
    }
};
class ResizeHandle:public QWidget {
public:explicit ResizeHandle(QWidget* parent):QWidget(parent){setFixedSize(18,14);setCursor(Qt::SizeFDiagCursor);setToolTip("拖动等比例缩放胶囊，大小会自动记住");setAccessibleName("调整胶囊大小");}
protected:void paintEvent(QPaintEvent*) override{QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.scale(width()/18.0,height()/14.0);p.setPen(QPen(Theme::color("#a8bfc5"),1.3,Qt::SolidLine,Qt::RoundCap));p.drawLine(4,11,14,1);p.drawLine(9,11,14,6);}
};
}
MiniPlayer::MiniPlayer(QListWidget* library):QWidget(nullptr,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|Qt::WindowDoesNotAcceptFocus){
    setObjectName("miniPlayer");setWindowTitle("九键手碟 · 小窗播放器");setAttribute(Qt::WA_TranslucentBackground);setAttribute(Qt::WA_ShowWithoutActivating);setAttribute(Qt::WA_QuitOnClose,false);setFocusPolicy(Qt::NoFocus);
    Theme::setStyle(this,R"(
        QWidget {font-family:'Microsoft YaHei UI';font-size:12px;color:#203d4e;}
        QFrame#miniCapsule {background:white;border:1px solid #dce6eb;border-radius:56px;}
        QFrame#miniQueue,QFrame#miniOpacityPanel {background:white;border:1px solid #dce6eb;border-radius:20px;}
        QLabel {background:transparent;border:0;}
        QLabel#miniTitle {font-size:15px;font-weight:600;}
        QLabel#miniStatus,QLabel#miniElapsed,QLabel#miniDuration,QLabel#miniCount,QLabel#miniQueueMode,QLabel#miniVolumeText {font-size:11px;color:#8397a3;}
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
    auto* separator=new QFrame;separator->setFixedSize(1,26);Theme::setStyle(separator,"background:#e4ebef;");top->addSpacing(5);top->addWidget(separator);top->addSpacing(5);
    auto* restore=action(PlaylistIcon::Restore,"返回主窗口","miniRestore");auto* quit=action(PlaylistIcon::Power,"退出整个程序","miniQuit");top->addWidget(restore);top->addWidget(quit);
    opacityButton_=action(PlaylistIcon::Opacity,"调整小窗透明度","miniOpacityButton");opacityButton_->setCheckable(true);top->insertWidget(top->count()-2,opacityButton_);
    auto* progressRow=new QHBoxLayout;progressRow->setSpacing(9);progressRow->setContentsMargins(76,0,10,0);capsuleLayout->addLayout(progressRow);
    elapsed_=new QLabel("00:00");elapsed_->setObjectName("miniElapsed");duration_=new QLabel("00:00");duration_->setObjectName("miniDuration");
    progress_=new SeekSlider;progress_->setObjectName("miniProgress");progress_->setAccessibleName("当前歌曲播放进度");progress_->setFixedHeight(14);progressRow->addWidget(elapsed_);progressRow->addWidget(progress_,1);progressRow->addWidget(duration_);
    volumeBox_=new QWidget;volumeBox_->setObjectName("miniVolumeBox");auto* volumeRow=new QHBoxLayout(volumeBox_);volumeRow->setContentsMargins(5,0,0,0);volumeRow->setSpacing(5);
    speaker_=new QLabel;speaker_->setToolTip("试听音量");volumeRow->addWidget(speaker_);
    volume_=new QSlider(Qt::Horizontal);volume_->setObjectName("miniVolume");volume_->setAccessibleName("试听音量");volume_->setFocusPolicy(Qt::NoFocus);volume_->setRange(0,100);volume_->setFixedWidth(64);volumeRow->addWidget(volume_);
    volumeText_=new QLabel;volumeText_->setObjectName("miniVolumeText");volumeText_->setFixedWidth(30);volumeRow->addWidget(volumeText_);progressRow->addWidget(volumeBox_);volumeBox_->hide();
    opacityPanel_=new QFrame;opacityPanel_->setObjectName("miniOpacityPanel");auto* opacityRow=new QHBoxLayout(opacityPanel_);opacityRow->setContentsMargins(18,10,18,10);opacityRow->setSpacing(12);
    opacityRow->addWidget(new QLabel("透明度"));opacity_=new QSlider(Qt::Horizontal);opacity_->setObjectName("miniOpacity");opacity_->setAccessibleName("小窗透明度");opacity_->setFocusPolicy(Qt::NoFocus);opacity_->setRange(20,100);opacity_->setFixedHeight(20);
    opacity_->setValue(std::clamp(QSettings().value("miniPlayer/opacity",100).toInt(),20,100));opacityRow->addWidget(opacity_,1);opacityText_=new QLabel;opacityText_->setFixedWidth(40);opacityRow->addWidget(opacityText_);layout_->addWidget(opacityPanel_,0,Qt::AlignHCenter);opacityPanel_->hide();
    queue_=new QFrame;queue_->setObjectName("miniQueue");auto* queueLayout=new QVBoxLayout(queue_);queueLayout->setContentsMargins(16,10,16,12);queueLayout->setSpacing(5);
    auto* head=new QHBoxLayout;head->addWidget(new QLabel("曲目列表"));count_=new QLabel;count_->setObjectName("miniCount");head->addWidget(count_);head->addStretch();
    up_=action(PlaylistIcon::Up,"上移当前曲目","miniUp");down_=action(PlaylistIcon::Down,"下移当前曲目","miniDown");remove_=action(PlaylistIcon::Remove,"移出共享曲目库，不删除原 MIDI 文件","miniRemove");for(auto* b:{up_,down_,remove_})head->addWidget(b);queueLayout->addLayout(head);
    songs_=new QListView;songs_->setObjectName("miniSongs");songs_->setModel(library->model());songs_->setSelectionModel(library->selectionModel());songs_->setItemDelegate(new SongDelegate(songs_));songs_->setUniformItemSizes(true);songs_->setMouseTracking(true);songs_->setSelectionMode(QAbstractItemView::SingleSelection);songs_->setEditTriggers(QAbstractItemView::NoEditTriggers);songs_->setFocusPolicy(Qt::NoFocus);songs_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);queueLayout->addWidget(songs_,1);
    queueMode_=new QLabel;queueMode_->setObjectName("miniQueueMode");queueLayout->addWidget(queueMode_);layout_->addWidget(queue_,0,Qt::AlignHCenter);queue_->hide();
    capsule_->installEventFilter(this);capsule_->setCursor(Qt::SizeAllCursor);resizeHandle_=new ResizeHandle(capsule_);resizeHandle_->setObjectName("miniResizeHandle");resizeHandle_->installEventFilter(this);
    connect(audition_,&QPushButton::clicked,this,[this]{emit sourceRequested(false);});connect(performance_,&QPushButton::clicked,this,[this]{emit sourceRequested(true);});
    connect(play_,&QPushButton::clicked,this,&MiniPlayer::playRequested);connect(previous_,&QPushButton::clicked,this,[this]{emit navigateRequested(true);});connect(next_,&QPushButton::clicked,this,[this]{emit navigateRequested(false);});
    connect(mode_,&QPushButton::clicked,this,&MiniPlayer::modeRequested);connect(list_,&QPushButton::clicked,this,&MiniPlayer::toggleList);connect(restore,&QPushButton::clicked,this,&MiniPlayer::restoreRequested);connect(quit,&QPushButton::clicked,this,&MiniPlayer::quitRequested);
    connect(opacityButton_,&QPushButton::clicked,this,&MiniPlayer::toggleOpacity);
    auto updateOpacity=[this](int value){setWindowOpacity(value/100.0);opacityText_->setText(QString::number(value)+"%");opacity_->setToolTip(QString("小窗透明度 %1%（100% 为不透明）").arg(value));opacityButton_->setToolTip(QString("调整小窗透明度 · %1%").arg(value));};
    connect(opacity_,&QSlider::valueChanged,this,[updateOpacity](int value){updateOpacity(value);QSettings().setValue("miniPlayer/opacity",value);});updateOpacity(opacity_->value());
    connect(progress_,&QSlider::sliderPressed,this,[this]{if(state_.seekEnabled)emit seekStarted();});
    connect(progress_,&QSlider::valueChanged,this,[this](int value){if(progress_->isSliderDown())elapsed_->setText(clockText(state_.duration*value/1000000.0));});
    connect(progress_,&QSlider::sliderReleased,this,[this]{if(state_.seekEnabled)emit seekRequested(state_.duration*progress_->value()/1000000.0);});
    connect(songs_,&QListView::clicked,this,[this](const QModelIndex& index){if(!state_.busy&&index.isValid())emit songPlayRequested(index.row());});
    connect(volume_,&QSlider::valueChanged,this,[this](int value){volumeText_->setText(QString::number(value)+"%");volume_->setToolTip(QString("试听音量 %1%（0 为静音）").arg(value));emit volumeRequested(value);});
    connect(up_,&QPushButton::clicked,this,[this]{const int row=songs_->currentIndex().row();emit moveRequested(row,row-1);});connect(down_,&QPushButton::clicked,this,[this]{const int row=songs_->currentIndex().row();emit moveRequested(row,row+1);});connect(remove_,&QPushButton::clicked,this,[this]{emit removeRequested(songs_->currentIndex().row());});
    captureMetrics();applyScale(1);
    connect(&Preferences::instance(),&Preferences::themeChanged,this,[this]{speaker_->setPixmap(playlistIcon(PlaylistIcon::Volume).pixmap(qRound(16*scale_),qRound(16*scale_)));});
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
    const int width=std::clamp(savedSize.width(),minimumCapsuleWidth,maximumCapsuleWidth);capsuleSize_={width,qRound(width*112.0/664)};
    const auto origin=settings.value("miniPlayer/position",QPoint(available.right()-capsuleSize_.width()-20,available.top()+24)).toPoint();
    arrange(origin);show();
}
void MiniPlayer::arrange(const QPoint& capsuleOrigin,QScreen* preferred){
    auto* screen=preferred?preferred:QGuiApplication::screenAt(capsuleOrigin+QPoint(100,40));if(!screen)screen=QGuiApplication::primaryScreen();if(!screen)return;const auto area=screen->availableGeometry();
    applyScale(std::min({capsuleSize_.width()/664.0,std::max(1,area.width()-2)/684.0,std::max(1,area.height()-2)/132.0}));
    auto px=[this](int n){return qRound(n*scale_);};
    const QSize capsuleExtent(px(664),px(112));const int margin=px(10),gap=px(10),opacityHeight=px(52);
    const int opacityExtra=opacityExpanded_?opacityHeight+gap:0;
    const int requestedHeight=px(std::max(140,80+46*std::min(5,songs_->model()->rowCount())));
    const int queueHeight=std::min(requestedHeight,std::max(1,area.height()-capsuleExtent.height()-2*margin-opacityExtra-gap));
    const int extra=opacityExtra+(expanded_?queueHeight+gap:0);
    const bool nextAbove=resizing_?above_:extra>0&&capsuleOrigin.y()+capsuleExtent.height()+extra+margin>area.bottom();
    if(nextAbove!=above_){above_=nextAbove;layout_->removeWidget(capsule_);layout_->removeWidget(queue_);layout_->removeWidget(opacityPanel_);
        if(above_){layout_->addWidget(queue_,0,Qt::AlignHCenter);layout_->addWidget(opacityPanel_,0,Qt::AlignHCenter);layout_->addWidget(capsule_);}
        else{layout_->addWidget(capsule_);layout_->addWidget(opacityPanel_,0,Qt::AlignHCenter);layout_->addWidget(queue_,0,Qt::AlignHCenter);}}
    capsule_->setFixedSize(capsuleExtent);
    // CSS drops oversized corner radii; floor half of an odd pixel height.
    const QString rounded=QString("QFrame#miniCapsule {border-radius:%1px;}").arg(capsuleExtent.height()/2);
    if(capsule_->styleSheet()!=rounded)Theme::setStyle(capsule_,rounded);
    queue_->setFixedSize(px(620),queueHeight);queue_->setVisible(expanded_);
    opacityPanel_->setFixedSize(px(620),opacityHeight);opacityPanel_->setVisible(opacityExpanded_);
    const QSize extent(capsuleExtent.width()+2*margin,capsuleExtent.height()+2*margin+extra);
    const auto origin=capsuleOrigin-QPoint(margin,margin+(above_?extra:0));
    const QRect geometry(boundedPosition(origin,screen,extent),extent);if(this->geometry()!=geometry)setGeometry(geometry);layout_->activate();
    positionResizeHandle();setState(state_);
}
void MiniPlayer::toggleList(){const auto origin=capsule_->mapToGlobal(QPoint());expanded_=!expanded_;list_->setChecked(expanded_);list_->setToolTip(expanded_?"收起曲目列表":"展开曲目列表");list_->setAccessibleName(list_->toolTip());arrange(origin);savePosition();}
void MiniPlayer::toggleOpacity(){const auto origin=capsule_->mapToGlobal(QPoint());opacityExpanded_=!opacityExpanded_;opacityButton_->setChecked(opacityExpanded_);arrange(origin);savePosition();}
void MiniPlayer::captureMetrics(){
    baseStyle_=Theme::sourceStyle(this);
    for(auto* widget:findChildren<QWidget*>()){
        const auto minimum=widget->minimumSize(),maximum=widget->maximumSize();
        const QSize fixed(minimum.width()==maximum.width()?minimum.width():-1,minimum.height()==maximum.height()?minimum.height():-1);
        const auto* button=qobject_cast<QPushButton*>(widget);const QSize icon=button?button->iconSize():QSize();
        if(fixed.width()>0||fixed.height()>0||button)widgetMetrics_.push_back({widget,fixed,icon});
    }
    for(auto* layout:findChildren<QLayout*>()){
        layoutMetrics_.push_back({layout,layout->contentsMargins(),layout->spacing()});
        for(int i=0;i<layout->count();++i)if(auto* spacer=layout->itemAt(i)->spacerItem())spacerMetrics_.push_back({spacer,spacer->sizeHint()});
    }
}
void MiniPlayer::applyScale(double factor){
    if(qFuzzyCompare(scale_,factor))return;scale_=factor;auto px=[factor](int n){return qRound(n*factor);};
    // Always scale the captured base metrics, never the already scaled UI.
    QString scaled;qsizetype offset=0;const QRegularExpression pattern("(\\d+)px");auto matches=pattern.globalMatch(baseStyle_);
    while(matches.hasNext()){const auto match=matches.next();scaled+=baseStyle_.mid(offset,match.capturedStart()-offset)+QString::number(px(match.captured(1).toInt()))+"px";offset=match.capturedEnd();}
    scaled+=baseStyle_.mid(offset);Theme::setStyle(this,scaled);
    for(const auto& m:widgetMetrics_){
        if(m.fixed.width()>0)m.widget->setFixedWidth(std::max(1,px(m.fixed.width())));
        if(m.fixed.height()>0)m.widget->setFixedHeight(std::max(1,px(m.fixed.height())));
        if(auto* b=qobject_cast<QPushButton*>(m.widget))b->setIconSize({px(m.icon.width()),px(m.icon.height())});
    }
    for(const auto& m:layoutMetrics_){const auto& r=m.margins;m.layout->setContentsMargins(px(r.left()),px(r.top()),px(r.right()),px(r.bottom()));if(m.spacing>=0)m.layout->setSpacing(px(m.spacing));}
    for(const auto& m:spacerMetrics_){const auto policy=m.spacer->sizePolicy();m.spacer->changeSize(px(m.size.width()),px(m.size.height()),policy.horizontalPolicy(),policy.verticalPolicy());}
    speaker_->setPixmap(playlistIcon(PlaylistIcon::Volume).pixmap(px(16),px(16)));
    songs_->setProperty("miniScale",factor);progress_->setProperty("miniScale",factor);songs_->doItemsLayout();
}
void MiniPlayer::positionResizeHandle(){if(resizeHandle_){resizeHandle_->move(capsule_->width()-capsule_->height()/2-qRound(8*scale_),capsule_->height()-qRound(18*scale_));resizeHandle_->raise();}}
QPoint MiniPlayer::boundedPosition(const QPoint& requested,QScreen* screen,QSize extent) const{
    if(!screen)return requested;if(!extent.isValid())extent=size();const auto r=screen->availableGeometry();
    return {std::clamp(requested.x(),r.left(),std::max(r.left(),r.right()-extent.width()+1)),std::clamp(requested.y(),r.top(),std::max(r.top(),r.bottom()-extent.height()+1))};
}
void MiniPlayer::savePosition(){QSettings settings;settings.setValue("miniPlayer/position",capsule_->mapToGlobal(QPoint()));settings.setValue("miniPlayer/size",capsuleSize_);}
bool MiniPlayer::eventFilter(QObject* watched,QEvent* event){
    if(watched==resizeHandle_){
        if(event->type()==QEvent::MouseButtonPress){auto* e=static_cast<QMouseEvent*>(event);if(e->button()==Qt::LeftButton){
            resizing_=true;resizeStartPoint_=e->globalPosition().toPoint();resizeStartSize_=capsule_->size();resizeOrigin_=capsule_->mapToGlobal(QPoint());gestureScreen_=QGuiApplication::screenAt(resizeOrigin_+capsule_->rect().center());return true;}}
        else if(event->type()==QEvent::MouseMove&&resizing_){
            const auto delta=static_cast<QMouseEvent*>(event)->globalPosition().toPoint()-resizeStartPoint_;
            const double x=delta.x()/664.0,y=delta.y()/112.0;const double change=std::abs(x)>=std::abs(y)?x:y;
            const int width=std::clamp(qRound(resizeStartSize_.width()+change*664),minimumCapsuleWidth,maximumCapsuleWidth);capsuleSize_={width,qRound(width*112.0/664)};arrange(resizeOrigin_,gestureScreen_);return true;
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
void MiniPlayer::resizeEvent(QResizeEvent* e){QWidget::resizeEvent(e);positionResizeHandle();}
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
