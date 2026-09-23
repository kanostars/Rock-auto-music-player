#include "performance_page.h"
#include "piano_roll.h"
#include "handpan_test.h"
#include <QComboBox>
#include <QCheckBox>
#include <QTimer>
#include <QHideEvent>
#include <QDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>

namespace rock {
namespace {
QLabel* text(const QString& value,const char* role) {
    auto* label=new QLabel(value);label->setProperty("role",role);label->setTextFormat(Qt::PlainText);return label;
}
QFrame* panel() {auto* frame=new QFrame;frame->setObjectName("panel");return frame;}
QPushButton* button(const QString& value,const char* name) {
    auto* button=new QPushButton(value);button->setObjectName(name);button->setCursor(Qt::PointingHandCursor);return button;
}
QString timeText(double seconds) {
    const int whole=static_cast<int>(std::clamp(seconds,0.0,2e9));
    return QString("%1:%2").arg(whole/60,2,10,QChar('0')).arg(whole%60,2,10,QChar('0'));
}
QString previewTimeText(double seconds) {
    const int ms=static_cast<int>(std::clamp(seconds*1000,0.0,2e9));
    return QString("%1:%2.%3").arg(ms/60000,2,10,QChar('0')).arg(ms/1000%60,2,10,QChar('0')).arg(ms%1000/10,2,10,QChar('0'));
}
double mapPosition(const Song& song,const Settings& from,const Settings& to,double seconds){
    if(from.fixedTempo==to.fixedTempo&&(!from.fixedTempo||from.bpm==to.bpm)&&from.speed==to.speed)return seconds;
    auto time=[&](const Settings& s,int tick){return (s.fixedTempo?double(tick)/song.ppq*60/s.bpm:song.secondsAt(tick))/s.speed;};
    int tick=tickAtSeconds(song,from,seconds);if(tick>0&&time(from,tick)>seconds)--tick;
    const double fraction=(seconds-time(from,tick))/(time(from,tick+1)-time(from,tick));
    return time(to,tick)+fraction*(time(to,tick+1)-time(to,tick));
}
}
PerformancePage::PerformancePage(QWidget* parent,OutputDiscovery discovery,AudioBackend backend):QWidget(parent),discovery_(std::move(discovery)),audioBackend_(backend) {
    setObjectName("performancePage");
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(14);
    auto* songCard=panel();auto* songLayout=new QVBoxLayout(songCard);songLayout->setContentsMargins(22,16,22,16);songLayout->setSpacing(7);
    auto* titleBar=new QHBoxLayout;title_=text("尚未选择歌曲","title");title_->setObjectName("performanceSongTitle");
    title_->setWordWrap(true);title_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);titleBar->addWidget(title_,1);
    auto* badge=text("自动演奏","section");badge->setStyleSheet("color:#178e80; background:#e5f3ef; padding:6px 12px; border-radius:6px;");titleBar->addWidget(badge);songLayout->addLayout(titleBar);
    metadata_=text("请导入 MIDI，或在音符工作台选择一首歌曲","muted");metadata_->setObjectName("performanceSongInfo");metadata_->setWordWrap(true);songLayout->addWidget(metadata_);layout->addWidget(songCard);

