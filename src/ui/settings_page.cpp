#include "settings_page.h"
#include "handpan_test.h"
#include "theme.h"
#include <QComboBox>
#include <QFrame>
#include <QFormLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>
namespace rock {
namespace {
QLabel* text(const QString& value,const char* role){auto* label=new QLabel(value);label->setProperty("role",role);label->setWordWrap(true);return label;}
QFrame* card(QVBoxLayout* layout){auto* frame=new QFrame;frame->setObjectName("panel");layout->addWidget(frame);return frame;}
}
SettingsPage::SettingsPage(QWidget* parent):QWidget(parent){
    setObjectName("appSettingsPage");auto* outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);
    auto* scroll=new QScrollArea;scroll->setObjectName("appSettingsScroll");scroll->setWidgetResizable(true);outer->addWidget(scroll);
    auto* page=new QWidget;auto* content=new QVBoxLayout(page);content->setContentsMargins(22,18,22,22);content->setSpacing(16);scroll->setWidget(page);
    content->addWidget(text("设置","brand"));content->addWidget(text("外观即时生效；快捷键保存后生效，重启程序后会保留。","muted"));
    auto* appearance=new QVBoxLayout(card(content));appearance->setContentsMargins(22,18,22,18);appearance->setSpacing(12);appearance->addWidget(text("外观","title"));
    auto* themeRow=new QHBoxLayout;themeRow->addWidget(text("全局主题","section"));auto* theme=new QComboBox;theme->setObjectName("themeSelector");theme->addItems({"浅色","深色"});theme->setMinimumWidth(200);theme->setCurrentIndex(Preferences::instance().darkTheme()?1:0);themeRow->addWidget(theme);themeRow->addStretch();appearance->addLayout(themeRow);
    appearance->addWidget(text("主窗口、音符轨道、胶囊小窗、测试窗口和弹窗同步切换。","muted"));connect(theme,qOverload<int>(&QComboBox::currentIndexChanged),this,[](int index){Preferences::instance().setDarkTheme(index==1);});
    auto* shortcuts=new QVBoxLayout(card(content));shortcuts->setContentsMargins(22,18,22,18);shortcuts->setSpacing(14);shortcuts->addWidget(text("快捷键","title"));
    shortcuts->addWidget(text("点击输入框后按下新组合。工作台快捷键可清空以停用；鼠标的 Ctrl / Shift 多选及 Ctrl + 滚轮缩放保持原样。九个音符按键固定。","muted"));
    shortcutForm_=new QWidget;auto* columns=new QHBoxLayout(shortcutForm_);columns->setContentsMargins(0,0,0,0);columns->setSpacing(36);
    for(bool global:{true,false}){auto* column=new QVBoxLayout;column->setSpacing(10);column->addWidget(text(global?"全局演奏控制":"工作台与独立轨道","section"));auto* form=new QFormLayout;form->setSpacing(9);form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        for(const auto& d:shortcutDefinitions())if(d.global==global){auto* edit=new QKeySequenceEdit;edit->setObjectName("shortcut_"+QString::fromUtf8(d.id));edit->setMaximumSequenceLength(1);edit->setClearButtonEnabled(true);edit->setMinimumWidth(180);editors_[static_cast<size_t>(d.action)]=edit;auto* row=new QHBoxLayout;row->addWidget(edit,1);auto* defaultText=text(QString("默认 %1").arg(QKeySequence(QString::fromUtf8(d.defaultKey)).toString(QKeySequence::NativeText)),"muted");row->addWidget(defaultText);form->addRow(QString::fromUtf8(d.name),row);connect(edit,&QKeySequenceEdit::keySequenceChanged,this,&SettingsPage::updateDraft);}
        column->addLayout(form);if(global)column->addWidget(text("可用 F1～F24，或 Ctrl / Alt 加字母、数字，可同时使用 Shift。暂停 / 继续与终止在演奏会话中生效；切歌与小窗切换在程序运行期间生效。切换其他窗口后仍有效。暂停 / 继续和终止不能清空，其余可清空以停用。","muted"));else column->addWidget(text("试听快捷键在工作台非输入控件中生效；空格不会点击焦点按钮。其余编辑快捷键在音符轨道获得焦点时生效，全屏切换在独立轨道窗口生效。","muted"));column->addStretch();columns->addLayout(column,1);}
    shortcuts->addWidget(shortcutForm_);status_=text({},"muted");status_->setObjectName("shortcutStatus");shortcuts->addWidget(status_);
    auto* actions=new QHBoxLayout;reset_=new QPushButton("恢复默认快捷键");reset_->setObjectName("resetShortcutsButton");cancel_=new QPushButton("取消修改");save_=new QPushButton("保存快捷键");save_->setObjectName("saveShortcutsButton");save_->setProperty("primary",true);actions->addWidget(reset_);actions->addWidget(cancel_);actions->addStretch();actions->addWidget(save_);shortcuts->addLayout(actions);
    connect(reset_,&QPushButton::clicked,this,[this]{loadBindings(defaultShortcuts());updateDraft();});connect(cancel_,&QPushButton::clicked,this,[this]{loadBindings(Preferences::instance().shortcuts());updateDraft();});connect(save_,&QPushButton::clicked,this,&SettingsPage::saveBindings);
    auto* testing=new QVBoxLayout(card(content));testing->setContentsMargins(22,18,22,18);testing->setSpacing(12);testing->addWidget(text("九键手碟测试","title"));testing->addWidget(text("用键盘或鼠标试听九个音，查看按键时间、音高和输入日志。测试窗口需要保持前台并获得焦点。","muted"));test_=new QPushButton("测试按键");test_->setObjectName("testKeysButton");testing->addWidget(test_,0,Qt::AlignLeft);
    connect(test_,&QPushButton::clicked,this,[this]{if(active_)return;if(!testWindow_){testWindow_=new HandpanTestDialog(this);testWindow_->setAttribute(Qt::WA_DeleteOnClose);}testWindow_->show();testWindow_->raise();testWindow_->activateWindow();});
    content->addStretch();loadBindings(Preferences::instance().shortcuts());updateDraft();
}
void SettingsPage::loadBindings(const ShortcutBindings& bindings){for(size_t i=0;i<editors_.size();++i){const QSignalBlocker blocker(editors_[i]);editors_[i]->setKeySequence(bindings[i]);}}
void SettingsPage::updateDraft(){if(active_)return;ShortcutBindings draft;for(size_t i=0;i<editors_.size();++i)draft[i]=editors_[i]->keySequence();const bool changed=draft!=Preferences::instance().shortcuts();save_->setEnabled(changed);cancel_->setEnabled(changed);Theme::setStyle(status_,"color:#8397a3;");status_->setText(changed?"有未保存的快捷键修改。":"快捷键已保存。恢复默认后需点击保存。");}
void SettingsPage::saveBindings(){if(active_)return;ShortcutBindings bindings;for(size_t i=0;i<editors_.size();++i)bindings[i]=editors_[i]->keySequence();QString error;if(!Preferences::instance().saveShortcuts(bindings,error)){Theme::setStyle(status_,"color:#bd4f5b;");status_->setText(error);return;}updateDraft();status_->setText("快捷键已保存并生效。");}
void SettingsPage::setPerformanceActive(bool active){active_=active;shortcutForm_->setEnabled(!active);reset_->setEnabled(!active);test_->setEnabled(!active);if(active){save_->setEnabled(false);cancel_->setEnabled(false);status_->setText("演奏会话中暂不修改快捷键，请终止演奏后再设置。主题仍可调整。");if(testWindow_)testWindow_->close();}else updateDraft();}
}
