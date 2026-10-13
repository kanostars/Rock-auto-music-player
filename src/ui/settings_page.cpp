#include "settings_page.h"
#include "handpan_test.h"
#include "theme.h"
#include "app/app_log.h"
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <exception>
#include <initializer_list>
namespace rock {
namespace {
QLabel* text(const QString& value,const char* role){auto* label=new QLabel(value);label->setProperty("role",role);label->setWordWrap(true);return label;}
QFrame* card(QVBoxLayout* layout,const char* name="panel"){auto* frame=new QFrame;frame->setObjectName(name);frame->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);layout->addWidget(frame);return frame;}
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
    auto* outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);
    auto* scroll=new QScrollArea;scroll->setObjectName("appSettingsScroll");scroll->setWidgetResizable(true);outer->addWidget(scroll);
    auto* page=new QWidget;auto* content=new QVBoxLayout(page);content->setContentsMargins(16,12,16,16);content->setSpacing(12);scroll->setWidget(page);
    auto* testing=new QHBoxLayout(card(content,"settingsTestCard"));testing->setContentsMargins(18,14,18,14);testing->setSpacing(24);
    auto* testIntro=new QVBoxLayout;testIntro->setSpacing(4);
    testIntro->addWidget(text("九键手碟测试","settingsTitle"));
    testIntro->addWidget(text("用键盘或鼠标试听九个音，查看按键时间、音高和输入日志。测试窗口需保持前台并获得焦点。","muted"));testing->addLayout(testIntro,1);
    test_=new QPushButton("测试按键");test_->setObjectName("testKeysButton");test_->setProperty("primary",true);test_->setMinimumWidth(160);testing->addWidget(test_);
    connect(test_,&QPushButton::clicked,this,[this]{if(active_)return;if(!testWindow_){testWindow_=new HandpanTestDialog(this);testWindow_->setAttribute(Qt::WA_DeleteOnClose);}testWindow_->show();testWindow_->raise();testWindow_->activateWindow();});

    auto* driverCard=card(content,"settingsDriverCard");
    Theme::setStyle(driverCard,"QFrame#settingsDriverCard {background:#ffffff;border:1px solid #e0e9ed;border-radius:10px;}");
    auto* driver=new QHBoxLayout(driverCard);driver->setContentsMargins(18,14,18,14);driver->setSpacing(24);
    auto* driverIntro=new QVBoxLayout;driverIntro->setSpacing(5);
    auto* driverHeading=new QHBoxLayout;driverHeading->setSpacing(12);driverHeading->addWidget(text("Interception 驱动","settingsTitle"));
    driverStatus_=new QLabel("尚未检测");driverStatus_->setObjectName("interceptionStatus");driverStatus_->setTextFormat(Qt::PlainText);
    driverStatus_->setSizePolicy(QSizePolicy::Maximum,QSizePolicy::Preferred);driverHeading->addWidget(driverStatus_);driverHeading->addStretch();
    driverIntro->addLayout(driverHeading);
    driverDetail_=text("进入设置页面后自动检测驱动连接与可用键盘。","muted");driverDetail_->setObjectName("interceptionDetail");driverDetail_->setTextFormat(Qt::PlainText);driverIntro->addWidget(driverDetail_);driver->addLayout(driverIntro,1);
    refreshDriver_=new QPushButton("重新检测");refreshDriver_->setObjectName("refreshInterceptionButton");refreshDriver_->setMinimumWidth(160);driver->addWidget(refreshDriver_);
    connect(refreshDriver_,&QPushButton::clicked,this,&SettingsPage::refreshInterception);
    connect(&driverWatcher_,&QFutureWatcher<InterceptionStatus>::finished,this,[this]{
        const auto result=driverWatcher_.result();logInterceptionResult(result);
        emit driverInspectionActiveChanged(false);
        if(active_){driverRefreshPending_=false;if(isVisible())showInterceptionPaused();return;}
        if(!isVisible()){driverRefreshPending_=false;return;}
        if(keyboardDiscoveryActive_){driverRefreshPending_=true;showInterceptionWaiting();return;}
        if(driverRefreshPending_){refreshInterception();return;}
        showInterceptionStatus(result);
    });

    auto* appearance=new QHBoxLayout(card(content));appearance->setContentsMargins(18,14,18,14);appearance->setSpacing(24);
    auto* appearanceIntro=new QVBoxLayout;appearanceIntro->setSpacing(4);
    appearanceIntro->addWidget(text("外观","settingsTitle"));
    appearanceIntro->addWidget(text("主窗口、音符轨道、胶囊小窗、测试窗口和弹窗同步切换。","muted"));appearance->addLayout(appearanceIntro,1);
    auto* theme=new QComboBox;theme->setObjectName("themeSelector");theme->setAccessibleName("全局主题");theme->addItems({"浅色","深色"});theme->setFixedWidth(160);theme->setCurrentIndex(Preferences::instance().darkTheme()?1:0);appearance->addWidget(theme);
    connect(theme,qOverload<int>(&QComboBox::currentIndexChanged),this,[](int index){Preferences::instance().setDarkTheme(index==1);AppLog::instance().write(LogLevel::Info,"设置",index==1?"主题切换为深色。":"主题切换为浅色。");});

    auto* shortcuts=new QVBoxLayout(card(content));shortcuts->setContentsMargins(18,16,18,16);shortcuts->setSpacing(12);
    auto* shortcutIntro=new QVBoxLayout;shortcutIntro->setSpacing(4);
    shortcutIntro->addWidget(text("快捷键","settingsTitle"));
    shortcutIntro->addWidget(text("点击输入框后按下新组合；可清空以停用。九个音符按键及鼠标的 Ctrl / Shift 多选、Ctrl + 滚轮缩放保持固定。","muted"));shortcuts->addLayout(shortcutIntro);
    shortcutForm_=new QWidget;auto* columns=new QHBoxLayout(shortcutForm_);columns->setContentsMargins(0,0,0,0);columns->setSpacing(12);
    auto addGroup=[this,columns](const char* name,const QString& title,const QString& scope,
                                std::initializer_list<ShortcutAction> actions,const QString& help={}){
        auto* group=new QFrame;group->setObjectName(name);group->setProperty("shortcutGroup",true);
        auto* column=new QVBoxLayout(group);column->setContentsMargins(14,14,14,14);column->setSpacing(10);
        auto* intro=new QVBoxLayout;intro->setSpacing(4);intro->addWidget(text(title,"section"));intro->addWidget(text(scope,"muted"));column->addLayout(intro);
        auto* form=new QGridLayout;form->setContentsMargins(0,0,0,0);form->setHorizontalSpacing(10);form->setVerticalSpacing(10);form->setColumnStretch(0,1);
        int row=0;
        for(const auto action:actions){
            const auto index=static_cast<size_t>(action);const auto& d=shortcutDefinitions()[index];
            auto* labelBox=new QVBoxLayout;labelBox->setSpacing(2);labelBox->addWidget(text(QString::fromUtf8(d.name),"shortcutName"));
            labelBox->addWidget(text(QString("默认 %1").arg(QKeySequence(QString::fromUtf8(d.defaultKey)).toString(QKeySequence::NativeText)),"muted"));form->addLayout(labelBox,row,0);
            auto* edit=new QKeySequenceEdit;edit->setObjectName("shortcut_"+QString::fromUtf8(d.id));edit->setAccessibleName(QString::fromUtf8(d.name));edit->setMaximumSequenceLength(1);edit->setClearButtonEnabled(true);edit->setMinimumWidth(136);edit->setMaximumWidth(160);edit->setFixedHeight(34);editors_[index]=edit;form->addWidget(edit,row++,1);
            connect(edit,&QKeySequenceEdit::keySequenceChanged,this,&SettingsPage::updateDraft);
        }
        column->addLayout(form);if(!help.isEmpty())column->addWidget(text(help,"muted"));
        columns->addWidget(group,1,Qt::AlignTop);
    };
    addGroup("globalShortcutGroup","全局演奏控制","切换到其他窗口后仍可使用。",{
        ShortcutAction::PerformancePause,ShortcutAction::PerformanceStop,ShortcutAction::PerformancePrevious,
        ShortcutAction::PerformanceNext,ShortcutAction::MiniMode
    },"可用 F1～F24，或 Ctrl / Alt 加字母、数字（可加 Shift）。暂停 / 继续、终止仅在演奏会话中生效，且不能为空。");
    addGroup("editingShortcutGroup","文件与音符编辑","文件快捷键适用于工作台、设置、跟练和独立轨道；试听避开输入控件，音符编辑需轨道焦点。",{
        ShortcutAction::ExportMidi,ShortcutAction::ExportProject,ShortcutAction::ImportMidi,ShortcutAction::ImportProject,
        ShortcutAction::Preview,ShortcutAction::Undo,ShortcutAction::Redo,ShortcutAction::RedoAlternate,
        ShortcutAction::SelectAll,ShortcutAction::DeleteNotes
    });
    addGroup("trackShortcutGroup","轨道与区间","音高与区间操作需轨道焦点；全屏适用于独立轨道窗口。",{
        ShortcutAction::PitchUp,ShortcutAction::PitchDown,ShortcutAction::CancelEdit,ShortcutAction::Fullscreen,
        ShortcutAction::RangeLeftToPlayhead,ShortcutAction::RangeRightToPlayhead
    });
    shortcuts->addWidget(shortcutForm_);
    auto* actions=new QHBoxLayout;actions->setSpacing(8);
    status_=text({},"muted");status_->setObjectName("shortcutStatus");actions->addWidget(status_,1);
    reset_=new QPushButton("恢复默认快捷键");reset_->setObjectName("resetShortcutsButton");cancel_=new QPushButton("取消修改");save_=new QPushButton("保存快捷键");save_->setObjectName("saveShortcutsButton");save_->setProperty("primary",true);
    actions->addWidget(reset_);actions->addWidget(cancel_);actions->addWidget(save_);shortcuts->addLayout(actions);
    connect(reset_,&QPushButton::clicked,this,[this]{loadBindings(defaultShortcuts());updateDraft();AppLog::instance().write(LogLevel::Info,"设置","快捷键已恢复默认草稿，等待保存。");});
    connect(cancel_,&QPushButton::clicked,this,[this]{loadBindings(Preferences::instance().shortcuts());updateDraft();});connect(save_,&QPushButton::clicked,this,&SettingsPage::saveBindings);
    content->addStretch();loadBindings(Preferences::instance().shortcuts());updateDraft();
}
void SettingsPage::loadBindings(const ShortcutBindings& bindings){for(size_t i=0;i<editors_.size();++i){const QSignalBlocker blocker(editors_[i]);editors_[i]->setKeySequence(bindings[i]);}}
void SettingsPage::updateDraft(){if(active_)return;ShortcutBindings draft;for(size_t i=0;i<editors_.size();++i)draft[i]=editors_[i]->keySequence();const bool changed=draft!=Preferences::instance().shortcuts();save_->setEnabled(changed);cancel_->setEnabled(changed);Theme::setStyle(status_,"color:#8397a3;");status_->setText(changed?"有未保存的快捷键修改。":"快捷键已保存。恢复默认后需点击保存。");}
void SettingsPage::saveBindings(){if(active_)return;ShortcutBindings bindings;for(size_t i=0;i<editors_.size();++i)bindings[i]=editors_[i]->keySequence();QString error;if(!Preferences::instance().saveShortcuts(bindings,error)){AppLog::instance().write(LogLevel::Error,"设置",QString("快捷键保存失败：%1").arg(error));Theme::setStyle(status_,"color:#bd4f5b;");status_->setText(error);return;}updateDraft();status_->setText("快捷键已保存并生效。");AppLog::instance().write(LogLevel::Info,"设置","快捷键已保存并生效。");}
void SettingsPage::setPerformanceActive(bool active){
    const bool changed=active_!=active;active_=active;shortcutForm_->setEnabled(!active);reset_->setEnabled(!active);test_->setEnabled(!active);
    if(active){save_->setEnabled(false);cancel_->setEnabled(false);status_->setText("演奏会话中暂不修改快捷键，请终止演奏后再设置。主题仍可调整。");if(testWindow_)testWindow_->close();driverRefreshPending_=false;showInterceptionPaused();}
    else{updateDraft();refreshDriver_->setEnabled(!driverWatcher_.isRunning()&&!keyboardDiscoveryActive_);if(changed&&isVisible())refreshInterception();}
}
void SettingsPage::setKeyboardDiscoveryActive(bool active){
    const bool changed=keyboardDiscoveryActive_!=active;keyboardDiscoveryActive_=active;
    if(active_){showInterceptionPaused();return;}
    if(active){driverRefreshPending_=true;showInterceptionWaiting();return;}
    refreshDriver_->setEnabled(!driverWatcher_.isRunning());if(changed&&isVisible())refreshInterception();
}
void SettingsPage::showEvent(QShowEvent* event){QWidget::showEvent(event);refreshInterception();}
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
    if(!isVisible())return;
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
    case InterceptionState::Ready:status="驱动可用";color="#178e80";detail=QString("已连接到 Interception 驱动，检测到 %1 个可用键盘。").arg(result.keyboardCount);break;
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