    auto* middle=new QHBoxLayout;middle->setSpacing(14);layout->addLayout(middle,1);
    auto* previewCard=panel();auto* previewLayout=new QVBoxLayout(previewCard);previewLayout->setContentsMargins(18,16,18,12);previewLayout->setSpacing(10);
    auto* previewBar=new QHBoxLayout;previewBar->addWidget(text("音符播放预览","section"));previewBar->addWidget(text("只读","muted"));previewBar->addStretch();
    auto* fit=button("查看全部","performanceFitButton");previewBar->addWidget(fit);previewLayout->addLayout(previewBar);
    empty_=text("暂无音符 · 请先在工作台导入并转换歌曲","muted");empty_->setAlignment(Qt::AlignCenter);previewLayout->addWidget(empty_);
    preview_=new PianoRoll;preview_->setObjectName("performanceRoll");preview_->setEditingEnabled(false);previewLayout->addWidget(preview_,1);
    connect(fit,&QPushButton::clicked,preview_,&PianoRoll::fitAll);
    connect(preview_,&PianoRoll::seekRequested,this,[this](double seconds){if(!running_)setPreviewPosition(seconds);});
    auto* previewFooter=new QHBoxLayout;clock_=text("00:00.00 / 00:00.00","section");clock_->setObjectName("performanceClock");previewFooter->addWidget(clock_);
    previewFooter->addStretch();previewFooter->addWidget(text("点击定位 · Ctrl + 滚轮缩放","muted"));
    auto* minus=button("−","performanceZoomOut");auto* plus=button("＋","performanceZoomIn");
    minus->setFixedWidth(34);plus->setFixedWidth(34);previewFooter->addWidget(minus);previewFooter->addWidget(plus);previewLayout->addLayout(previewFooter);
    connect(minus,&QPushButton::clicked,this,[this]{preview_->setZoom(preview_->zoom()/1.4);});
    connect(plus,&QPushButton::clicked,this,[this]{preview_->setZoom(preview_->zoom()*1.4);});middle->addWidget(previewCard,1);

