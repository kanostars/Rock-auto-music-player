#include "practice_page.h"
#include "app/preferences.h"
#include "app/app_log.h"
#include "handpan_test.h"
#include "piano_roll.h"
#include "playback_icons.h"
#include "theme.h"
#include "time_seek_edit.h"
#include "number_score_view.h"
#include "practice_mini.h"
#include "score_archive.h"
#include <QAbstractSpinBox>
#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QSettings>
#include <QStandardPaths>
#include <QSlider>
#include <QSaveFile>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rock {
namespace {
QLabel* label(const QString& text,const char* role){auto* value=new QLabel(text);value->setProperty("role",role);value->setWordWrap(true);return value;}
QPushButton* button(const QString& text,const char* name){auto* value=new QPushButton(text);value->setObjectName(name);value->setCursor(Qt::PointingHandCursor);return value;}
QFrame* panel(const char* name="panel"){auto* value=new QFrame;value->setObjectName(name);return value;}
const std::array<const char*,9> degrees{"低音 6","3","4","5","6","7","高音 1","高音 2","高音 3"};
void logPractice(LogLevel level,const QString& message){AppLog::instance().write(level,"歌曲练习",message);}
QString practiceMode(bool following){return following?"跟弹练习":"自动预览";}
class PracticeProgressSlider final:public QSlider {
public:
    PracticeProgressSlider():QSlider(Qt::Horizontal){}
protected:
    void mousePressEvent(QMouseEvent* event) override{
        if(event->button()!=Qt::LeftButton){QSlider::mousePressEvent(event);return;}
        QStyleOptionSlider option;initStyleOption(&option);
        const auto handle=style()->subControlRect(QStyle::CC_Slider,&option,QStyle::SC_SliderHandle,this);
        if(handle.contains(event->position().toPoint())){QSlider::mousePressEvent(event);return;}
        seeking_=true;setSliderDown(true);seekPointer(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override{
        if(seeking_&&isSliderDown())seekPointer(event);else QSlider::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override{
        if(seeking_&&event->button()==Qt::LeftButton){
            seeking_=false;seekPointer(event);setSliderDown(false);
        }else QSlider::mouseReleaseEvent(event);
    }
private:
    bool seeking_{};
    void seekPointer(QMouseEvent* event){
        QStyleOptionSlider option;initStyleOption(&option);
        const auto groove=style()->subControlRect(QStyle::CC_Slider,&option,QStyle::SC_SliderGroove,this);
        const auto handle=style()->subControlRect(QStyle::CC_Slider,&option,QStyle::SC_SliderHandle,this);
        const int first=groove.left(),span=std::max(1,groove.width()-handle.width());
        const int position=std::clamp(qRound(event->position().x())-handle.width()/2-first,0,span);
        setSliderPosition(QStyle::sliderValueFromPosition(minimum(),maximum(),position,span,option.upsideDown));
        event->accept();
    }
};
}
PracticePage::PracticePage(QWidget* parent):QWidget(parent){
    setObjectName("practicePage");setFocusPolicy(Qt::StrongFocus);
    loadPreferences();
    auto* page=new QVBoxLayout(this);page->setContentsMargins(0,0,0,0);page->setSpacing(12);
    auto* controls=new QHBoxLayout(panel("practiceTransport"));controls->setContentsMargins(16,12,16,12);controls->setSpacing(12);
    play_=button({},"practicePlayButton");play_->setProperty("primary",true);play_->setFixedSize(46,46);play_->setIconSize(QSize(25,25));controls->addWidget(play_);
    auto* restart=button({},"practiceRestartButton");restart->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));restart->setAccessibleName("从头开始");restart->setToolTip("从头开始");restart->setFixedSize(34,34);controls->addWidget(restart);
    auto* progressBox=new QVBoxLayout;progressBox->setSpacing(3);
    progress_=new PracticeProgressSlider;progress_->setObjectName("practiceProgress");progress_->setAccessibleName("歌曲练习进度");progress_->setRange(0,100000);progressBox->addWidget(progress_);
    clock_=new TimeSeekEdit;clock_->setProperty("role","muted");clock_->setObjectName("practiceClock");progressBox->addWidget(clock_,0,Qt::AlignLeft);controls->addLayout(progressBox,1);
    connect(clock_,&TimeSeekEdit::seekRequested,this,[this](double seconds){seek(seconds);});
    controls->addWidget(label("速度","muted"));speed_=new QComboBox;speed_->setObjectName("practiceSpeed");speed_->setAccessibleName("练习播放速度");
    for(double value:{.25,.5,.75,1.0,1.25,1.5,2.0})speed_->addItem(QString::number(value)+"×",value);speed_->setCurrentIndex(3);speed_->setFixedWidth(82);controls->addWidget(speed_);
    auto* volumeIcon=new QLabel;volumeIcon->setPixmap(playlistIcon(PlaylistIcon::Volume).pixmap(22,22));controls->addWidget(volumeIcon);
    connect(&Preferences::instance(),&Preferences::themeChanged,this,[volumeIcon]{volumeIcon->setPixmap(playlistIcon(PlaylistIcon::Volume).pixmap(22,22));});
    volume_=new QSlider(Qt::Horizontal);volume_->setObjectName("practiceVolume");volume_->setAccessibleName("练习音量");volume_->setRange(0,100);volume_->setValue(60);volume_->setFixedWidth(88);controls->addWidget(volume_);
    miniButton_=button("小窗","practiceMiniButton");controls->addWidget(miniButton_);
    auto* settings=button("练习设置 / 键位","practiceSettingsButton");controls->addWidget(settings);page->addWidget(controls->parentWidget());
    connect(play_,&QPushButton::clicked,this,[this]{if(running_)pause();else start();setFocus();});
    connect(restart,&QPushButton::clicked,this,[this]{this->restart();setFocus();});
    connect(miniButton_,&QPushButton::clicked,this,[this]{enterMiniMode(screen());});
    auto updateMiniShortcut=[this]{miniButton_->setToolTip("进入游戏跟练小窗（"+Preferences::instance().shortcutText(ShortcutAction::MiniMode)+" 切换）");};
    connect(&Preferences::instance(),&Preferences::shortcutsChanged,this,updateMiniShortcut);updateMiniShortcut();
    connect(volume_,&QSlider::valueChanged,this,[this](int value){if(!miniMode_)miniPreviewVolume_=value;applyVolume();volume_->setToolTip(QString("音量 %1%").arg(value));updatePrompt();});
    connect(speed_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{logPractice(LogLevel::Info,QString("播放倍速调整为 %1×").arg(speed_->currentData().toDouble()));if(running_&&!followingMode_){pause();start();}else if(running_)updateMetronomeRhythm();setFocus();});
    connect(settings,&QPushButton::clicked,this,&PracticePage::openSettings);
    connect(progress_,&QSlider::sliderPressed,this,[this]{seekResume_=running_;pause();});
    connect(progress_,&QSlider::sliderMoved,this,[this](int value){seek(result_?result_->duration*value/100000.0:0,false);});
    connect(progress_,&QSlider::sliderReleased,this,[this]{seek(result_?result_->duration*progress_->value()/100000.0:0,false);if(result_)logPractice(LogLevel::Info,QString("歌曲进度定位：%1，%2 秒").arg(songName_).arg(position_,0,'f',2));if(seekResume_&&(options_.loopEnabled||position_<rangeEnd()))start();seekResume_=false;setFocus();});
    connect(progress_,&QSlider::valueChanged,this,[this](int value){if(!progress_->isSliderDown())seek(result_?result_->duration*value/100000.0:0);});

