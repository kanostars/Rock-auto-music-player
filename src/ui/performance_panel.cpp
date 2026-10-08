#include "performance_panel.h"
#include "playback_icons.h"
#include "theme.h"
#include "app/preferences.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <limits>
namespace rock {
namespace {
QLabel* text(const QString& value,const char* role){auto* w=new QLabel(value);w->setProperty("role",role);w->setTextFormat(Qt::PlainText);return w;}
QPushButton* button(const QString& value,const char* name){auto* w=new QPushButton(value);w->setObjectName(name);w->setCursor(Qt::PointingHandCursor);return w;}

}
PerformancePanel::PerformancePanel(QWidget* parent):QWidget(parent){
    setObjectName("performancePanel");Theme::setStyle(this,"QWidget#performancePanel {background:white;}");
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(14,18,14,14);layout->setSpacing(12);
    auto* heading=new QHBoxLayout;heading->addWidget(text("演奏输出","section"));heading->addStretch();
    layout->addLayout(heading);
    auto* hint=text("共用工作台的曲目、音符、时间与已应用参数。设置 BPM、速度或按键时长后，请先应用转换设置。","muted");hint->setWordWrap(true);layout->addWidget(hint);
    auto addSelector=[&](const QString& name,const char* objectName,const char* refreshName,const char* statusName,const QString& refreshText,QComboBox*& combo,QPushButton*& refresh,QLabel*& status){
        layout->addWidget(text(name,"section"));auto* row=new QHBoxLayout;
        refresh=button(refreshText,refreshName);Theme::setStyle(refresh,"padding:7px 8px;");row->addWidget(refresh);
        combo=new QComboBox;combo->setObjectName(objectName);combo->setMinimumWidth(80);combo->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);combo->setEnabled(false);row->addWidget(combo,1);layout->addLayout(row);
        status=text("尚未查找，请点击"+refreshText,"muted");status->setObjectName(statusName);status->setWordWrap(true);layout->addWidget(status);
        connect(combo,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,combo,status,name](int index){
            combo->setToolTip(index>=0?combo->itemData(index,Qt::ToolTipRole).toString():QString());
            if(index>=0){status->setText("已选择"+name);Theme::setStyle(status,"color:#178e80;");}if(start_)updateControls();
        });
    };
    addSelector("输出键盘","performanceKeyboard","refreshKeyboardsButton","keyboardDiscoveryStatus","刷新键盘",keyboards_,refreshKeyboards_,keyboardStatus_);
    addSelector("目标窗口","performanceWindow","refreshWindowsButton","windowDiscoveryStatus","刷新窗口",windows_,refreshWindows_,windowStatus_);
    connect(refreshKeyboards_,&QPushButton::clicked,this,[this]{refreshDevices(true);});
    connect(refreshWindows_,&QPushButton::clicked,this,[this]{refreshDevices(false);});
    connect(&keyboardWatcher_,&QFutureWatcher<DiscoveryResult>::finished,this,[this]{discoveryFinished(true);});
    connect(&windowWatcher_,&QFutureWatcher<DiscoveryResult>::finished,this,[this]{discoveryFinished(false);});
    auto* form=new QFormLayout;countdown_=new QSpinBox;countdown_->setObjectName("performanceCountdown");
    countdown_->setRange(1,std::numeric_limits<int>::max());countdown_->setValue(5);countdown_->setSuffix(" 秒");countdown_->setButtonSymbols(QAbstractSpinBox::NoButtons);form->addRow("开始倒计时",countdown_);layout->addLayout(form);
    activate_=new QCheckBox("立即打开目标窗口");activate_->setObjectName("activatePerformanceWindow");activate_->setChecked(true);layout->addWidget(activate_);
    state_=text("请刷新并选择输出键盘和目标窗口。","muted");state_->setObjectName("performanceState");state_->setWordWrap(true);layout->addWidget(state_);
    layout->addStretch();shortcutHint_=text({},"muted");shortcutHint_->setWordWrap(true);layout->addWidget(shortcutHint_);
    auto updateHint=[this]{auto& p=Preferences::instance();shortcutHint_->setText(p.shortcutText(ShortcutAction::PerformancePause)+" 暂停 / 继续 · "+p.shortcutText(ShortcutAction::PerformanceStop)+" 终止\n"+p.shortcutText(ShortcutAction::PerformancePrevious)+" 上一首 · "+p.shortcutText(ShortcutAction::PerformanceNext)+" 下一首\n"+p.shortcutText(ShortcutAction::MiniMode)+" 主窗口 / 小窗切换\n目标窗口离开前台时暂停。自动演奏期间锁定音符编辑，终止后恢复。\n测试按键已移至顶部的设置页面。");};
    updateHint();connect(&Preferences::instance(),&Preferences::shortcutsChanged,this,updateHint);

    playlistControls_=new QWidget(this);auto* actions=new QHBoxLayout(playlistControls_);actions->setContentsMargins(0,0,0,0);actions->setSpacing(6);
    moveUp_=button({},"movePerformanceSongUp");moveDown_=button({},"movePerformanceSongDown");removeSong_=button({},"removePerformanceSong");playMode_=button({},"performancePlayMode");
    for(auto* action:{moveUp_,moveDown_,removeSong_,playMode_}){
        action->setIconSize(QSize(22,22));action->setFixedHeight(34);action->setMinimumWidth(32);
        action->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);Theme::setStyle(action,"padding:4px;");actions->addWidget(action,1);
    }
    moveUp_->setIcon(playlistIcon(PlaylistIcon::Up));moveUp_->setToolTip("上移曲目");moveUp_->setAccessibleName("上移曲目");
    moveDown_->setIcon(playlistIcon(PlaylistIcon::Down));moveDown_->setToolTip("下移曲目");moveDown_->setAccessibleName("下移曲目");
    removeSong_->setIcon(playlistIcon(PlaylistIcon::Remove));removeSong_->setAccessibleName("删除曲目");
    removeSong_->setToolTip("删除曲目：移出曲目库并清除该曲目的会话编辑，不删除 MIDI 文件。");
    Theme::setStyle(playMode_,"QPushButton {padding:4px;background:#e5f3ef;border-color:#badfd5;} QPushButton:hover {background:#d8eee7;} QPushButton:disabled {background:#f5f7f8;border-color:#e6edf0;}");
    updatePlayMode();
    connect(moveUp_,&QPushButton::clicked,this,[this]{if(library_)emit songMoveRequested(library_->currentRow(),library_->currentRow()-1);});
    connect(moveDown_,&QPushButton::clicked,this,[this]{if(library_)emit songMoveRequested(library_->currentRow(),library_->currentRow()+1);});
    connect(removeSong_,&QPushButton::clicked,this,[this]{if(library_)emit songRemoveRequested(library_->currentRow());});
    connect(playMode_,&QPushButton::clicked,this,&PerformancePanel::cyclePlayMode);
    transportControls_=new QWidget(this);auto* transport=new QHBoxLayout(transportControls_);transport->setContentsMargins(0,0,0,0);transport->setSpacing(6);
    previousSong_=button("上一首","previousPerformanceSong");nextSong_=button("下一首","nextPerformanceSong");start_=button("开始演奏","startPerformanceButton");
    auto updateTransportHints=[this]{auto& p=Preferences::instance();previousSong_->setToolTip("上一首（"+p.shortcutText(ShortcutAction::PerformancePrevious)+"）");nextSong_->setToolTip("下一首（"+p.shortcutText(ShortcutAction::PerformanceNext)+"）");};updateTransportHints();connect(&Preferences::instance(),&Preferences::shortcutsChanged,this,updateTransportHints);
    transport->addWidget(previousSong_);transport->addWidget(nextSong_);transport->addWidget(start_);
    connect(previousSong_,&QPushButton::clicked,this,[this]{navigateSong(true);});connect(nextSong_,&QPushButton::clicked,this,[this]{navigateSong(false);});
    connect(start_,&QPushButton::clicked,this,&PerformancePanel::startRequested);
    timer_=new QTimer(this);timer_->setInterval(16);connect(timer_,&QTimer::timeout,this,&PerformancePanel::updatePerformance);updateControls();
}
void PerformancePanel::updatePlayMode(){
    const QStringList names{"顺序播放","单曲循环","随机播放","单曲播放（结束后停止）"};
    const PlaylistIcon icons[]{PlaylistIcon::Loop,PlaylistIcon::Single,PlaylistIcon::Shuffle,PlaylistIcon::Once};
    playMode_->setIcon(playlistIcon(icons[playModeIndex_]));playMode_->setAccessibleName(names[playModeIndex_]);
    playMode_->setToolTip(names[playModeIndex_]+" · 试听与自动演奏共用\n点击切换为"+names[(playModeIndex_+1)%4]);
}
void PerformancePanel::cyclePlayMode(){if(libraryBusy_||(running_&&controller_.snapshot().state!=PerformanceState::Paused))return;playModeIndex_=(playModeIndex_+1)%4;updatePlayMode();resetQueue();}
int PerformancePanel::countdown() const{return countdown_->value();}
bool PerformancePanel::activatesTarget() const{return activate_->isChecked();}
void PerformancePanel::setStartOptions(int countdown,bool activateTarget){if(running_)return;countdown_->setValue(countdown);activate_->setChecked(activateTarget);}
bool PerformancePanel::outputReady() const{return keyboards_->currentIndex()>=0&&windows_->currentIndex()>=0;}
QString PerformancePanel::statusText() const{return state_->text();}
void PerformancePanel::setLibrary(QListWidget* library){
    library_=library;
    connect(library_,&QListWidget::currentRowChanged,this,[this]{updateControls();});
    connect(library_->model(),&QAbstractItemModel::rowsInserted,this,[this]{updateControls();});
    connect(library_->model(),&QAbstractItemModel::rowsRemoved,this,[this]{updateControls();});updateControls();
}
void PerformancePanel::setLibraryBusy(bool busy){libraryBusy_=busy;updateControls();}
void PerformancePanel::resetQueue(){
    randomRemaining_.clear();songHistory_.clear();historyCursor_=-1;
    if(library_&&library_->currentIndex().isValid()){songHistory_.push_back(library_->currentIndex().row());historyCursor_=0;}
}
int PerformancePanel::nextSong(bool natural){
    const int count=library_?library_->count():0,current=library_?library_->currentRow():-1;
    if(count==0||current<0)return -1;
    if(natural&&playModeIndex_==3)return -1;
    if(natural&&playModeIndex_==1)return current;
    if(historyCursor_+1<static_cast<int>(songHistory_.size()))return songHistory_[++historyCursor_];
    int next=(current+1)%count;
    if(playModeIndex_==2&&count>1){
        if(randomRemaining_.empty()){
            for(int row=0;row<count;++row)if(row!=current)randomRemaining_.push_back(row);
            std::shuffle(randomRemaining_.begin(),randomRemaining_.end(),*QRandomGenerator::global());
        }
        next=randomRemaining_.back();randomRemaining_.pop_back();
    }
    songHistory_.push_back(next);historyCursor_=static_cast<int>(songHistory_.size())-1;
    // Bound the navigation history during long continuous performances.
    if(songHistory_.size()>256){songHistory_.erase(songHistory_.begin());--historyCursor_;}
    return next;
}
void PerformancePanel::navigateSong(bool previous){
    if(libraryBusy_||!library_||!library_->currentIndex().isValid())return;
    if(historyCursor_<0)resetQueue();
    int row;
    if(previous){
        if(historyCursor_>0)row=songHistory_[--historyCursor_];
        else {const int count=library_->model()->rowCount();row=(library_->currentIndex().row()+count-1)%count;
            songHistory_.insert(songHistory_.begin(),row);historyCursor_=0;}
    }else row=nextSong(false);
    const auto state=controller_.snapshot().state;
    switchSong(row,state==PerformanceState::Playing||state==PerformanceState::Countdown,previewPlaying_);
}
void PerformancePanel::previewFinished(){
    if(running_||libraryBusy_)return;
    const int row=nextSong(true);
    if(row>=0)switchSong(row,false,true);
}
void PerformancePanel::switchSong(int row,bool continuePlaying,bool preview){
    if(libraryBusy_||!library_||!library_->model()||row<0||row>=library_->model()->rowCount()){queueActive_=false;return;}
    switchingSong_=true;stopPerformance();
    emit songChangeRequested(row); // The shared library owner selects the session synchronously.
    setPreviewPosition(rangeStart_);switchingSong_=false;
    queueActive_=continuePlaying;
    if(continuePlaying&&library_->currentIndex().row()==row&&song_&&result_)beginSong();
    else queueActive_=false;
    if(preview&&library_->currentRow()==row&&song_&&result_)emit previewStartRequested();
}
void PerformancePanel::setSong(std::shared_ptr<const Song> song,std::shared_ptr<const Conversion> result,const QString&,const Settings& settings){
    stopPerformance();const bool changed=song_!=song;song_=std::move(song);result_=std::move(result);settings_=settings;
    if(changed){position_=0;rangeStart_=0;rangeEnd_=-1;}
    if(!song_||!result_){song_.reset();result_.reset();position_=0;state_->setText("曲目库为空，请导入 MIDI。");}
    else if(changed&&!switchingSong_)state_->setText("请刷新并选择输出键盘和目标窗口。");
    setPreviewPosition(position_);updateControls();
}
void PerformancePanel::setPreviewPosition(double seconds){
    position_=std::clamp(std::isfinite(seconds)?seconds:0.0,0.0,result_?result_->duration:0.0);
    emit sourcePositionChanged(position_);
}
void PerformancePanel::refreshDevices(bool keyboard) {
    if(running_)return;
    auto& watcher=keyboard?keyboardWatcher_:windowWatcher_;if(watcher.isRunning())return;
    auto* combo=keyboard?keyboards_:windows_;auto* refresh=keyboard?refreshKeyboards_:refreshWindows_;
    auto* status=keyboard?keyboardStatus_:windowStatus_;
    {const QSignalBlocker blocker(combo);combo->clear();combo->setCurrentIndex(-1);}
    combo->setToolTip({});combo->setEnabled(false);refresh->setEnabled(false);
    Theme::setStyle(status,"color:#8397a3;");status->setText(keyboard?"正在查找键盘…":"正在查找窗口…");
    const auto query=keyboard?discoverKeyboards:discoverWindows;
    // The worker owns its callable and never touches widgets, including after this page is closed.
    watcher.setFuture(QtConcurrent::run([query]{
        try{return query();}catch(...){return DiscoveryResult{{},"查找失败，请重试。"};}
    }));
    updateControls();
}
void PerformancePanel::discoveryFinished(bool keyboard) {
    auto* combo=keyboard?keyboards_:windows_;auto* refresh=keyboard?refreshKeyboards_:refreshWindows_;
    auto* status=keyboard?keyboardStatus_:windowStatus_;
    const auto result=(keyboard?keyboardWatcher_:windowWatcher_).result();refresh->setEnabled(true);
    {const QSignalBlocker blocker(combo);
        for(const auto& choice:result.choices) {
            combo->addItem(choice.label,choice.id);int row=combo->count()-1;
            combo->setItemData(row,choice.detail,Qt::ToolTipRole);combo->setItemData(row,choice.processId,Qt::UserRole+1);
        }
        combo->setCurrentIndex(-1); // Never silently select the first discovered item.
    }
    if(!result.error.isEmpty()) {
        combo->clear();combo->setEnabled(false);status->setText(result.error);Theme::setStyle(status,"color:#bd4f5b;");
    } else if(combo->count()==0) {
        status->setText(keyboard?"未找到键盘，请检查连接后重试。":"未找到窗口，请打开目标程序后重试。");Theme::setStyle(status,"color:#a77722;");
    } else {
        combo->setEnabled(true);status->setText(QString("找到 %1 个%2，请手动选择。").arg(combo->count()).arg(keyboard?"键盘":"窗口"));Theme::setStyle(status,"color:#178e80;");
    }
    updateControls();
}
PerformancePanel::~PerformancePanel(){controller_.stop();}
void PerformancePanel::stopPerformance(){if(!switchingSong_){queueActive_=false;resetQueue();}controller_.stop();if(timer_)updatePerformance();}
void PerformancePanel::updateControls(){
    const bool available=song_&&result_&&keyboards_->currentIndex()>=0&&windows_->currentIndex()>=0;
    start_->setEnabled(running_||available);
    refreshKeyboards_->setEnabled(!running_&&!keyboardWatcher_.isRunning());refreshWindows_->setEnabled(!running_&&!windowWatcher_.isRunning());
    keyboards_->setEnabled(!running_&&keyboards_->count()>0);windows_->setEnabled(!running_&&windows_->count()>0);
    activate_->setEnabled(!running_);
    countdown_->setEnabled(!running_);
    playMode_->setEnabled(canChangePlayMode());if(library_)library_->setEnabled(!libraryBusy_);
    const int count=library_?library_->count():0,row=library_?library_->currentRow():-1;
    const bool selected=!libraryBusy_&&row>=0&&row<count;
    previousSong_->setEnabled(selected&&count>1);nextSong_->setEnabled(selected&&count>1);
    removeSong_->setEnabled(selected);moveUp_->setEnabled(selected&&row>0);moveDown_->setEnabled(selected&&row+1<count);
    if(libraryBusy_)start_->setEnabled(false);
}
void PerformancePanel::updatePerformance(){
    const auto s=controller_.snapshot();snapshot_=s;const bool wasRunning=running_;running_=s.active();
    if(wasRunning||running_)setPreviewPosition(s.position);
    if(!s.message.isEmpty())state_->setText(s.state==PerformanceState::Countdown?QString("倒计时 %1 秒 · %2").arg(static_cast<int>(std::ceil(s.countdown))).arg(s.message):s.message);
    Theme::setStyle(state_,s.state==PerformanceState::Failed?"color:#bd4f5b;":"color:#178e80;");
    start_->setText(s.state==PerformanceState::Paused?"继续演奏":running_?"暂停演奏":"开始演奏");
    updateControls();if(!running_)timer_->stop();
    if(wasRunning!=running_)emit activeChanged(running_);
    emit statusChanged(state_->text());
    if(wasRunning&&!running_&&!switchingSong_&&queueActive_){
        if(s.state==PerformanceState::Finished){const int row=nextSong(true);if(row>=0){switchSong(row,true);return;}}
        queueActive_=false;
    }
}
void PerformancePanel::startPerformance(){
    if(running_){controller_.togglePause();updatePerformance();return;}
    resetQueue();queueActive_=true;
    if(result_&&(position_<rangeStart_||position_>=(rangeEnd_<0?result_->duration:rangeEnd_)))setPreviewPosition(rangeStart_);
    beginSong();
}
void PerformancePanel::beginSeek(){if(running_){controller_.beginSeek();updatePerformance();}}
void PerformancePanel::seekPerformance(double seconds){
    if(!running_||libraryBusy_||!result_||!std::isfinite(seconds))return;
    const double end=rangeEnd_<0?result_->duration:std::min(rangeEnd_,result_->duration);
    if(end<=rangeStart_){controller_.cancelSeek();return;}
    const double position=std::clamp(seconds,rangeStart_,end);
    try{
        // The new plan includes earlier strikes when rewinding. At the exact
        // range end, use an empty tail and let the engine finish naturally.
        auto plan=makePerformancePlan(*result_,std::min(position,std::nextafter(end,rangeStart_)),settings_.holdMs,settings_.gapMs,end);
        plan.start=position;controller_.seek(std::move(plan));updatePerformance();
    }catch(const std::exception& e){
        controller_.cancelSeek(QString::fromUtf8(e.what()));updatePerformance();
        Theme::setStyle(state_,"color:#bd4f5b;");
    }
}
void PerformancePanel::beginSong(){
    if(libraryBusy_||!song_||!result_||keyboards_->currentIndex()<0||windows_->currentIndex()<0){queueActive_=false;return;}
    QString error;
    try{auto plan=makePerformancePlan(*result_,position_,settings_.holdMs,settings_.gapMs,rangeEnd_);
        countdown_->interpretText();plan.countdownSeconds=countdown_->value();
        OutputTarget target{keyboards_->currentData().toString(),windows_->currentData().toULongLong(),windows_->currentData(Qt::UserRole+1).toUInt()};
        // Finish local focus changes before handing the foreground to the target.
        running_=true;updateControls();emit activeChanged(true);
        if(activate_->isChecked())emit targetActivationRequested();
        if(controller_.start(std::move(plan),target,activate_->isChecked(),error)){timer_->start();updatePerformance();return;}
    }catch(const std::exception& e){error=QString::fromUtf8(e.what());}
    queueActive_=false;running_=false;updateControls();emit activeChanged(false);
    snapshot_.state=PerformanceState::Failed;snapshot_.message=error;
    state_->setText(error);Theme::setStyle(state_,"color:#bd4f5b;");emit statusChanged(error);
}
}
