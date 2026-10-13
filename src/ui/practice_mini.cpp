#include "practice_mini.h"
#include "number_score_view.h"
#include "playback_icons.h"
#include "theme.h"
#include "app/preferences.h"
#include <QCloseEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTouchEvent>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
#include <cmath>
#include <functional>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rock {
namespace {
constexpr double numberMiniScale=.5;
const QString baseStyle=QString::fromUtf8(R"(
    QWidget {font-family:'Microsoft YaHei UI';font-size:12px;color:#203d4e;}
    QFrame#practiceMiniCapsule {background:#ffffff;border:1px solid #dce6eb;border-radius:22px;}
    QLabel {background:transparent;border:0;}
    QLabel#practiceMiniTitle {font-size:15px;font-weight:600;}
    QLabel#practiceMiniStatus {font-size:11px;font-weight:600;color:#107566;}
    QLabel#practiceMiniProgress,QLabel#practiceMiniEmpty {font-size:11px;color:#8397a3;}
    QLabel#practiceMiniClock,QLabel#practiceMiniVolumeValue {font-size:10px;color:#8397a3;}
    QWidget#practiceMiniKeys {background:#f5f9fa;border:1px solid #e0e9ed;border-radius:22px;}
    QLabel[practiceMiniKey=true] {font-size:18px;font-weight:600;background:transparent;border:1px solid transparent;border-radius:9px;}
    QLabel[practiceMiniKey=true][matched=true] {background:#d9eeea;border-color:#8ac8bb;color:#0e796c;}
    QPushButton {background:#f7fafb;border:1px solid #dce6eb;border-radius:7px;padding:0;font-size:11px;font-weight:600;}
    QPushButton:hover {background:#e8f4f1;border-color:#8ac8bb;}
    QPushButton:checked {background:#e5f3ef;border-color:#8ac8bb;color:#0e796c;}
    QPushButton:disabled {background:#f5f7f8;color:#afbdc5;border-color:#e6edf0;}
    QPushButton#practiceMiniPlay {background:#178e80;border-color:#178e80;color:white;}
    QPushButton#practiceMiniPlay:hover {background:#107566;}
    QPushButton#practiceMiniPlay:disabled {background:#c5dcd7;border-color:#c5dcd7;}
    QFrame#practiceMiniAppearance {background:#f7fafb;border:1px solid #e0e9ed;border-radius:9px;}
    QSlider::groove:horizontal {height:4px;background:#e7eef0;border-radius:2px;}
    QSlider::sub-page:horizontal {background:#178e80;border-radius:2px;}
    QSlider::handle:horizontal {width:10px;height:10px;margin:-4px 0;border-radius:5px;background:#178e80;}
    QToolTip {background:#203d4e;color:white;padding:6px;border:0;}
)");
QLabel* label(const char* name,const QString& text={}) {
    auto* value=new QLabel(text);value->setObjectName(name);value->setTextFormat(Qt::PlainText);
    value->setFocusPolicy(Qt::NoFocus);return value;
}
QPushButton* button(const char* name,const QString& text,const QString& tip) {
    auto* value=new QPushButton(text);value->setObjectName(name);value->setFocusPolicy(Qt::NoFocus);
    value->setCursor(Qt::PointingHandCursor);value->setToolTip(tip);value->setAccessibleName(tip);return value;
}
QString degreeText(int target) {
    if(target==0)return QString("6")+QChar(0x0323);
    if(target>=6)return QString::number(target-5)+QChar(0x0307);
    return QString::number(target+2);
}
QString fullDegree(int target) {
    if(target==0)return "低音 6";
    if(target>=6)return QString("高音 %1").arg(target-5);
    return QString::number(target+2);
}
QString clockText(double seconds,bool precise=false) {
    const auto hundredths=static_cast<qint64>(std::round(std::clamp(std::isfinite(seconds)?seconds:0,0.0,1e12)*100));
    auto text=QString("%1:%2").arg(hundredths/6000,2,10,QChar('0')).arg(hundredths/100%60,2,10,QChar('0'));
    if(precise)text+=QString(".%1").arg(hundredths%100,2,10,QChar('0'));return text;
}
void actionTip(QPushButton* control,const QString& text){control->setToolTip(text);control->setAccessibleName(text);}
class MiniSeekSlider:public QSlider {
public:
    MiniSeekSlider():QSlider(Qt::Horizontal){setRange(0,1000000);setFocusPolicy(Qt::NoFocus);setAttribute(Qt::WA_AcceptTouchEvents);}
    std::function<void()> started,completed;
protected:
    void mousePressEvent(QMouseEvent* event) override {
        if(event->button()!=Qt::LeftButton||!isEnabled())return;
        begin(event->position());event->accept();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if(gesture_){locate(event->position());event->accept();}else QSlider::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if(event->button()==Qt::LeftButton&&gesture_){locate(event->position());finish();event->accept();}
        else QSlider::mouseReleaseEvent(event);
    }
    void wheelEvent(QWheelEvent* event) override{event->ignore();}
    bool event(QEvent* event) override {
        if(event->type()==QEvent::TouchBegin||event->type()==QEvent::TouchUpdate||event->type()==QEvent::TouchEnd||event->type()==QEvent::TouchCancel){
            auto* touch=static_cast<QTouchEvent*>(event);const auto& points=touch->points();
            if(event->type()==QEvent::TouchBegin){if(!isEnabled()||points.isEmpty())return false;begin(points.front().position());}
            else if(gesture_){
                if(event->type()==QEvent::TouchCancel)setValue(original_);
                else if(!points.isEmpty())locate(points.front().position());
                if(event->type()==QEvent::TouchEnd||event->type()==QEvent::TouchCancel)finish();
            }
            event->accept();return true;
        }
        if(gesture_&&(event->type()==QEvent::Hide||event->type()==QEvent::UngrabMouse)){setValue(original_);finish();}
        return QSlider::event(event);
    }
private:
    bool gesture_{};int original_{};
    void begin(const QPointF& point){if(gesture_)return;gesture_=true;original_=value();setSliderDown(true);if(started)started();locate(point);}
    void finish(){gesture_=false;setSliderDown(false);if(completed)completed();}
    void locate(const QPointF& point){QStyleOptionSlider option;initStyleOption(&option);const auto handle=style()->subControlRect(QStyle::CC_Slider,&option,QStyle::SC_SliderHandle,this);const int span=std::max(1,width()-handle.width());setValue(QStyle::sliderValueFromPosition(minimum(),maximum(),std::clamp(qRound(point.x())-handle.width()/2,0,span),span,option.upsideDown));}
};
}

PracticeMini::PracticeMini(QWidget* parent):QWidget(parent,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|Qt::WindowDoesNotAcceptFocus) {
    setObjectName("practiceMini");setWindowTitle("九键手碟 · 跟练小窗");
    setAttribute(Qt::WA_TranslucentBackground);setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_QuitOnClose,false);setFocusPolicy(Qt::NoFocus);
    QSettings saved;
    numbers_=saved.value("practiceMini/numbers",false).toBool();
    appearanceExpanded_=saved.value("practiceMini/appearanceExpanded",false).toBool();
    layout_=new QVBoxLayout(this);layout_->setSizeConstraint(QLayout::SetNoConstraint);
    capsule_=new QFrame;capsule_->setObjectName("practiceMiniCapsule");layout_->addWidget(capsule_);
    capsuleLayout_=new QVBoxLayout(capsule_);capsuleLayout_->setSizeConstraint(QLayout::SetNoConstraint);
    headerLayout_=new QHBoxLayout;capsuleLayout_->addLayout(headerLayout_);
    title_=label("practiceMiniTitle","歌曲跟练");title_->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Preferred);title_->setMinimumWidth(0);
    headerLayout_->addWidget(title_);
    status_=label("practiceMiniStatus","准备跟弹");headerLayout_->addWidget(status_);
    headerLayout_->addStretch();
    progress_=label("practiceMiniProgress","0 / 0 组");progress_->setAlignment(Qt::AlignRight|Qt::AlignVCenter);headerLayout_->addWidget(progress_);
    views_=new QStackedWidget;views_->setObjectName("practiceMiniViews");views_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);capsuleLayout_->addWidget(views_);
    keyView_=new QWidget;keyView_->setObjectName("practiceMiniKeys");keyView_->setAttribute(Qt::WA_StyledBackground);keysLayout_=new QHBoxLayout(keyView_);keysLayout_->setContentsMargins(0,0,0,0);keysLayout_->addStretch();
    for(int target=0;target<9;++target) {
        auto* chip=label(qPrintable(QString("practiceMiniKey%1").arg(target)));
        chip->setProperty("practiceMiniKey",true);chip->setProperty("matched",false);chip->setAlignment(Qt::AlignCenter);
        chip->hide();chips_[target]=chip;keysLayout_->addWidget(chip);
    }
    empty_=label("practiceMiniEmpty","等待下一个按键组");keysLayout_->addWidget(empty_);keysLayout_->addStretch();views_->addWidget(keyView_);
    measure_=new NumberMeasureView;measure_->setObjectName("practiceMiniMeasure");measure_->setFocusPolicy(Qt::NoFocus);views_->addWidget(measure_);
    views_->setCurrentWidget(numbers_?static_cast<QWidget*>(measure_):keyView_);
    toolsLayout_=new QHBoxLayout;capsuleLayout_->addLayout(toolsLayout_);
    play_=button("practiceMiniPlay",{},"开始 / 暂停练习");
    restart_=button("practiceMiniRestart",{},"从练习区间起点重新开始");
    mode_=button("practiceMiniMode",{},"切换自动预览 / 跟弹练习");mode_->setCheckable(true);
    display_=button("practiceMiniDisplay",{},"切换按键提示 / 当前小节数字简谱");display_->setCheckable(true);display_->setChecked(numbers_);
    appearanceButton_=button("practiceMiniAppearanceButton",{},"展开透明度和大小设置");appearanceButton_->setCheckable(true);appearanceButton_->setChecked(appearanceExpanded_);
    restore_=button("practiceMiniRestore",{},"返回完整练习页面");
    for(auto* value:{play_,restart_,mode_,display_})toolsLayout_->addWidget(value);
    clock_=label("practiceMiniClock","00:00 / 00:00");toolsLayout_->addWidget(clock_);
    auto* seek=new MiniSeekSlider;seek_=seek;seek_->setObjectName("practiceMiniProgressSlider");seek_->setAccessibleName("跟练歌曲进度，点击或拖动跳转");seek_->setCursor(Qt::PointingHandCursor);toolsLayout_->addWidget(seek_,1);
    volumeButton_=button("practiceMiniVolumeButton",{},"静音 / 恢复小窗音量");volumeButton_->setCheckable(true);toolsLayout_->addWidget(volumeButton_);
    volume_=new QSlider(Qt::Horizontal);volume_->setObjectName("practiceMiniVolume");volume_->setFocusPolicy(Qt::NoFocus);volume_->setCursor(Qt::PointingHandCursor);volume_->setRange(0,100);volume_->setAccessibleName("跟练小窗音量");toolsLayout_->addWidget(volume_);
    volumeValue_=label("practiceMiniVolumeValue","60%");toolsLayout_->addWidget(volumeValue_);toolsLayout_->addWidget(appearanceButton_);toolsLayout_->addWidget(restore_);
    appearance_=new QFrame;appearance_->setObjectName("practiceMiniAppearance");appearanceLayout_=new QVBoxLayout(appearance_);
    appearance_->setCursor(Qt::ArrowCursor);
    opacityLayout_=new QHBoxLayout;appearanceLayout_->addLayout(opacityLayout_);opacityLayout_->addWidget(label("practiceMiniOpacityLabel","透明度"));
    opacity_=new QSlider(Qt::Horizontal);opacity_->setObjectName("practiceMiniOpacity");opacity_->setFocusPolicy(Qt::NoFocus);opacity_->setCursor(Qt::PointingHandCursor);opacity_->setRange(30,100);
    opacity_->setValue(std::clamp(saved.value("practiceMini/opacity",100).toInt(),30,100));opacity_->setAccessibleName("跟练小窗透明度");opacityLayout_->addWidget(opacity_,1);
    opacityValue_=label("practiceMiniOpacityValue");opacityLayout_->addWidget(opacityValue_);
    sizeLayout_=new QHBoxLayout;appearanceLayout_->addLayout(sizeLayout_);sizeLayout_->addWidget(label("practiceMiniSizeLabel","大小"));
    sizeSlider_=new QSlider(Qt::Horizontal);sizeSlider_->setObjectName("practiceMiniSize");sizeSlider_->setFocusPolicy(Qt::NoFocus);sizeSlider_->setCursor(Qt::PointingHandCursor);sizeSlider_->setRange(70,150);sizeSlider_->setSingleStep(5);
    sizeSlider_->setValue(std::clamp(saved.value("practiceMini/scale",100).toInt(),70,150));sizeSlider_->setAccessibleName("跟练小窗大小");sizeLayout_->addWidget(sizeSlider_,1);
    sizeValue_=label("practiceMiniSizeValue");sizeLayout_->addWidget(sizeValue_);capsuleLayout_->addWidget(appearance_);appearance_->setVisible(appearanceExpanded_);
    for(auto* value:{static_cast<QWidget*>(this),static_cast<QWidget*>(capsule_),static_cast<QWidget*>(title_),static_cast<QWidget*>(status_),static_cast<QWidget*>(progress_),static_cast<QWidget*>(keyView_),static_cast<QWidget*>(measure_),static_cast<QWidget*>(empty_)}) {
        value->installEventFilter(this);value->setCursor(Qt::SizeAllCursor);
    }
    for(auto* value:chips_){value->installEventFilter(this);value->setCursor(Qt::SizeAllCursor);}
    connect(play_,&QPushButton::clicked,this,&PracticeMini::playRequested);
    connect(restart_,&QPushButton::clicked,this,&PracticeMini::restartRequested);
    connect(mode_,&QPushButton::clicked,this,[this]{emit followingRequested(!state_.following);});
    connect(restore_,&QPushButton::clicked,this,[this]{savePosition();emit restoreRequested();});
    connect(display_,&QPushButton::clicked,this,[this] {
        numbers_=!numbers_;display_->setChecked(numbers_);actionTip(display_,numbers_?"当前显示数字简谱 · 点击切换按键提示":"当前显示按键提示 · 点击切换数字简谱");refreshIcons();
        views_->setCurrentWidget(numbers_?static_cast<QWidget*>(measure_):keyView_);
        QSettings().setValue("practiceMini/numbers",numbers_);arrange(pos(),screen_);
    });
    connect(appearanceButton_,&QPushButton::clicked,this,[this] {
        appearanceExpanded_=!appearanceExpanded_;appearance_->setVisible(appearanceExpanded_);
        appearanceButton_->setChecked(appearanceExpanded_);actionTip(appearanceButton_,appearanceExpanded_?"收起透明度和大小设置":"展开透明度和大小设置");
        QSettings().setValue("practiceMini/appearanceExpanded",appearanceExpanded_);arrange(pos(),screen_);
    });
    connect(opacity_,&QSlider::valueChanged,this,[this](int value) {
        setWindowOpacity(value/100.0);opacityValue_->setText(QString::number(value)+"%");QSettings().setValue("practiceMini/opacity",value);
    });
    connect(sizeSlider_,&QSlider::valueChanged,this,[this](int value) {
        sizeValue_->setText(QString::number(value)+"%");QSettings().setValue("practiceMini/scale",value);arrange(pos(),screen_);
    });
    seek->started=[this]{seekDuration_=std::isfinite(state_.duration)?std::max(0.0,state_.duration):0;emit seekStarted();};
    seek->completed=[this]{emit seekRequested(seekDuration_*seek_->value()/seek_->maximum());};
    connect(seek_,&QSlider::valueChanged,this,[this]{if(seek_->isSliderDown())refreshClock();});
    connect(volume_,&QSlider::valueChanged,this,[this](int value){if(value>0)audibleVolumes_[state_.following?1:0]=value;volumeValue_->setText(QString::number(value)+"%");volumeButton_->setChecked(value==0);refreshIcons();emit volumeRequested(value);});
    connect(volumeButton_,&QPushButton::clicked,this,[this]{volume_->setValue(volume_->value()>0?0:audibleVolumes_[state_.following?1:0]);});
    connect(measure_,&NumberMeasureView::preferredSizeChanged,this,[this]{if(numbers_&&isVisible())arrange(pos(),screen_);});
    connect(&Preferences::instance(),&Preferences::themeChanged,this,&PracticeMini::refreshIcons);
    connect(qGuiApp,&QGuiApplication::screenRemoved,this,[this](QScreen*){if(isVisible())arrange(pos());});
    setWindowOpacity(opacity_->value()/100.0);opacityValue_->setText(QString::number(opacity_->value())+"%");sizeValue_->setText(QString::number(sizeSlider_->value())+"%");
    applyScale(sizeSlider_->value()/100.0*(numbers_?numberMiniScale:1));setState(state_);
}

void PracticeMini::setSong(const QString& name,std::shared_ptr<const Song> song,std::shared_ptr<const Conversion> result,const Settings& settings) {
    state_.title=name;measure_->setSong(std::move(song),std::move(result),settings);refreshText();
    if(isVisible())arrange(pos(),screen_);
}
void PracticeMini::setState(const PracticeMiniState& state) {
    const bool iconsChanged=state.playing!=state_.playing||state.following!=state_.following||(std::clamp(state.volume,0,100)==0)!=(volume_->value()==0);
    state_=state;refreshText();
    mode_->setChecked(state.following);actionTip(mode_,state.following?"当前跟弹练习 · 点击切换自动预览":"当前自动预览 · 点击切换跟弹练习");
    actionTip(play_,state.playing?"暂停练习":"开始 / 继续练习");
    play_->setEnabled(state.canPlay);restart_->setEnabled(state.canPlay);mode_->setEnabled(state.canPlay);
    const bool seekEnabled=state.canPlay&&std::isfinite(state.duration)&&state.duration>0;seek_->setEnabled(seekEnabled);
    if(!seek_->isSliderDown()){
        const QSignalBlocker blocker(seek_);const auto position=std::isfinite(state.playbackPosition)?state.playbackPosition:0;
        seek_->setValue(seekEnabled?qRound(std::clamp(position/state.duration,0.0,1.0)*seek_->maximum()):0);
    }
    const int volume=std::clamp(state.volume,0,100);if(volume>0)audibleVolumes_[state.following?1:0]=volume;
    if(!volume_->isSliderDown()){const QSignalBlocker blocker(volume_);volume_->setValue(volume);}
    volumeValue_->setText(QString::number(volume)+"%");volumeButton_->setChecked(volume==0);
    actionTip(volumeButton_,volume==0?"小窗已静音 · 点击恢复音量":"静音小窗音频");volume_->setToolTip(QString("小窗音量 %1% · 与完整练习页独立").arg(volume));refreshClock();
    for(int target=0;target<9;++target) {
        auto* chip=chips_[target];const bool expected=state.keys&(1u<<target),matched=state.matched&(1u<<target);
        const auto key=QString(QChar(static_cast<char16_t>(state.inputKeys[target])));
        chip->setText(degreeText(target)+" · "+key);chip->setVisible(expected);
        const auto description=QString("%1 · 按键 %2%3").arg(fullDegree(target),key,matched?" · 已匹配":"");
        chip->setToolTip(description);chip->setAccessibleName(description);
        if(chip->property("matched").toBool()!=matched){chip->setProperty("matched",matched);chip->style()->unpolish(chip);chip->style()->polish(chip);chip->update();}
    }
    empty_->setVisible((state.keys&0x1ff)==0);if(iconsChanged)refreshIcons();
    measure_->setPosition(std::isfinite(state.position)?std::max(0.0,state.position):0,state.keys,state.matched,state.following?state.practiceGroup:-1);
}
void PracticeMini::refreshText() {
    QString status=state_.status.section(" · ",0,0);if(status.isEmpty())status=state_.following?"准备跟弹":"准备预览";
    if(status=="暂无可练习音符")status="暂无音符";else status=status.left(4);
    status_->setText(status);status_->setToolTip(state_.status+"\n"+state_.hint);capsule_->setToolTip(state_.hint);
    progress_->setText(state_.progress);
    const auto fullTitle=state_.title.isEmpty()?QString("歌曲跟练"):state_.title;
    const auto margins=capsuleLayout_->contentsMargins();
    const int available=std::max(1,capsule_->width()-margins.left()-margins.right()-status_->sizeHint().width()-progress_->sizeHint().width()-3*headerLayout_->spacing());
    const int titleWidth=std::min(available,std::max(1,title_->fontMetrics().horizontalAdvance(fullTitle)));
    title_->setFixedWidth(titleWidth);title_->setText(title_->fontMetrics().elidedText(fullTitle,Qt::ElideRight,titleWidth));title_->setToolTip(fullTitle);
    headerLayout_->invalidate();capsuleLayout_->activate();
}
void PracticeMini::refreshIcons() {
    play_->setIcon(playlistIcon(state_.playing?PlaylistIcon::Pause:PlaylistIcon::Play,true));
    restart_->setIcon(playlistIcon(PlaylistIcon::Loop));restore_->setIcon(playlistIcon(PlaylistIcon::Restore));
    mode_->setIcon(playlistIcon(state_.following?PlaylistIcon::Keyboard:PlaylistIcon::Headphones));
    display_->setIcon(playlistIcon(PlaylistIcon::List));appearanceButton_->setIcon(playlistIcon(PlaylistIcon::Opacity));
    if(volume_->value()>0)volumeButton_->setIcon(playlistIcon(PlaylistIcon::Volume));
    else{auto pixmap=playlistIcon(PlaylistIcon::Volume).pixmap(32,32);QPainter painter(&pixmap);painter.setRenderHint(QPainter::Antialiasing);painter.scale(pixmap.width()/24.0,pixmap.height()/24.0);painter.setPen(QPen(Theme::color("#178e80"),2.2,Qt::SolidLine,Qt::RoundCap));painter.drawLine(QPointF(4,3),QPointF(21,21));painter.end();volumeButton_->setIcon(QIcon(pixmap));}
}
void PracticeMini::refreshClock() {
    const double duration=seek_->isSliderDown()?seekDuration_:std::isfinite(state_.duration)?std::max(0.0,state_.duration):0;
    const double position=seek_->isSliderDown()?duration*seek_->value()/seek_->maximum():std::clamp(std::isfinite(state_.playbackPosition)?state_.playbackPosition:0,0.0,duration);
    const auto text=clockText(position)+" / "+clockText(duration);clock_->setText(clock_->fontMetrics().elidedText(text,Qt::ElideRight,clock_->width()));
    const auto precise=clockText(position,true)+" / "+clockText(duration,true);clock_->setToolTip(precise);seek_->setToolTip("点击或拖动跳转 · "+precise);
}
void PracticeMini::applyScale(double value) {
    scale_=value;auto px=[value](int number){return std::max(1,qRound(number*value));};
    QString scaled;qsizetype offset=0;auto matches=QRegularExpression("(\\d+)px").globalMatch(baseStyle);
    while(matches.hasNext()){const auto match=matches.next();scaled+=baseStyle.mid(offset,match.capturedStart()-offset)+QString::number(px(match.captured(1).toInt()))+"px";offset=match.capturedEnd();}
    scaled+=baseStyle.mid(offset);Theme::setStyle(this,scaled);
    layout_->setContentsMargins(px(6),px(6),px(6),px(6));layout_->setSpacing(0);
    capsuleLayout_->setContentsMargins(px(18),px(14),px(18),px(14));capsuleLayout_->setSpacing(px(7));
    for(auto* row:{headerLayout_,keysLayout_,toolsLayout_,opacityLayout_,sizeLayout_})row->setSpacing(px(6));
    appearanceLayout_->setContentsMargins(px(12),px(9),px(12),px(9));appearanceLayout_->setSpacing(px(6));
    for(auto* chip:chips_)chip->setFixedSize(px(59),px(45));
    for(auto* control:{play_,restart_,mode_,display_,appearanceButton_,restore_,volumeButton_}){control->setIconSize(QSize(px(17),px(17)));control->setFixedSize(px(29),px(29));}
    for(auto* slider:{opacity_,sizeSlider_,seek_,volume_})slider->setFixedHeight(px(18));
    volume_->setFixedWidth(px(64));volumeValue_->setFixedWidth(px(27));clock_->setFixedWidth(px(82));seek_->setMinimumWidth(px(40));
    opacityValue_->setFixedWidth(px(39));sizeValue_->setFixedWidth(px(39));
    refreshIcons();refreshClock();
}
void PracticeMini::watchScreen(QScreen* value) {
    if(screen_==value)return;disconnect(screenGeometry_);disconnect(screenDpi_);screen_=value;
    if(!value)return;
    screenGeometry_=connect(value,&QScreen::availableGeometryChanged,this,[this]{if(isVisible())arrange(pos(),screen_);});
    screenDpi_=connect(value,&QScreen::logicalDotsPerInchChanged,this,[this]{if(isVisible())arrange(pos(),screen_);});
}
void PracticeMini::arrange(const QPoint& origin,QScreen* preferred) {
    if(arranging_)return;
    auto* value=preferred?preferred:QGuiApplication::screenAt(origin+QPoint(80,30));
    if(!value)value=QGuiApplication::primaryScreen();if(!value)return;
    arranging_=true;watchScreen(value);const auto available=value->availableGeometry();
    const double modeScale=numbers_?numberMiniScale:1;
    const int maximumWidth=std::max(1,qRound((available.width()-8)*modeScale)),maximumHeight=std::max(1,qRound((available.height()-8)*modeScale));
    const double requested=sizeSlider_->value()/100.0*modeScale;
    const double scale=std::min({requested,maximumWidth/(numbers_?900.0:680.0),maximumHeight/130.0});
    if(std::abs(scale-scale_)>1e-6)applyScale(scale);
    const auto natural=measure_->sizeHint();
    const int preferredWidth=numbers_?qRound((std::max(852,natural.width())+48)*scale_):qRound(680*scale_);
    const int width=std::clamp(preferredWidth,1,maximumWidth);
    const int preferredBodyHeight=numbers_?std::max(1,qRound(natural.height()*std::max(1,width-qRound(48*scale_))/static_cast<double>(std::max(1,natural.width())))):qRound(53*scale_);
    views_->setFixedHeight(std::max(1,preferredBodyHeight));
    // A stacked view can retain its previous size hint until the next layout
    // event. Resolve both layouts before measuring the surrounding controls.
    capsuleLayout_->invalidate();layout_->invalidate();capsuleLayout_->activate();layout_->activate();
    const int chrome=std::max(1,layout_->sizeHint().height()-views_->height());
    const int height=std::clamp(chrome+preferredBodyHeight,1,maximumHeight);
    views_->setFixedHeight(std::max(1,height-chrome));
    const QSize extent(width,height);const QRect target(boundedPosition(origin,value,extent),extent);
    if(geometry()!=target)setGeometry(target);layout_->activate();refreshText();arranging_=false;
}
QPoint PracticeMini::boundedPosition(const QPoint& origin,QScreen* value,const QSize& extent) const {
    if(!value)return origin;const auto area=value->availableGeometry();
    return {std::clamp(origin.x(),area.left(),std::max(area.left(),area.right()-extent.width()+1)),
            std::clamp(origin.y(),area.top(),std::max(area.top(),area.bottom()-extent.height()+1))};
}
void PracticeMini::present(QScreen* preferred) {
    auto* value=preferred?preferred:QGuiApplication::primaryScreen();if(!value)return;
    const auto area=value->availableGeometry();
    const double modeScale=numbers_?numberMiniScale:1;
    const int baseWidth=numbers_?std::max(852,measure_->sizeHint().width())+48:680;
    const int initialWidth=std::clamp(qRound(baseWidth*sizeSlider_->value()/100.0*modeScale),1,std::max(1,qRound((area.width()-8)*modeScale)));
    const QPoint initial(area.right()-initialWidth-20,area.top()+24);
    const auto saved=QSettings().value("practiceMini/position",initial).toPoint();
    auto* target=QGuiApplication::screenAt(saved+QPoint(80,30));arrange(saved,target?target:value);show();
}
void PracticeMini::savePosition(){QSettings().setValue("practiceMini/position",pos());}
bool PracticeMini::eventFilter(QObject* watched,QEvent* event) {
    if(event->type()==QEvent::MouseButtonPress) {
        auto* mouse=static_cast<QMouseEvent*>(event);
        if(mouse->button()==Qt::LeftButton){dragging_=true;dragOffset_=mouse->globalPosition().toPoint()-pos();gestureScreen_=QGuiApplication::screenAt(mouse->globalPosition().toPoint());return true;}
    }else if(event->type()==QEvent::MouseMove&&dragging_) {
        const auto cursor=static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
        if(auto* value=QGuiApplication::screenAt(cursor))gestureScreen_=value;
        const auto origin=boundedPosition(cursor-dragOffset_,gestureScreen_,size());if(pos()!=origin)move(origin);return true;
    }else if(event->type()==QEvent::MouseButtonRelease&&dragging_) {
        dragging_=false;savePosition();gestureScreen_.clear();return true;
    }
    return QWidget::eventFilter(watched,event);
}
void PracticeMini::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if(!screenConnected_&&windowHandle()) {
        screenConnected_=true;connect(windowHandle(),&QWindow::screenChanged,this,[this](QScreen* value){if(isVisible())arrange(pos(),value);});
    }
#ifdef Q_OS_WIN
    const auto hwnd=reinterpret_cast<HWND>(winId());
    SetWindowLongPtrW(hwnd,GWL_EXSTYLE,GetWindowLongPtrW(hwnd,GWL_EXSTYLE)|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW);
    SetWindowPos(hwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
#endif
    refreshText();
}
void PracticeMini::resizeEvent(QResizeEvent* event){QWidget::resizeEvent(event);refreshText();}
bool PracticeMini::nativeEvent(const QByteArray& type,void* message,qintptr* result) {
#ifdef Q_OS_WIN
    if(static_cast<MSG*>(message)->message==WM_MOUSEACTIVATE){*result=MA_NOACTIVATE;return true;}
#endif
    return QWidget::nativeEvent(type,message,result);
}
void PracticeMini::closeEvent(QCloseEvent* event){event->ignore();savePosition();emit restoreRequested();}
void PracticeMini::keyPressEvent(QKeyEvent* event) {
    if(event->key()==Qt::Key_Escape&&event->modifiers()==Qt::NoModifier){event->accept();if(!event->isAutoRepeat()){savePosition();emit restoreRequested();}return;}
    QWidget::keyPressEvent(event);
}
}
