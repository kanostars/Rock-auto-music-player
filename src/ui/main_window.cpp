#include "main_window.h"
#include "piano_roll.h"
#include "range_spinbox.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSlider>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <limits>

namespace rock {
namespace {
QLabel* label(const QString& text,const char* role,QWidget* parent=nullptr) {
    auto* w=new QLabel(text,parent); w->setProperty("role",role);w->setTextFormat(Qt::PlainText);return w;
}
QPushButton* button(const QString& text,const char* name=nullptr) {
    auto* b=new QPushButton(text);b->setCursor(Qt::PointingHandCursor);if(name)b->setObjectName(name);return b;
}
QString formatTime(double seconds) {
    int ms=static_cast<int>(std::min(seconds*1000,2e9));
    return QString("%1:%2.%3").arg(ms/60000,2,10,QChar('0')).arg(ms/1000%60,2,10,QChar('0')).arg(ms%1000/10,2,10,QChar('0'));
}
QString noteName(int p) {return QString::fromStdString(pitchName(p));}
}
MainWindow::MainWindow(QWidget* parent,AudioBackend audioBackend):QMainWindow(parent),audio_(audioBackend) {
    setWindowTitle("RockAutoMusicPlay · 九键音乐工作台");resize(1460,900);setMinimumSize(1120,740);setAcceptDrops(true);
    buildUi();
    connect(&importWatcher_,&QFutureWatcher<std::vector<Loaded>>::finished,this,[this] {
        auto loaded=importWatcher_.result();
        busy_=false;import_->setEnabled(true);workspace_->setEnabled(true);progress_->hide();cancelButton_->hide();
        if(cancel_->load()) {status_->setText("导入已取消，已有曲目保留。");return;}
        QStringList errors;int last=-1;
        for(auto& item:loaded) {
            if(!item.error.isEmpty()) {errors<<QFileInfo(item.session.path).fileName()+"："+item.error;continue;}
            sessions_.push_back(std::move(item.session));
            const auto& session=sessions_.back();
            auto* row=new QListWidgetItem(QFileInfo(session.path).completeBaseName()+"\n"+QString("%1 音符 · %2 个音轨").arg(session.song->notes.size()).arg(session.song->tracks.size()));
            row->setToolTip(session.path);row->setSizeHint(QSize(180,66));library_->addItem(row);last=library_->count()-1;
        }
        if(last>=0) library_->setCurrentRow(last);
        if(!errors.isEmpty()) {status_->setText("导入失败："+errors.join("；"));status_->setToolTip(errors.join('\n'));}
    });
    timer_.setInterval(16);
    connect(&timer_,&QTimer::timeout,this,[this]{
        const auto* r=currentResult();if(!r){pausePreview();return;}
        position_=std::min(r->duration,audio_.position());
        roll_->setPlayhead(position_,true);refreshClock();
        if(audio_.finished())pausePreview();
        else if(!audio_.running()){pausePreview();status_->setText("音频输出已中断，请检查系统默认输出设备后重新试听。");}
    });
}
MainWindow::~MainWindow() {audio_.pause();if(cancel_)cancel_->store(true);importWatcher_.waitForFinished();}
const Conversion* MainWindow::currentResult() const {return current_>=0?sessions_[current_].result.get():nullptr;}

void MainWindow::buildUi() {
    setStyleSheet(R"(
        QMainWindow, QWidget#root { background: #eef3f5; color: #203d4e; }
        QWidget { font-family: 'Microsoft YaHei UI'; font-size: 12px; color: #203d4e; }
        QFrame#panel, QFrame#header, QFrame#transport { background: white; border: 1px solid #e0e9ed; border-radius: 10px; }
        QLabel { background: transparent; border: none; }
        QLabel[role=brand] {font-size: 22px; font-weight: 700; color: #183d4d;}
        QLabel[role=title] {font-size: 18px; font-weight: 700;}
        QLabel[role=section] {font-size: 13px; font-weight: 700;}
        QLabel[role=muted] {color: #8397a3; font-size: 11px;}
        QPushButton {background: #f7fafb; border: 1px solid #dce6eb; border-radius: 6px; padding: 9px 13px; font-weight: 600;}
        QPushButton:hover {background: #e8f4f1; border-color: #8ac8bb;}
        QPushButton:pressed {background: #d4e9e4;}
        QPushButton#deleteMode:checked {background:#fbebed; border-color:#cf7580; color:#b64957;}
        QPushButton#addNoteButton:checked {background:#d8f2ea; border-color:#178e80; color:#10695f;}
        QPushButton:disabled {color: #afbdc5; background: #f5f7f8; border-color: #e6edf0;}
        QPushButton#primary, QPushButton#importButton, QPushButton#playButton {background: #178e80; border-color: #178e80; color: white;}
        QPushButton#primary:hover, QPushButton#importButton:hover, QPushButton#playButton:hover {background: #107566;}
        QPushButton#playButton:disabled {background: #aecfc9; border-color: #aecfc9;}
        QListWidget, QTreeWidget {border: none; background: white; outline: none;}
        QListWidget::item {border-radius: 6px; padding: 6px; margin: 2px;}
        QListWidget::item:selected {background: #e5f3ef; color: #0e796c;}
        QTreeWidget::item {padding: 9px 2px;}
        QTreeWidget::item:selected {background: #e9f3f7; color: #204557;}
        QHeaderView::section {background: #f6f9fa; color: #8397a3; border: none; padding: 7px 3px; font-size: 10px;}
        QComboBox, QDoubleSpinBox, QSpinBox {background: white; border: 1px solid #dce5eb; border-radius: 6px; padding: 8px; min-height: 20px;}
        QComboBox::drop-down {border: none; width: 20px;}
        QTabWidget::pane {border: none; background: white;}
        QTabBar::tab {padding: 12px 20px; color: #8699a3; border-bottom: 2px solid transparent;}
        QTabBar::tab:selected {color: #138777; border-bottom: 2px solid #138777;}
        QScrollArea {border: none; background: white;}
        QScrollBar:horizontal {height: 11px; background: #f0f4f6;}
        QScrollBar::handle:horizontal {background: #c4d3da; border-radius: 5px; min-width: 30px;}
        QScrollBar:vertical {width: 9px; background: #f0f4f6;}
        QScrollBar::handle:vertical {background: #c4d3da; border-radius: 4px; min-height: 30px;}
        QScrollBar::add-line, QScrollBar::sub-line {width: 0; height: 0;}
        QSplitter::handle {background: transparent;}
        QTableWidget {background: white; border: 1px solid #e0e9ed; gridline-color: #eef3f5;}
        QToolTip {background: #203d4e; color: white; padding: 6px; border: none;}
    )");
    auto* root=new QWidget;root->setObjectName("root");setCentralWidget(root);
    auto* outer=new QVBoxLayout(root);outer->setContentsMargins(22,20,22,14);outer->setSpacing(14);
    auto* header=new QFrame;header->setObjectName("header");auto* top=new QHBoxLayout(header);top->setContentsMargins(22,17,22,17);
    auto* brandBox=new QVBoxLayout;brandBox->setSpacing(5);brandBox->addWidget(label("ROCK  /  九键音乐工作台","brand"));
    brandBox->addWidget(label("MIDI → NINE KEYS     ·     让每个音符找到它的位置","muted"));top->addLayout(brandBox);top->addStretch();
    top->addWidget(label("本地转换  ·  原曲保留","muted"));top->addSpacing(16);
    import_=button("＋  导入 MIDI","importButton");import_->setMinimumWidth(154);top->addWidget(import_);outer->addWidget(header);
    connect(import_,&QPushButton::clicked,this,[this]{importFiles(QFileDialog::getOpenFileNames(this,"选择 MIDI 文件",{},"MIDI 文件 (*.mid *.midi)"));});

    workspace_=new QWidget;auto* work=new QHBoxLayout(workspace_);work->setContentsMargins(0,0,0,0);work->setSpacing(0);
    auto* split=new QSplitter;split->setChildrenCollapsible(false);work->addWidget(split);outer->addWidget(workspace_,1);
    auto* left=new QFrame;left->setObjectName("panel");left->setMinimumWidth(220);left->setMaximumWidth(360);
    auto* ll=new QVBoxLayout(left);ll->setContentsMargins(14,16,14,14);ll->setSpacing(12);
    ll->addWidget(label("曲目库","section"));ll->addWidget(label("导入的文件只在本地处理","muted"));
    library_=new QListWidget;library_->setObjectName("songList");library_->setMinimumHeight(120);library_->setMaximumHeight(240);ll->addWidget(library_,1);
    connect(library_,&QListWidget::currentRowChanged,this,&MainWindow::selectSong);
    auto* line=new QFrame;line->setFrameShape(QFrame::HLine);line->setStyleSheet("color:#e7eef1;");ll->addWidget(line);
    ll->addWidget(label("当前曲目音轨","section"));
    ll->addWidget(label("勾选参与转换 · 独奏只保留该轨","muted"));
    tracks_=new QTreeWidget;tracks_->setObjectName("trackTree");tracks_->setColumnCount(3);tracks_->setHeaderLabels({"音轨 / 通道","独奏","音符"});
    tracks_->setRootIsDecorated(false);tracks_->setIndentation(0);tracks_->header()->setMinimumSectionSize(32);tracks_->header()->setStretchLastSection(false);tracks_->setColumnWidth(0,118);tracks_->setColumnWidth(1,38);
    tracks_->header()->setSectionResizeMode(0,QHeaderView::Stretch);tracks_->header()->setSectionResizeMode(1,QHeaderView::Fixed);tracks_->header()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    ll->addWidget(tracks_,2);
    auto* trackButtons=new QHBoxLayout;auto* all=button("全选","allTracks");auto* none=button("全不选","noTracks");trackButtons->addWidget(all);trackButtons->addWidget(none);ll->addLayout(trackButtons);
    connect(all,&QPushButton::clicked,this,[this]{setAllTracks(true);});connect(none,&QPushButton::clicked,this,[this]{setAllTracks(false);});
    connect(tracks_,&QTreeWidget::itemChanged,this,[this](QTreeWidgetItem* item,int column){
        if(updating_||current_<0||column>1)return;
        auto& s=sessions_[current_].settings;int i=tracks_->indexOfTopLevelItem(item);
        if(column==0)s.enabled[i]=item->checkState(0)==Qt::Checked;else s.solo[i]=item->checkState(1)==Qt::Checked;
        recalculate();
    });
    split->addWidget(left);

    auto* center=new QFrame;center->setObjectName("panel");center->setMinimumWidth(490);
    auto* cl=new QVBoxLayout(center);cl->setContentsMargins(18,18,18,16);cl->setSpacing(14);
    songTitle_=label("等待第一首旋律","title");songTitle_->setObjectName("songTitle");cl->addWidget(songTitle_);
    subtitle_=label("导入 MIDI 后，自动生成九键音符轨道","muted");
    subtitle_->setTextFormat(Qt::RichText);
    subtitle_->setWordWrap(true);subtitle_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    cl->addWidget(subtitle_);
    auto* rollBar=new QHBoxLayout;rollBar->addWidget(label("九键轨道","section"));rollBar->addStretch();
    filter_=new QComboBox;filter_->setObjectName("trackFilter");filter_->setMinimumWidth(140);filter_->setMaximumWidth(220);filter_->addItem("全部音轨",-1);rollBar->addWidget(filter_);
    auto* fit=button("查看全部","fitButton");rollBar->addWidget(fit);cl->addLayout(rollBar);
    auto* editBar=new QHBoxLayout;editBar->setSpacing(6);
    addNote_=button("添加音符","addNoteButton");addNote_->setCheckable(true);addNote_->setEnabled(false);
    addNote_->setToolTip("开启后点击九键行的空白位置添加一拍音符（力度 100）；可连续添加，Esc 退出。\n添加到显示筛选指定的音轨；显示全部时优先使用左侧选中的可播放音轨。");
    deleteNote_=button("删除","deleteNoteButton");deleteNote_->setToolTip("删除选中音符（Delete），可撤销");deleteNote_->setEnabled(false);
    deleteMode_=button("点选删除","deleteMode");deleteMode_->setCheckable(true);deleteMode_->setEnabled(false);deleteMode_->setToolTip("开启后直接点击音符删除；关闭后可拖动音符及两端");
    undo_=button("撤销","undoButton");undo_->setToolTip("撤销音符编辑（Ctrl+Z）");undo_->setEnabled(false);
    redo_=button("重做","redoButton");redo_->setToolTip("重做音符编辑（Ctrl+Y / Ctrl+Shift+Z）");redo_->setEnabled(false);
    for(auto* b:{addNote_,deleteNote_,deleteMode_,undo_,redo_}){b->setStyleSheet("padding:6px 9px;");editBar->addWidget(b);}
    editBar->addStretch();auto* editHint=label("框选 / 拖动","muted");editHint->setToolTip("空白处拖动框选；Ctrl/Shift 追加选择；Ctrl+A 全选当前显示音符\n拖动选中音符可整体移动，拖动任一两端可批量调整时长\n↑/↓ 整体升降一个九键音级，B～U 为上下边界；Delete 批量删除；Esc 取消\n添加模式下点击空白创建音符，再点按钮或 Esc 退出");editBar->addWidget(editHint);cl->addLayout(editBar);
    roll_=new PianoRoll;cl->addWidget(roll_,1);
    auto* legend=new QHBoxLayout;auto* legendText=label("● 原样保留    ● 近似转换    ● 同键冲突","muted");legendText->setTextFormat(Qt::RichText);
    legendText->setText("<span style='color:#189e91'>●</span> 准确　<span style='color:#d5a14a'>●</span> 近似　<span style='color:#6582bd'>●</span> 已编辑　<span style='color:#d4656d'>●</span> 冲突");legend->addWidget(legendText);legend->addStretch();
    auto* minus=button("−");auto* plus=button("＋");minus->setFixedWidth(34);plus->setFixedWidth(34);zoomText_=label("80 px/s","muted");legend->addWidget(minus);legend->addWidget(zoomText_);legend->addWidget(plus);cl->addLayout(legend);
    connect(minus,&QPushButton::clicked,this,[this]{roll_->setZoom(roll_->zoom()/1.4);});connect(plus,&QPushButton::clicked,this,[this]{roll_->setZoom(roll_->zoom()*1.4);});
    zoomText_->setMinimumWidth(64);
    connect(roll_,&PianoRoll::zoomChanged,this,[this](double p){zoomText_->setText(QString::number(p,'f',p<1?2:0)+" px/s");});
    connect(filter_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{roll_->setTrackFilter(filter_->currentData().toInt());});
    connect(fit,&QPushButton::clicked,roll_,&PianoRoll::fitAll);connect(roll_,&PianoRoll::noteSelected,this,&MainWindow::showNote);
    connect(roll_,&PianoRoll::seekRequested,this,[this](double t){pausePreview();position_=t;roll_->setPlayhead(t);refreshClock();});
    connect(roll_,&PianoRoll::editStarted,this,[this]{pausePreview();});
    connect(roll_,&PianoRoll::notesEdited,this,&MainWindow::editNotes);
    connect(roll_,&PianoRoll::deleteRequested,this,&MainWindow::deleteNotes);
    connect(roll_,&PianoRoll::addRequested,this,&MainWindow::addNote);
    connect(addNote_,&QPushButton::toggled,this,[this](bool enabled){
        if(enabled)deleteMode_->setChecked(false);
        roll_->setAddMode(enabled);
        if(enabled){roll_->setFocus();status_->setText("添加模式 · 点击九键行空白处创建一拍音符 · 再点按钮或 Esc 退出");}
    });
    connect(roll_,&PianoRoll::addModeChanged,addNote_,&QPushButton::setChecked);
    connect(deleteMode_,&QPushButton::toggled,this,[this](bool enabled){if(enabled)addNote_->setChecked(false);});
    connect(deleteNote_,&QPushButton::clicked,this,&MainWindow::deleteNotes);
    connect(deleteMode_,&QPushButton::toggled,roll_,&PianoRoll::setDeleteMode);
    connect(undo_,&QPushButton::clicked,this,[this]{stepHistory(false);});
    connect(redo_,&QPushButton::clicked,this,[this]{stepHistory(true);});
    auto* undoShortcut=new QShortcut(QKeySequence::Undo,roll_);
    undoShortcut->setContext(Qt::WidgetWithChildrenShortcut);connect(undoShortcut,&QShortcut::activated,this,[this]{stepHistory(false);});
    for(const auto& key:{QKeySequence(Qt::CTRL|Qt::Key_Y),QKeySequence(Qt::CTRL|Qt::SHIFT|Qt::Key_Z)}) {
        auto* shortcut=new QShortcut(key,roll_);shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut,&QShortcut::activated,this,[this]{stepHistory(true);});
    }
    auto* playbackShortcut=new QShortcut(QKeySequence(Qt::Key_Space),roll_);
    playbackShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(playbackShortcut,&QShortcut::activated,this,&MainWindow::togglePlayback);
    split->addWidget(center);

    auto* right=new QFrame;right->setObjectName("panel");right->setMinimumWidth(270);right->setMaximumWidth(340);
    auto* rl=new QVBoxLayout(right);rl->setContentsMargins(6,4,6,10);tabs_=new QTabWidget;rl->addWidget(tabs_);
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);auto* settings=new QWidget;settings->setObjectName("settingsPage");settings->setStyleSheet("QWidget#settingsPage {background:white;}");auto* sl=new QVBoxLayout(settings);sl->setContentsMargins(14,18,14,14);sl->setSpacing(13);
    sl->addWidget(label("音域适配","section"));octaveMode_=new QComboBox;octaveMode_->setObjectName("octaveMode");octaveMode_->addItems({"自动八度适配（推荐）","保持原始音高"});sl->addWidget(octaveMode_);
    octaveInfo_=label("导入后显示整体八度调整","muted");octaveInfo_->setObjectName("octaveInfo");octaveInfo_->setWordWrap(true);sl->addWidget(octaveInfo_);
    sl->addWidget(label("适配后仍不支持的音符","section"));strategy_=new QComboBox;strategy_->setObjectName("strategyCombo");strategy_->addItems({"转换成最近音","直接跳过"});sl->addWidget(strategy_);
    auto* help=label("先整体调整八度，保留音程；再对不支持的音取最近音（等距取低）或跳过。宽音域仍可能需要简化，手工修改保留。","muted");help->setWordWrap(true);sl->addWidget(help);
    sl->addSpacing(8);sl->addWidget(label("节奏与速度","section"));tempoMode_=new QComboBox;tempoMode_->setObjectName("tempoMode");tempoMode_->addItems({"跟随 MIDI 原曲速度","使用固定 BPM"});sl->addWidget(tempoMode_);
    auto* form=new QFormLayout;form->setSpacing(10);bpm_=new RangeDoubleSpinBox("固定 BPM");bpm_->setObjectName("bpmSpin");bpm_->setRange(20,400);bpm_->setDecimals(1);bpm_->setValue(120);bpm_->setEnabled(false);
    speed_=new RangeDoubleSpinBox("播放倍率");speed_->setObjectName("speedSpin");speed_->setRange(.25,3);speed_->setSingleStep(.05);speed_->setDecimals(2);speed_->setValue(1);speed_->setSuffix(" ×");
    form->addRow("固定 BPM",bpm_);form->addRow("播放倍率",speed_);sl->addLayout(form);
    sl->addSpacing(8);sl->addWidget(label("同键触发检查","section"));auto* timing=new QFormLayout;timing->setSpacing(10);
    hold_=new RangeIntSpinBox("按下时长");hold_->setRange(1,1000);hold_->setValue(30);hold_->setSuffix(" ms");hold_->setObjectName("holdSpin");
    gap_=new RangeIntSpinBox("松开后间隔");gap_->setRange(0,1000);gap_->setValue(20);gap_->setSuffix(" ms");gap_->setObjectName("gapSpin");timing->addRow("按下时长",hold_);timing->addRow("松开后间隔",gap_);sl->addLayout(timing);
    auto invalid=[this](const QString& message){pausePreview();showWarning("参数超出有效范围",message);};
    for(auto* spin:{bpm_,speed_}){static_cast<RangeDoubleSpinBox*>(spin)->rejected=invalid;spin->setToolTip(QString("有效范围：%1～%2%3").arg(spin->minimum()).arg(spin->maximum()).arg(spin->suffix()));}
    for(auto* spin:{hold_,gap_}){static_cast<RangeIntSpinBox*>(spin)->rejected=invalid;spin->setToolTip(QString("有效范围：%1～%2 ms").arg(spin->minimum()).arg(spin->maximum()));}
    auto* hint=label("不同键可同时按下，和弦不计入过密冲突。","muted");hint->setWordWrap(true);sl->addWidget(hint);
    dirty_=label("设置已应用","muted");dirty_->setObjectName("settingsState");sl->addWidget(dirty_);
    apply_=button("应用设置 · 重新转换","primary");apply_->setEnabled(false);sl->addWidget(apply_);sl->addStretch();scroll->setWidget(settings);tabs_->addTab(scroll,"转换设置");
    auto* detailPage=new QWidget;auto* dl=new QVBoxLayout(detailPage);dl->setContentsMargins(16,24,16,20);
    dl->addWidget(label("音符详情","section"));details_=label("点击轨道上的音符，查看原始音高、转换结果与时间。","muted");details_->setObjectName("noteDetails");details_->setWordWrap(true);details_->setTextInteractionFlags(Qt::TextSelectableByMouse);details_->setAlignment(Qt::AlignTop);dl->addWidget(details_,1);tabs_->addTab(detailPage,"音符详情");
    connect(apply_,&QPushButton::clicked,this,&MainWindow::applySettings);
    connect(strategy_,qOverload<int>(&QComboBox::currentIndexChanged),this,&MainWindow::markDirty);
    connect(octaveMode_,qOverload<int>(&QComboBox::currentIndexChanged),this,&MainWindow::markDirty);
    connect(tempoMode_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int i){bpm_->setEnabled(i==1);markDirty();});
    for(auto* spin:{bpm_,speed_})connect(spin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,&MainWindow::markDirty);
    for(auto* spin:{hold_,gap_})connect(spin,qOverload<int>(&QSpinBox::valueChanged),this,&MainWindow::markDirty);
    split->addWidget(right);split->setSizes({240,850,290});split->setStretchFactor(1,1);

    auto* transport=new QFrame;transport->setObjectName("transport");auto* tl=new QHBoxLayout(transport);tl->setContentsMargins(16,12,16,12);
    play_=button("手盘试听","playButton");play_->setEnabled(false);stop_=button("停止","stopButton");stop_->setEnabled(false);tl->addWidget(play_);tl->addWidget(stop_);
    clock_=label("00:00.00 / 00:00.00","section");clock_->setObjectName("previewClock");tl->addSpacing(16);tl->addWidget(clock_);tl->addStretch();summary_=label("等待导入","muted");tl->addWidget(summary_);
    tl->addWidget(label("音量","muted"));auto* volume=new QSlider(Qt::Horizontal);volume->setObjectName("volumeSlider");volume->setRange(0,100);volume->setValue(60);volume->setFixedWidth(70);volume->setToolTip("试听音量 60%（0 为静音）");tl->addWidget(volume);
    connect(volume,&QSlider::valueChanged,this,[this,volume](int value){audio_.setVolume(value/100.f);volume->setToolTip(QString("试听音量 %1%（0 为静音）").arg(value));});
    auto* diagnostics=button("转换报告","reportButton");tl->addWidget(diagnostics);outer->addWidget(transport);
    connect(diagnostics,&QPushButton::clicked,this,&MainWindow::showDiagnostics);
    connect(play_,&QPushButton::clicked,this,&MainWindow::togglePlayback);
    connect(stop_,&QPushButton::clicked,this,[this]{pausePreview(true);});
    auto* statusLine=new QHBoxLayout;status_=label("就绪 · 支持 SMF 0 / 1 · 本地导入，无需驱动","muted");status_->setObjectName("statusText");status_->setWordWrap(true);statusLine->addWidget(status_,1);
    progress_=new QProgressBar;progress_->setRange(0,0);progress_->setMaximumWidth(120);progress_->setMaximumHeight(5);progress_->hide();statusLine->addWidget(progress_);
    cancelButton_=button("取消导入");cancelButton_->hide();statusLine->addWidget(cancelButton_);connect(cancelButton_,&QPushButton::clicked,this,[this]{if(cancel_)cancel_->store(true);});
    statusLine->addWidget(label("手盘采样试听 · 暂未连接键盘输出","muted"));outer->addLayout(statusLine);
}

void MainWindow::importFiles(const QStringList& paths) {
    if(paths.isEmpty()||busy_)return;
    pausePreview();busy_=true;import_->setEnabled(false);workspace_->setEnabled(false);progress_->show();cancelButton_->show();
    status_->setText("正在读取 MIDI 并转换九键音符…");cancel_=std::make_shared<std::atomic_bool>(false);
    importWatcher_.setFuture(QtConcurrent::run([paths,cancel=cancel_] {
        std::vector<Loaded> loaded;
        for(const auto& path:paths) {
            if(cancel->load())break;
            Loaded item;item.session.path=path;
            try {
                QFile file(path);
                if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("无法打开文件。");
                if(file.size()>32*1024*1024)throw std::runtime_error("文件超过 32 MB 限制。");
                auto bytes=file.readAll();
                if(file.error()!=QFile::NoError)throw std::runtime_error("文件读取失败。");
                item.session.song=std::make_shared<Song>(parseMidi(bytes.toStdString(),cancel.get()));
                item.session.settings=defaultSettings(*item.session.song);
                item.session.result=std::make_shared<Conversion>(convert(*item.session.song,item.session.settings));
            } catch(const std::exception& e) {item.error=QString::fromUtf8(e.what());}
            loaded.push_back(std::move(item));
        }
        return loaded;
    }));
}
void MainWindow::selectSong(int index) {
    if(index<0||index>=static_cast<int>(sessions_.size()))return;
    pausePreview(true);current_=index;updating_=true;deleteMode_->setChecked(false);addNote_->setChecked(false);
    const auto& s=sessions_[index];const auto& settings=s.settings;
    songTitle_->setText(QFileInfo(s.path).completeBaseName());
    tracks_->clear();filter_->clear();filter_->addItem("全部音轨",-1);
    for(size_t i=0;i<s.song->tracks.size();++i) {
        const auto& tr=s.song->tracks[i];QString name=QString::fromUtf8(tr.name)+QString(" · CH %1").arg(tr.channel+1);
        auto* row=new QTreeWidgetItem(tracks_,{QString::fromUtf8(tr.name)+QString("\n轨 %1 · CH %2").arg(tr.source+1).arg(tr.channel+1),"",QString::number(tr.count)});
        row->setFlags(row->flags()|Qt::ItemIsUserCheckable);row->setCheckState(0,settings.enabled[i]?Qt::Checked:Qt::Unchecked);row->setCheckState(1,settings.solo[i]?Qt::Checked:Qt::Unchecked);
        row->setToolTip(0,QString("原始轨道 %1 · 通道 %2\n%3").arg(tr.source+1).arg(tr.channel+1).arg(tr.channel==9?"可能为打击乐，默认排除；可手动勾选。":"勾选参与转换，取消勾选即静音。"));
        filter_->addItem(name,static_cast<int>(i));
    }
    octaveMode_->setCurrentIndex(settings.autoOctave?0:1);
    strategy_->setCurrentIndex(settings.nearest?0:1);tempoMode_->setCurrentIndex(settings.fixedTempo?1:0);bpm_->setValue(settings.bpm);speed_->setValue(settings.speed);hold_->setValue(settings.holdMs);gap_->setValue(settings.gapMs);
    updating_=false;settingsPending_=false;dirty_->setText("设置已应用");apply_->setEnabled(true);
    roll_->setTrackFilter(-1);refreshResult(true);
}
void MainWindow::markDirty() {if(!updating_&&current_>=0){settingsPending_=true;dirty_->setText("有未应用设置 · 点击下方应用");}}
void MainWindow::showWarning(const QString& title,const QString& message) {
    status_->setText(message.section('\n',0,0));
    if(auto* existing=findChild<QMessageBox*>("validationWarning");existing&&existing->isVisible()){existing->raise();return;}
    auto* box=new QMessageBox(QMessageBox::Warning,title,message,QMessageBox::Ok,this);
    box->setObjectName("validationWarning");box->setAttribute(Qt::WA_DeleteOnClose);box->setTextFormat(Qt::PlainText);
    box->setStyleSheet("QMessageBox {background:#eef3f5;} QLabel {color:#203d4e;}");
    box->button(QMessageBox::Ok)->setText("知道了");box->setWindowModality(Qt::WindowModal);box->open();
}
bool MainWindow::validateParameters() {
    for(auto* spin:{bpm_,speed_})if(!static_cast<RangeDoubleSpinBox*>(spin)->commitInput())return false;
    for(auto* spin:{hold_,gap_})if(!static_cast<RangeIntSpinBox*>(spin)->commitInput())return false;
    if(auto* warning=findChild<QMessageBox*>("validationWarning");warning&&warning->isVisible())return false;
    return true;
}
void MainWindow::applySettings() {
    if(current_<0||!validateParameters())return;auto& s=sessions_[current_].settings;
    s.autoOctave=octaveMode_->currentIndex()==0;
    s.nearest=strategy_->currentIndex()==0;s.fixedTempo=tempoMode_->currentIndex()==1;s.bpm=bpm_->value();s.speed=speed_->value();s.holdMs=hold_->value();s.gapMs=gap_->value();
    settingsPending_=false;dirty_->setText("设置已应用");recalculate();
}
void MainWindow::recalculate(bool fit) {
    if(current_<0)return;pausePreview(true);auto& s=sessions_[current_];
    s.result=std::make_shared<Conversion>(convert(*s.song,s.settings,s.edits));refreshResult(fit);
}
void MainWindow::refreshResult(bool fit) {
    const auto& s=sessions_[current_];const auto& r=*s.result;
    std::vector<int> added(s.song->tracks.size());int originalCount=0,addedCount=0;
    for(size_t i=0;i<s.song->notes.size();++i) {
        const auto& n=s.song->notes[i];
        if(!n.added){++originalCount;continue;}
        if(auto it=s.edits.find(static_cast<int>(i));it!=s.edits.end()&&!it->second.deleted){++added[n.track];++addedCount;}
    }
    subtitle_->setText(QString("SMF %1 / %2 PPQ · %3 个音轨 · <span style='color:#516c7c'>%4 个原始音符</span>"
        " · <span style='color:#526fa8'>新增 %5</span>"
        " · <span style='color:#178e80'>准确映射 %6</span>"
        " · <span style='color:#a77722'>近似转换 %7</span>"
        " · <span style='color:#748593'>已跳过 %8</span>"
        " · <span style='color:#bd4f5b'>同键冲突 %9</span>")
        .arg(s.song->format).arg(s.song->ppq).arg(s.song->tracks.size()).arg(originalCount).arg(addedCount)
        .arg(r.exact).arg(r.approximate).arg(r.skipped).arg(r.conflicts));
    const QSignalBlocker trackSignals(tracks_);
    for(int i=0;i<tracks_->topLevelItemCount();++i) {
        tracks_->topLevelItem(i)->setText(2,added[i]?QString("%1 + %2").arg(s.song->tracks[i].count).arg(added[i]):QString::number(s.song->tracks[i].count));
        tracks_->topLevelItem(i)->setToolTip(2,"原始音符数 + 当前手工新增音符数");
    }
    octaveInfo_->setText(s.settings.autoOctave?QString("已应用：整体 %1 半音（%2 八度）\n按当前参与音轨计算，准确映射包含八度适配。").arg(r.octaveShift).arg(r.octaveShift/12):"已应用：保持原始音高（0 半音）");
    auto selected=roll_->selectedSources();roll_->setMusic(s.song,s.result);roll_->selectSources(selected);if(fit)roll_->fitAll();
    summary_->setText(QString("映射 %1 · 手改 %2 · 删除 %3 · 和弦 %4 · 排除 %5").arg(r.exact+r.approximate+r.edited).arg(r.edited).arg(r.deleted).arg(r.chords).arg(r.excluded));
    play_->setEnabled(r.exact+r.approximate+r.edited>0);stop_->setEnabled(r.exact+r.approximate+r.edited>0);
    play_->setToolTip(r.conflicts>0?QString("存在 %1 处同键冲突，需消除后才能试听").arg(r.conflicts):"播放转换后的九键手盘音符");
    details_->setText("点击轨道上的音符，查看原始音高、转换结果与时间。");
    if(s.song->notes.empty())status_->setText("文件中没有有效音符，仅包含空轨道或元事件。");
    else status_->setText(QString("%1 次同键重合已合并 · %2 条导入提示 · 编辑即时生效，可撤销；重新转换保留手工修改").arg(r.merged).arg(s.song->warnings.size()));
    if(roll_->selectedSource()>=0)showNote(roll_->selectedSource());
    updateEditActions();refreshClock();
}
void MainWindow::setAllTracks(bool enabled) {
    if(current_<0)return;updating_=true;auto& s=sessions_[current_].settings;
    std::fill(s.enabled.begin(),s.enabled.end(),enabled);std::fill(s.solo.begin(),s.solo.end(),false);
    for(int i=0;i<tracks_->topLevelItemCount();++i){tracks_->topLevelItem(i)->setCheckState(0,enabled?Qt::Checked:Qt::Unchecked);tracks_->topLevelItem(i)->setCheckState(1,Qt::Unchecked);}
    updating_=false;recalculate();
}
void MainWindow::togglePlayback() {
    if(timer_.isActive()){pausePreview();return;}
    const auto* r=currentResult();if(!r||!play_->isEnabled())return;
    if(!validateParameters())return;
    if(settingsPending_){showWarning("请先应用参数","转换参数已修改，请先点击“应用设置 · 重新转换”，检查更新后的同键冲突后再试听。");return;}
    if(r->conflicts>0) {
        showWarning("存在同键冲突，无法试听",QString("当前曲目有 %1 处同键过密冲突，已阻止音频播放。\n\n请通过转换报告定位冲突，降低播放倍率、缩短按下时长/松开后间隔，或移动、删除冲突音符。重新转换并消除冲突后再试听。\n不同键的和弦不会阻止播放。").arg(r->conflicts));return;
    }
    if(position_>=r->duration)position_=0;
    QString error;
    if(!audio_.play(*sessions_[current_].song,*r,position_,error)){status_->setText(error);return;}
    timer_.start();deleteMode_->setChecked(false);roll_->setEditingEnabled(false);updateEditActions();
    play_->setText("暂停试听");status_->setText("手盘试听中 · 音符编辑已锁定 · 空格暂停/继续");
}
void MainWindow::showNote(int source) {
    updateEditActions();
    if(source<0){details_->setText("空白处拖动框选多个音符。拖动中间移动，拖动两端调整时长，↑/↓ 调整音高，Delete 删除。");return;}
    if(roll_->selectedSources().size()>1) {
        details_->setText(QString("已选中 %1 个音符\n\n拖动中间：整体移动时间与音高\n拖动任一两端：批量延长或缩短\n↑ / ↓：升降一个九键音级\nDelete：批量删除\nCtrl+Z：撤销整次编辑\n\n音高限于 B（A2）～U（E4），整组到边界即停止。\n\nCtrl 点击增减选择，Shift 点击追加；Ctrl/Shift 框选追加。已跳过音符仅参与删除。").arg(roll_->selectedSources().size()));return;
    }
    if(current_<0)return;const auto& s=sessions_[current_];if(source>=static_cast<int>(s.song->notes.size()))return;
    const auto& n=s.song->notes[source];const auto& m=s.result->notes[source];
    QString target=m.target<0?"未映射":QString("%1  /  %2").arg(QChar(keys[m.target])).arg(noteName(pitches[m.target]));
    QString reason=m.mapping==Mapping::Edited?(n.added?"手工新增（重新转换会保留）":"手工编辑（重新转换会保留）"):m.mapping==Mapping::Deleted?"已手工删除（可撤销）":m.mapping==Mapping::Exact?(s.result->octaveShift?"八度适配后准确映射":"原样保留"):m.mapping==Mapping::Approximate?"转换成相近音":m.mapping==Mapping::Skipped?"适配后仍不在九音范围，已跳过":"音轨未参与转换";
    if(m.mapping==Mapping::Exact||m.mapping==Mapping::Approximate||m.mapping==Mapping::Skipped)
        reason+=QString("\n整体八度调整：%1 半音").arg(s.result->octaveShift);
    details_->setText(QString("原始音高\n%1  ·  MIDI %2\n\n目标按键 / 音高\n%3\n\n转换结果\n%4\n\n开始时间     %5 s\n持续时间     %6 s\n原始力度     %7\n\n音轨 %8 / 通道 %9%10")
        .arg(noteName(n.pitch)).arg(n.pitch).arg(target,reason).arg(m.start,0,'f',3).arg(m.duration,0,'f',3).arg(n.velocity)
        .arg(s.song->tracks[n.track].source+1).arg(s.song->tracks[n.track].channel+1)
        .arg(m.target>=0?QString("\n音高偏移     %1 半音%2").arg(pitches[m.target]-n.pitch).arg(m.conflict?"\n\n⚠ 同键重复触发过密":""):""));
    tabs_->setCurrentIndex(1);
}
void MainWindow::showDiagnostics() {
    if(current_<0)return;auto& s=sessions_[current_];
    auto* dialog=new QDialog(this);dialog->setObjectName("conversionReport");dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("转换报告");dialog->resize(900,560);
    dialog->setStyleSheet("QDialog#conversionReport {background:#eef3f5;} QDialog#conversionReport QLabel {color:#203d4e;} QTableWidget {background:white; color:#203d4e; alternate-background-color:#f5f9fa; selection-background-color:#d9eeea; selection-color:#203d4e;}");
    auto* layout=new QVBoxLayout(dialog);
    QString warnings;for(const auto& warning:s.song->warnings)warnings+=QString::fromUtf8(warning)+"\n";
    warnings.prepend(QString("整体八度调整：%1 半音（准确映射包含八度适配）\n").arg(s.result->octaveShift));
    auto* summary=new QLabel(QString("准确 %1 · 近似 %2 · 跳过 %3 · 排除 %4 · 同键冲突 %5\n手工编辑 %6 · 手工删除 %7\n%8\n包含准确映射在内的全部音符，按开始时间排列；每页显示 200 条。\n双击记录定位音符（已删除或已排除音符仅显示详情）。")
        .arg(s.result->exact).arg(s.result->approximate).arg(s.result->skipped).arg(s.result->excluded).arg(s.result->conflicts).arg(s.result->edited).arg(s.result->deleted).arg(warnings));summary->setWordWrap(true);summary->setTextFormat(Qt::PlainText);layout->addWidget(summary);
    auto* table=new QTableWidget(0,6);table->setObjectName("conversionTable");table->setHorizontalHeaderLabels({"开始 / 秒","原始音","目标音","目标键","音轨","状态"});table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);table->horizontalHeader()->setSectionResizeMode(5,QHeaderView::ResizeToContents);table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setAlternatingRowColors(true);layout->addWidget(table);
    std::vector<const MappedNote*> ordered;ordered.reserve(s.result->notes.size());
    for(const auto& m:s.result->notes)if(!s.song->notes[m.source].added||s.edits.contains(m.source))ordered.push_back(&m);
    std::stable_sort(ordered.begin(),ordered.end(),[](const auto* a,const auto* b){return a->startTick<b->startTick;});
    constexpr int pageSize=200;
    const int pageCount=std::max(1,(static_cast<int>(ordered.size())+pageSize-1)/pageSize);
    auto* navigation=new QHBoxLayout;
    auto* range=label("","muted");range->setObjectName("reportRecordRange");navigation->addWidget(range);navigation->addStretch();
    auto* first=button("首页","reportFirstPage");auto* previous=button("上一页","reportPreviousPage");
    auto* next=button("下一页","reportNextPage");auto* last=button("末页","reportLastPage");
    for(auto* control:{first,previous,next,last})control->setAutoDefault(false);
    auto* page=new QSpinBox;page->setObjectName("reportPage");page->setRange(1,pageCount);page->setKeyboardTracking(false);
    page->setPrefix("第 ");page->setSuffix(QString(" / %1 页").arg(pageCount));page->setToolTip("输入页码并按回车跳转");page->setEnabled(pageCount>1);
    navigation->addWidget(first);navigation->addWidget(previous);navigation->addWidget(page);navigation->addWidget(next);navigation->addWidget(last);layout->addLayout(navigation);
    // Keep the conversion snapshot alive while later pages reference its notes.
    auto renderPage=[table,range,first,previous,next,last,pageCount,song=s.song,result=s.result,ordered=std::move(ordered)](int value) {
        const int begin=(value-1)*pageSize,end=std::min(begin+pageSize,static_cast<int>(ordered.size()));
        table->setUpdatesEnabled(false);table->clearContents();table->setRowCount(end-begin);
        for(int index=begin;index<end;++index) {
            const auto& m=*ordered[index];const auto& n=song->notes[m.source];const int row=index-begin;
            QString state=m.mapping==Mapping::Deleted?"手工删除":m.mapping==Mapping::Excluded?"音轨已排除":m.mapping==Mapping::Skipped?"无法映射，跳过":m.mapping==Mapping::Edited?"手工编辑":m.mapping==Mapping::Exact?(result->octaveShift?"八度适配 · 准确":"原音准确映射"):"近似转换";
            if(n.added&&m.mapping==Mapping::Edited)state="手工新增";
            if(m.conflict)state+=" · 同键过密";
            QStringList values{QString::number(m.start,'f',3),noteName(n.pitch),m.target<0?"—":noteName(pitches[m.target]),m.target<0?"—":QString(QChar(keys[m.target])),QString::number(song->tracks[n.track].source+1),state};
            for(int c=0;c<6;++c)table->setItem(row,c,new QTableWidgetItem(values[c]));table->item(row,0)->setData(Qt::UserRole,m.source);
            table->setVerticalHeaderItem(row,new QTableWidgetItem(QString::number(index+1)));
        }
        table->clearSelection();table->setCurrentCell(-1,-1);table->scrollToTop();table->setUpdatesEnabled(true);
        range->setText(ordered.empty()?"共 0 条记录":QString("显示 %1–%2 / %3 条记录").arg(begin+1).arg(end).arg(ordered.size()));
        first->setEnabled(value>1);previous->setEnabled(value>1);next->setEnabled(value<pageCount);last->setEnabled(value<pageCount);
    };
    renderPage(1);connect(page,qOverload<int>(&QSpinBox::valueChanged),dialog,renderPage);
    connect(first,&QPushButton::clicked,dialog,[page]{page->setValue(1);});
    connect(previous,&QPushButton::clicked,dialog,[page]{page->setValue(page->value()-1);});
    connect(next,&QPushButton::clicked,dialog,[page]{page->setValue(page->value()+1);});
    connect(last,&QPushButton::clicked,dialog,[page,pageCount]{page->setValue(pageCount);});
    connect(table,&QTableWidget::cellDoubleClicked,dialog,[this,table,dialog](int row,int){
        int source=table->item(row,0)->data(Qt::UserRole).toInt();filter_->setCurrentIndex(0);roll_->selectSource(source);showNote(source);dialog->accept();
    });
    auto* close=new QDialogButtonBox(QDialogButtonBox::Close);close->button(QDialogButtonBox::Close)->setText("关闭");close->button(QDialogButtonBox::Close)->setAutoDefault(false);connect(close,&QDialogButtonBox::rejected,dialog,&QDialog::reject);layout->addWidget(close);dialog->setModal(true);dialog->show();
}
void MainWindow::pausePreview(bool reset) {
    bool wasPlaying=timer_.isActive();timer_.stop();audio_.pause();play_->setText("手盘试听");
    roll_->setEditingEnabled(true);updateEditActions();
    if(wasPlaying&&currentResult()){position_=std::min(currentResult()->duration,audio_.position());roll_->setPlayhead(position_);status_->setText(audio_.finished()?"试听结束。":"试听已暂停，再次点击可从当前位置继续。");}
    if(reset){position_=0;roll_->setPlayhead(0);}refreshClock();
}
void MainWindow::refreshClock() {clock_->setText(formatTime(position_)+" / "+formatTime(currentResult()?currentResult()->duration:0));}
void MainWindow::addNote(double start,int target) {
    if(current_<0||busy_||timer_.isActive()||roll_->isEditing()||!std::isfinite(start)||start<0||target<0||target>=9)return;
    auto& s=sessions_[current_];
    if(s.song->notes.size()>=200000){status_->setText("已达到每曲目 20 万音符上限，无法继续添加。");return;}
    int startTick=tickAtSeconds(*s.song,s.settings,start);
    int length=std::max(1,s.song->ppq);
    if(startTick>std::numeric_limits<int>::max()-length){status_->setText("该位置超出可编辑的时间范围。");return;}
    // An empty imported MIDI has no channel tracks. Create a session-only lane.
    if(s.song->tracks.empty()) {
        const QSignalBlocker trackSignals(tracks_),filterSignals(filter_);
        s.song->tracks.push_back({"手工音轨",0,0,0});s.settings.enabled.push_back(true);s.settings.solo.push_back(false);
        auto* row=new QTreeWidgetItem(tracks_,{"手工音轨\n轨 1 · CH 1","","0"});
        row->setFlags(row->flags()|Qt::ItemIsUserCheckable);row->setCheckState(0,Qt::Checked);row->setCheckState(1,Qt::Unchecked);
        filter_->addItem("手工音轨 · CH 1",0);
    }
    bool anySolo=false;
    for(size_t i=0;i<s.settings.enabled.size();++i)if(s.settings.enabled[i]&&s.settings.solo[i])anySolo=true;
    auto playable=[&](int track){return track>=0&&track<static_cast<int>(s.song->tracks.size())&&s.settings.enabled[track]&&(!anySolo||s.settings.solo[track]);};
    int track=filter_->currentData().toInt();
    if(track<0) {
        track=tracks_->indexOfTopLevelItem(tracks_->currentItem());
        if(!playable(track)) {
            track=-1;for(size_t i=0;i<s.song->tracks.size();++i)if(playable(static_cast<int>(i))){track=static_cast<int>(i);break;}
        }
    }
    if(!playable(track)){status_->setText("请先启用目标音轨，并确认它在当前独奏范围内，再添加音符。");return;}
    int source=static_cast<int>(s.song->notes.size());
    s.song->notes.push_back({track,pitches[target],100,startTick,startTick+length,true});
    commitEdits({{source,{startTick,startTick+length,target,false}}});
    roll_->selectSource(source,false);showNote(source);
    status_->setText(QString("已添加 %1 · 一拍 · 力度 100 · 可继续点击添加，Esc 退出").arg(QChar(keys[target])));
}
void MainWindow::editNotes(const std::vector<MappedNote>& notes) {
    if(current_<0||busy_||timer_.isActive()||roll_->isEditing())return;
    const auto& s=sessions_[current_];
    NoteEdits edits;
    for(const auto& n:notes) {
        if(!std::isfinite(n.start)||!std::isfinite(n.duration)||n.start<0||n.duration<=0||n.target<0||n.target>=9)return;
        int startTick=std::min(tickAtSeconds(*s.song,s.settings,n.start),std::numeric_limits<int>::max()-1);
        int endTick=std::max(startTick+1,tickAtSeconds(*s.song,s.settings,n.start+n.duration));
        edits[n.source]={startTick,endTick,n.target,false};
    }
    commitEdits(edits);
}
void MainWindow::deleteNotes() {
    if(current_<0||busy_||timer_.isActive()||roll_->isEditing())return;
    const auto& result=*sessions_[current_].result;
    NoteEdits edits;
    for(int source:roll_->selectedSources()) {
        const auto& note=result.notes[source];
        if(note.mapping!=Mapping::Deleted&&note.mapping!=Mapping::Excluded)
            edits[source]={note.startTick,note.endTick,note.target,true};
    }
    commitEdits(edits);
}
void MainWindow::commitEdits(const NoteEdits& edits) {
    if(current_<0)return;auto& s=sessions_[current_];
    std::vector<EditChange> changes;
    for(const auto& [source,edit]:edits) {
        if(source<0||source>=static_cast<int>(s.song->notes.size()))return;
        std::optional<NoteEdit> before;
        if(auto it=s.edits.find(source);it!=s.edits.end())before=it->second;
        if(before&&*before==edit)continue;
        // Rounding a sub-tick movement should not create a spurious edit.
        if(source<static_cast<int>(s.result->notes.size())) {
            const auto& current=s.result->notes[source];
            if(!edit.deleted&&current.target>=0&&current.startTick==edit.startTick&&current.endTick==edit.endTick&&current.target==edit.target)continue;
        }
        changes.push_back({source,before,edit});
    }
    if(changes.empty())return;
    s.history.resize(s.historyCursor);s.history.push_back(changes);
    if(s.history.size()>256)s.history.erase(s.history.begin());
    s.historyCursor=s.history.size();for(const auto& change:changes)s.edits[change.source]=*change.after;
    recalculate();
    showNote(roll_->selectedSource());
}
void MainWindow::stepHistory(bool redo) {
    if(current_<0||busy_||timer_.isActive()||roll_->isEditing())return;auto& s=sessions_[current_];
    if((redo&&s.historyCursor==s.history.size())||(!redo&&s.historyCursor==0))return;
    const auto& changes=s.history[redo?s.historyCursor++:--s.historyCursor];
    std::set<int> selected;
    for(const auto& change:changes) {
        const auto& edit=redo?change.after:change.before;
        if(edit)s.edits[change.source]=*edit;else s.edits.erase(change.source);
        selected.insert(change.source);
    }
    recalculate();roll_->selectSources(selected,true);showNote(roll_->selectedSource());
}
void MainWindow::updateEditActions() {
    bool hasSong=current_>=0, editable=hasSong&&!timer_.isActive();
    addNote_->setEnabled(editable);
    deleteMode_->setEnabled(editable);deleteNote_->setEnabled(editable&&roll_->selectedSource()>=0);
    deleteNote_->setText(roll_->selectedSources().size()>1?QString("删除 (%1)").arg(roll_->selectedSources().size()):"删除");
    deleteNote_->setMinimumWidth(deleteNote_->fontMetrics().horizontalAdvance(deleteNote_->text())+24);
    undo_->setEnabled(editable&&sessions_[current_].historyCursor>0);
    redo_->setEnabled(editable&&sessions_[current_].historyCursor<sessions_[current_].history.size());
}
void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if(!busy_&&e->mimeData()->hasUrls())for(const auto& u:e->mimeData()->urls())if(u.isLocalFile()){e->acceptProposedAction();break;}
}
void MainWindow::dropEvent(QDropEvent* e) {
    QStringList paths;for(const auto& u:e->mimeData()->urls())if(u.isLocalFile())paths<<u.toLocalFile();
    importFiles(paths);e->acceptProposedAction();
}
}
