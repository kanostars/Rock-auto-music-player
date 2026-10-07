#include "main_window.h"
#include "core/midi_export.h"
#include "core/hand_score.h"
#include "piano_roll.h"
#include <QPainter>
#include <QStyle>
#include "performance_panel.h"
#include "mini_player.h"
#include "settings_page.h"
#include "practice_page.h"
#include "time_seek_edit.h"
#include "theme.h"
#include "app/preferences.h"
#include "platform/global_shortcut.h"
#include <QAbstractSpinBox>
#include <QApplication>
#include <QClipboard>
#include <QSettings>
#include "range_spinbox.h"
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QMessageBox>
#include <QProgressBar>
#include <QResizeEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSaveFile>
#include <QShortcut>
#include <QSlider>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextEdit>
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
QString noteName(int p) {return QString::fromStdString(pitchName(p));}
class PageHeaderLabel final:public QLabel {
public:
    PageHeaderLabel(){setProperty("role","brand");setTextFormat(Qt::PlainText);setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);setMinimumWidth(0);}
    void setTitle(const QString& text){fullText_=text;setToolTip(text);refreshText();}
protected:
    void resizeEvent(QResizeEvent* event) override{QLabel::resizeEvent(event);refreshText();}
    void changeEvent(QEvent* event) override{QLabel::changeEvent(event);if(event->type()==QEvent::FontChange||event->type()==QEvent::StyleChange)refreshText();}
private:
    QString fullText_;
    void refreshText(){setText(QFontMetrics(font()).elidedText(fullText_,Qt::ElideRight,contentsRect().width()));}
};
}
MainWindow::MainWindow(QWidget* parent):QMainWindow(parent) {
    Theme::initialize();
    setWindowTitle("RockAutoMusicPlay · 九键音乐工作台");resize(1460,900);setMinimumSize(1120,740);setAcceptDrops(true);
    buildUi();
    for(size_t i=0;i<globalShortcuts_.size();++i)globalShortcuts_[i]=new GlobalShortcut(0x5243+static_cast<int>(i),this);
    connect(globalShortcuts_[0],&GlobalShortcut::activated,performance_,[this]{performance_->navigateSong(true);syncMiniPlayer();});
    connect(globalShortcuts_[1],&GlobalShortcut::activated,performance_,[this]{performance_->navigateSong(false);syncMiniPlayer();});
    connect(globalShortcuts_[2],&GlobalShortcut::activated,this,&MainWindow::toggleMiniPlayer);
    connect(qApp,&QApplication::focusChanged,this,[this]{updateGlobalShortcuts();});
    updateShortcuts();connect(&Preferences::instance(),&Preferences::shortcutsChanged,this,&MainWindow::updateShortcuts);
    miniPerformance_=QSettings().value("miniPlayer/performance",true).toBool();
    miniRefresh_.setInterval(80);connect(&miniRefresh_,&QTimer::timeout,this,&MainWindow::syncMiniPlayer);
    connect(&importWatcher_,&QFutureWatcher<std::vector<Loaded>>::finished,this,[this] {
        auto loaded=importWatcher_.result();
        busy_=false;performance_->setLibraryBusy(false);import_->setEnabled(true);workspace_->setEnabled(true);editorPanel_->setEnabled(true);progress_->hide();cancelButton_->hide();updateEditActions();
        if(cancel_->load()) {status_->setText("导入已取消，已有曲目保留。");return;}
        QStringList errors;int last=-1;const int firstImported=static_cast<int>(sessions_.size());
        for(auto& item:loaded) {
            if(!item.error.isEmpty()) {errors<<QFileInfo(item.session.path).fileName()+"："+item.error;continue;}
            sessions_.push_back(std::move(item.session));
            const auto& session=sessions_.back();
            auto* row=new QListWidgetItem(QFileInfo(session.path).completeBaseName()+"\n"+QString("%1 音符 · %2 个音轨").arg(session.song->notes.size()).arg(session.song->tracks.size()));
            row->setToolTip(session.path);row->setSizeHint(QSize(180,66));library_->addItem(row);last=library_->count()-1;
            row->setData(Qt::UserRole,session.result->duration);
        }
        if(last>=0) library_->setCurrentRow(last);
        if(!errors.isEmpty()) {status_->setText("导入失败："+errors.join("；"));status_->setToolTip(errors.join('\n'));}
        promptImportConflicts(firstImported);
    });
    timer_.setInterval(16);
    connect(&timer_,&QTimer::timeout,this,[this]{
        const auto* r=currentResult();if(!r){pausePreview();return;}
        position_=std::min(selectedRange().second,audio_.position());
        roll_->setPlayhead(position_,true);refreshClock();
        if(audio_.finished()){pausePreview();performance_->previewFinished();}
        else if(!audio_.running()){pausePreview();status_->setText("音频输出已中断，请检查系统默认输出设备后重新试听。");}
    });
    qApp->installEventFilter(this);
}
MainWindow::~MainWindow() {closing_=true;if(practicePage_)practicePage_->stop();qApp->removeEventFilter(this);for(auto* shortcut:globalShortcuts_)shortcut->disable();miniRefresh_.stop();delete mini_;audio_.pause();if(cancel_)cancel_->store(true);importWatcher_.waitForFinished();}
const Conversion* MainWindow::currentResult() const {return current_>=0?sessions_[current_].result.get():nullptr;}