    auto* body=new QHBoxLayout;body->setSpacing(14);
    auto* timelineCard=panel();auto* timelineLayout=new QVBoxLayout(timelineCard);timelineLayout->setContentsMargins(16,14,16,12);timelineLayout->setSpacing(10);
    auto* timelineHeading=new QHBoxLayout;timelineHeading->setSpacing(6);
    timelineMode_=button("时间轴","practiceTimelineMode");numberMode_=button("简谱","practiceNumberMode");
    auto* displayModes=new QButtonGroup(this);displayModes->setExclusive(true);
    for(auto* mode:{timelineMode_,numberMode_}){mode->setCheckable(true);displayModes->addButton(mode);Theme::setStyle(mode,"padding:6px 12px;");timelineHeading->addWidget(mode);}
    timelineMode_->setChecked(true);timelineHeading->addStretch();
    scoreDisplayHint_=label("按键 / 音高","muted");timelineHeading->addWidget(scoreDisplayHint_);
    downloadScore_=button("下载简谱","practiceDownloadScore");downloadScore_->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    downloadScore_->setToolTip("左键：下载当前页 PNG\n右键：下载全部页面 PNG（ZIP）");downloadScore_->setContextMenuPolicy(Qt::CustomContextMenu);downloadScore_->hide();
    Theme::setStyle(downloadScore_,"padding:6px 10px;");timelineHeading->addWidget(downloadScore_);timelineLayout->addLayout(timelineHeading);
    scoreViews_=new QStackedWidget;timelineLayout->addWidget(scoreViews_,1);
    auto* timelineView=new QWidget;auto* timelineBody=new QVBoxLayout(timelineView);timelineBody->setContentsMargins(0,0,0,0);timelineBody->setSpacing(10);
    timeline_=new PianoRoll;timeline_->setObjectName("practiceTimeline");timelineBody->addWidget(timeline_,1);
    auto* zoomRow=new QHBoxLayout;auto* zoomHint=label("点击时间轴定位 · Ctrl + 滚轮缩放","muted");zoomHint->setWordWrap(false);zoomRow->addWidget(zoomHint);zoomRow->addStretch();
    auto* zoomOut=button("−","practiceZoomOut");auto* zoomIn=button("＋","practiceZoomIn");zoomOut->setFixedSize(32,30);zoomIn->setFixedSize(32,30);
    zoom_=label("75 px/s","muted");zoom_->setMinimumWidth(64);zoomRow->addWidget(zoomOut);zoomRow->addWidget(zoom_);zoomRow->addWidget(zoomIn);timelineBody->addLayout(zoomRow);scoreViews_->addWidget(timelineView);
    auto* numberView=new QWidget;auto* numberBody=new QVBoxLayout(numberView);numberBody->setContentsMargins(0,0,0,0);numberBody->setSpacing(10);
    numberScore_=new NumberScoreView;numberScore_->setObjectName("practiceNumberScore");numberBody->addWidget(numberScore_,1);
    auto* pageRow=new QHBoxLayout;pageRow->setSpacing(6);
    previousPage_=button("上一页","practiceScorePrevious");nextPage_=button("下一页","practiceScoreNext");
    scorePage_=new QSpinBox;scorePage_->setObjectName("practiceScorePage");scorePage_->setPrefix("第 ");scorePage_->setRange(1,1);scorePage_->setSuffix(" / 1 页");scorePage_->setKeyboardTracking(false);scorePage_->setToolTip("输入页码，按回车翻页");scorePage_->setAccessibleName("简谱页码");
    Theme::setStyle(scorePage_,"padding:3px 6px;min-height:20px;");
    followPage_=button("定位当前","practiceScoreFollow");followPage_->setToolTip("回到当前播放或跟弹的位置，继续自动翻页");
    for(auto* control:{previousPage_,nextPage_,followPage_})Theme::setStyle(control,"padding:6px 10px;");
    pageRow->addWidget(previousPage_);pageRow->addWidget(scorePage_);pageRow->addWidget(nextPage_);pageRow->addStretch();pageRow->addWidget(followPage_);numberBody->addLayout(pageRow);scoreViews_->addWidget(numberView);
    connect(timelineMode_,&QPushButton::clicked,this,[this]{setScoreMode(false);setFocus();});
    connect(numberMode_,&QPushButton::clicked,this,[this]{setScoreMode(true);setFocus();});
    connect(numberScore_,&NumberScoreView::pageChanged,this,&PracticePage::refreshScorePages);
    connect(numberScore_,&NumberScoreView::seekRequested,this,[this](double seconds){seek(seconds);setFocus();});
    connect(previousPage_,&QPushButton::clicked,this,[this]{browseScorePage(numberScore_->currentPage()-1);setFocus();});
    connect(nextPage_,&QPushButton::clicked,this,[this]{browseScorePage(numberScore_->currentPage()+1);setFocus();});
    connect(scorePage_,qOverload<int>(&QSpinBox::valueChanged),this,[this](int page){browseScorePage(page-1);});
    connect(followPage_,&QPushButton::clicked,this,[this]{scoreFollow_=true;updateScorePosition(true);followPage_->setEnabled(false);setFocus();});
    connect(downloadScore_,&QPushButton::clicked,this,[this]{downloadScore(false);});
    connect(downloadScore_,&QPushButton::customContextMenuRequested,this,[this]{downloadScore(true);});
    refreshScorePages(0,0);
    connect(zoomOut,&QPushButton::clicked,this,[this]{timeline_->setZoom(timeline_->zoom()/1.4);});connect(zoomIn,&QPushButton::clicked,this,[this]{timeline_->setZoom(timeline_->zoom()*1.4);});
    connect(timeline_,&PianoRoll::zoomChanged,this,[this](double value){zoom_->setText(QString::number(value,'f',value<1?2:0)+" px/s");});
    connect(timeline_,&PianoRoll::seekRequested,this,[this](double seconds){seek(seconds);setFocus();});body->addWidget(timelineCard,3);

