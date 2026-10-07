#include "practice_settings_dialog.h"
#include "theme.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <set>

namespace rock {
namespace {
QLabel* text(const QString& value,const char* role=nullptr){
    auto* label=new QLabel(value);label->setWordWrap(true);
    if(role)label->setProperty("role",role);
    return label;
}
QFrame* section(QVBoxLayout* layout){
    auto* frame=new QFrame;frame->setProperty("practiceSettingsSection",true);
    layout->addWidget(frame);return frame;
}
void tabPage(QTabWidget* tabs,const QString& title,QVBoxLayout*& content){
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* page=new QWidget;content=new QVBoxLayout(page);
    content->setContentsMargins(0,10,0,0);content->setSpacing(10);
    scroll->setWidget(page);tabs->addTab(scroll,title);
}
double finiteOr(double value,double fallback){return std::isfinite(value)?value:fallback;}
}

PracticeSettingsDialog::PracticeSettingsDialog(const PracticeOptions& options,double duration,double position,
                                               const std::vector<PracticeGroup>& groups,QWidget* parent):QDialog(parent),
    duration_(std::max(0.0,finiteOr(duration,0))),position_(std::clamp(finiteOr(position,0),0.0,duration_)),groups_(groups){
    setObjectName("practiceSettingsDialog");setWindowTitle("练习设置与键位");setModal(true);
    resize(640,600);setMinimumSize(580,540);
    Theme::setStyle(this,QString::fromUtf8(R"(
        QDialog#practiceSettingsDialog {background:#eef3f5;}
        QFrame[practiceSettingsSection=true] {background:#ffffff;border:1px solid #e0e9ed;border-radius:10px;}
        QFrame[practiceKeyCard=true] {background:#f7fafb;border:1px solid #e0e9ed;border-radius:8px;}
        QDialog#practiceSettingsDialog QTabWidget::pane {background:transparent;border:none;}
        QDialog#practiceSettingsDialog QScrollArea,
        QDialog#practiceSettingsDialog QScrollArea > QWidget > QWidget {background:#eef3f5;}
        QDialog#practiceSettingsDialog QTabBar::tab {padding:10px 18px;font-weight:600;}
        QDialog#practiceSettingsDialog QCheckBox {spacing:8px;}
        QDialog#practiceSettingsDialog QDoubleSpinBox,
        QDialog#practiceSettingsDialog QSpinBox {padding:6px 9px;min-height:20px;}
        QDialog#practiceSettingsDialog QKeySequenceEdit {background:transparent;border:none;padding:0;min-height:0;}
        QDialog#practiceSettingsDialog QKeySequenceEdit QLineEdit {background:#ffffff;border:1px solid #dce5eb;border-radius:6px;padding:6px 8px;min-height:20px;}
        QDialog#practiceSettingsDialog QKeySequenceEdit QLineEdit:focus {border-color:#178e80;}
        QLabel#practiceSettingsError {color:#bd4f5b;font-size:11px;}
    )"));
    auto* outer=new QVBoxLayout(this);outer->setContentsMargins(20,18,20,18);outer->setSpacing(8);
    outer->addWidget(text("练习设置与键位","title"));
    auto* help=text("仅影响歌曲练习，不会修改 MIDI、转换结果或游戏演奏键位。","muted");
    help->setObjectName("practiceSettingsHelp");outer->addWidget(help);
    tabs_=new QTabWidget;tabs_->setObjectName("practiceSettingsTabs");outer->addWidget(tabs_,1);
    QVBoxLayout* practiceContent{};tabPage(tabs_,"练习设置",practiceContent);

    auto* loopLayout=new QVBoxLayout(section(practiceContent));loopLayout->setContentsMargins(14,12,14,12);loopLayout->setSpacing(8);
    loop_=new QCheckBox("A / B 循环");loop_->setObjectName("practiceLoopEnabled");loop_->setChecked(options.loopEnabled);
    loopLayout->addWidget(loop_);loopLayout->addWidget(text("重复练习 A 到 B 之间的音符；B 点不包含在循环内。","muted"));
    auto* loopForm=new QGridLayout;loopForm->setHorizontalSpacing(14);loopForm->setVerticalSpacing(6);
    loopForm->setColumnStretch(0,1);loopForm->setColumnStretch(1,1);
    loopForm->addWidget(text("A 起点"),0,0);loopForm->addWidget(text("B 终点"),0,1);
    auto loopSpin=[this](const char* name,double value){
        auto* spin=new QDoubleSpinBox;spin->setObjectName(name);spin->setDecimals(3);spin->setRange(0,duration_);
        spin->setSingleStep(.1);spin->setSuffix(" 秒");
        spin->setValue(std::clamp(finiteOr(value,0),0.0,duration_));return spin;
    };
    start_=loopSpin("practiceLoopStart",options.loopStart);
    end_=loopSpin("practiceLoopEnd",options.loopEnd>0?options.loopEnd:duration_);
    loopForm->addWidget(start_,1,0);loopForm->addWidget(end_,1,1);
    setStart_=new QPushButton("当前定位设为 A");setStart_->setObjectName("practiceSetLoopStart");setStart_->setAutoDefault(false);
    setEnd_=new QPushButton("当前定位设为 B");setEnd_->setObjectName("practiceSetLoopEnd");setEnd_->setAutoDefault(false);
    loopForm->addWidget(setStart_,2,0);loopForm->addWidget(setEnd_,2,1);loopLayout->addLayout(loopForm);
    connect(setStart_,&QPushButton::clicked,this,[this]{start_->setValue(position_);error_->clear();});
    connect(setEnd_,&QPushButton::clicked,this,[this]{end_->setValue(position_);error_->clear();});

    auto* metroLayout=new QVBoxLayout(section(practiceContent));metroLayout->setContentsMargins(14,12,14,12);metroLayout->setSpacing(8);
    metronome_=new QCheckBox("节拍器");metronome_->setObjectName("practiceMetronomeEnabled");metronome_->setChecked(options.metronomeEnabled);
    metroLayout->addWidget(metronome_);
    follow_=new QCheckBox("跟随曲谱速度与拍号");follow_->setObjectName("practiceFollowScore");follow_->setChecked(options.followScore);metroLayout->addWidget(follow_);
    metroLayout->addWidget(text("跟随时自动读取曲谱的速度与拍号变化，节拍器随播放倍速调整。取消跟随后可指定参考节拍。","muted"));
    auto* metroForm=new QGridLayout;metroForm->setHorizontalSpacing(14);metroForm->setVerticalSpacing(6);
    metroForm->setColumnStretch(0,1);metroForm->setColumnStretch(1,1);
    metroForm->addWidget(text("参考速度"),0,0);metroForm->addWidget(text("每小节拍数"),0,1);
    bpm_=new QDoubleSpinBox;bpm_->setObjectName("practiceMetronomeBpm");bpm_->setRange(30,300);bpm_->setDecimals(1);
    bpm_->setSingleStep(1);bpm_->setSuffix(" BPM");bpm_->setValue(finiteOr(options.metronomeBpm,120));
    beats_=new QSpinBox;beats_->setObjectName("practiceBeatsPerBar");beats_->setRange(1,12);beats_->setSuffix(" 拍");beats_->setValue(options.beatsPerBar);
    metroForm->addWidget(bpm_,1,0);metroForm->addWidget(beats_,1,1);metroLayout->addLayout(metroForm);
    practiceContent->addWidget(text("键位和节拍器设置会记住；A / B 区间按当前曲目设置。","muted"));
    practiceContent->addStretch();

    QVBoxLayout* keysContent{};tabPage(tabs_,"自定义键位",keysContent);
    keysContent->addWidget(text("为九个音指定练习按键","section"));
    keysContent->addWidget(text("点击输入框后按下一个英文字母或数字。九个按键必须各不相同；空格用于播放 / 暂停，不能设为音符键。","muted"));
    auto* keyGrid=new QGridLayout;keyGrid->setContentsMargins(0,0,0,0);keyGrid->setSpacing(10);
    for(size_t i=0;i<keys.size();++i){
        auto* card=new QFrame;card->setProperty("practiceKeyCard",true);
        auto* column=new QVBoxLayout(card);column->setContentsMargins(12,8,12,8);column->setSpacing(6);
        column->addWidget(text(QString("%1  ·  默认 %2").arg(QString::fromStdString(pitchName(pitches[i])),QChar(keys[i])),"section"));
        auto* edit=new QKeySequenceEdit;edit->setObjectName(QString("practiceInputKey_%1").arg(QChar(keys[i])));
        edit->setMaximumSequenceLength(1);edit->setClearButtonEnabled(true);edit->setMinimumWidth(100);
        edit->setAccessibleName(QString("%1 的练习按键").arg(QString::fromStdString(pitchName(pitches[i]))));
        edit->setKeySequence(QKeySequence(options.inputKeys[i]));keyEdits_[i]=edit;column->addWidget(edit);
        connect(edit,&QKeySequenceEdit::keySequenceChanged,this,[this]{error_->clear();});
        keyGrid->addWidget(card,static_cast<int>(i)/3,static_cast<int>(i)%3);
    }
    for(int i=0;i<3;++i)keyGrid->setColumnStretch(i,1);
    keysContent->addLayout(keyGrid);
    auto* keyActions=new QHBoxLayout;keyActions->addStretch();
    auto* reset=new QPushButton("恢复默认九键");reset->setObjectName("practiceResetKeys");reset->setAutoDefault(false);keyActions->addWidget(reset);keysContent->addLayout(keyActions);
    keysContent->addWidget(text("保存后，音符时间轴、手碟提示和键盘输入会同步使用新键位。","muted"));keysContent->addStretch();
    connect(reset,&QPushButton::clicked,this,[this]{for(size_t i=0;i<keys.size();++i)keyEdits_[i]->setKeySequence(QKeySequence(keys[i]));error_->clear();});

    error_=text({});error_->setObjectName("practiceSettingsError");error_->setMinimumHeight(22);error_->setAccessibleName("设置校验提示");outer->addWidget(error_);
    auto* actions=new QHBoxLayout;actions->setSpacing(8);actions->addStretch();
    auto* cancel=new QPushButton("取消");cancel->setObjectName("practiceSettingsCancel");cancel->setMinimumWidth(90);cancel->setAutoDefault(false);actions->addWidget(cancel);
    auto* save=new QPushButton("保存并关闭");save->setObjectName("practiceSettingsSave");save->setProperty("primary",true);save->setMinimumWidth(120);save->setDefault(true);actions->addWidget(save);outer->addLayout(actions);
    connect(cancel,&QPushButton::clicked,this,&QDialog::reject);connect(save,&QPushButton::clicked,this,&PracticeSettingsDialog::accept);
    connect(loop_,&QCheckBox::toggled,this,[this]{updateEnabled();error_->clear();});
    connect(metronome_,&QCheckBox::toggled,this,[this]{updateEnabled();error_->clear();});
    connect(follow_,&QCheckBox::toggled,this,[this]{updateEnabled();error_->clear();});
    connect(start_,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{error_->clear();});
    connect(end_,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{error_->clear();});
    updateEnabled();
}

void PracticeSettingsDialog::updateEnabled(){
    const bool loop=loop_->isChecked()&&duration_>0;
    start_->setEnabled(loop);end_->setEnabled(loop);setStart_->setEnabled(loop);setEnd_->setEnabled(loop);
    loop_->setEnabled(duration_>0);
    follow_->setEnabled(metronome_->isChecked());
    const bool custom=metronome_->isChecked()&&!follow_->isChecked();bpm_->setEnabled(custom);beats_->setEnabled(custom);
}

PracticeOptions PracticeSettingsDialog::options() const {
    PracticeOptions result;result.loopEnabled=loop_->isChecked();
    result.loopStart=std::clamp(start_->value(),0.0,duration_);result.loopEnd=std::clamp(end_->value(),0.0,duration_);
    result.metronomeEnabled=metronome_->isChecked();result.followScore=follow_->isChecked();
    result.metronomeBpm=bpm_->value();result.beatsPerBar=beats_->value();
    for(size_t i=0;i<keyEdits_.size();++i){const auto sequence=keyEdits_[i]->keySequence();result.inputKeys[i]=sequence.isEmpty()?0:sequence[0].toCombined();}
    return result;
}

void PracticeSettingsDialog::showError(const QString& message,int tab,QWidget* field){
    tabs_->setCurrentIndex(tab);if(field)field->setFocus(Qt::OtherFocusReason);error_->setText(message);
}

void PracticeSettingsDialog::accept(){
    start_->interpretText();end_->interpretText();bpm_->interpretText();beats_->interpretText();
    const auto draft=options();
    if(draft.loopEnabled){
        if(draft.loopEnd<=draft.loopStart){showError("B 终点必须晚于 A 起点，请调整循环区间。",0,end_);return;}
        const bool containsGroup=std::any_of(groups_.begin(),groups_.end(),[&draft](const PracticeGroup& group){return group.keys&&group.start>=draft.loopStart&&group.start<draft.loopEnd;});
        if(!containsGroup){showError("A / B 区间内没有可练习的按键组，请包含至少一个音符起点。",0,start_);return;}
    }
    std::set<int> assigned;
    for(size_t i=0;i<keyEdits_.size();++i){
        const auto sequence=keyEdits_[i]->keySequence();const int key=draft.inputKeys[i];
        if(sequence.count()!=1||!((key>=Qt::Key_A&&key<=Qt::Key_Z)||(key>=Qt::Key_0&&key<=Qt::Key_9))){
            showError(QString("%1 的练习按键需要是单个英文字母或数字，不能使用空格、组合键或空键位。").arg(QString::fromStdString(pitchName(pitches[i]))),1,keyEdits_[i]);return;
        }
        if(!assigned.insert(key).second){showError(QString("按键 %1 已被另一音高使用，请为九个音设置不同按键。").arg(QChar(key)),1,keyEdits_[i]);return;}
    }
    error_->clear();QDialog::accept();
}
}