void MainWindow::buildUi() {

    auto* root=new QWidget;root->setObjectName("root");setCentralWidget(root);
    auto* outer=new QVBoxLayout(root);outerLayout_=outer;outer->setContentsMargins(22,20,22,14);outer->setSpacing(14);
    auto* header=new QFrame;header->setObjectName("header");auto* top=new QHBoxLayout(header);top->setContentsMargins(22,17,22,17);
    auto* brandBox=new QVBoxLayout;brandBox->setSpacing(5);
    auto* logo=button("ROCK  /  九键音乐工作台","miniPlayerButton");headerLogo_=logo;
    Theme::setStyle(logo,"QPushButton {background:transparent;border:none;padding:0;text-align:left;font-size:22px;font-weight:700;color:#183d4d;} QPushButton:hover {color:#178e80;}");
    logo->setToolTip("点击 Logo 进入小窗模式");logo->setAccessibleName("Logo · 进入小窗模式");brandBox->addWidget(logo,0,Qt::AlignLeft);
    connect(logo,&QPushButton::clicked,this,&MainWindow::openMiniPlayer);
    headerPageTitle_=new PageHeaderLabel;headerPageTitle_->setObjectName("pageHeaderTitle");brandBox->addWidget(headerPageTitle_);headerPageTitle_->hide();
    headerSubtitle_=label("MIDI → NINE KEYS     ·     让每个音符找到它的位置","muted");headerSubtitle_->setObjectName("pageHeaderSubtitle");brandBox->addWidget(headerSubtitle_);top->addLayout(brandBox,1);
    import_=button("＋  导入 MIDI","importButton");import_->setMinimumWidth(154);top->addWidget(import_);outer->addWidget(header);
    settingsNavigation_=button("设置","settingsNavigation");settingsNavigation_->setCheckable(true);top->addWidget(settingsNavigation_);
    practiceBack_=button("返回工作台","practiceBackButton");top->addWidget(practiceBack_);practiceBack_->hide();
    connect(practiceBack_,&QPushButton::clicked,this,&MainWindow::leavePractice);
    connect(settingsNavigation_,&QPushButton::toggled,this,&MainWindow::showAppSettings);
    connect(import_,&QPushButton::clicked,this,[this]{importFiles(QFileDialog::getOpenFileNames(this,"选择 MIDI 文件",{},"MIDI 文件 (*.mid *.midi)"));});

    workspace_=new QWidget;auto* work=new QHBoxLayout(workspace_);work->setContentsMargins(0,0,0,0);work->setSpacing(0);
    auto* split=new QSplitter;workspaceSplit_=split;split->setChildrenCollapsible(false);work->addWidget(split);outer->addWidget(workspace_,1);
    auto* left=new QFrame;left->setObjectName("panel");left->setMinimumWidth(220);left->setMaximumWidth(360);
    auto* ll=new QVBoxLayout(left);ll->setContentsMargins(14,16,14,14);ll->setSpacing(12);
    ll->addWidget(label("曲目库","section"));ll->addWidget(label("导入的文件只在本地处理","muted"));
    library_=new QListWidget;library_->setObjectName("songList");library_->setMinimumHeight(120);library_->setMaximumHeight(240);ll->addWidget(library_,1);
    connect(library_,&QListWidget::currentRowChanged,this,&MainWindow::selectSong);
    auto* line=new QFrame;line->setFrameShape(QFrame::HLine);Theme::setStyle(line,"color:#e7eef1;");ll->addWidget(line);
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

    auto* center=new QFrame;editorPanel_=center;center->setObjectName("panel");center->setMinimumWidth(490);
    auto* cl=new QVBoxLayout(center);cl->setContentsMargins(18,18,18,16);cl->setSpacing(14);
    songTitle_=label("等待第一首旋律","title");songTitle_->setObjectName("songTitle");cl->addWidget(songTitle_);
    subtitle_=label("导入 MIDI 后，自动生成九键音符轨道","muted");
    subtitle_->setTextFormat(Qt::RichText);
    subtitle_->setWordWrap(true);subtitle_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    cl->addWidget(subtitle_);
    auto* rollBar=new QHBoxLayout;rollBar->addWidget(label("九键轨道","section"));rollBar->addStretch();
    filter_=new QComboBox;filter_->setObjectName("trackFilter");filter_->setMinimumWidth(140);filter_->setMaximumWidth(220);filter_->addItem("全部音轨",-1);rollBar->addWidget(filter_);
    auto* fit=button("查看全部","fitButton");rollBar->addWidget(fit);
    expandTrack_=button("全屏轨道","expandTrackButton");expandTrack_->setToolTip("在独立全屏窗口中播放和编辑音符");rollBar->addWidget(expandTrack_);cl->addLayout(rollBar);
    connect(expandTrack_,&QPushButton::clicked,this,[this]{
        if(trackWindow_&&trackWindow_->isVisible())trackWindow_->close();else openTrackWindow();
    });
    auto* editBar=new QHBoxLayout;editBar->setSpacing(4);
    addNote_=button("添加音符","addNoteButton");addNote_->setCheckable(true);addNote_->setEnabled(false);
    addNote_->setToolTip("开启后点击九键行的空白位置添加一拍音符（力度 100）；可连续添加，Esc 退出。\n添加到显示筛选指定的音轨；显示全部时优先使用左侧选中的可播放音轨。");
    deleteNote_=button("删除","deleteNoteButton");deleteNote_->setToolTip("删除选中音符（Delete），可撤销");deleteNote_->setEnabled(false);
    deleteMode_=button("点选删除","deleteMode");deleteMode_->setCheckable(true);deleteMode_->setEnabled(false);deleteMode_->setToolTip("开启后直接点击音符删除；关闭后可拖动音符及两端");
    undo_=button("撤销","undoButton");undo_->setToolTip("撤销音符编辑（Ctrl+Z）");undo_->setEnabled(false);
    redo_=button("重做","redoButton");redo_->setToolTip("重做音符编辑（Ctrl+Y / Ctrl+Shift+Z）");redo_->setEnabled(false);
    for(auto* b:{addNote_,deleteNote_,deleteMode_,undo_,redo_}){Theme::setStyle(b,"padding:6px 7px;");b->ensurePolished();b->setMinimumWidth(b->sizeHint().width());editBar->addWidget(b);}
    editBar->addSpacing(8);
    exportMidi_=button("导出 MIDI","exportMidiButton");exportMidi_->setEnabled(false);Theme::setStyle(exportMidi_,"padding:6px 7px;");
    exportMidi_->setToolTip("导出当前曲目的完整九键谱，包含音符编辑与已应用的节奏设置");editBar->addWidget(exportMidi_);
    connect(exportMidi_,&QPushButton::clicked,this,&MainWindow::exportCurrentMidi);
    copyHandScore_=button("复制手弹谱","copyHandScoreButton");
    copyHandScore_->setToolTip("左键复制手弹谱，右键导出 TXT：每拍一组，每 4 拍换行，空拍为 —，方括号内的按键同时按下");
    copyKeyScore_=button("复制键谱","copyKeyScoreButton");
    copyKeyScore_->setToolTip("左键复制键谱，右键导出 TXT：# BPM: 速度，随后为大写按键:到下一次按键的拍数");
    for(const auto& [control,format]:{std::pair{copyHandScore_,TextScoreFormat::Hand},std::pair{copyKeyScore_,TextScoreFormat::Keys}}){
        control->setEnabled(false);Theme::setStyle(control,"padding:6px 7px;");editBar->addWidget(control);
        control->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(control,&QPushButton::clicked,this,[this,format]{copyCurrentScore(format);});
        connect(control,&QWidget::customContextMenuRequested,this,[this,format](const QPoint&){exportCurrentScore(format);});
    }
    for(auto* b:{exportMidi_,copyHandScore_,copyKeyScore_}){b->ensurePolished();b->setMinimumWidth(b->sizeHint().width());}
    editBar->addStretch();
    resetRange_=button("","resetTimelineRange");resetRange_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    deleteRange_=button("","deleteTimeRange");createRange_=button("","createTimeRange");
    auto rangeIcon=[](bool create){
        QPixmap pixmap(24,24);pixmap.fill(Qt::transparent);QPainter painter(&pixmap);painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(Theme::color("#315162"),1.7,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        if(create){painter.drawRoundedRect(QRectF(3,4,14,16),2,2);painter.fillRect(QRect(12,10,11,13),Theme::color("#ffffff"));
            painter.setPen(QPen(Theme::color("#20a45b"),2,Qt::SolidLine,Qt::RoundCap));painter.drawLine(17,12,17,21);painter.drawLine(13,16,21,16);}
        else {painter.drawEllipse(QRectF(3,3,6,6));painter.drawEllipse(QRectF(3,15,6,6));painter.drawLine(8,8,20,20);painter.drawLine(8,16,20,4);}
        return QIcon(pixmap);
    };
    deleteRange_->setIcon(rangeIcon(false));createRange_->setIcon(rangeIcon(true));
    connect(&Preferences::instance(),&Preferences::themeChanged,this,[this,rangeIcon]{deleteRange_->setIcon(rangeIcon(false));createRange_->setIcon(rangeIcon(true));});
    resetRange_->setAccessibleName("重置片段区间");deleteRange_->setAccessibleName("删除片段");createRange_->setAccessibleName("创建空片段");
    resetRange_->setToolTip("重置片段区间：覆盖整首歌");
    deleteRange_->setToolTip("删除片段：删除所选时间（所有音轨），后续音符前移，可撤销");
    createRange_->setToolTip("创建空片段：在区间起点插入等长空白，后续音符后移，可撤销");
    for(auto* b:{resetRange_,deleteRange_,createRange_}){b->setFixedSize(28,28);b->setIconSize(QSize(20,20));Theme::setStyle(b,"padding:3px;");editBar->addWidget(b);}
    cl->addLayout(editBar);
    connect(resetRange_,&QPushButton::clicked,this,[this]{if(current_<0)return;pausePreview();auto& s=sessions_[current_];s.rangeFirst=0;s.rangeLast=-1;refreshRange();});
    connect(deleteRange_,&QPushButton::clicked,this,&MainWindow::deleteRange);
    connect(createRange_,&QPushButton::clicked,this,&MainWindow::createRange);
    roll_=new PianoRoll;cl->addWidget(roll_,1);
    connect(roll_,&PianoRoll::rangeEdited,this,&MainWindow::changeRange);
    connect(roll_,&PianoRoll::rangeBoundaryToPlayheadRequested,this,&MainWindow::moveRangeBoundaryToPlayhead);
    auto* legend=new QHBoxLayout;auto* legendText=label("● 原样保留    ● 近似转换    ● 同键冲突","muted");legendText->setTextFormat(Qt::RichText);
    Theme::setRichText(legendText,"<span style='color:#189e91'>●</span> 同音名　<span style='color:#d5a14a'>●</span> 近似　<span style='color:#6582bd'>●</span> 已编辑　<span style='color:#d4656d'>●</span> 冲突");legend->addWidget(legendText);legend->addStretch();
    practiceButton_=button("跟练练习","practiceButton");practiceButton_->setEnabled(false);practiceButton_->setToolTip("进入当前曲目的自动预览与按组跟弹练习");Theme::setStyle(practiceButton_,"padding:6px 10px;");legend->addWidget(practiceButton_);connect(practiceButton_,&QPushButton::clicked,this,&MainWindow::openPractice);
    auto* minus=button("−");auto* plus=button("＋");minus->setFixedWidth(34);plus->setFixedWidth(34);zoomText_=label("80 px/s","muted");legend->addWidget(minus);legend->addWidget(zoomText_);legend->addWidget(plus);cl->addLayout(legend);
    connect(minus,&QPushButton::clicked,this,[this]{roll_->setZoom(roll_->zoom()/1.4);});connect(plus,&QPushButton::clicked,this,[this]{roll_->setZoom(roll_->zoom()*1.4);});
    zoomText_->setMinimumWidth(64);
    connect(roll_,&PianoRoll::zoomChanged,this,[this](double p){zoomText_->setText(QString::number(p,'f',p<1?2:0)+" px/s");});
    connect(filter_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{roll_->setTrackFilter(filter_->currentData().toInt());});
    connect(fit,&QPushButton::clicked,roll_,&PianoRoll::fitAll);connect(roll_,&PianoRoll::noteSelected,this,&MainWindow::showNote);
    connect(roll_,&PianoRoll::seekRequested,this,[this](double t){if(performance_->active())return;pausePreview();position_=t;roll_->setPlayhead(t);refreshClock();});
    connect(roll_,&PianoRoll::editStarted,this,[this]{pausePreview();});
    connect(roll_,&PianoRoll::notesEdited,this,&MainWindow::editNotes);
    connect(roll_,&PianoRoll::deleteRequested,this,&MainWindow::deleteNotes);
    connect(roll_,&PianoRoll::addRequested,this,&MainWindow::addNote);
    connect(addNote_,&QPushButton::toggled,this,[this](bool enabled){
        if(enabled)deleteMode_->setChecked(false);
        roll_->setAddMode(enabled);
        if(enabled){roll_->setFocus();status_->setText("添加模式 · 点击九键行空白处创建一拍音符 · 再点按钮或 "+Preferences::instance().shortcutText(ShortcutAction::CancelEdit)+" 退出");}
    });
    connect(roll_,&PianoRoll::addModeChanged,addNote_,&QPushButton::setChecked);
    connect(deleteMode_,&QPushButton::toggled,this,[this](bool enabled){if(enabled)addNote_->setChecked(false);});
    connect(deleteNote_,&QPushButton::clicked,this,&MainWindow::deleteNotes);
    connect(deleteMode_,&QPushButton::toggled,roll_,&PianoRoll::setDeleteMode);
    connect(undo_,&QPushButton::clicked,this,[this]{stepHistory(false);});
    connect(redo_,&QPushButton::clicked,this,[this]{stepHistory(true);});
    auto* undoShortcut=new QShortcut(roll_);editorShortcuts_[0]=undoShortcut;
    undoShortcut->setContext(Qt::WidgetWithChildrenShortcut);connect(undoShortcut,&QShortcut::activated,this,[this]{stepHistory(false);});
    for(int index:{1,2}) {
        auto* shortcut=new QShortcut(roll_);editorShortcuts_[index]=shortcut;shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut,&QShortcut::activated,this,[this]{stepHistory(true);});
    }
    split->addWidget(center);

    auto* right=new QFrame;right->setObjectName("panel");right->setMinimumWidth(270);right->setMaximumWidth(340);
    auto* rl=new QVBoxLayout(right);rl->setContentsMargins(6,4,6,10);tabs_=new QTabWidget;tabs_->setObjectName("workbenchTabs");rl->addWidget(tabs_);
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);auto* settings=new QWidget;settings->setObjectName("settingsPage");Theme::setStyle(settings,"QWidget#settingsPage {background:white;}");auto* sl=new QVBoxLayout(settings);sl->setContentsMargins(14,18,14,14);sl->setSpacing(13);
    sl->addWidget(label("音高适配","section"));octaveMode_=new QComboBox;octaveMode_->setObjectName("octaveMode");octaveMode_->addItems({"自动移调适配（推荐）","不整体移调（仍折叠八度）"});sl->addWidget(octaveMode_);
    octaveInfo_=label("导入后显示整体移调与八度折叠","muted");octaveInfo_->setObjectName("octaveInfo");octaveInfo_->setWordWrap(true);sl->addWidget(octaveInfo_);
    sl->addWidget(label("适配后仍不支持的音符","section"));strategy_=new QComboBox;strategy_->setObjectName("strategyCombo");strategy_->addItems({"转换成最近音","直接跳过"});sl->addWidget(strategy_);
    auto* help=label("自动尝试 −12～+12 半音移调，优先保留同音名。逐音选择最近的同音名八度；无同音名时才取最近音或跳过，等距取低。手工修改保留。","muted");help->setWordWrap(true);sl->addWidget(help);
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
    auto* outputScroll=new QScrollArea;outputScroll->setWidgetResizable(true);
    performance_=new PerformancePanel;performance_->setLibrary(library_);outputScroll->setWidget(performance_);tabs_->addTab(outputScroll,"自动演奏");
    ll->insertWidget(3,performance_->playlistControls());performance_->playlistControls()->show();
    connect(apply_,&QPushButton::clicked,this,&MainWindow::applySettings);
    connect(strategy_,qOverload<int>(&QComboBox::currentIndexChanged),this,&MainWindow::markDirty);
    connect(octaveMode_,qOverload<int>(&QComboBox::currentIndexChanged),this,&MainWindow::markDirty);
    connect(tempoMode_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int i){bpm_->setEnabled(i==1);markDirty();});
    for(auto* spin:{bpm_,speed_})connect(spin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,&MainWindow::markDirty);
    for(auto* spin:{hold_,gap_})connect(spin,qOverload<int>(&QSpinBox::valueChanged),this,&MainWindow::markDirty);
    split->addWidget(right);split->setSizes({240,850,290});split->setStretchFactor(1,1);

    auto* transport=new QFrame;transportPanel_=transport;transport->setObjectName("transport");auto* tl=new QHBoxLayout(transport);tl->setContentsMargins(16,12,16,12);
    play_=button("手碟试听","playButton");play_->setEnabled(false);stop_=button("停止","stopButton");stop_->setEnabled(false);tl->addWidget(play_);tl->addWidget(stop_);
    clock_=new TimeSeekEdit;clock_->setProperty("role","section");clock_->setObjectName("previewClock");tl->addSpacing(16);tl->addWidget(clock_);
    connect(clock_,&TimeSeekEdit::seekRequested,this,&MainWindow::seekPreviewTime);
    tl->addWidget(performance_->transportControls());performance_->transportControls()->show();
    tl->addStretch();summary_=label("等待导入","muted");
    tl->addWidget(label("音量","muted"));auto* volume=new QSlider(Qt::Horizontal);volume_=volume;volume->setObjectName("volumeSlider");volume->setRange(0,100);volume->setValue(60);volume->setFixedWidth(70);volume->setToolTip("试听音量 60%（0 为静音）");tl->addWidget(volume);
    connect(volume,&QSlider::valueChanged,this,[this,volume](int value){audio_.setVolume(value/100.f);volume->setToolTip(QString("试听音量 %1%（0 为静音）").arg(value));});
    auto* diagnostics=button("转换报告","reportButton");tl->addWidget(diagnostics);outer->addWidget(transport);
    connect(diagnostics,&QPushButton::clicked,this,&MainWindow::showDiagnostics);
    connect(play_,&QPushButton::clicked,this,&MainWindow::togglePlayback);
    connect(stop_,&QPushButton::clicked,this,[this]{performance_->stopPerformance();pausePreview(true);});
    statusPanel_=new QWidget;auto* statusLine=new QHBoxLayout(statusPanel_);statusLine->setContentsMargins(0,0,0,0);status_=label("就绪 · 支持 SMF 0 / 1 · 本地导入，无需驱动","muted");status_->setObjectName("statusText");status_->setWordWrap(true);statusLine->addWidget(status_,1);
    progress_=new QProgressBar;progress_->setRange(0,0);progress_->setMaximumWidth(120);progress_->setMaximumHeight(5);progress_->hide();statusLine->addWidget(progress_);
    cancelButton_=button("取消导入");cancelButton_->hide();statusLine->addWidget(cancelButton_);connect(cancelButton_,&QPushButton::clicked,this,[this]{if(cancel_)cancel_->store(true);});
    statusLine->addWidget(summary_);outer->addWidget(statusPanel_);
    appSettings_=new SettingsPage;outer->insertWidget(2,appSettings_,1);appSettings_->hide();
    connect(performance_,&PerformancePanel::activeChanged,appSettings_,&SettingsPage::setPerformanceActive);
    practicePage_=new PracticePage;outer->insertWidget(2,practicePage_,1);practicePage_->hide();

    connect(performance_,&PerformancePanel::songChangeRequested,this,[this](int row){if(!busy_)library_->setCurrentRow(row);});
    connect(performance_,&PerformancePanel::previewStartRequested,this,[this]{pausePreview(true);startPreview();});
    connect(performance_,&PerformancePanel::songRemoveRequested,this,&MainWindow::removeSong);
    connect(performance_,&PerformancePanel::songMoveRequested,this,&MainWindow::moveSong);
    connect(performance_,&PerformancePanel::sourcePositionChanged,this,[this](double seconds){
        position_=std::clamp(seconds,0.0,currentResult()?currentResult()->duration:0.0);
        roll_->setPlayhead(position_,true);
        // Publish the shared position without feeding it back into the output controller.
        clock_->setPosition(position_,currentResult()?currentResult()->duration:0);
    });
    connect(performance_,&PerformancePanel::startRequested,this,&MainWindow::togglePerformance);
    connect(performance_,&PerformancePanel::targetActivationRequested,this,[this]{
        if(!mini_||!mini_->isVisible())openMiniPlayer();
    });
    connect(performance_,&PerformancePanel::activeChanged,this,[this,settings,all,none](bool active){
        settings->setEnabled(!active);tracks_->setEnabled(!active);all->setEnabled(!active);none->setEnabled(!active);
        roll_->setEditingEnabled(!active&&!timer_.isActive());if(active){addNote_->setChecked(false);deleteMode_->setChecked(false);}
        updateEditActions();
    });
    connect(performance_,&PerformancePanel::statusChanged,status_,&QLabel::setText);
    center->setMinimumWidth(std::max(center->minimumWidth(),cl->minimumSize().width()));
    setMinimumWidth(std::max(1120,root->minimumSizeHint().width()));
}