    auto* right=new QWidget;right->setMinimumWidth(330);right->setMaximumWidth(420);auto* rightLayout=new QVBoxLayout(right);rightLayout->setContentsMargins(0,0,0,0);rightLayout->setSpacing(12);
    auto* boardCard=panel();auto* boardLayout=new QVBoxLayout(boardCard);boardLayout->setContentsMargins(12,12,12,12);boardLayout->setSpacing(8);
    auto* modes=new QHBoxLayout;modes->setSpacing(6);automatic_=button("自动播放","practiceAutomaticMode");following_=button("跟弹练习","practiceFollowingMode");
    auto* modeGroup=new QButtonGroup(this);modeGroup->setExclusive(true);for(auto* mode:{automatic_,following_}){mode->setCheckable(true);modeGroup->addButton(mode);modes->addWidget(mode,1);}boardLayout->addLayout(modes);
    boardLayout->addWidget(label("九键手碟 · 点击音键试听","muted"));board_=new HandpanBoard;board_->setPracticeLayout(true);boardLayout->addWidget(board_,1);rightLayout->addWidget(boardCard,1);
    connect(automatic_,&QPushButton::clicked,this,[this]{setMode(false);setFocus();});connect(following_,&QPushButton::clicked,this,[this]{setMode(true);setFocus();});
    connect(board_,&HandpanBoard::padPressed,this,[this](int target){press(target,true);});connect(board_,&HandpanBoard::padReleased,this,[this](int target){release(target,true);});
    auto* prompt=panel("practicePrompt");auto* promptLayout=new QVBoxLayout(prompt);promptLayout->setContentsMargins(16,14,16,14);promptLayout->setSpacing(10);
    auto* promptHeading=new QHBoxLayout;promptTitle_=label("准备预览","section");promptTitle_->setObjectName("practicePromptTitle");groupCount_=label({},"muted");groupCount_->setObjectName("practiceGroupCount");promptHeading->addWidget(promptTitle_,1);promptHeading->addWidget(groupCount_);promptLayout->addLayout(promptHeading);
    hint_=label("点击播放按钮开始预览。","muted");hint_->setObjectName("practiceHint");promptLayout->addWidget(hint_);
    auto* chips=new QGridLayout;chipsLayout_=chips;chips->setSpacing(6);
    for(int target=0;target<9;++target){auto* chip=label(QString::fromUtf8(degrees[target])+" · "+QChar(keys[target]),"practiceKey");chip->setObjectName("practiceKey_"+QString(QChar(keys[target])));chip->setAlignment(Qt::AlignCenter);chip->setWordWrap(false);chips_[target]=chip;chips->addWidget(chip,target/3,target%3);chip->hide();}
    promptLayout->addLayout(chips);rightLayout->addWidget(prompt);body->addWidget(right,1);page->addLayout(body,1);
    timer_.setInterval(16);connect(&timer_,&QTimer::timeout,this,&PracticePage::tick);
    connect(qApp,&QGuiApplication::applicationStateChanged,this,[this](Qt::ApplicationState state){if(state==Qt::ApplicationSuspended||(state!=Qt::ApplicationActive&&isVisible()&&!miniMode_))pause();});
    connect(&miniInput_,&PracticeInput::keyChanged,this,[this](int target,bool down){
        if(!miniMode_||!running_||!followingMode_)return;
        if(QApplication::activeModalWidget()){pause();return;}
        if(down)press(target,false);else release(target,false);
    });
    qApp->installEventFilter(this);updateKeyLabels();setMode(false);setVolume(60);
}
PracticePage::~PracticePage(){qApp->removeEventFilter(this);miniMode_=false;stop();delete mini_;}
void PracticePage::setSong(const QString& name,std::shared_ptr<const Song> song,std::shared_ptr<const Conversion> result,const Settings& settings){
    stop();song_=std::move(song);result_=std::move(result);settings_=settings;
    songName_=name;scoreReady_=false;scoreFollow_=true;numberScore_->setSong({},nullptr,nullptr,settings_);setScoreMode(false);
    options_.loopEnabled=false;options_.loopStart=0;options_.loopEnd=result_?result_->duration:0;
    metronomeBeats_.clear();metronomePlanReady_=false;
    practice_.setGroups(result_?makePracticeGroups(*result_):std::vector<PracticeGroup>{});
    pageTitle_="演奏与练习 · "+name;
    const double bpm=settings.fixedTempo?settings.bpm:(song_&&!song_->tempos.empty()?60000000.0/song_->tempos.front().micros:120);
    pageSubtitle_=QString("九键手碟 · %1 组 · 参考速度 %2 BPM").arg(practice_.groups().size()).arg(bpm*settings.speed,0,'f',1);
    timeline_->setMusic(song_,result_);timeline_->setPracticeMode(true,settings);timeline_->setZoom(75);timeline_->setPlaybackRange(0,-1);timeline_->setEditingEnabled(false);updateKeyLabels();
    {const QSignalBlocker blocker(speed_);speed_->setCurrentIndex(3);}setMode(false);updatePosition(0);updatePrompt();
    logPractice(LogLevel::Info,QString("进入歌曲练习：%1，%2 组，时长 %3 秒").arg(songName_).arg(practice_.groups().size()).arg(result_?result_->duration:0,0,'f',2));
}
void PracticePage::setVolume(int value){
    volume_->setValue(std::clamp(value,0,100));
    if(!miniMode_)miniPreviewVolume_=volume_->value();
    applyVolume();updatePrompt();
}
int PracticePage::outputVolume() const{
    return miniMode_?(followingMode_?miniFollowingVolume_:miniPreviewVolume_):volume_->value();
}
void PracticePage::applyVolume(){
    const float level=outputVolume()/100.f;
    audio_.setVolume(level);live_.setVolume(level);metronome_.setVolume(level);
}
void PracticePage::stop(){const bool active=running_||miniMode_;clock_->cancelEditing();if(miniMode_)leaveMiniMode();else pause();if(settingsDialog_)settingsDialog_->close();if(active)logPractice(LogLevel::Info,QString("停止歌曲练习：%1，停留于 %2 秒").arg(songName_).arg(position_,0,'f',2));}
void PracticePage::enterMiniMode(QScreen* preferred){
    if(miniMode_||!song_||!result_||QApplication::activeModalWidget())return;
    clock_->cancelEditing();
    if(!mini_){
        mini_=new PracticeMini;
        connect(mini_,&PracticeMini::playRequested,this,[this]{if(running_)pause();else start();});
        connect(mini_,&PracticeMini::restartRequested,this,&PracticePage::restart);
        connect(mini_,&PracticeMini::followingRequested,this,&PracticePage::setMode);
        connect(mini_,&PracticeMini::restoreRequested,this,[this]{emit miniRestoreRequested();});
        connect(mini_,&PracticeMini::seekStarted,this,&PracticePage::beginMiniSeek);
        connect(mini_,&PracticeMini::seekRequested,this,&PracticePage::finishMiniSeek);
        connect(mini_,&PracticeMini::volumeRequested,this,[this](int value){
            if(!miniMode_)return;
            (followingMode_?miniFollowingVolume_:miniPreviewVolume_)=std::clamp(value,0,100);
            applyVolume();updatePrompt();
        });
    }
    try{mini_->setSong(songName_,song_,result_,settings_);}
    catch(const std::exception& error){logPractice(LogLevel::Error,"打开跟练小窗失败："+QString::fromUtf8(error.what()));QMessageBox::warning(this,"无法打开跟练小窗",QString::fromUtf8(error.what()));return;}
    clearInput();miniMode_=true;applyVolume();
    if(running_&&followingMode_){
        QString error;if(!live_.start(error)){feedback_=error+" 仍可进行按键跟练。";logPractice(LogLevel::Warning,"跟练试听输出不可用："+error);}
        if(!startMiniInput())pause();
    }
    updatePrompt();mini_->present(preferred);emit miniModeChanged(true);
    logPractice(LogLevel::Info,QString("打开游戏跟练小窗：%1，%2，音量 %3%").arg(songName_,practiceMode(followingMode_)).arg(outputVolume()));
}
void PracticePage::leaveMiniMode(){
    if(!miniMode_)return;
    pause();if(mini_)mini_->hide();miniMode_=false;applyVolume();
    emit miniModeChanged(false);
    logPractice(LogLevel::Info,"关闭游戏跟练小窗："+songName_);
}
bool PracticePage::startMiniInput(){
    if(!miniMode_||!followingMode_)return true;
    QString error;if(miniInput_.start(options_.inputKeys,error))return true;
    feedback_=error;logPractice(LogLevel::Error,"跟练小窗键盘监听启动失败："+error);return false;
}
void PracticePage::clearInput(){keyboardHeld_.fill(false);mouseHeld_.fill(false);practice_.clearHeld();live_.stop();}
void PracticePage::pause(){
    const bool wasRunning=running_;
    miniSeeking_=false;miniSeekResume_=false;
    miniInput_.stop();
    audio_.pause();metronome_.stop();
    if(running_&&!followingMode_&&result_)updatePosition(std::min(audio_.position(),result_->duration));
    running_=false;timer_.stop();clearInput();updatePlayButton();updatePrompt();
    if(wasRunning)logPractice(LogLevel::Info,QString("暂停%1：%2，%3 秒").arg(practiceMode(followingMode_),songName_).arg(position_,0,'f',2));
}
void PracticePage::setMode(bool following){const bool changed=followingMode_!=following;pause();followingMode_=following;applyVolume();automatic_->setChecked(!following);following_->setChecked(following);practice_.seek(position_);alignFollowingGroup();feedback_.clear();updatePrompt();if(changed)logPractice(LogLevel::Info,"练习模式切换为 "+practiceMode(following));}
void PracticePage::start(){
    if(!song_||!result_||practice_.groups().empty()||QApplication::activeModalWidget())return;
    scoreFollow_=true;followPage_->setEnabled(false);updateScorePosition(true);
    feedback_.clear();clearInput();
    if(position_<rangeStart()||position_>=rangeEnd()||(followingMode_&&practice_.finished())){practice_.seek(rangeStart());updatePosition(rangeStart());}
    if(followingMode_){
        if(practice_.finished())practice_.seek(position_);
        if(options_.loopEnabled&&!practice_.finished()&&practice_.groups()[practice_.index()].start>=rangeEnd())practice_.seek(rangeStart());
        if(!practice_.finished())updatePosition(practice_.groups()[practice_.index()].start);
        QString error;if(!live_.start(error)){feedback_=error+" 仍可进行按键跟练。";logPractice(LogLevel::Warning,"跟练试听输出不可用："+error);}
        running_=true;
        if(!startMiniInput()){running_=false;clearInput();updatePlayButton();updatePrompt();return;}
    }else{
        QString error;if(!audio_.play(*song_,*result_,position_,error,rangeEnd(),speed_->currentData().toDouble(),true)){feedback_=error;logPractice(LogLevel::Error,"启动歌曲预览失败："+error);updatePrompt();return;}
        running_=true;timer_.start();
    }
    startMetronome();updatePlayButton();updatePrompt();
    logPractice(LogLevel::Info,QString("开始%1：%2，%3 秒，%4×，音量 %5%").arg(practiceMode(followingMode_),songName_).arg(position_,0,'f',2).arg(speed_->currentData().toDouble()).arg(outputVolume()));
}
void PracticePage::restart(){
    const bool resume=running_;scoreFollow_=true;followPage_->setEnabled(false);pause();
    practice_.seek(rangeStart());updatePosition(rangeStart(),true);feedback_.clear();
    if(resume)start();else updatePrompt();
    logPractice(LogLevel::Info,QString("歌曲练习从头开始：%1，%2 秒").arg(songName_).arg(rangeStart(),0,'f',2));
}
void PracticePage::seek(double seconds,bool resume){
    if(!result_||!std::isfinite(seconds))return;
    scoreFollow_=true;followPage_->setEnabled(false);
    const bool wasRunning=running_;pause();
    if(options_.loopEnabled)seconds=std::clamp(seconds,rangeStart(),rangeEnd());
    updatePosition(seconds,true);practice_.seek(position_);feedback_.clear();
    alignFollowingGroup();
    if(resume)logPractice(LogLevel::Info,QString("歌曲进度定位：%1，%2 秒").arg(songName_).arg(position_,0,'f',2));
    if(wasRunning&&resume&&(options_.loopEnabled||position_<rangeEnd()))start();else updatePrompt();
}
void PracticePage::beginMiniSeek(){
    if(!miniMode_||!result_||result_->duration<=0)return;
    const bool resume=running_;pause();miniSeeking_=true;miniSeekResume_=resume;
}
void PracticePage::finishMiniSeek(double seconds){
    if(!miniMode_||!miniSeeking_){updatePrompt();return;}
    const bool resume=miniSeekResume_;miniSeeking_=false;miniSeekResume_=false;
    if(!std::isfinite(seconds)){updatePrompt();return;}
    seek(seconds,false);
    logPractice(LogLevel::Info,QString("小窗歌曲进度定位：%1，%2 秒").arg(songName_).arg(position_,0,'f',2));
    if(resume&&(options_.loopEnabled||position_<rangeEnd()))start();
}
void PracticePage::tick(){
    if(!running_||followingMode_||!result_)return;
    updatePosition(audio_.position());
    if(audio_.finished()||position_>=rangeEnd()){
        pause();
        if(options_.loopEnabled){practice_.seek(rangeStart());updatePosition(rangeStart());start();}
        else{updatePosition(result_->duration);feedback_="预览结束，可以从头播放或切换到跟弹练习。";logPractice(LogLevel::Info,"歌曲预览完成："+songName_);}
    }
    else if(!audio_.running()){pause();feedback_="音频输出已中断，请检查系统输出设备后继续。";logPractice(LogLevel::Warning,"歌曲预览音频输出中断："+songName_);}
    updatePrompt();
}
void PracticePage::updatePosition(double seconds,bool focusScore){
    position_=std::clamp(seconds,0.0,result_?std::max(0.0,result_->duration):0.0);timeline_->setPlayhead(position_,true);
    const QSignalBlocker blocker(progress_);progress_->setValue(result_&&result_->duration>0?static_cast<int>(position_/result_->duration*100000):0);
    clock_->setPosition(position_,result_?result_->duration:0);
    updateScorePosition(focusScore);
}
void PracticePage::updateScorePosition(bool forceFocus){
    if(!scoreReady_)return;
    const int group=followingMode_&&!practice_.finished()?static_cast<int>(practice_.index()):-1;
    numberScore_->setPosition(position_,scoreFollow_,forceFocus,group);
}
void PracticePage::updatePlayButton(){play_->setIcon(playlistIcon(running_?PlaylistIcon::Pause:PlaylistIcon::Play,true));play_->setAccessibleName(running_?"暂停练习":"开始练习");play_->setToolTip(running_?"暂停":"播放 / 开始跟弹");}
void PracticePage::updatePrompt(){
    const auto& groups=practice_.groups();size_t index=groups.size();uint16_t expected=0,matched=0;
    const auto [firstGroup,lastGroup]=groupRange();
    QString title,hint;
    if(groups.empty()){title="暂无可练习音符";hint="请返回工作台，选择包含九键音符的曲目。";}
    else if(followingMode_){
        index=practice_.index();
        if(practice_.finished()){title="练习完成";hint="所有按键组已完成，点击播放可重新练习。";}
        else{
            expected=groups[index].keys;matched=practice_.matchedMask();
            title=running_?"等待跟弹":"准备跟弹";
            hint=!running_?"点击播放按钮开始，按对才前进。":practice_.waitingRelease()?"本组完成，请松开按键，再弹下一组。":"弹奏下方按键；同一组的按键需要一起按下。";
        }
    }else{
        auto it=std::upper_bound(groups.begin(),groups.end(),position_+1e-8,[](double value,const PracticeGroup& group){return value<group.start;});
        if(it!=groups.begin()){index=static_cast<size_t>(it-groups.begin()-1);if(index>=firstGroup&&index<lastGroup&&position_<groups[index].end)expected=groups[index].keys;}
        title=running_?"自动预览":"准备预览";hint=running_?"按键随当前组高亮，可提前熟悉下一段旋律。":"点击播放预览，或切换到跟弹练习。";
    }
    if(!feedback_.isEmpty())hint=feedback_;
    if(options_.loopEnabled)title+=" · A/B 循环";
    promptTitle_->setText(title);hint_->setText(hint);
    const auto currentGroup=index>=firstGroup&&index<lastGroup?index-firstGroup+1:
        followingMode_&&practice_.finished()?lastGroup-firstGroup:0;
    groupCount_->setText(QString("%1 / %2 组").arg(currentGroup).arg(lastGroup-firstGroup));
    const auto chipMask=followingMode_?expected:(index>=firstGroup&&index<lastGroup?groups[index].keys:0);
    int cell=0;
    for(int target=0;target<9;++target){
        if(chipMask!=shownMask_){
            chips_[target]->setVisible(chipMask&(1u<<target));
            if(chipMask&(1u<<target)){chipsLayout_->addWidget(chips_[target],cell/3,cell%3);++cell;}
        }
        Theme::setStyle(chips_[target],matched&(1u<<target)?"background:#d9eeea;color:#0e796c;border-radius:6px;padding:7px 5px;":"background:#f5f9fa;color:#203d4e;border-radius:6px;padding:7px 5px;");
        const bool held=keyboardHeld_[target]||mouseHeld_[target];
        board_->setHighlighted(target,held||((expected&(1u<<target))&&(!followingMode_||!practice_.waitingRelease())));
    }
    shownMask_=chipMask;
    play_->setEnabled(!groups.empty());
    miniButton_->setEnabled(song_&&result_&&!groups.empty());
    updateScorePosition();
    if(miniMode_&&mini_){
        PracticeMiniState state;state.title=songName_;state.status=title;state.hint=hint;state.progress=groupCount_->text();
        state.keys=chipMask;state.matched=matched;state.inputKeys=options_.inputKeys;
        state.following=followingMode_;state.playing=running_;state.canPlay=!groups.empty();
        state.practiceGroup=followingMode_&&!practice_.finished()?static_cast<int>(practice_.index()):-1;
        state.position=followingMode_&&!practice_.finished()?groups[practice_.index()].start:position_;
        state.playbackPosition=position_;state.duration=result_?result_->duration:0;state.volume=outputVolume();
        mini_->setState(state);
    }
}
bool PracticePage::acceptsInput() const{
    if(QApplication::activeModalWidget())return false;
    if(miniMode_)return mini_&&mini_->isVisible()&&running_&&followingMode_;
    return isVisible()&&window()->isActiveWindow()&&QGuiApplication::applicationState()==Qt::ApplicationActive;
}
void PracticePage::press(int target,bool mouse){
    if(target<0||target>=9||!acceptsInput())return;auto& held=mouse?mouseHeld_:keyboardHeld_;if(held[target])return;
    const bool alreadyHeld=keyboardHeld_[target]||mouseHeld_[target];held[target]=true;
    if(!alreadyHeld){QString error;if(!live_.running()&&!live_.start(error))feedback_=error;if(live_.running())live_.strike(target);
        if(followingMode_&&running_){
            const auto result=practice_.press(target);
            if(result==PracticePress::Wrong)feedback_=QString("%1 不在本组，请弹奏提示中的按键。").arg(QChar(options_.inputKeys[target]));
            else if(result!=PracticePress::Ignored)feedback_.clear();
            if(result==PracticePress::Advanced||result==PracticePress::Finished){
                scoreFollow_=true;followPage_->setEnabled(false);
                if(options_.loopEnabled&&(practice_.finished()||practice_.groups()[practice_.index()].start>=rangeEnd()))practice_.loopTo(rangeStart());
                if(!practice_.finished()){updatePosition(practice_.groups()[practice_.index()].start);updateMetronomeRhythm();}
                else{running_=false;miniInput_.stop();keyboardHeld_.fill(false);mouseHeld_.fill(false);practice_.clearHeld();metronome_.stop();updatePosition(result_?result_->duration:0);updatePlayButton();logPractice(LogLevel::Info,"跟弹练习完成："+songName_);}
            }
        }
    }
    updatePrompt();
}
void PracticePage::release(int target,bool mouse){if(target<0||target>=9)return;(mouse?mouseHeld_:keyboardHeld_)[target]=false;if(!keyboardHeld_[target]&&!mouseHeld_[target])practice_.release(target);updatePrompt();}
bool PracticePage::eventFilter(QObject* object,QEvent* event){
    if(miniMode_){
        auto* widget=qobject_cast<QWidget*>(object);
        if(event->type()==QEvent::Show&&widget&&widget->isWindow()&&(widget->isModal()||widget->windowType()==Qt::Popup))pause();
        return false;
    }
    if(!isVisible())return false;
    if(object==window()&&(event->type()==QEvent::WindowDeactivate||event->type()==QEvent::Hide||event->type()==QEvent::Close)){pause();return false;}
    if(event->type()!=QEvent::KeyPress&&event->type()!=QEvent::KeyRelease&&event->type()!=QEvent::ShortcutOverride)return false;
    auto* widget=qobject_cast<QWidget*>(object);if(!widget||widget->window()!=window()||!acceptsInput())return false;
    for(auto* input=widget;input;input=input->parentWidget())if(qobject_cast<QLineEdit*>(input)||qobject_cast<QAbstractSpinBox*>(input)||qobject_cast<QKeySequenceEdit*>(input))return false;
    auto* key=static_cast<QKeyEvent*>(event);
    if(key->key()==Qt::Key_Space&&!(key->modifiers()&(Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier))){if(event->type()==QEvent::KeyPress&&!key->isAutoRepeat()){if(running_)pause();else start();}key->accept();return true;}
    auto it=std::find(options_.inputKeys.begin(),options_.inputKeys.end(),key->key());if(it==options_.inputKeys.end())return false;
    if(event->type()!=QEvent::KeyRelease&&(key->modifiers()&(Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier)))return false;
    const int target=static_cast<int>(it-options_.inputKeys.begin());
    if(!key->isAutoRepeat()){if(event->type()==QEvent::KeyPress)press(target,false);else if(event->type()==QEvent::KeyRelease)release(target,false);}
    key->accept();return true;
}
void PracticePage::hideEvent(QHideEvent* event){if(!miniMode_)stop();QWidget::hideEvent(event);}
void PracticePage::setScoreMode(bool numbers){
    const bool changed=scoreViews_->currentIndex()!=(numbers?1:0);
    if(numbers&&!scoreReady_){
        if(!song_||!result_){timelineMode_->setChecked(true);return;}
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QString error;
        try{numberScore_->setSong(songName_,song_,result_,settings_);scoreReady_=numberScore_->pageCount()>0;}
        catch(const std::exception& e){error=QString::fromUtf8(e.what());}
        QApplication::restoreOverrideCursor();
        if(!scoreReady_){
            logPractice(LogLevel::Error,"生成数字简谱失败："+(error.isEmpty()?QString("当前曲目没有可显示的简谱。"):error));
            setScoreMode(false);QMessageBox::warning(this,"无法生成简谱",error.isEmpty()?"当前曲目没有可显示的简谱。":error);return;
        }
    }
    timelineMode_->setChecked(!numbers);numberMode_->setChecked(numbers);
    scoreViews_->setCurrentIndex(numbers?1:0);downloadScore_->setVisible(numbers);
    scoreDisplayHint_->setText(numbers?"按小节分页":"按键 / 音高");
    if(numbers){scoreFollow_=true;updateScorePosition(true);refreshScorePages(numberScore_->currentPage(),numberScore_->pageCount());}
    if(changed)logPractice(LogLevel::Info,numbers?"曲谱显示切换为数字简谱":"曲谱显示切换为时间轴");
}
void PracticePage::browseScorePage(int page){
    if(!scoreReady_||page<0||page>=numberScore_->pageCount())return;
    scoreFollow_=false;numberScore_->setPage(page);followPage_->setEnabled(true);
}
void PracticePage::refreshScorePages(int page,int count){
    const QSignalBlocker blocker(scorePage_);scorePage_->setRange(1,std::max(1,count));
    scorePage_->setSuffix(QString(" / %1 页").arg(std::max(1,count)));scorePage_->setValue(std::clamp(page+1,1,std::max(1,count)));
    scorePage_->setEnabled(count>1);previousPage_->setEnabled(page>0&&count>0);nextPage_->setEnabled(page+1<count);
    followPage_->setEnabled(!scoreFollow_&&count>0);downloadScore_->setEnabled(count>0&&!exportingScore_);
}
void PracticePage::downloadScore(bool allPages){
    if(exportingScore_||!scoreReady_||scoreViews_->currentIndex()!=1)return;
    const int page=numberScore_->currentPage(),count=numberScore_->pageCount();if(page<0||count<=0)return;
    clock_->cancelEditing();pause();
    const QScopedValueRollback<bool> exporting(exportingScore_,true);
    const auto base=scoreFileBase(songName_);
    QFileDialog dialog(this,allPages?"下载全部简谱 PNG（ZIP）":"下载当前页简谱 PNG",
                       QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                       allPages?"ZIP 文件 (*.zip)":"PNG 图片 (*.png)");
    dialog.setObjectName(allPages?"practiceExportScoreZip":"practiceExportScorePng");dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);dialog.setDefaultSuffix(allPages?"zip":"png");
    dialog.selectFile(allPages?base+"_简谱.zip":base+QString("_简谱_%1.png").arg(page+1,3,10,QChar('0')));
    if(dialog.exec()!=QDialog::Accepted||dialog.selectedFiles().isEmpty())return;
    const auto path=dialog.selectedFiles().front();QString error;bool success=false;
    logPractice(LogLevel::Info,QString("导出%1简谱：%2").arg(allPages?"全部页面":"当前页",path));
    if(allPages){
        QProgressDialog progress("正在生成简谱 PNG…","取消",0,count,this);progress.setObjectName("practiceScoreExportProgress");
        progress.setWindowModality(Qt::WindowModal);progress.setMinimumDuration(0);progress.setAutoClose(false);progress.setAutoReset(false);
        success=writeScoreArchive(path,count,[this](int index){return numberScore_->renderPage(index);},songName_,error,
            [&progress](int done,int total){progress.setLabelText(QString("正在生成简谱 PNG · %1 / %2 页").arg(done).arg(total));progress.setValue(done);QApplication::processEvents();return !progress.wasCanceled();});
        if(progress.wasCanceled()){feedback_="已取消下载简谱。";logPractice(LogLevel::Info,"已取消导出全部简谱："+path);updatePrompt();return;}
    }else{
        QSaveFile file(path);const auto image=numberScore_->renderPage(page);
        if(!image.isNull()&&file.open(QIODevice::WriteOnly)&&image.save(&file,"PNG")&&file.commit())success=true;
        else{error="无法保存 PNG："+file.errorString();file.cancelWriting();}
    }
    if(!success){logPractice(LogLevel::Error,QString("导出简谱失败：%1；%2").arg(path,error));QMessageBox::warning(this,"下载简谱失败",error);return;}
    feedback_=allPages?QString("已下载全部 %1 页简谱：%2").arg(count).arg(QFileInfo(path).fileName()):
        QString("已下载第 %1 页简谱：%2").arg(page+1).arg(QFileInfo(path).fileName());updatePrompt();
    logPractice(LogLevel::Info,QString("简谱导出完成：%1，%2 页").arg(path).arg(allPages?count:1));
}
void PracticePage::openSettings(){
    pause();if(settingsDialog_){settingsDialog_->show();settingsDialog_->raise();return;}
    auto* dialog=new PracticeSettingsDialog(options_,result_?result_->duration:0,position_,practice_.groups(),this);
    settingsDialog_=dialog;dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog,&QDialog::accepted,this,[this,dialog]{applyOptions(dialog->options());setFocus();});
    connect(dialog,&QDialog::rejected,this,[this]{setFocus();});dialog->open();
}
double PracticePage::rangeStart() const{return options_.loopEnabled?options_.loopStart:0;}
double PracticePage::rangeEnd() const{return options_.loopEnabled?options_.loopEnd:result_?result_->duration:0;}
std::pair<size_t,size_t> PracticePage::groupRange() const{
    const auto& groups=practice_.groups();
    if(!options_.loopEnabled)return {0,groups.size()};
    auto first=std::lower_bound(groups.begin(),groups.end(),rangeStart(),[](const auto& group,double time){return group.start<time;});
    auto last=std::lower_bound(first,groups.end(),rangeEnd(),[](const auto& group,double time){return group.start<time;});
    return {static_cast<size_t>(first-groups.begin()),static_cast<size_t>(last-groups.begin())};
}
void PracticePage::updateKeyLabels(){
    timeline_->setPracticeKeys(options_.inputKeys);board_->setKeyLabels(options_.inputKeys);
    for(int target=0;target<9;++target)chips_[target]->setText(QString::fromUtf8(degrees[target])+" · "+QChar(options_.inputKeys[target]));
}
void PracticePage::loadPreferences(){
    QSettings saved;
    options_.metronomeEnabled=saved.value("practice/metronomeEnabled",false).toBool();
    options_.followScore=saved.value("practice/followScore",true).toBool();
    options_.metronomeBpm=saved.value("practice/metronomeBpm",120).toDouble();
    if(!std::isfinite(options_.metronomeBpm)||options_.metronomeBpm<30||options_.metronomeBpm>300)options_.metronomeBpm=120;
    options_.beatsPerBar=std::clamp(saved.value("practice/beatsPerBar",4).toInt(),1,12);
    const auto storedKeys=saved.value("practice/inputKeys").toString().toUpper();
    if(storedKeys.size()==9){
        auto candidate=options_.inputKeys;bool valid=true;
        for(int i=0;i<9;++i){
            candidate[i]=storedKeys[i].unicode();
            if(!((candidate[i]>='A'&&candidate[i]<='Z')||(candidate[i]>='0'&&candidate[i]<='9'))||
               std::find(candidate.begin(),candidate.begin()+i,candidate[i])!=candidate.begin()+i)valid=false;
        }
        if(valid)options_.inputKeys=candidate;
    }
}
void PracticePage::applyOptions(const PracticeOptions& options){
    pause();options_=options;metronomeBeats_.clear();metronomePlanReady_=false;feedback_.clear();
    timeline_->setPlaybackRange(rangeStart(),options_.loopEnabled?rangeEnd():-1);updateKeyLabels();
    if(options_.loopEnabled&&(position_<rangeStart()||position_>=rangeEnd()))updatePosition(rangeStart());
    practice_.seek(position_);
    alignFollowingGroup();
    QSettings saved;saved.setValue("practice/metronomeEnabled",options_.metronomeEnabled);saved.setValue("practice/followScore",options_.followScore);
    saved.setValue("practice/metronomeBpm",options_.metronomeBpm);saved.setValue("practice/beatsPerBar",options_.beatsPerBar);
    QString keyText;for(auto value:options_.inputKeys)keyText+=QChar(value);saved.setValue("practice/inputKeys",keyText);
    logPractice(LogLevel::Info,QString("练习设置已更新：循环 %1（%2～%3 秒），节拍器 %4，跟随曲谱 %5，%6 BPM / %7 拍，键位 %8")
        .arg(options_.loopEnabled?"开启":"关闭").arg(options_.loopStart,0,'f',2).arg(options_.loopEnd,0,'f',2)
        .arg(options_.metronomeEnabled?"开启":"关闭",options_.followScore?"开启":"关闭").arg(options_.metronomeBpm).arg(options_.beatsPerBar).arg(keyText));
    updatePrompt();
}
void PracticePage::alignFollowingGroup(){
    if(followingMode_&&options_.loopEnabled&&(practice_.finished()||practice_.groups()[practice_.index()].start>=rangeEnd())){
        practice_.seek(rangeStart());updatePosition(rangeStart());
    }
}
void PracticePage::startMetronome(){
    if(!options_.metronomeEnabled||!running_||!song_||!result_)return;
    QString error;bool started=false;
    try{
        const auto speed=speed_->currentData().toDouble();
        if(followingMode_){
            auto [bpm,beats]=options_.followScore?metronomeRhythmAt(*song_,settings_,position_):std::pair{options_.metronomeBpm,options_.beatsPerBar};
            started=metronome_.start(bpm*speed,beats,error);
        }else{
            if(!metronomePlanReady_){
                if(options_.followScore)metronomeBeats_=makeMetronomeBeats(*song_,settings_,result_->duration);
                else{
                    const double step=60/options_.metronomeBpm;
                    const double count=std::ceil(result_->duration/step);
                    if(count>500000)throw std::runtime_error("曲目拍数过多，请缩短曲目后开启节拍器。");
                    metronomeBeats_.reserve(static_cast<size_t>(count));
                    for(size_t i=0;i<static_cast<size_t>(count);++i)metronomeBeats_.push_back({i*step,i%options_.beatsPerBar==0});
                }
                metronomePlanReady_=true;
            }
            started=metronome_.play(metronomeBeats_,audio_.position(),rangeEnd(),speed,error,&audio_);
        }
    }catch(const std::exception& e){error=QString::fromUtf8(e.what());}
    if(!started){feedback_=error+" 歌曲练习仍可继续。";logPractice(LogLevel::Warning,"节拍器启动失败："+error);}
}
void PracticePage::updateMetronomeRhythm(){
    if(!running_||!followingMode_||!options_.metronomeEnabled||!song_)return;
    try{
        auto [bpm,beats]=options_.followScore?metronomeRhythmAt(*song_,settings_,position_):std::pair{options_.metronomeBpm,options_.beatsPerBar};
        metronome_.setRhythm(bpm*speed_->currentData().toDouble(),beats);
    }catch(const std::exception& e){metronome_.stop();feedback_=QString::fromUtf8(e.what());logPractice(LogLevel::Warning,"节拍器节奏更新失败："+feedback_);}
}
}
