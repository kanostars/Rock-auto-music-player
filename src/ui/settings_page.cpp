#include "settings_page.h"
#include "handpan_test.h"
#include "theme.h"
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <initializer_list>
namespace rock {
namespace {
QLabel* text(const QString& value,const char* role){auto* label=new QLabel(value);label->setProperty("role",role);label->setWordWrap(true);return label;}
QFrame* card(QVBoxLayout* layout,const char* name="panel"){auto* frame=new QFrame;frame->setObjectName(name);frame->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);layout->addWidget(frame);return frame;}
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

    auto* appearance=new QHBoxLayout(card(content));appearance->setContentsMargins(18,14,18,14);appearance->setSpacing(24);
    auto* appearanceIntro=new QVBoxLayout;appearanceIntro->setSpacing(4);
    appearanceIntro->addWidget(text("外观","settingsTitle"));
    appearanceIntro->addWidget(text("主窗口、音符轨道、胶囊小窗、测试窗口和弹窗同步切换。","muted"));appearance->addLayout(appearanceIntro,1);
    auto* theme=new QComboBox;theme->setObjectName("themeSelector");theme->setAccessibleName("全局主题");theme->addItems({"浅色","深色"});theme->setFixedWidth(160);theme->setCurrentIndex(Preferences::instance().darkTheme()?1:0);appearance->addWidget(theme);
    connect(theme,qOverload<int>(&QComboBox::currentIndexChanged),this,[](int index){Preferences::instance().setDarkTheme(index==1);});

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
    addGroup("editingShortcutGroup","试听与音符编辑","试听适用于工作台非输入控件；编辑需轨道获得焦点。",{
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
    connect(reset_,&QPushButton::clicked,this,[this]{loadBindings(defaultShortcuts());updateDraft();});
    connect(cancel_,&QPushButton::clicked,this,[this]{loadBindings(Preferences::instance().shortcuts());updateDraft();});connect(save_,&QPushButton::clicked,this,&SettingsPage::saveBindings);
    content->addStretch();loadBindings(Preferences::instance().shortcuts());updateDraft();
}
void SettingsPage::loadBindings(const ShortcutBindings& bindings){for(size_t i=0;i<editors_.size();++i){const QSignalBlocker blocker(editors_[i]);editors_[i]->setKeySequence(bindings[i]);}}
void SettingsPage::updateDraft(){if(active_)return;ShortcutBindings draft;for(size_t i=0;i<editors_.size();++i)draft[i]=editors_[i]->keySequence();const bool changed=draft!=Preferences::instance().shortcuts();save_->setEnabled(changed);cancel_->setEnabled(changed);Theme::setStyle(status_,"color:#8397a3;");status_->setText(changed?"有未保存的快捷键修改。":"快捷键已保存。恢复默认后需点击保存。");}
void SettingsPage::saveBindings(){if(active_)return;ShortcutBindings bindings;for(size_t i=0;i<editors_.size();++i)bindings[i]=editors_[i]->keySequence();QString error;if(!Preferences::instance().saveShortcuts(bindings,error)){Theme::setStyle(status_,"color:#bd4f5b;");status_->setText(error);return;}updateDraft();status_->setText("快捷键已保存并生效。");}
void SettingsPage::setPerformanceActive(bool active){active_=active;shortcutForm_->setEnabled(!active);reset_->setEnabled(!active);test_->setEnabled(!active);if(active){save_->setEnabled(false);cancel_->setEnabled(false);status_->setText("演奏会话中暂不修改快捷键，请终止演奏后再设置。主题仍可调整。");if(testWindow_)testWindow_->close();}else updateDraft();}
}