void MainWindow::showAppSettings(bool show){
    if(show)clock_->cancelEditing();
    if(practicePage_&&practicePage_->isVisible())leavePractice();
    if(show&&trackWindow_&&trackWindow_->isVisible())trackWindow_->close();
    workspace_->setVisible(!show);appSettings_->setVisible(show);transportPanel_->setVisible(!show);statusPanel_->setVisible(!show);import_->setVisible(!show);
    settingsNavigation_->setText(show?"返回工作台":"设置");
    if(show)setPageHeader("设置","外观即时生效；快捷键保存后生效，重启程序后会保留。");else setPageHeader();
    setMinimumWidth(std::max(1120,centralWidget()->minimumSizeHint().width()));
}
void MainWindow::setPageHeader(const QString& title,const QString& subtitle){
    const bool page=!title.isEmpty();headerLogo_->setVisible(!page);headerPageTitle_->setVisible(page);
    static_cast<PageHeaderLabel*>(headerPageTitle_)->setTitle(title);
    headerSubtitle_->setText(page?subtitle:QString("MIDI → NINE KEYS     ·     让每个音符找到它的位置"));
}
void MainWindow::openPractice(){
    if(current_<0||busy_||performance_->active()||roll_->isEditing())return;
    if(settingsPending_){showWarning("请先应用参数","转换参数已修改，请先应用设置，再进入歌曲练习。");return;}
    const auto& session=sessions_[current_];
    if(!session.result||!std::any_of(session.result->notes.begin(),session.result->notes.end(),[](const auto& note){return note.target>=0&&note.target<9;})){
        showWarning("暂无可练习音符","当前曲目没有可弹的九键音符，请检查音轨和转换设置。");return;
    }
    clock_->cancelEditing();pausePreview();if(trackWindow_&&trackWindow_->isVisible())trackWindow_->close();
    addNote_->setChecked(false);deleteMode_->setChecked(false);
    practicePage_->setSong(QFileInfo(session.path).completeBaseName(),std::make_shared<Song>(*session.song),
                           std::make_shared<Conversion>(*session.result),session.settings);
    practicePage_->setVolume(volume_->value());
    workspace_->hide();appSettings_->hide();transportPanel_->hide();statusPanel_->hide();import_->hide();settingsNavigation_->hide();
    setPageHeader(practicePage_->pageTitle(),practicePage_->pageSubtitle());practiceBack_->show();
    practicePage_->show();practicePage_->setFocus();updateGlobalShortcuts();
    setMinimumWidth(std::max(1120,centralWidget()->minimumSizeHint().width()));
}
void MainWindow::leavePractice(){
    if(!practicePage_||!practicePage_->isVisible())return;
    practicePage_->stop();const auto [first,last]=selectedRange();position_=std::clamp(practicePage_->position(),first,last);
    practicePage_->hide();workspace_->show();transportPanel_->show();statusPanel_->show();import_->show();settingsNavigation_->show();
    practiceBack_->hide();setPageHeader();
    roll_->setPlayhead(position_,true);refreshClock();updateGlobalShortcuts();updateEditActions();
}
void MainWindow::updateShortcuts(){
    updateGlobalShortcuts();
    auto& preferences=Preferences::instance();const ShortcutAction actions[]{ShortcutAction::Undo,ShortcutAction::Redo,ShortcutAction::RedoAlternate};
    for(size_t i=0;i<editorShortcuts_.size();++i){editorShortcuts_[i]->setKey(preferences.shortcut(actions[i]));editorShortcuts_[i]->setAutoRepeat(false);}
    if(fullscreenShortcut_)fullscreenShortcut_->setKey(preferences.shortcut(ShortcutAction::Fullscreen));
    undo_->setToolTip("撤销音符编辑（"+preferences.shortcutText(ShortcutAction::Undo)+"）");redo_->setToolTip("重做音符编辑（"+preferences.shortcutText(ShortcutAction::Redo)+" / "+preferences.shortcutText(ShortcutAction::RedoAlternate)+"）");
    deleteNote_->setToolTip("删除选中音符（"+preferences.shortcutText(ShortcutAction::DeleteNotes)+"），可撤销");
    play_->setToolTip("试听播放 / 暂停（"+preferences.shortcutText(ShortcutAction::Preview)+"），工作台非输入控件获得焦点时生效");
    roll_->setToolTip(preferences.shortcutText(ShortcutAction::RangeLeftToPlayhead)+" 左边界移至蓝色播放标\n"+preferences.shortcutText(ShortcutAction::RangeRightToPlayhead)+" 右边界移至蓝色播放标\n音轨区获得焦点时生效");
    findChild<QPushButton*>("miniPlayerButton")->setToolTip("点击 Logo 进入小窗模式（"+preferences.shortcutText(ShortcutAction::MiniMode)+" 切换）");
    addNote_->setToolTip("开启后点击九键行空白位置添加一拍音符（力度 100）；可连续添加，"+preferences.shortcutText(ShortcutAction::CancelEdit)+" 退出。\n添加到显示筛选指定的音轨；显示全部时优先使用左侧选中的可播放音轨。");
    if(trackWindow_){if(auto* hint=trackWindow_->findChild<QLabel*>("trackShortcutHint"))hint->setText(preferences.shortcutText(ShortcutAction::Preview)+" 试听 / 暂停 · "+preferences.shortcutText(ShortcutAction::CancelEdit)+" 取消编辑 · "+preferences.shortcutText(ShortcutAction::Fullscreen)+" 切换全屏");if(auto* full=trackWindow_->findChild<QPushButton*>("trackFullscreenButton"))full->setText("切换全屏 · "+preferences.shortcutText(ShortcutAction::Fullscreen));}
    if(current_>=0)showNote(roll_->selectedSource());
}
void MainWindow::updateGlobalShortcuts(){
    bool inhibited=closing_||QApplication::activeModalWidget()||(practicePage_&&practicePage_->isVisible());
    for(auto* focus=QApplication::focusWidget();focus;focus=focus->parentWidget())if(qobject_cast<QKeySequenceEdit*>(focus)||qobject_cast<TimeSeekEdit*>(focus)){inhibited=true;break;}
    constexpr ShortcutAction actions[]{ShortcutAction::PerformancePrevious,ShortcutAction::PerformanceNext,ShortcutAction::MiniMode};
    auto& preferences=Preferences::instance();
    // Release all changed combinations first so users can swap two bindings.
    for(size_t i=0;i<globalShortcuts_.size();++i)if(inhibited||globalShortcuts_[i]->key()!=preferences.shortcut(actions[i]))globalShortcuts_[i]->disable();
    if(inhibited)return;
    for(size_t i=0;i<globalShortcuts_.size();++i){
        QString error;if(!globalShortcuts_[i]->enable(preferences.shortcut(actions[i]),error))status_->setText(error);
    }
}
void MainWindow::toggleMiniPlayer(){
    if(busy_||roll_->isEditing()||QApplication::activeModalWidget())return;
    if(mini_&&mini_->isVisible())restoreMainWindow();else openMiniPlayer();
}
void MainWindow::togglePerformance(){
    if(busy_)return;
    const bool wasMini=mini_&&mini_->isVisible();
    if(!performance_->active()){
        pausePreview();if(current_<0||!validateParameters())return;
        if(!performance_->outputReady()){
            if(mini_&&mini_->isVisible())restoreMainWindow();
            tabs_->setCurrentIndex(2);status_->setText("请先刷新并选择输出键盘和目标窗口，再开始演奏。");return;
        }
        if(settingsPending_)applySettings();
    }
    miniPerformance_=true;performance_->startPerformance();
    if(!wasMini&&!performance_->active()&&mini_&&mini_->isVisible())restoreMainWindow();
    syncMiniPlayer();
}
void MainWindow::openMiniPlayer(){
    if(busy_||roll_->isEditing()||QApplication::activeModalWidget()||(practicePage_&&practicePage_->isVisible()))return;
    clock_->cancelEditing();
    if(trackWindow_&&trackWindow_->isVisible())trackWindow_->close();
    if(!mini_){
        mini_=new MiniPlayer(library_);
        connect(mini_,&MiniPlayer::restoreRequested,this,&MainWindow::restoreMainWindow);
        connect(mini_,&MiniPlayer::quitRequested,this,&MainWindow::quitFromMiniPlayer);
        connect(mini_,&MiniPlayer::sourceRequested,this,[this](bool output){
            if(busy_||miniPerformance_==output){syncMiniPlayer();return;}
            pausePreview();performance_->stopPerformance();miniPerformance_=output;QSettings().setValue("miniPlayer/performance",output);syncMiniPlayer();
        });
        connect(mini_,&MiniPlayer::playRequested,this,[this]{if(miniPerformance_)togglePerformance();else togglePlayback();syncMiniPlayer();});
        connect(mini_,&MiniPlayer::navigateRequested,performance_,&PerformancePanel::navigateSong);
        connect(mini_,&MiniPlayer::modeRequested,performance_,&PerformancePanel::cyclePlayMode);
        connect(mini_,&MiniPlayer::moveRequested,this,&MainWindow::moveSong);
        connect(mini_,&MiniPlayer::removeRequested,this,&MainWindow::removeSong);
        connect(mini_,&MiniPlayer::songPlayRequested,this,[this](int row){
            if(busy_||row<0||row>=static_cast<int>(sessions_.size()))return;
            performance_->stopPerformance();pausePreview();
            library_->setCurrentRow(row);miniSeekResume_=false;miniSeekSong_.reset();
            position_=selectedRange().first;roll_->setPlayhead(position_);refreshClock();
            if(miniPerformance_)togglePerformance();else startPreview();
            syncMiniPlayer();
        });
        connect(mini_,&MiniPlayer::volumeRequested,volume_,&QSlider::setValue);
        connect(mini_,&MiniPlayer::seekStarted,this,[this]{
            miniSeekResume_=false;miniSeekSong_.reset();
            if(busy_||current_<0)return;
            miniSeekPerformance_=miniPerformance_;miniSeekSong_=sessions_[current_].song;
            if(miniSeekPerformance_)performance_->beginSeek();
            else {miniSeekResume_=timer_.isActive();pausePreview();}
        });
        connect(mini_,&MiniPlayer::seekRequested,this,[this](double seconds){
            const bool sameSong=current_>=0&&miniSeekSong_==sessions_[current_].song;
            const bool resume=miniSeekResume_&&sameSong;
            miniSeekResume_=false;miniSeekSong_.reset();
            if(!sameSong||miniSeekPerformance_!=miniPerformance_)return;
            if(busy_||!currentResult()||!std::isfinite(seconds))return;
            if(miniSeekPerformance_){
                if(performance_->active())performance_->seekPerformance(seconds);
                else {const auto [first,last]=selectedRange();position_=std::clamp(seconds,first,last);roll_->setPlayhead(position_);refreshClock();}
                syncMiniPlayer();return;
            }
            if(performance_->active())return;
            pausePreview();position_=std::clamp(seconds,0.0,currentResult()->duration);roll_->setPlayhead(position_);refreshClock();
            if(resume)startPreview();syncMiniPlayer();
        });
    }
    if(performance_->active())miniPerformance_=true;else if(timer_.isActive())miniPerformance_=false;
    mini_->present(screen());syncMiniPlayer();miniRefresh_.start();hide();
}
void MainWindow::restoreMainWindow(){
    miniRefresh_.stop();show();raise();activateWindow();if(mini_)mini_->hide();
}
void MainWindow::syncMiniPlayer(){
    if(!mini_||!mini_->isVisible())return;
    if(performance_->active())miniPerformance_=true;else if(timer_.isActive())miniPerformance_=false;
    MiniPlayerState s;s.title=current_>=0?QFileInfo(sessions_[current_].path).completeBaseName():"尚无曲目";
    s.performance=miniPerformance_;s.position=position_;s.duration=currentResult()?currentResult()->duration:0;s.busy=busy_;s.volume=volume_->value();
    std::tie(s.rangeFirst,s.rangeLast)=selectedRange();s.mode=performance_->playMode();s.modeEnabled=performance_->canChangePlayMode();s.seekEnabled=current_>=0;
    s.detail=status_->text();s.canPlay=current_>=0&&(miniPerformance_||play_->isEnabled());
    if(miniPerformance_){
        const auto snapshot=performance_->snapshot();s.playing=performance_->active()&&(snapshot.state==PerformanceState::Playing||snapshot.state==PerformanceState::Countdown);
        QString state="待演奏";
        switch(snapshot.state){
        case PerformanceState::Countdown:state=QString("倒计时 %1 秒").arg(static_cast<int>(std::ceil(snapshot.countdown)));break;
        case PerformanceState::Playing:state="演奏中";break;
        case PerformanceState::Paused:state="已暂停";break;
        case PerformanceState::Finished:state="已完成";break;
        case PerformanceState::Stopped:state="已终止";break;
        case PerformanceState::Failed:state="输出失败";break;
        default:break;
        }
        if(!performance_->active()&&!performance_->outputReady())state="待设置输出";
        s.status="自动演奏 · "+state;s.detail=performance_->statusText();
    }else{s.playing=timer_.isActive();s.status=s.playing?"本地试听 · 播放中":"本地试听 · 已暂停";}
    if(current_<0)s.status="返回主窗口导入 MIDI";if(busy_)s.status="正在导入…";mini_->setState(s);
}
void MainWindow::quitFromMiniPlayer(){
    if(mini_)if(auto* existing=mini_->findChild<QMessageBox*>("miniExitPrompt")){existing->raise();return;}
    performance_->stopPerformance();pausePreview();syncMiniPlayer();
    const bool edited=std::any_of(sessions_.begin(),sessions_.end(),[](const Session& s){return !s.edits.empty()||s.historyCursor>0;});
    if(!edited){close();QApplication::quit();return;}
    auto* box=new QMessageBox(QMessageBox::Question,"退出整个程序","当前会话的音符编辑不会自动保存。请确认需要的曲目已导出 MIDI，再退出程序。",QMessageBox::Yes|QMessageBox::Cancel,mini_);
    box->setObjectName("miniExitPrompt");box->setAttribute(Qt::WA_DeleteOnClose);box->setDefaultButton(QMessageBox::Cancel);
    box->button(QMessageBox::Yes)->setText("退出程序");box->button(QMessageBox::Cancel)->setText("取消");
    connect(box,&QMessageBox::finished,this,[this](int result){if(result==QMessageBox::Yes){close();QApplication::quit();}});box->open();
}

