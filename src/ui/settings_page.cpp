#include "settings_page.h"
#include "handpan_test.h"
#include "theme.h"
#include "appearance_settings.h"
#include "about_dialog.h"
#include "app/app_log.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <exception>
#include <initializer_list>
namespace rock {
namespace {
QLabel* text(const QString& value,const char* role){auto* label=new QLabel(value);label->setProperty("role",role);label->setTextFormat(Qt::PlainText);label->setWordWrap(true);return label;}
QFrame* card(QVBoxLayout* layout,const char* name="panel"){auto* frame=new QFrame;frame->setObjectName(name);frame->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);layout->addWidget(frame);return frame;}
enum class AppIcon {Appearance,Shortcuts,Driver,Handpan,Logs,About};
QPixmap appIcon(AppIcon kind){
    QPixmap image(96,96);image.fill(Qt::transparent);QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);
    const std::array<QColor,6> colors{QColor("#5488ce"),QColor("#8972c9"),QColor("#399b91"),QColor("#c4944a"),QColor("#637c9b"),QColor("#b16f86")};
    const auto color=colors[static_cast<size_t>(kind)];
    painter.setPen(Qt::NoPen);painter.setBrush(QColor(20,40,60,18));painter.drawRoundedRect(QRectF(8,10,80,80),21,21);
    QLinearGradient gradient(10,6,86,88);gradient.setColorAt(0,color.lighter(119));gradient.setColorAt(1,color.darker(108));
    painter.setBrush(gradient);painter.drawRoundedRect(QRectF(8,6,80,80),21,21);
    painter.setPen(QPen(Qt::white,3.4,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));painter.setBrush(Qt::NoBrush);
    switch(kind){
    case AppIcon::Appearance:{
        painter.drawEllipse(QRectF(26,24,44,44));
        QPainterPath half;half.moveTo(48,24);half.arcTo(QRectF(26,24,44,44),90,180);half.closeSubpath();painter.fillPath(half,Qt::white);
        painter.drawLine(QPointF(48,16),QPointF(48,18));painter.drawLine(QPointF(48,74),QPointF(48,76));
        painter.drawLine(QPointF(18,46),QPointF(20,46));painter.drawLine(QPointF(76,46),QPointF(78,46));break;
    }
    case AppIcon::Shortcuts:
        painter.drawRoundedRect(QRectF(22,29,52,35),6,6);painter.setPen(Qt::NoPen);painter.setBrush(Qt::white);
        for(int row=0;row<2;++row)for(int col=0;col<5;++col)painter.drawRoundedRect(QRectF(28+col*8,35+row*8,4,4),1,1);
        painter.drawRoundedRect(QRectF(35,53,25,4),1,1);break;
    case AppIcon::Driver:
        painter.drawLine(QPointF(48,67),QPointF(48,26));painter.drawLine(QPointF(48,49),QPointF(32,40));painter.drawLine(QPointF(32,40),QPointF(32,31));
        painter.drawLine(QPointF(48,58),QPointF(64,49));painter.drawLine(QPointF(64,49),QPointF(64,38));
        painter.drawLine(QPointF(42,31),QPointF(48,24));painter.drawLine(QPointF(48,24),QPointF(54,31));
        painter.setBrush(Qt::white);painter.drawEllipse(QPointF(48,68),4,4);painter.drawEllipse(QPointF(32,28),3,3);painter.drawRect(QRectF(61,31,6,6));break;
    case AppIcon::Handpan:
        painter.drawEllipse(QRectF(22,23,52,48));painter.drawEllipse(QRectF(40,39,16,14));
        for(const auto point:{QPointF(33,36),QPointF(48,30),QPointF(63,36),QPointF(65,51),QPointF(55,62),QPointF(39,62),QPointF(29,51)}){
            painter.setBrush(QColor(255,255,255,215));painter.setPen(Qt::NoPen);painter.drawEllipse(point,3,3);
        }break;
    case AppIcon::Logs:
        painter.drawRoundedRect(QRectF(29,23,39,48),5,5);painter.drawLine(QPointF(38,35),QPointF(59,35));painter.drawLine(QPointF(38,44),QPointF(59,44));
        painter.drawLine(QPointF(38,53),QPointF(53,53));painter.drawLine(QPointF(38,62),QPointF(48,62));break;
    case AppIcon::About:
        painter.drawEllipse(QRectF(22,21,52,52));painter.drawLine(QPointF(48,44),QPointF(48,61));painter.drawLine(QPointF(43,61),QPointF(53,61));
        painter.setBrush(Qt::white);painter.drawEllipse(QPointF(48,33),1.7,1.7);break;
    }
    return image;
}
QPushButton* desktopApp(AppIcon icon,const QString& title,const QString& description,const char* name){
    auto* app=new QPushButton;app->setObjectName(name);app->setProperty("settingsApp",true);app->setCursor(Qt::PointingHandCursor);app->setFocusPolicy(Qt::StrongFocus);
    app->setAccessibleName(title);app->setAccessibleDescription(description);app->setToolTip(description);app->setMinimumSize(152,210);app->setMaximumWidth(216);
    auto* layout=new QVBoxLayout(app);layout->setContentsMargins(10,18,10,18);layout->setSpacing(10);
    auto* image=new QLabel;image->setPixmap(appIcon(icon).scaled(84,84,Qt::KeepAspectRatio,Qt::SmoothTransformation));image->setAlignment(Qt::AlignCenter);layout->addWidget(image);
    auto* heading=text(title,"settingsTitle");heading->setAlignment(Qt::AlignCenter);layout->addWidget(heading);
    auto* detail=text(description,"muted");detail->setAlignment(Qt::AlignHCenter|Qt::AlignTop);detail->setMinimumHeight(36);layout->addWidget(detail);layout->addStretch();
    for(auto* label:app->findChildren<QLabel*>())label->setAttribute(Qt::WA_TransparentForMouseEvents);
    Theme::setStyle(app,"QPushButton[settingsApp=true] {background:transparent;border:1px solid transparent;border-radius:16px;padding:0;} QPushButton[settingsApp=true]:hover {background:#ffffff;border-color:#e0e9ed;} QPushButton[settingsApp=true]:focus {background:#ffffff;border:2px solid #178e80;} QPushButton[settingsApp=true]:pressed {background:#e5f3ef;border-color:#8ac8bb;} QPushButton[settingsApp=true]:disabled {background:transparent;border-color:transparent;} QPushButton[settingsApp=true] QLabel:disabled {color:#afbdc5;}");
    return app;
}
void logInterceptionResult(const InterceptionStatus& result){
    QString status;
    switch(result.state){
    case InterceptionState::Ready:status=QString("驱动可用，%1 个键盘").arg(result.keyboardCount);break;
    case InterceptionState::NoKeyboard:status="已连接，未找到键盘";break;
    case InterceptionState::RuntimeUnavailable:status="检测组件不可用";break;
    case InterceptionState::DriverUnavailable:status="未连接到驱动";break;
    case InterceptionState::Unsupported:status="当前系统不支持";break;
    }
    const auto level=result.state==InterceptionState::Ready?LogLevel::Info:
        result.state==InterceptionState::NoKeyboard||result.state==InterceptionState::Unsupported?LogLevel::Warning:LogLevel::Error;
    AppLog::instance().write(level,"驱动",QString("Interception 检测完成：%1%2").arg(status,
        result.detail.isEmpty()?QString():QString(" · %1").arg(result.detail)));
}
}
SettingsPage::SettingsPage(QWidget* parent):QWidget(parent){
    setObjectName("appSettingsPage");
    setAttribute(Qt::WA_StyledBackground,true);
    auto* outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);
    auto* scroll=new QScrollArea;scroll->setObjectName("appSettingsScroll");scroll->setWidgetResizable(true);outer->addWidget(scroll);
    auto* page=new QWidget;auto* desktop=new QVBoxLayout(page);desktop->setContentsMargins(26,24,26,24);desktop->setSpacing(12);scroll->setWidget(page);
    desktop->addWidget(text("工作台应用","muted"));desktop->addWidget(text("按你的习惯，调整每一次演奏。","title"));
    desktop->addWidget(text("选择一个应用，打开独立窗口进行设置或检查。","muted"));desktop->addSpacing(22);
    auto* apps=new QGridLayout;apps->setObjectName("settingsAppsGrid");apps->setContentsMargins(0,0,0,0);apps->setHorizontalSpacing(20);apps->setVerticalSpacing(14);
    auto* appearanceButton=desktopApp(AppIcon::Appearance,"外观主题","明亮或深色\n随心切换","appearanceSettingsButton");
    shortcutButton_=desktopApp(AppIcon::Shortcuts,"快捷键","编辑组合键\n让操作更顺手","shortcutSettingsButton");
    auto* driverButton=desktopApp(AppIcon::Driver,"驱动连接","检查 Interception\n与键盘连接","driverSettingsButton");
    test_=desktopApp(AppIcon::Handpan,"手碟测试","试听九个音\n查看按键与输入","testKeysButton");
    auto* logsButton=desktopApp(AppIcon::Logs,"运行日志","搜索运行记录\n查看与导出日志","settingsLogsButton");
    auto* aboutButton=desktopApp(AppIcon::About,"关于本程序","版本与功能\n作者与项目主页","aboutSettingsButton");
    const std::array<QPushButton*,6> launchers{appearanceButton,shortcutButton_,driverButton,test_,logsButton,aboutButton};
    for(size_t i=0;i<launchers.size();++i)apps->addWidget(launchers[i],static_cast<int>(i)/3,static_cast<int>(i)%3,Qt::AlignTop|Qt::AlignHCenter);
    for(int column=0;column<3;++column)apps->setColumnStretch(column,1);
    desktop->addLayout(apps);desktop->addStretch(1);desktop->addWidget(text("外观即时生效 · 快捷键保存后生效 · 演奏中暂停连接检测与按键测试","muted"));
    auto createWindow=[this](const char* name,const QString& title,int width,int height){
        auto* window=new QDialog(this);window->setObjectName(name);window->setProperty("settingsDialog",true);window->setWindowTitle(title);
        if(const auto* display=screen()){const auto available=display->availableGeometry();width=std::min(width,std::max(320,available.width()-48));height=std::min(height,std::max(260,available.height()-80));}
        window->setWindowFlag(Qt::WindowContextHelpButtonHint,false);window->setModal(false);window->resize(width,height);return window;
    };
    auto addHeading=[](QVBoxLayout* layout,AppIcon icon,const QString& title,const QString& description){
        auto* heading=new QHBoxLayout;heading->setSpacing(12);auto* image=new QLabel;image->setPixmap(appIcon(icon).scaled(48,48,Qt::KeepAspectRatio,Qt::SmoothTransformation));heading->addWidget(image,0,Qt::AlignTop);
        auto* words=new QVBoxLayout;words->setSpacing(5);words->addWidget(text(title,"settingsTitle"));words->addWidget(text(description,"muted"));heading->addLayout(words,1);layout->addLayout(heading);
    };
    auto addClose=[](QDialog* window,QVBoxLayout* layout){
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close);buttons->button(QDialogButtonBox::Close)->setText("关闭");connect(buttons,&QDialogButtonBox::rejected,window,&QDialog::close);layout->addWidget(buttons);
    };
    appearanceWindow_=new AppearanceSettingsDialog(this);
    connect(appearanceButton,&QPushButton::clicked,this,[this]{openSettingsWindow(appearanceWindow_);});
    connect(aboutButton,&QPushButton::clicked,this,[this]{if(!aboutWindow_)aboutWindow_=new AboutDialog(this);openSettingsWindow(aboutWindow_);});
    driverWindow_=createWindow("driverSettingsDialog","驱动连接",530,330);driverWindow_->setMinimumSize(460,300);
    auto* driver=new QVBoxLayout(driverWindow_);driver->setContentsMargins(22,22,22,18);driver->setSpacing(18);addHeading(driver,AppIcon::Driver,"Interception 驱动","检查驱动连接与可用键盘，演奏前确认输入就绪。");
    auto* driverCard=new QVBoxLayout(card(driver,"settingsDriverCard"));driverCard->setContentsMargins(16,16,16,16);driverCard->setSpacing(12);
    Theme::setStyle(driverCard->parentWidget(),"QFrame#settingsDriverCard {background:#ffffff;border:1px solid #e0e9ed;border-radius:10px;}");
    auto* driverHeading=new QHBoxLayout;driverStatus_=new QLabel("尚未检测");driverStatus_->setObjectName("interceptionStatus");driverStatus_->setTextFormat(Qt::PlainText);driverHeading->addWidget(driverStatus_);driverHeading->addStretch();
    refreshDriver_=new QPushButton("重新检测");refreshDriver_->setObjectName("refreshInterceptionButton");driverHeading->addWidget(refreshDriver_);driverCard->addLayout(driverHeading);
    driverDetail_=text("打开此应用后检测驱动连接与可用键盘。","muted");driverDetail_->setObjectName("interceptionDetail");driverCard->addWidget(driverDetail_);driver->addStretch();addClose(driverWindow_,driver);
    connect(driverButton,&QPushButton::clicked,this,[this]{const bool visible=driverWindow_->isVisible();openSettingsWindow(driverWindow_);if(!visible)refreshInterception();});
    connect(refreshDriver_,&QPushButton::clicked,this,&SettingsPage::refreshInterception);
    connect(&driverWatcher_,&QFutureWatcher<InterceptionStatus>::finished,this,[this]{
        const auto result=driverWatcher_.result();logInterceptionResult(result);emit driverInspectionActiveChanged(false);
        if(active_){driverRefreshPending_=false;showInterceptionPaused();return;}
        if(keyboardDiscoveryActive_){driverRefreshPending_=driverWindow_->isVisible();showInterceptionWaiting();return;}
        if(driverRefreshPending_&&driverWindow_->isVisible()){refreshInterception();return;}
        driverRefreshPending_=false;showInterceptionStatus(result);
    });
    shortcutWindow_=createWindow("shortcutSettingsDialog","快捷键",680,740);shortcutWindow_->setMinimumSize(560,500);
    auto* shortcuts=new QVBoxLayout(shortcutWindow_);shortcuts->setContentsMargins(20,20,20,18);shortcuts->setSpacing(14);addHeading(shortcuts,AppIcon::Shortcuts,"快捷键","点击输入框后按下新组合；清空可停用。修改后点击保存。");
    auto* shortcutScroll=new QScrollArea;shortcutScroll->setObjectName("shortcutSettingsScroll");shortcutScroll->setWidgetResizable(true);shortcutScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);shortcuts->addWidget(shortcutScroll,1);
    shortcutForm_=new QWidget;shortcutForm_->setObjectName("shortcutForm");auto* sections=new QVBoxLayout(shortcutForm_);sections->setContentsMargins(0,0,8,0);sections->setSpacing(12);shortcutScroll->setWidget(shortcutForm_);
    auto addGroup=[this,sections](const char* name,const QString& title,const QString& scope,std::initializer_list<ShortcutAction> actions,const QString& help={}){
        auto* group=new QFrame;group->setObjectName(name);group->setProperty("shortcutGroup",true);group->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);
        auto* column=new QVBoxLayout(group);column->setContentsMargins(16,16,16,16);column->setSpacing(13);column->addWidget(text(title,"section"));column->addWidget(text(scope,"muted"));
        auto* form=new QGridLayout;form->setContentsMargins(0,0,0,0);form->setHorizontalSpacing(16);form->setVerticalSpacing(12);form->setColumnStretch(0,1);int row=0;
        for(const auto action:actions){
            const auto index=static_cast<size_t>(action);const auto& definition=shortcutDefinitions()[index];
            auto* labelBox=new QVBoxLayout;labelBox->setSpacing(3);labelBox->addWidget(text(QString::fromUtf8(definition.name),"shortcutName"));labelBox->addWidget(text(QString("默认 %1").arg(QKeySequence(QString::fromUtf8(definition.defaultKey)).toString(QKeySequence::NativeText)),"muted"));form->addLayout(labelBox,row,0);
            auto* edit=new QKeySequenceEdit;edit->setObjectName("shortcut_"+QString::fromUtf8(definition.id));edit->setAccessibleName(QString::fromUtf8(definition.name));edit->setMaximumSequenceLength(1);edit->setClearButtonEnabled(true);edit->setMinimumWidth(180);edit->setMaximumWidth(210);edit->setFixedHeight(38);editors_[index]=edit;form->addWidget(edit,row++,1);
            edit->installEventFilter(this);for(auto* input:edit->findChildren<QLineEdit*>())input->installEventFilter(this);connect(edit,&QKeySequenceEdit::keySequenceChanged,this,&SettingsPage::updateDraft);
        }
        column->addLayout(form);if(!help.isEmpty())column->addWidget(text(help,"muted"));sections->addWidget(group);
    };
    addGroup("fileShortcutGroup","文件与工程","在工作台与独立轨道窗口中使用。",{ShortcutAction::ExportMidi,ShortcutAction::SaveProject,ShortcutAction::ExportProject,ShortcutAction::ImportMidi,ShortcutAction::ImportProject});
    addGroup("editingShortcutGroup","试听与音符编辑","音符编辑需轨道焦点；试听不会打断文字输入。",{ShortcutAction::Preview,ShortcutAction::Undo,ShortcutAction::Redo,ShortcutAction::RedoAlternate,ShortcutAction::SelectAll,ShortcutAction::DeleteNotes},"音符复制 Ctrl+C、剪切 Ctrl+X、粘贴 Ctrl+V 为固定操作；Ctrl / Shift 多选、Ctrl + 滚轮缩放保持固定。");
    addGroup("trackShortcutGroup","轨道与区间","音高与区间操作需轨道焦点；全屏适用于独立轨道窗口。",{ShortcutAction::PitchUp,ShortcutAction::PitchDown,ShortcutAction::CancelEdit,ShortcutAction::Fullscreen,ShortcutAction::RangeLeftToPlayhead,ShortcutAction::RangeRightToPlayhead});
    addGroup("globalShortcutGroup","全局演奏","切换到其他窗口后仍可使用。",{ShortcutAction::PerformancePause,ShortcutAction::PerformanceStop,ShortcutAction::PerformancePrevious,ShortcutAction::PerformanceNext,ShortcutAction::MiniMode},"可用 F1～F24，或 Ctrl / Alt 加字母、数字（可加 Shift）。暂停 / 继续、终止仅在演奏会话中生效，且不能为空。");sections->addStretch();
    status_=text({},"muted");status_->setObjectName("shortcutStatus");status_->setMinimumHeight(32);shortcuts->addWidget(status_);
    auto* actions=new QHBoxLayout;actions->setSpacing(8);reset_=new QPushButton("恢复默认");reset_->setObjectName("resetShortcutsButton");cancel_=new QPushButton("取消修改");cancel_->setObjectName("cancelShortcutsButton");save_=new QPushButton("保存快捷键");save_->setObjectName("saveShortcutsButton");save_->setProperty("primary",true);actions->addWidget(reset_);actions->addStretch();actions->addWidget(cancel_);actions->addWidget(save_);shortcuts->addLayout(actions);
    for(auto* button:{reset_,cancel_,save_})button->setAutoDefault(false);
    connect(reset_,&QPushButton::clicked,this,[this]{loadBindings(defaultShortcuts());updateDraft();AppLog::instance().write(LogLevel::Info,"设置","快捷键已恢复默认草稿，等待保存。");});connect(cancel_,&QPushButton::clicked,this,[this]{loadBindings(Preferences::instance().shortcuts());updateDraft();});connect(save_,&QPushButton::clicked,this,&SettingsPage::saveBindings);
    connect(shortcutWindow_,&QDialog::finished,this,[this]{loadBindings(Preferences::instance().shortcuts());updateDraft();});
    connect(shortcutButton_,&QPushButton::clicked,this,[this]{if(active_)return;if(!shortcutWindow_->isVisible()){loadBindings(Preferences::instance().shortcuts());updateDraft();}openSettingsWindow(shortcutWindow_);});
    connect(&Preferences::instance(),&Preferences::shortcutsChanged,this,[this]{if(!shortcutWindow_->isVisible()){loadBindings(Preferences::instance().shortcuts());updateDraft();}});
    connect(test_,&QPushButton::clicked,this,[this]{if(active_)return;if(!testWindow_){testWindow_=new HandpanTestDialog(this);testWindow_->setProperty("settingsDialog",true);testWindow_->setAttribute(Qt::WA_DeleteOnClose);}openSettingsWindow(testWindow_);});connect(logsButton,&QPushButton::clicked,this,&SettingsPage::openLogsRequested);
    loadBindings(Preferences::instance().shortcuts());updateDraft();
}
void SettingsPage::openSettingsWindow(QDialog* window){if(!window)return;if(window->isMinimized())window->showNormal();else window->show();window->raise();window->activateWindow();}
void SettingsPage::closeWindows(){
    driverRefreshPending_=false;
    if(auto* appearance=qobject_cast<AppearanceSettingsDialog*>(appearanceWindow_.data()))appearance->closeWindows();
    for(auto* window:{appearanceWindow_.data(),shortcutWindow_.data(),driverWindow_.data(),testWindow_.data(),aboutWindow_.data()})if(window)window->close();
}
void SettingsPage::hideEvent(QHideEvent* event){QWidget::hideEvent(event);closeWindows();}
bool SettingsPage::eventFilter(QObject* watched,QEvent* event){
    if(event->type()==QEvent::ShortcutOverride||event->type()==QEvent::KeyPress||event->type()==QEvent::KeyRelease){
        auto* key=static_cast<QKeyEvent*>(event);
        if(key->key()==Qt::Key_Escape){
            auto* widget=qobject_cast<QWidget*>(watched);auto* editor=qobject_cast<QKeySequenceEdit*>(widget);if(!editor&&widget)editor=qobject_cast<QKeySequenceEdit*>(widget->parentWidget());
            if(editor&&editor->isEnabled()){if(event->type()==QEvent::KeyPress&&!key->isAutoRepeat())editor->setKeySequence(QKeySequence(key->keyCombination()));key->accept();return true;}
        }
    }
    return QWidget::eventFilter(watched,event);
}
void SettingsPage::loadBindings(const ShortcutBindings& bindings){for(size_t i=0;i<editors_.size();++i){const QSignalBlocker blocker(editors_[i]);editors_[i]->setKeySequence(bindings[i]);}}
void SettingsPage::updateDraft(){if(active_)return;ShortcutBindings draft;for(size_t i=0;i<editors_.size();++i)draft[i]=editors_[i]->keySequence();const bool changed=draft!=Preferences::instance().shortcuts();save_->setEnabled(changed);cancel_->setEnabled(changed);Theme::setStyle(status_,"color:#8397a3;");status_->setText(changed?"有未保存的快捷键修改。":"快捷键已保存。恢复默认后需点击保存。");}
void SettingsPage::saveBindings(){if(active_)return;ShortcutBindings bindings;for(size_t i=0;i<editors_.size();++i)bindings[i]=editors_[i]->keySequence();QString error;if(!Preferences::instance().saveShortcuts(bindings,error)){AppLog::instance().write(LogLevel::Error,"设置",QString("快捷键保存失败：%1").arg(error));Theme::setStyle(status_,"color:#bd4f5b;");status_->setText(error);return;}updateDraft();status_->setText("快捷键已保存并生效。");AppLog::instance().write(LogLevel::Info,"设置","快捷键已保存并生效。");}
void SettingsPage::setPerformanceActive(bool active){
    const bool changed=active_!=active;active_=active;shortcutForm_->setEnabled(!active);reset_->setEnabled(!active);test_->setEnabled(!active);shortcutButton_->setEnabled(!active);
    if(active){save_->setEnabled(false);cancel_->setEnabled(false);status_->setText("演奏会话中暂不修改快捷键，请终止演奏后再设置。主题仍可调整。");if(testWindow_)testWindow_->close();driverRefreshPending_=false;showInterceptionPaused();}
    else{updateDraft();refreshDriver_->setEnabled(!driverWatcher_.isRunning()&&!keyboardDiscoveryActive_);if(changed&&driverWindow_->isVisible())refreshInterception();}
}
void SettingsPage::setKeyboardDiscoveryActive(bool active){
    const bool changed=keyboardDiscoveryActive_!=active;keyboardDiscoveryActive_=active;
    if(active_){showInterceptionPaused();return;}
    if(active){driverRefreshPending_=driverWindow_->isVisible();showInterceptionWaiting();return;}
    refreshDriver_->setEnabled(!driverWatcher_.isRunning());if(changed&&driverWindow_->isVisible())refreshInterception();
}
void SettingsPage::showInterceptionPaused(){
    if(driverStatus_->text()!="演奏中，暂不检测")AppLog::instance().write(LogLevel::Info,"驱动","演奏会话中暂停 Interception 检测。");
    refreshDriver_->setEnabled(false);driverStatus_->setText("演奏中，暂不检测");Theme::setStyle(driverStatus_,"color:#a77722;font-weight:600;padding:3px 8px;border-radius:5px;background:#f7fafb;");
    driverDetail_->setText("为保持演奏稳定，驱动检测暂时停用。终止演奏后可重新检测。");
}
void SettingsPage::showInterceptionWaiting(){
    if(driverStatus_->text()!="等待检测")AppLog::instance().write(LogLevel::Info,"驱动","等待输出键盘查找完成后检测 Interception。");
    refreshDriver_->setEnabled(false);driverStatus_->setText("等待检测");Theme::setStyle(driverStatus_,"color:#8397a3;font-weight:600;padding:3px 8px;border-radius:5px;background:#f7fafb;");
    driverDetail_->setText("正在查找键盘，等待检测…");
}
void SettingsPage::refreshInterception(){
    if(active_){showInterceptionPaused();return;}
    if(!driverWindow_->isVisible())return;
    if(keyboardDiscoveryActive_){driverRefreshPending_=true;showInterceptionWaiting();return;}
    if(driverWatcher_.isRunning()){driverRefreshPending_=true;return;}
    AppLog::instance().write(LogLevel::Info,"驱动","开始检测 Interception 驱动。");
    driverRefreshPending_=false;refreshDriver_->setEnabled(false);driverStatus_->setText("正在检测…");
    Theme::setStyle(driverStatus_,"color:#8397a3;font-weight:600;padding:3px 8px;border-radius:5px;background:#f7fafb;");
    driverDetail_->setText("正在检查 Interception 驱动连接与可用键盘…");
    emit driverInspectionActiveChanged(true);
    driverWatcher_.setFuture(QtConcurrent::run([]{
        try{return inspectInterception();}
        catch(const std::exception& error){return InterceptionStatus{InterceptionState::DriverUnavailable,0,QString("检测发生异常：%1").arg(QString::fromUtf8(error.what()))};}
        catch(...){return InterceptionStatus{InterceptionState::DriverUnavailable,0,"检测发生异常，请稍后重新检测。"};}
    }));
}
void SettingsPage::showInterceptionStatus(const InterceptionStatus& result){
    QString status,detail,color;
    switch(result.state){
    case InterceptionState::Ready:status="驱动可用";color="#16865a";detail=QString("已连接到 Interception 驱动，检测到 %1 个可用键盘。").arg(result.keyboardCount);break;
    case InterceptionState::NoKeyboard:status="已连接，未找到键盘";color="#a77722";detail="已连接到 Interception 驱动，但未检测到可用键盘。检查键盘连接后重新检测。";break;
    case InterceptionState::RuntimeUnavailable:status="检测组件不可用";color="#bd4f5b";detail="Interception 运行库加载失败，暂时无法完成检测。";break;
    case InterceptionState::DriverUnavailable:status="未连接到驱动";color="#bd4f5b";detail="当前无法连接到 Interception 驱动。请确认驱动安装状态，或重启后重新检测。";break;
    case InterceptionState::Unsupported:status="当前系统不支持";color="#a77722";detail="当前平台不支持 Interception 驱动检测。";break;
    }
    driverStatus_->setText(status);Theme::setStyle(driverStatus_,QString("color:%1;font-weight:600;padding:3px 8px;border-radius:5px;background:#f7fafb;").arg(color));
    if(!result.detail.isEmpty())detail=result.detail;
    driverDetail_->setText(detail);refreshDriver_->setEnabled(true);
}
}