    auto* settingsCard=panel();settingsCard->setFixedWidth(260);auto* settingsLayout=new QVBoxLayout(settingsCard);settingsLayout->setContentsMargins(18,18,18,18);settingsLayout->setSpacing(8);
    settingsLayout->addWidget(text("演奏参数","section"));settingsLayout->addSpacing(4);
    tempoMode_=new QComboBox;tempoMode_->setObjectName("performanceTempoMode");tempoMode_->addItems({"跟随歌曲速度","固定 BPM"});settingsLayout->addWidget(tempoMode_);
    auto* timing=new QFormLayout;timing->setSpacing(10);timing->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);settingsLayout->addLayout(timing);
    bpm_=new QDoubleSpinBox;bpm_->setObjectName("performanceBpm");bpm_->setRange(20,400);bpm_->setDecimals(1);bpm_->setValue(120);bpm_->setSuffix(" BPM");bpm_->setButtonSymbols(QAbstractSpinBox::NoButtons);bpm_->setEnabled(false);timing->addRow(text("速度","muted"),bpm_);
    connect(tempoMode_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){bpm_->setEnabled(index==1);});
    auto addTiming=[&](const QString& name,const char* objectName,int minimum,int maximum,int value,const QString& suffix){
        auto* spin=new QSpinBox;spin->setObjectName(objectName);spin->setRange(minimum,maximum);spin->setValue(value);spin->setSuffix(suffix);spin->setButtonSymbols(QAbstractSpinBox::NoButtons);timing->addRow(text(name,"muted"),spin);return spin;
    };
    hold_=addTiming("按下时长","performanceHold",1,1000,30," ms");
    gap_=addTiming("松开后间隔","performanceGap",0,1000,20," ms");
    countdown_=addTiming("开始倒计时","performanceCountdown",5,5,5," 秒");countdown_->setEnabled(false);
    settingsLayout->addStretch();auto* hint=text("从当前预览位置开始。目标窗口需保持前台；切走或最小化后暂停。Ctrl+Alt+Q 暂停/继续，Ctrl+Alt+E 终止。","muted");hint->setWordWrap(true);settingsLayout->addWidget(hint);middle->addWidget(settingsCard);

    auto* outputCard=panel();auto* outputLayout=new QVBoxLayout(outputCard);outputLayout->setContentsMargins(20,15,20,15);outputLayout->setSpacing(9);
    auto* outputTitle=new QHBoxLayout;outputTitle->addWidget(text("演奏输出","section"));
    auto* testKeys=testKeys_=button("测试按键","testKeysButton");testKeys->setStyleSheet("padding:4px 10px;");outputTitle->addWidget(testKeys);
    connect(testKeys,&QPushButton::clicked,this,&PerformancePage::showKeyTestWindow);
    outputTitle->addStretch();outputTitle->addWidget(text("九键  B · F · G · H · J · K · T · Y · U","muted"));outputLayout->addLayout(outputTitle);
    auto* selectors=new QHBoxLayout;selectors->setSpacing(16);
    auto addSelector=[&](const QString& name,const char* objectName,const char* refreshName,const char* statusName,const QString& refreshText,QComboBox*& combo,QPushButton*& refresh,QLabel*& status){
        auto* group=new QVBoxLayout;group->setSpacing(5);group->addWidget(text(name,"muted"));
        auto* row=new QHBoxLayout;row->setSpacing(8);refresh=button(refreshText,refreshName);row->addWidget(refresh);
        combo=new QComboBox;combo->setObjectName(objectName);combo->setEnabled(false);
        combo->setMinimumWidth(100);combo->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        row->addWidget(combo,1);group->addLayout(row);
        status=text("尚未查找，请点击"+refreshText,"muted");status->setObjectName(statusName);status->setWordWrap(true);
        status->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);group->addWidget(status);selectors->addLayout(group,1);
        connect(combo,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,combo,status,name](int index){
            combo->setToolTip(index>=0?combo->itemData(index,Qt::ToolTipRole).toString():QString());
            if(index>=0){status->setText("已选择"+name);status->setStyleSheet("color:#178e80;");}
            if(start_)updateControls();
        });
    };
    addSelector("输出键盘","performanceKeyboard","refreshKeyboardsButton","keyboardDiscoveryStatus","刷新键盘",keyboards_,refreshKeyboards_,keyboardStatus_);
    addSelector("目标窗口","performanceWindow","refreshWindowsButton","windowDiscoveryStatus","刷新窗口",windows_,refreshWindows_,windowStatus_);
    connect(refreshKeyboards_,&QPushButton::clicked,this,[this]{refreshDevices(true);});
    connect(refreshWindows_,&QPushButton::clicked,this,[this]{refreshDevices(false);});
    connect(&keyboardWatcher_,&QFutureWatcher<DiscoveryResult>::finished,this,[this]{discoveryFinished(true);});
    connect(&windowWatcher_,&QFutureWatcher<DiscoveryResult>::finished,this,[this]{discoveryFinished(false);});
    auto* actions=new QVBoxLayout;
    auto* actionButtons=new QHBoxLayout;
    start_=button("开始演奏","startPerformanceButton");start_->setMinimumWidth(120);start_->setMinimumHeight(44);start_->setEnabled(false);actionButtons->addWidget(start_);
    stop_=button("终止","stopPerformanceButton");stop_->setMinimumHeight(44);stop_->setEnabled(false);actionButtons->addWidget(stop_);actions->addLayout(actionButtons);
    activate_=new QCheckBox("立即打开目标窗口");activate_->setObjectName("activatePerformanceWindow");activate_->setChecked(false);actions->addWidget(activate_);
    selectors->addLayout(actions);outputLayout->addLayout(selectors);
    state_=text("请选择输出键盘和目标窗口，开始后倒计时 5 秒。","muted");state_->setObjectName("performanceState");state_->setWordWrap(true);outputLayout->addWidget(state_);layout->addWidget(outputCard);
    connect(start_,&QPushButton::clicked,this,&PerformancePage::startPerformance);
    connect(stop_,&QPushButton::clicked,this,&PerformancePage::stopPerformance);
    connect(tempoMode_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{retime();});
    connect(bpm_,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{retime();});
    connect(hold_,qOverload<int>(&QSpinBox::valueChanged),this,[this]{retime();});
    connect(gap_,qOverload<int>(&QSpinBox::valueChanged),this,[this]{retime();});
    timer_=new QTimer(this);timer_->setInterval(16);connect(timer_,&QTimer::timeout,this,&PerformancePage::updatePerformance);
}
void PerformancePage::setSong(std::shared_ptr<const Song> song,std::shared_ptr<const Conversion> result,const QString& path,const Settings& settings) {
    stopPerformance();updating_=true;sourceSettings_=settings;sourceResult_=result;
    const bool changed=song_!=song;song_=std::move(song);result_=std::move(result);
    title_->setText(QFileInfo(path).completeBaseName());title_->setToolTip(path);
    const double firstBpm=song_->tempos.empty()?120.0:60000000.0/song_->tempos.front().micros;
    const double actualBpm=(settings.fixedTempo?settings.bpm:firstBpm)*settings.speed;
    metadata_->setText(QString("%1 个音轨  ·  %2 个可演奏音符  ·  时长 %3  ·  %4 %5 BPM  ·  同键冲突 %6")
        .arg(song_->tracks.size()).arg(result_->exact+result_->approximate+result_->edited).arg(timeText(result_->duration))
        .arg(!settings.fixedTempo&&song_->tempos.size()>1?"起始":"速度").arg(actualBpm,0,'f',1).arg(result_->conflicts));
    preview_->setMusic(song_,result_);empty_->setVisible(result_->exact+result_->approximate+result_->edited==0);if(changed)position_=0;
    if(changed){tempoMode_->setCurrentIndex(settings.fixedTempo?1:0);bpm_->setValue(settings.fixedTempo?settings.bpm:firstBpm);hold_->setValue(settings.holdMs);gap_->setValue(settings.gapMs);fitPending_=true;}
    if(changed)playSettings_=settings;updating_=false;retime();updateControls();
    if(isVisible()&&fitPending_){preview_->fitAll();fitPending_=false;}
}
void PerformancePage::setPreviewPosition(double seconds) {
    position_=std::clamp(std::isfinite(seconds)?seconds:0.0,0.0,result_?result_->duration:0.0);
    preview_->setPlayhead(position_,true);
    clock_->setText(previewTimeText(position_)+" / "+previewTimeText(result_?result_->duration:0));
    emit sourcePositionChanged(song_?mapPosition(*song_,playSettings_,sourceSettings_,position_):position_);
}
void PerformancePage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);if(fitPending_){preview_->fitAll();fitPending_=false;}preview_->setPlayhead(position_,true);
}
void PerformancePage::refreshDevices(bool keyboard) {
    if(running_)return;
    auto& watcher=keyboard?keyboardWatcher_:windowWatcher_;if(watcher.isRunning())return;
    auto* combo=keyboard?keyboards_:windows_;auto* refresh=keyboard?refreshKeyboards_:refreshWindows_;
    auto* status=keyboard?keyboardStatus_:windowStatus_;
    {const QSignalBlocker blocker(combo);combo->clear();combo->setCurrentIndex(-1);}
    combo->setToolTip({});combo->setEnabled(false);refresh->setEnabled(false);
    status->setStyleSheet("color:#8397a3;");status->setText(keyboard?"正在查找键盘…":"正在查找窗口…");
    const auto query=keyboard?discovery_.keyboards:discovery_.windows;
    // The worker owns its callable and never touches widgets, including after this page is closed.
    watcher.setFuture(QtConcurrent::run([query]{
        try{return query();}catch(...){return DiscoveryResult{{},"查找失败，请重试。"};}
    }));
    updateControls();
}
void PerformancePage::discoveryFinished(bool keyboard) {
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
        combo->clear();combo->setEnabled(false);status->setText(result.error);status->setStyleSheet("color:#bd4f5b;");
    } else if(combo->count()==0) {
        status->setText(keyboard?"未找到键盘，请检查连接后重试。":"未找到窗口，请打开目标程序后重试。");status->setStyleSheet("color:#a77722;");
    } else {
        combo->setEnabled(true);status->setText(QString("找到 %1 个%2，请手动选择。").arg(combo->count()).arg(keyboard?"键盘":"窗口"));status->setStyleSheet("color:#178e80;");
    }
    updateControls();
}
void PerformancePage::showKeyTestWindow() {
    if(!keyTestWindow_) {
        keyTestWindow_=new HandpanTestDialog(this,audioBackend_);keyTestWindow_->setAttribute(Qt::WA_DeleteOnClose);
    }
    keyTestWindow_->show();keyTestWindow_->raise();keyTestWindow_->activateWindow();
}
PerformancePage::~PerformancePage(){controller_.stop();}
void PerformancePage::hideEvent(QHideEvent* event){if(!event->spontaneous())stopPerformance();QWidget::hideEvent(event);}
void PerformancePage::stopPerformance(){controller_.stop();if(timer_)updatePerformance();}
void PerformancePage::updateControls(){
    const bool available=song_&&result_&&keyboards_->currentIndex()>=0&&windows_->currentIndex()>=0;
    start_->setEnabled(running_||available);stop_->setEnabled(running_);
    refreshKeyboards_->setEnabled(!running_&&!keyboardWatcher_.isRunning());refreshWindows_->setEnabled(!running_&&!windowWatcher_.isRunning());
    keyboards_->setEnabled(!running_&&keyboards_->count()>0);windows_->setEnabled(!running_&&windows_->count()>0);
    tempoMode_->setEnabled(!running_);bpm_->setEnabled(!running_&&tempoMode_->currentIndex()==1);hold_->setEnabled(!running_);gap_->setEnabled(!running_);activate_->setEnabled(!running_);testKeys_->setEnabled(!running_);
}
void PerformancePage::updatePerformance(){
    const auto s=controller_.snapshot();const bool wasRunning=running_;running_=s.active();
    if(wasRunning||running_)setPreviewPosition(s.position);
    if(!s.message.isEmpty())state_->setText(s.state==PerformanceState::Countdown?QString("倒计时 %1 秒 · %2").arg(static_cast<int>(std::ceil(s.countdown))).arg(s.message):s.message);
    state_->setStyleSheet(s.state==PerformanceState::Failed?"color:#bd4f5b;":"color:#178e80;");
    start_->setText(s.state==PerformanceState::Paused?"继续演奏":running_?"暂停演奏":"开始演奏");
    updateControls();if(!running_)timer_->stop();
}
void PerformancePage::startPerformance(){
    if(running_){controller_.togglePause();updatePerformance();return;}
    if(!song_||!result_||keyboards_->currentIndex()<0||windows_->currentIndex()<0)return;
    QString error;
    try{auto plan=makePerformancePlan(*result_,position_,hold_->value(),gap_->value());
        OutputTarget target{keyboards_->currentData().toString(),windows_->currentData().toULongLong(),windows_->currentData(Qt::UserRole+1).toUInt()};
        if(controller_.start(std::move(plan),target,activate_->isChecked(),error)){timer_->start();updatePerformance();return;}
    }catch(const std::exception& e){error=QString::fromUtf8(e.what());}
    state_->setText(error);state_->setStyleSheet("color:#bd4f5b;");
}
void PerformancePage::retime(){
    if(updating_||running_||!song_||!sourceResult_)return;
    const auto previousSettings=playSettings_;
    playSettings_=sourceSettings_;playSettings_.fixedTempo=tempoMode_->currentIndex()==1;playSettings_.bpm=bpm_->value();playSettings_.holdMs=hold_->value();playSettings_.gapMs=gap_->value();
    result_=std::make_shared<Conversion>(retimePerformance(*song_,*sourceResult_,playSettings_));preview_->setMusic(song_,result_);
    setPreviewPosition(mapPosition(*song_,previousSettings,playSettings_,position_));
    const double firstBpm=song_->tempos.empty()?120.0:60000000.0/song_->tempos.front().micros;
    metadata_->setText(QString("%1 个音轨  ·  %2 个可演奏音符  ·  时长 %3  ·  %4 %5 BPM  ·  同键冲突 %6")
        .arg(song_->tracks.size()).arg(result_->exact+result_->approximate+result_->edited).arg(timeText(result_->duration))
        .arg(!playSettings_.fixedTempo&&song_->tempos.size()>1?"起始":"速度").arg((playSettings_.fixedTempo?playSettings_.bpm:firstBpm)*playSettings_.speed,0,'f',1).arg(result_->conflicts));
}
void PerformancePage::inheritPreviewPosition(double seconds){
    if(!song_){setPreviewPosition(seconds);return;}
    setPreviewPosition(mapPosition(*song_,sourceSettings_,playSettings_,seconds));
}
}