void MainWindow::openTrackWindow() {
    if(trackWindow_&&trackWindow_->isVisible()){trackWindow_->raise();trackWindow_->activateWindow();return;}
    if(busy_||roll_->isEditing())return;
    if(!trackWindow_) {
        trackWindow_=new QWidget(this,Qt::Window);trackWindow_->setObjectName("trackWindow");
        trackWindow_->setWindowTitle("九键轨道 · 播放与编辑");trackWindow_->setMinimumSize(960,600);trackWindow_->resize(1280,800);
        auto* layout=new QVBoxLayout(trackWindow_);layout->setContentsMargins(16,12,16,12);layout->setSpacing(10);
        auto* bar=new QHBoxLayout;bar->addWidget(label("轨道工作台","section"));
        auto* shortcutHint=label({},"muted");shortcutHint->setObjectName("trackShortcutHint");bar->addWidget(shortcutHint);bar->addStretch();
        auto* fullscreen=button("切换全屏","trackFullscreenButton");bar->addWidget(fullscreen);layout->addLayout(bar);
        auto toggleFullscreen=[this]{if(trackWindow_->isFullScreen())trackWindow_->showNormal();else trackWindow_->showFullScreen();};
        connect(fullscreen,&QPushButton::clicked,this,toggleFullscreen);
        auto* shortcut=new QShortcut(Preferences::instance().shortcut(ShortcutAction::Fullscreen),trackWindow_);fullscreenShortcut_=shortcut;shortcut->setAutoRepeat(false);
        connect(shortcut,&QShortcut::activated,this,toggleFullscreen);
        updateShortcuts();
        editorPlaceholder_=new QWidget;editorPlaceholder_->setMinimumWidth(490);
        auto* placeholderLayout=new QVBoxLayout(editorPlaceholder_);placeholderLayout->addStretch();
        auto* info=label("音符轨道已在独立窗口打开\n可继续在此切换曲目和调整转换设置","muted");info->setAlignment(Qt::AlignCenter);placeholderLayout->addWidget(info);
        auto* activate=button("前往轨道窗口","activateTrackWindowButton");placeholderLayout->addWidget(activate);
        connect(activate,&QPushButton::clicked,this,&MainWindow::openTrackWindow);
        auto* restore=button("将轨道放回主界面","restoreTrackButton");placeholderLayout->addWidget(restore);
        connect(restore,&QPushButton::clicked,trackWindow_,&QWidget::close);placeholderLayout->addStretch();
    }
    // Move the live editor and transport together: history, selection, shortcuts and audio keep their state.
    workspaceSizes_=workspaceSplit_->sizes();workspaceSplit_->replaceWidget(1,editorPlaceholder_);editorPlaceholder_->show();
    auto* layout=static_cast<QVBoxLayout*>(trackWindow_->layout());
    layout->addWidget(editorPanel_,1);layout->addWidget(transportPanel_);layout->addWidget(statusPanel_);
    editorPanel_->show();transportPanel_->show();statusPanel_->show();
    expandTrack_->setText("返回主界面");expandTrack_->setToolTip("关闭独立窗口并保留当前编辑和播放状态");
    trackWindow_->showFullScreen();trackWindow_->raise();trackWindow_->activateWindow();roll_->setFocus();
}
void MainWindow::restoreTrackPanel() {
    if(!trackWindow_||editorPanel_->window()!=trackWindow_)return;
    workspaceSplit_->replaceWidget(1,editorPanel_);
    // Retain the placeholder under the main window so it is also destroyed on application exit.
    editorPlaceholder_->setParent(centralWidget());editorPlaceholder_->hide();
    outerLayout_->addWidget(transportPanel_);outerLayout_->addWidget(statusPanel_);
    editorPanel_->show();transportPanel_->show();statusPanel_->show();workspaceSplit_->setSizes(workspaceSizes_);
    expandTrack_->setText("全屏轨道");expandTrack_->setToolTip("在独立全屏窗口中播放和编辑音符");
    updateEditActions();
    raise();activateWindow();roll_->setFocus();
}
bool MainWindow::eventFilter(QObject* watched,QEvent* event) {
    if(watched==trackWindow_&&event->type()==QEvent::Close)restoreTrackPanel();
    if(practicePage_&&practicePage_->isVisible())return QMainWindow::eventFilter(watched,event);
    if(!closing_&&(event->type()==QEvent::ShortcutOverride||event->type()==QEvent::KeyPress||event->type()==QEvent::KeyRelease)){
        auto* widget=qobject_cast<QWidget*>(watched);auto* key=static_cast<QKeyEvent*>(event);
        const auto* window=widget?widget->window():nullptr;
        const bool playbackWindow=widget&&(window==this||window==trackWindow_||window==mini_);
        bool ownedWindow=playbackWindow;
        for(auto* parent=widget;!ownedWindow&&parent;parent=parent->parentWidget())ownedWindow=parent==this||parent==mini_;
        if(widget&&ownedWindow){
            const bool preview=matchesShortcut(ShortcutAction::Preview,key);
            if(key->key()==Qt::Key_Space||(preview&&playbackWindow)){
                // Text entry and shortcut capture keep their normal key handling.
                for(auto* input=widget;input;input=input->parentWidget())if(qobject_cast<QLineEdit*>(input)||qobject_cast<QAbstractSpinBox*>(input)||qobject_cast<QKeySequenceEdit*>(input)||qobject_cast<QTextEdit*>(input)||qobject_cast<QPlainTextEdit*>(input))return false;
                // Reserve press and release so a focused button never sees Space.
                key->accept();
                const bool auditionSurface=(window==this&&!appSettings_->isVisible())||window==trackWindow_||(window==mini_&&!miniPerformance_);
                if(event->type()==QEvent::KeyPress&&!key->isAutoRepeat()&&preview&&auditionSurface&&!busy_&&!roll_->isEditing()&&!performance_->active()&&!QApplication::activeModalWidget()){
                    togglePlayback();syncMiniPlayer();
                }
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched,event);
}
void MainWindow::closeEvent(QCloseEvent* event) {
    clock_->cancelEditing();
    closing_=true;for(auto* shortcut:globalShortcuts_)shortcut->disable();
    if(practicePage_)practicePage_->stop();
    performance_->stopPerformance();
    miniRefresh_.stop();audio_.pause();timer_.stop();if(mini_)mini_->hide();
    if(trackWindow_)trackWindow_->close();
    QMainWindow::closeEvent(event);
}

void MainWindow::importFiles(const QStringList& paths) {
    if(paths.isEmpty()||busy_)return;
    if(practicePage_&&practicePage_->isVisible())leavePractice();
    if(auto* prompt=findChild<QMessageBox*>("importConflictDialog");prompt&&prompt->isVisible())return;
    clock_->cancelEditing();
    performance_->stopPerformance();performance_->setLibraryBusy(true);
    pausePreview();busy_=true;import_->setEnabled(false);updateEditActions();workspace_->setEnabled(false);editorPanel_->setEnabled(false);progress_->show();cancelButton_->show();
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
void MainWindow::promptImportConflicts(int firstImported){
    std::vector<std::shared_ptr<Song>> affected;int conflicts=0,notes=0;
    QStringList names;
    for(int i=firstImported;i<static_cast<int>(sessions_.size());++i){
        const auto& s=sessions_[i];if(!s.result->conflicts)continue;
        affected.push_back(s.song);conflicts+=s.result->conflicts;
        notes+=static_cast<int>(resolveSameKeyConflicts(*s.result,s.settings,false).size());
        names<<QFileInfo(s.path).fileName();
    }
    if(affected.empty())return;
    auto* box=new QMessageBox(QMessageBox::Warning,"导入曲目存在同键冲突",
        QString("本次导入的 %1 首曲目存在 %2 处同键过密冲突。\n按当前按下时长与松开间隔，需要处理 %3 个音符。\n\n跳过：保留在“已跳过”行，不参与试听、演奏或导出。\n删除：从当前曲谱移除，可撤销。\n保留：维持原样，消除冲突后才能试听或演奏。\n\n跳过和删除均可通过撤销恢复；原 MIDI 文件不会修改。").arg(affected.size()).arg(conflicts).arg(notes),QMessageBox::NoButton,roll_->window());
    box->setObjectName("importConflictDialog");box->setAttribute(Qt::WA_DeleteOnClose);box->setTextFormat(Qt::PlainText);
    box->setDetailedText(names.join('\n'));box->setWindowModality(Qt::WindowModal);
    Theme::setStyle(box,"QMessageBox {background:#eef3f5;} QLabel {color:#203d4e;}");
    auto* skip=box->addButton("跳过冲突音符",QMessageBox::ActionRole);skip->setObjectName("skipImportConflicts");
    auto* remove=box->addButton("删除冲突音符",QMessageBox::DestructiveRole);remove->setObjectName("deleteImportConflicts");
    auto* keep=box->addButton("保留",QMessageBox::RejectRole);keep->setObjectName("keepImportConflicts");
    box->setDefaultButton(keep);box->setEscapeButton(keep);
    connect(box,&QMessageBox::finished,this,[this,box,skip,remove,affected](int){
        const bool deleting=box->clickedButton()==remove;
        if(!deleting&&box->clickedButton()!=skip)return;
        int changed=0;
        for(auto& s:sessions_){
            if(std::find(affected.begin(),affected.end(),s.song)==affected.end())continue;
            const auto edits=resolveSameKeyConflicts(*s.result,s.settings,deleting);
            std::vector<EditChange> changes;
            for(const auto& [source,edit]:edits){
                std::optional<NoteEdit> before;
                if(auto it=s.edits.find(source);it!=s.edits.end())before=it->second;
                changes.push_back({source,before,edit});s.edits[source]=edit;
            }
            if(changes.empty())continue;
            changed+=static_cast<int>(changes.size());s.history.resize(s.historyCursor);s.history.push_back(std::move(changes));
            if(s.history.size()>256)s.history.erase(s.history.begin());s.historyCursor=s.history.size();
            s.result=std::make_shared<Conversion>(convert(*s.song,s.settings,s.edits));
        }
        if(current_>=0)refreshResult(false);
        status_->setText(QString("已%1 %2 个冲突音符，可在对应曲目中撤销。").arg(deleting?"删除":"跳过").arg(changed));
    });
    box->open();
}
void MainWindow::selectSong(int index) {
    if(busy_||index<0||index>=static_cast<int>(sessions_.size()))return;
    clock_->cancelEditing();
    if(practicePage_&&practicePage_->isVisible())leavePractice();
    performance_->stopPerformance();
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
    octaveMode_->setCurrentIndex(settings.autoTranspose?0:1);
    strategy_->setCurrentIndex(settings.nearest?0:1);tempoMode_->setCurrentIndex(settings.fixedTempo?1:0);bpm_->setValue(settings.bpm);speed_->setValue(settings.speed);hold_->setValue(settings.holdMs);gap_->setValue(settings.gapMs);
    updating_=false;settingsPending_=false;dirty_->setText("设置已应用");apply_->setEnabled(true);tabs_->setEnabled(true);
    roll_->setTrackFilter(-1);refreshResult(true);
}
void MainWindow::moveSong(int from,int to){
    if(busy_||from<0||to<0||from>=static_cast<int>(sessions_.size())||to>=static_cast<int>(sessions_.size())||from==to)return;
    performance_->stopPerformance();pausePreview();
    const auto selected=current_>=0?sessions_[current_].song:nullptr;
    const QSignalBlocker blocker(library_);
    if(from<to)std::rotate(sessions_.begin()+from,sessions_.begin()+from+1,sessions_.begin()+to+1);
    else std::rotate(sessions_.begin()+to,sessions_.begin()+from,sessions_.begin()+from+1);
    auto* item=library_->takeItem(from);library_->insertItem(to,item);
    for(int i=0;i<static_cast<int>(sessions_.size());++i)if(sessions_[i].song==selected){current_=i;break;}
    library_->setCurrentRow(current_); // Reorder keeps the same session, edits and playhead.
    performance_->stopPerformance(); // Rebuild navigation history with the new row indexes.
}
void MainWindow::removeSong(int index){
    if(busy_||index<0||index>=static_cast<int>(sessions_.size()))return;
    clock_->cancelEditing();
    performance_->stopPerformance();pausePreview();
    const bool removedCurrent=index==current_;
    {const QSignalBlocker blocker(library_);
        sessions_.erase(sessions_.begin()+index);delete library_->takeItem(index);
        if(removedCurrent)current_=-1;else if(current_>index)--current_;
        library_->setCurrentRow(removedCurrent?std::min(index,static_cast<int>(sessions_.size())-1):current_);
    }
    if(sessions_.empty())clearSong();
    else if(removedCurrent)selectSong(library_->currentRow());
    else performance_->stopPerformance();
}
void MainWindow::clearSong(){
    clock_->cancelEditing();
    current_=-1;updating_=true;settingsPending_=false;position_=0;
    {const QSignalBlocker blocker(performance_);performance_->setSong({}, {}, {}, {});}
    tracks_->clear();filter_->clear();filter_->addItem("全部音轨",-1);
    deleteMode_->setChecked(false);addNote_->setChecked(false);roll_->setMusic({},{});roll_->setTrackFilter(-1);roll_->setPlayhead(0);
    songTitle_->setText("尚未选择歌曲");Theme::setRichText(subtitle_,"请导入 MIDI 文件");summary_->clear();details_->clear();
    octaveInfo_->setText("导入后显示整体移调与八度折叠");dirty_->setText("设置已应用");
    play_->setEnabled(false);stop_->setEnabled(false);apply_->setEnabled(false);tabs_->setEnabled(true);
    status_->setText("曲目库为空，请导入 MIDI。");updating_=false;refreshRange();updateEditActions();refreshClock();
}
void MainWindow::markDirty() {if(!updating_&&current_>=0){settingsPending_=true;dirty_->setText("有未应用设置 · 点击下方应用");}}
void MainWindow::showWarning(const QString& title,const QString& message) {
    if(mini_&&mini_->isVisible())restoreMainWindow();
    status_->setText(message.section('\n',0,0));
    if(auto* existing=findChild<QMessageBox*>("validationWarning");existing&&existing->isVisible()){existing->raise();return;}
    auto* box=new QMessageBox(QMessageBox::Warning,title,message,QMessageBox::Ok,roll_->window());
    box->setObjectName("validationWarning");box->setAttribute(Qt::WA_DeleteOnClose);box->setTextFormat(Qt::PlainText);
    Theme::setStyle(box,"QMessageBox {background:#eef3f5;} QLabel {color:#203d4e;}");
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
    s.autoTranspose=octaveMode_->currentIndex()==0;
    s.nearest=strategy_->currentIndex()==0;s.fixedTempo=tempoMode_->currentIndex()==1;s.bpm=bpm_->value();s.speed=speed_->value();s.holdMs=hold_->value();s.gapMs=gap_->value();
    settingsPending_=false;dirty_->setText("设置已应用");recalculate();
}
void MainWindow::recalculate(bool fit) {
    if(current_<0)return;pausePreview(true);auto& s=sessions_[current_];
    s.result=std::make_shared<Conversion>(convert(*s.song,s.settings,s.edits));refreshResult(fit);
}
void MainWindow::refreshResult(bool fit) {
    const auto& s=sessions_[current_];const auto& r=*s.result;
    if(auto* item=library_->item(current_))item->setData(Qt::UserRole,r.duration);
    const QSignalBlocker positionBlocker(performance_);
    performance_->setSong(s.song,s.result,s.path,s.settings);
    std::vector<int> added(s.song->tracks.size());int originalCount=0,addedCount=0;
    for(size_t i=0;i<s.song->notes.size();++i) {
        const auto& n=s.song->notes[i];
        if(!n.added){if(!n.derived)++originalCount;continue;}
        if(auto it=s.edits.find(static_cast<int>(i));it!=s.edits.end()&&!it->second.deleted){++added[n.track];++addedCount;}
    }
    Theme::setRichText(subtitle_,QString("SMF %1 / %2 PPQ · %3 个音轨 · <span style='color:#516c7c'>%4 个原始音符</span>"
        " · <span style='color:#526fa8'>新增 %5</span>"
        " · <span style='color:#178e80'>同音名 %6</span>"
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
    octaveInfo_->setText(QString("已应用：%1，整体 %2 半音\n同音名映射 %3（其中八度折叠 %4），近似替代 %5。")
        .arg(s.settings.autoTranspose?"自动移调":"不整体移调").arg(r.transpose).arg(r.exact).arg(r.octaveFolded).arg(r.approximate));
    auto selected=roll_->selectedSources();roll_->setMusic(s.song,s.result);roll_->selectSources(selected);if(fit)roll_->fitAll();
    summary_->setText(QString("映射 %1 · 手改 %2 · 删除 %3 · 和弦 %4 · 排除 %5").arg(r.exact+r.approximate+r.edited).arg(r.edited).arg(r.deleted).arg(r.chords).arg(r.excluded));
    play_->setEnabled(r.exact+r.approximate+r.edited>0);stop_->setEnabled(r.exact+r.approximate+r.edited>0);
    play_->setToolTip(r.conflicts>0?QString("存在 %1 处同键冲突，需消除后才能试听").arg(r.conflicts):"试听播放 / 暂停（"+Preferences::instance().shortcutText(ShortcutAction::Preview)+"）");
    details_->setText("点击轨道上的音符，查看原始音高、转换结果与时间。");
    if(s.song->notes.empty())status_->setText("文件中没有有效音符，仅包含空轨道或元事件。");
    else status_->setText(QString("%1 次同键重合已合并 · %2 条导入提示 · 编辑即时生效，可撤销；重新转换保留手工修改").arg(r.merged).arg(s.song->warnings.size()));
    if(roll_->selectedSource()>=0)showNote(roll_->selectedSource());
    refreshRange();updateEditActions();refreshClock();
}
void MainWindow::setAllTracks(bool enabled) {
    if(performance_->active())return;
    if(current_<0)return;updating_=true;auto& s=sessions_[current_].settings;
    std::fill(s.enabled.begin(),s.enabled.end(),enabled);std::fill(s.solo.begin(),s.solo.end(),false);
    for(int i=0;i<tracks_->topLevelItemCount();++i){tracks_->topLevelItem(i)->setCheckState(0,enabled?Qt::Checked:Qt::Unchecked);tracks_->topLevelItem(i)->setCheckState(1,Qt::Unchecked);}
    updating_=false;recalculate();
}
void MainWindow::togglePlayback() {
    if(busy_)return;
    if(performance_->active())performance_->stopPerformance();
    if(timer_.isActive()){pausePreview();return;}
    startPreview();
}
void MainWindow::startPreview() {
    const auto* r=currentResult();if(!r||!play_->isEnabled())return;
    if(!validateParameters())return;
    if(settingsPending_){showWarning("请先应用参数","转换参数已修改，请先点击“应用设置 · 重新转换”，检查更新后的同键冲突后再试听。");return;}
    const auto [first,last]=selectedRange();if(last<=first)return;
    auto segment=playbackRange(*r,first,last,sessions_[current_].settings);
    if(segment.conflicts>0) {
        showWarning("存在同键冲突，无法试听",QString("所选区间有 %1 处同键过密冲突，已阻止音频播放。\n\n请通过转换报告定位冲突，降低播放倍率、缩短按下时长/松开后间隔，或移动、删除冲突音符。重新转换并消除冲突后再试听。\n不同键的和弦不会阻止播放。").arg(segment.conflicts));return;
    }
    if(position_<first||position_>=last)position_=first;
    QString error;
    if(!audio_.play(*sessions_[current_].song,segment,position_,error,last)){status_->setText(error);return;}
    timer_.start();performance_->setPreviewPlaying(true);deleteMode_->setChecked(false);roll_->setEditingEnabled(false);updateEditActions();
    play_->setText("暂停试听");status_->setText("手碟试听中 · 音符编辑已锁定 · "+Preferences::instance().shortcutText(ShortcutAction::Preview)+" 暂停/继续");
}
void MainWindow::showNote(int source) {
    updateEditActions();
    auto& shortcuts=Preferences::instance();
    if(source<0){details_->setText(QString("空白处拖动框选多个音符。拖动中间移动，拖动两端调整时长，%1 / %2 调整音高，%3 删除。").arg(shortcuts.shortcutText(ShortcutAction::PitchUp),shortcuts.shortcutText(ShortcutAction::PitchDown),shortcuts.shortcutText(ShortcutAction::DeleteNotes)));return;}
    if(roll_->selectedSources().size()>1) {
        details_->setText(QString("已选中 %1 个音符\n\n拖动中间：整体移动时间与音高\n拖动任一两端：批量延长或缩短\n%2 / %3：升降一个九键音级\n%4：批量删除\n%5：撤销整次编辑\n\n音高限于 B（A2）～U（E4），整组到边界即停止。\n\nCtrl 点击增减选择，Shift 点击追加；Ctrl/Shift 框选追加。已跳过音符仅参与删除。").arg(roll_->selectedSources().size()).arg(shortcuts.shortcutText(ShortcutAction::PitchUp),shortcuts.shortcutText(ShortcutAction::PitchDown),shortcuts.shortcutText(ShortcutAction::DeleteNotes),shortcuts.shortcutText(ShortcutAction::Undo)));return;
    }
    if(current_<0)return;const auto& s=sessions_[current_];if(source>=static_cast<int>(s.song->notes.size()))return;
    const auto& n=s.song->notes[source];const auto& m=s.result->notes[source];
    QString target=m.target<0?"未映射":QString("%1  /  %2").arg(QChar(keys[m.target])).arg(noteName(pitches[m.target]));
    QString reason=m.mapping==Mapping::Edited?(n.added?"手工新增（重新转换会保留）":"手工编辑（重新转换会保留）"):m.mapping==Mapping::Deleted?"已手工删除（可撤销）":m.mapping==Mapping::Exact?(pitches[m.target]!=n.pitch+s.result->transpose?"同音名八度折叠":s.result->transpose?"整体移调后准确映射":"原样保留"):m.mapping==Mapping::Approximate?"无同音名，替代为最近音":m.mapping==Mapping::Skipped?"无同音名，按当前策略跳过":"音轨未参与转换";
    if(m.mapping==Mapping::ConflictSkipped)reason="导入时跳过同键冲突（可撤销，重新转换会保留）";
    if(m.mapping==Mapping::Exact||m.mapping==Mapping::Approximate||m.mapping==Mapping::Skipped)
        reason+=QString("\n整体移调：%1 半音").arg(s.result->transpose);
    if(m.mapping==Mapping::Exact&&pitches[m.target]!=n.pitch+s.result->transpose)
        reason+=QString("\n移调后再折叠：%1 半音").arg(pitches[m.target]-n.pitch-s.result->transpose);
    details_->setText(QString("原始音高\n%1  ·  MIDI %2\n\n目标按键 / 音高\n%3\n\n转换结果\n%4\n\n开始时间     %5 s\n持续时间     %6 s\n原始力度     %7\n\n音轨 %8 / 通道 %9%10")
        .arg(noteName(n.pitch)).arg(n.pitch).arg(target,reason).arg(m.start,0,'f',3).arg(m.duration,0,'f',3).arg(n.velocity)
        .arg(s.song->tracks[n.track].source+1).arg(s.song->tracks[n.track].channel+1)
        .arg(m.target>=0?QString("\n音高偏移     %1 半音%2").arg(pitches[m.target]-n.pitch).arg(m.conflict?"\n\n⚠ 同键重复触发过密":""):""));
    tabs_->setCurrentIndex(1);
}
void MainWindow::showDiagnostics() {
    if(current_<0)return;auto& s=sessions_[current_];
    auto* dialog=new QDialog(roll_->window());dialog->setObjectName("conversionReport");dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("转换报告");dialog->resize(900,560);
    Theme::setStyle(dialog,"QDialog#conversionReport {background:#eef3f5;} QDialog#conversionReport QLabel {color:#203d4e;} QTableWidget {background:white; color:#203d4e; alternate-background-color:#f5f9fa; selection-background-color:#d9eeea; selection-color:#203d4e;}");
    auto* layout=new QVBoxLayout(dialog);
    QString warnings;for(const auto& warning:s.song->warnings)warnings+=QString::fromUtf8(warning)+"\n";
    warnings.prepend(QString("整体移调：%1 半音；同音名映射 %2，其中八度折叠 %3（不计入近似替代）。\n").arg(s.result->transpose).arg(s.result->exact).arg(s.result->octaveFolded));
    auto* summary=new QLabel(QString("同音名 %1 · 近似 %2 · 跳过 %3 · 排除 %4 · 同键冲突 %5\n手工编辑 %6 · 手工删除 %7\n%8\n全部音符按开始时间排列；每页显示 200 条。\n双击记录定位音符（已删除或已排除音符仅显示详情）。")
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
            QString state=m.mapping==Mapping::Deleted?"手工删除":m.mapping==Mapping::Excluded?"音轨已排除":m.mapping==Mapping::Skipped?"无同音名，跳过":m.mapping==Mapping::Edited?"手工编辑":m.mapping==Mapping::Exact?(pitches[m.target]!=n.pitch+result->transpose?"同音名 · 八度折叠":result->transpose?"整体移调 · 准确":"原音准确映射"):"无同音名 · 近似替代";
            if(m.mapping==Mapping::ConflictSkipped)state="同键冲突，已跳过";
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
void MainWindow::exportCurrentMidi(){
    if(current_<0||busy_||performance_->active())return;
    pausePreview();
    if(!validateParameters())return;
    if(settingsPending_){showWarning("请先应用参数","转换参数已修改，请先应用设置，再导出当前曲目的 MIDI。");return;}
    const auto& session=sessions_[current_];
    std::string bytes;
    try{bytes=exportMidi(*session.song,*session.result,session.settings);}
    catch(const std::exception& e){showWarning("导出失败",QString::fromUtf8(e.what()));return;}
    const QFileInfo source(session.path);
    QFileDialog dialog(roll_->window(),"导出当前曲目 MIDI",source.absolutePath(),"MIDI 文件 (*.mid *.midi)");
    dialog.setObjectName("exportMidiDialog");dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);dialog.setDefaultSuffix("mid");
    dialog.selectFile(source.completeBaseName()+"_九键.mid");
    if(dialog.exec()!=QDialog::Accepted||dialog.selectedFiles().isEmpty())return;
    const auto path=dialog.selectedFiles().front();QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes.data(),static_cast<qint64>(bytes.size()))!=static_cast<qint64>(bytes.size())||!file.commit()){
        showWarning("导出失败","无法保存 MIDI："+file.errorString());return;
    }
    status_->setText("已导出当前曲目 MIDI："+QFileInfo(path).fileName());status_->setToolTip(path);
}
bool MainWindow::currentScoreText(TextScoreFormat format,QString& text){
    if(current_<0||busy_||performance_->active())return false;
    if(!validateParameters())return false;
    if(settingsPending_){showWarning("请先应用参数","转换参数已修改，请先应用设置，再复制或导出当前曲目的文字谱。");return false;}
    const auto& session=sessions_[current_];
    try{
        text=QString::fromUtf8(format==TextScoreFormat::Hand?
            exportHandScore(*session.song,*session.result,session.settings):
            exportKeyScore(*session.song,*session.result,session.settings));
    }catch(const std::exception& e){showWarning("生成文字谱失败",QString::fromUtf8(e.what()));return false;}
    return true;
}
void MainWindow::copyCurrentScore(TextScoreFormat format){
    QString text;if(!currentScoreText(format,text))return;
    QApplication::clipboard()->setText(text);
    status_->setText(QString("已复制%1：%2 · 可直接粘贴。")
        .arg(format==TextScoreFormat::Hand?"手弹谱":"键谱",QFileInfo(sessions_[current_].path).completeBaseName()));status_->setToolTip({});
}
void MainWindow::exportCurrentScore(TextScoreFormat format){
    QString text;if(!currentScoreText(format,text))return;
    const QString name=format==TextScoreFormat::Hand?"手弹谱":"键谱";
    const QFileInfo source(sessions_[current_].path);
    QFileDialog dialog(roll_->window(),"导出当前曲目"+name,source.absolutePath(),"文本文件 (*.txt)");
    dialog.setObjectName(format==TextScoreFormat::Hand?"exportHandScoreDialog":"exportKeyScoreDialog");
    dialog.setAcceptMode(QFileDialog::AcceptSave);dialog.setFileMode(QFileDialog::AnyFile);dialog.setDefaultSuffix("txt");
    dialog.selectFile(source.completeBaseName()+"_"+name+".txt");
    if(dialog.exec()!=QDialog::Accepted||dialog.selectedFiles().isEmpty())return;
    const auto path=dialog.selectedFiles().front();const auto bytes=text.toUtf8();QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()){
        showWarning("导出失败","无法保存 "+name+"："+file.errorString());return;
    }
    status_->setText("已导出"+name+" TXT："+QFileInfo(path).fileName());status_->setToolTip(path);
}
void MainWindow::pausePreview(bool reset) {
    bool wasPlaying=timer_.isActive();timer_.stop();audio_.pause();play_->setText("手碟试听");
    if(performance_)performance_->setPreviewPlaying(false);
    roll_->setEditingEnabled(!performance_||!performance_->active());updateEditActions();
    if(wasPlaying&&currentResult()){position_=std::min(selectedRange().second,audio_.position());roll_->setPlayhead(position_);status_->setText(audio_.finished()?"试听结束。":"试听已暂停，再次点击可从当前位置继续。");}
    if(reset){position_=selectedRange().first;roll_->setPlayhead(position_);}refreshClock();
}
void MainWindow::refreshClock() {
    clock_->setPosition(position_,currentResult()?currentResult()->duration:0);
    if(performance_){const QSignalBlocker blocker(performance_);performance_->setPreviewPosition(position_);}
}
void MainWindow::seekPreviewTime(double seconds) {
    if(!currentResult()||busy_||roll_->isEditing()||!std::isfinite(seconds))return;
    if(performance_->active()){
        performance_->beginSeek();performance_->seekPerformance(seconds);return;
    }
    const bool resume=timer_.isActive();pausePreview();
    const auto [first,last]=selectedRange();position_=std::clamp(seconds,first,last);
    roll_->setPlayhead(position_,true);refreshClock();
    if(resume&&position_<last)startPreview();
}
void MainWindow::addNote(double start,int target) {
    if(performance_->active())return;
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
    if(performance_->active())return;
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
    if(performance_->active())return;
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
            if(!edit.deleted&&!edit.skipped&&current.target>=0&&current.startTick==edit.startTick&&current.endTick==edit.endTick&&current.target==edit.target)continue;
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
    if(performance_->active())return;
    if(current_<0||busy_||timer_.isActive()||roll_->isEditing())return;auto& s=sessions_[current_];
    if((redo&&s.historyCursor==s.history.size())||(!redo&&s.historyCursor==0))return;
    const auto& entry=s.history[redo?s.historyCursor++:--s.historyCursor];
    if(entry.before){
        const auto& state=redo?*entry.after:*entry.before;
        // Later undone additions retain their source IDs for a subsequent redo.
        auto restored=state.song;
        if(s.song->notes.size()>restored.notes.size()){
            const auto oldSize=restored.notes.size();restored.notes.insert(restored.notes.end(),s.song->notes.begin()+oldSize,s.song->notes.end());
            for(size_t i=oldSize;i<restored.notes.size();++i)restored.notes[i].added=true;
        }
        if(s.song->tracks.size()>restored.tracks.size())restored.tracks.insert(restored.tracks.end(),s.song->tracks.begin()+restored.tracks.size(),s.song->tracks.end());
        *s.song=std::move(restored);s.edits=state.edits;s.rangeFirst=state.first;s.rangeLast=state.last;recalculate();return;
    }
    const auto& changes=entry.changes;
    std::set<int> selected;
    for(const auto& change:changes) {
        const auto& edit=redo?change.after:change.before;
        if(edit)s.edits[change.source]=*edit;else s.edits.erase(change.source);
        selected.insert(change.source);
    }
    recalculate();roll_->selectSources(selected,true);showNote(roll_->selectedSource());
}
void MainWindow::updateEditActions() {
    bool hasSong=current_>=0, editable=hasSong&&!timer_.isActive()&&(!performance_||!performance_->active());
    exportMidi_->setEnabled(hasSong&&!busy_&&(!performance_||!performance_->active()));
    copyHandScore_->setEnabled(exportMidi_->isEnabled());
    copyKeyScore_->setEnabled(exportMidi_->isEnabled());
    practiceButton_->setEnabled(hasSong&&!busy_&&(!performance_||!performance_->active()));
    clock_->setEnabled(hasSong&&!busy_&&!roll_->isEditing());
    resetRange_->setEnabled(editable&&!busy_);
    deleteRange_->setEnabled(editable&&!busy_&&selectedRange().second>selectedRange().first);
    createRange_->setEnabled(deleteRange_->isEnabled());
    addNote_->setEnabled(editable);
    deleteMode_->setEnabled(editable);deleteNote_->setEnabled(editable&&roll_->selectedSource()>=0);
    deleteNote_->setText(roll_->selectedSources().size()>1?QString("删除 (%1)").arg(roll_->selectedSources().size()):"删除");
    deleteNote_->setMinimumWidth(deleteNote_->fontMetrics().horizontalAdvance(deleteNote_->text())+24);
    editorPanel_->setMinimumWidth(std::max(490,editorPanel_->layout()->minimumSize().width()));
    setMinimumWidth(std::max(1120,centralWidget()->minimumSizeHint().width()));
    undo_->setEnabled(editable&&sessions_[current_].historyCursor>0);
    redo_->setEnabled(editable&&sessions_[current_].historyCursor<sessions_[current_].history.size());
}
std::pair<double,double> MainWindow::selectedRange() const {
    if(current_<0)return {0,0};const auto& s=sessions_[current_];const double duration=s.result?s.result->duration:0;
    const double first=std::clamp(secondsAtTick(*s.song,s.settings,s.rangeFirst),0.0,duration);
    const double last=s.rangeLast<0?duration:std::clamp(secondsAtTick(*s.song,s.settings,s.rangeLast),first,duration);
    return {first,last};
}
void MainWindow::refreshRange(){
    const auto [first,last]=selectedRange();
    roll_->setPlaybackRange(first,last);performance_->setPlaybackRange(first,last);
    if(position_<first||position_>last){position_=first;roll_->setPlayhead(position_);}
    refreshClock();updateEditActions();
}
void MainWindow::changeRange(double start,double end){
    if(current_<0||busy_||performance_->active())return;pausePreview();auto& s=sessions_[current_];
    int first=tickAtSeconds(*s.song,s.settings,start),last=tickAtSeconds(*s.song,s.settings,end);
    if(last<=first){refreshRange();return;}
    s.rangeFirst=first;s.rangeLast=end>=s.result->duration-1e-9?-1:last;
    position_=selectedRange().first;roll_->setPlayhead(position_,true);refreshRange();
}
void MainWindow::moveRangeBoundaryToPlayhead(bool left,double seconds){
    if(current_<0||busy_||performance_->active()||roll_->isEditing()||!std::isfinite(seconds))return;
    auto& s=sessions_[current_];if(!s.result||s.result->duration<=0)return;
    const auto [first,last]=selectedRange();
    const int tick=tickAtSeconds(*s.song,s.settings,std::clamp(seconds,0.0,s.result->duration));
    const int firstTick=tickAtSeconds(*s.song,s.settings,first),lastTick=tickAtSeconds(*s.song,s.settings,last);
    if((left&&tick>=lastTick)||(!left&&tick<=firstTick)){
        status_->setText(left?"左边界需早于右边界，请先调整右边界或重置区间。":"右边界需晚于左边界，请先调整左边界或重置区间。");return;
    }
    pausePreview();
    if(left)s.rangeFirst=tick;else s.rangeLast=tick>=tickAtSeconds(*s.song,s.settings,s.result->duration)?-1:tick;
    // Keep the blue marker at the chosen boundary after quantizing to MIDI ticks.
    position_=std::clamp(secondsAtTick(*s.song,s.settings,tick),0.0,s.result->duration);roll_->setPlayhead(position_,true);refreshRange();
    status_->setText(QString("%1边界已移至蓝色播放标 · %2 s").arg(left?"左":"右").arg(position_,0,'f',3));
}
void MainWindow::deleteRange(){
    if(current_<0||busy_||timer_.isActive()||performance_->active()||roll_->isEditing())return;
    auto& s=sessions_[current_];const auto [first,last]=selectedRange();
    const int a=tickAtSeconds(*s.song,s.settings,first),b=tickAtSeconds(*s.song,s.settings,last);if(b<=a)return;
    TimelineState before{*s.song,s.edits,s.rangeFirst,s.rangeLast};
    deleteTimeRange(*s.song,s.edits,a,b);
    s.result=std::make_shared<Conversion>(convert(*s.song,s.settings,s.edits));
    // Keep the handles at their displayed times, including across a tempo splice.
    // Only clamp when the shortened song can no longer contain the old interval.
    s.rangeFirst=tickAtSeconds(*s.song,s.settings,std::min(first,s.result->duration));
    s.rangeLast=s.result->duration>0?tickAtSeconds(*s.song,s.settings,std::min(last,s.result->duration)):-1;
    TimelineState after{*s.song,s.edits,s.rangeFirst,s.rangeLast};
    s.history.resize(s.historyCursor);s.history.emplace_back(std::move(before),std::move(after));
    if(s.history.size()>256)s.history.erase(s.history.begin());s.historyCursor=s.history.size();
    pausePreview(true);refreshResult();position_=std::min(first,s.result->duration);roll_->setPlayhead(position_);refreshClock();
    status_->setText(QString("已删除 %1 秒片段，后续音符与变速点已前移；保留区间位置，超出曲尾时收回，可撤销。").arg(last-first,0,'f',2));
}
void MainWindow::createRange(){
    if(current_<0||busy_||timer_.isActive()||performance_->active()||roll_->isEditing())return;
    auto& s=sessions_[current_];const auto [first,last]=selectedRange();
    const int a=tickAtSeconds(*s.song,s.settings,first),b=tickAtSeconds(*s.song,s.settings,last);if(b<=a)return;
    TimelineState before{*s.song,s.edits,s.rangeFirst,s.rangeLast},after=before;
    try{insertBlankRange(after.song,after.edits,a,b);}catch(const std::exception& error){showWarning("无法创建空片段",QString::fromUtf8(error.what()));return;}
    after.first=a;after.last=b;*s.song=after.song;s.edits=after.edits;s.rangeFirst=a;s.rangeLast=b;
    s.history.resize(s.historyCursor);s.history.emplace_back(std::move(before),std::move(after));
    if(s.history.size()>256)s.history.erase(s.history.begin());s.historyCursor=s.history.size();
    recalculate();position_=first;roll_->setPlayhead(first,true);refreshClock();
    status_->setText(QString("已插入 %1 秒空片段，后续音符已后移；绿色区间覆盖新空白，可撤销。").arg(last-first,0,'f',2));
}
void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if(!busy_&&e->mimeData()->hasUrls())for(const auto& u:e->mimeData()->urls())if(u.isLocalFile()){e->acceptProposedAction();break;}
}
void MainWindow::dropEvent(QDropEvent* e) {
    QStringList paths;for(const auto& u:e->mimeData()->urls())if(u.isLocalFile())paths<<u.toLocalFile();
    importFiles(paths);e->acceptProposedAction();
}
}
