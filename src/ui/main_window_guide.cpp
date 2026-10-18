#include "main_window.h"
#include "guide_overlay.h"
#include "help_guide.h"
#include "appearance_settings.h"
#include "log_dialog.h"
#include "mini_player.h"
#include "practice_mini.h"
#include "performance_panel.h"
#include "piano_roll.h"
#include "practice_page.h"
#include "settings_page.h"
#include "time_seek_edit.h"
#include "theme.h"
#include "app/preferences.h"
#include "core/practice.h"
#include <QApplication>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSet>
#include <QSlider>
#include <QTabWidget>
#include <QTreeWidget>
#include <algorithm>

namespace rock {
namespace {
constexpr auto helpSeenKey="help/seen";
}
void MainWindow::setupHelpGuide(QHBoxLayout* layout){
    helpGuideButton_=new QPushButton;helpGuideButton_->setObjectName("helpGuideButton");helpGuideButton_->setFixedSize(34,34);helpGuideButton_->setIconSize({24,24});helpGuideButton_->setCursor(Qt::PointingHandCursor);
    helpGuideButton_->setToolTip("功能帮助与操作引导");helpGuideButton_->setAccessibleName("功能帮助与操作引导");
    Theme::setStyle(helpGuideButton_,"QPushButton#helpGuideButton {background:transparent;border:1px solid transparent;border-radius:17px;padding:4px;} QPushButton#helpGuideButton:hover, QPushButton#helpGuideButton:focus {background:#f7fafb;border-color:#a8bfc5;}");
    auto refreshIcon=[this]{
        const qreal ratio=devicePixelRatioF();QPixmap pixmap(qRound(24*ratio),qRound(24*ratio));pixmap.setDevicePixelRatio(ratio);pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(QPen(Theme::color("#526c7c"),1.8));painter.setBrush(Qt::NoBrush);painter.drawEllipse(QRectF(3,3,18,18));
        painter.setPen(QPen(Theme::color("#526c7c"),1.8,Qt::SolidLine,Qt::RoundCap));painter.drawLine(QPointF(12,10.8),QPointF(12,16.2));painter.drawPoint(QPointF(12,7.6));helpGuideButton_->setIcon(QIcon(pixmap));
    };
    refreshIcon();connect(&Preferences::instance(),&Preferences::themeChanged,this,refreshIcon);layout->addWidget(helpGuideButton_);
    connect(helpGuideButton_,&QPushButton::clicked,this,&MainWindow::openHelpGuide);
}
void MainWindow::openHelpGuide(){
    if(closing_)return;
    finishHelpGuide();
    if(!helpGuideWindow_){helpGuideWindow_=new HelpGuideDialog(this);connect(helpGuideWindow_,&HelpGuideDialog::topicRequested,this,&MainWindow::startHelpGuide);}
    updateHelpGuideAvailability();
    if(helpGuideWindow_->isMinimized())helpGuideWindow_->showNormal();else helpGuideWindow_->show();
    helpGuideWindow_->raise();helpGuideWindow_->activateWindow();
    QTimer::singleShot(0,this,&MainWindow::updateHelpGuideAvailability);
    QSettings().setValue(helpSeenKey,true);
}
void MainWindow::showStartupHelp(){
    if(closing_||startupHelpScheduled_||QSettings().value(helpSeenKey,false).toBool())return;
    startupHelpScheduled_=true;
    QTimer::singleShot(250,this,[this]{
        startupHelpScheduled_=false;
        if(closing_||QSettings().value(helpSeenKey,false).toBool())return;
        // Let startup project loading and any import question finish first.
        if(busy_||roll_->isEditing()||closePromptOpen_||QApplication::activeModalWidget()||QApplication::activePopupWidget()||!isVisible()||isMinimized()){showStartupHelp();return;}
        openHelpGuide();
    });
}
void MainWindow::updateHelpGuideAvailability(){
    if(!helpGuideWindow_)return;
    QString reason;
    if(busy_)reason="正在导入曲目，完成后即可开始引导。";
    else if(performance_->active())reason="演奏期间可以阅读帮助；结束演奏后再开始界面引导。";
    else if(roll_->isEditing())reason="请先完成当前音符编辑，再开始引导。";
    helpGuideWindow_->setStartEnabled(reason.isEmpty(),reason);
}
void MainWindow::startHelpGuide(const QString& id){
    if(closing_||QApplication::activeModalWidget()||QApplication::activePopupWidget())return;
    updateHelpGuideAvailability();if(busy_||performance_->active()||roll_->isEditing())return;
    const auto& topics=guideTopics();if(std::none_of(topics.begin(),topics.end(),[&](const auto& topic){return topic.id==id;}))return;
    finishHelpGuide();
    if(helpGuideWindow_)helpGuideWindow_->hide();
    if((mini_&&mini_->isVisible())||(practicePage_&&practicePage_->miniMode()))restoreMainWindow();
    if(practicePage_&&practicePage_->isVisible())leavePractice();
    if(trackWindow_&&trackWindow_->isVisible())trackWindow_->close();
    pausePreview();clock_->cancelEditing();
    guideTopicId_=id;guideStepIndex_=0;updateGlobalShortcuts();showHelpStep(0);
}
QWidget* MainWindow::prepareGuideTarget(const QString& key){
    if(key.startsWith("practiceMini")||key.startsWith("playerMini"))return prepareGuideCapsule(key);
    closeGuideCapsule();
    static const QSet<QString> settingTargets{"appearance","shortcuts","driver","handpanTest","logs","about","settingsHome","interfaceModes","themeLibrary","wallpaperLibrary"};
    if(logWindow_)logWindow_->close();
    const bool settings=settingTargets.contains(key);
    if(settingsNavigation_->isChecked()!=settings)settingsNavigation_->setChecked(settings);
    else if(appSettings_->isVisible()!=settings)showAppSettings(settings);
    appSettings_->closeWindows();
    QWidget* target=nullptr;
    if(settings){
        const QHash<QString,QString> launchers{{"appearance","appearanceSettingsButton"},{"shortcuts","shortcutSettingsButton"},{"driver","driverSettingsButton"},{"handpanTest","testKeysButton"},{"logs","settingsLogsButton"},{"about","aboutSettingsButton"}};
        if(key=="settingsHome")target=appSettings_;
        else if(launchers.contains(key))target=appSettings_->findChild<QWidget*>(launchers.value(key));
        else{
            // Open only the view. Preset, image, wallpaper and device actions
            // remain under the user's control after the guide is dismissed.
            if(auto* launcher=appSettings_->findChild<QPushButton*>("appearanceSettingsButton"))launcher->click();
            auto* appearance=appSettings_->findChild<AppearanceSettingsDialog*>("appearanceSettingsDialog");
            const auto name=key=="interfaceModes"?"interfaceModeGroup":key=="themeLibrary"?"openThemeLibraryButton":"openWallpaperLibraryButton";
            if(appearance)target=appearance->findChild<QWidget*>(name);
        }
    }else{
        if(key=="pitchSettings"||key=="rhythmSettings"||key=="applySettings")tabs_->setCurrentIndex(0);
        if(key=="importMidi")target=import_;
        else if(key=="songLibrary")target=library_;
        else if(key=="tracks")target=tracks_;
        else if(key=="pitchSettings")target=octaveMode_;
        else if(key=="rhythmSettings")target=tempoMode_;
        else if(key=="applySettings")target=apply_;
        else if(key=="noteGrid"||key=="selectionRange")target=roll_->viewport();
        else if(key=="noteTools")target=addNote_;
        else if(key=="exports")target=exportMidi_;
        else if(key=="preview")target=play_;
        else if(key=="seek")target=clock_;
        else if(key=="practice")target=practiceButton_;
        else if(key=="performance"){tabs_->setCurrentIndex(2);target=performance_;}
        else if(key=="project")target=projectMenu_;
    }
    if(!target)return nullptr;
    auto* host=target->window();host->ensurePolished();if(host->layout())host->layout()->activate();
    for(auto* ancestor=target->parentWidget();ancestor&&ancestor->window()==host;ancestor=ancestor->parentWidget())if(auto* scroll=qobject_cast<QScrollArea*>(ancestor))scroll->ensureWidgetVisible(target,12,12);
    host->raise();host->activateWindow();return target;
}
QWidget* MainWindow::prepareGuideCapsule(const QString& key){
    const bool practicing=key.startsWith("practiceMini");
    if(guideCapsule_&&practicing!=(qobject_cast<PracticeMini*>(guideCapsule_.data())!=nullptr))closeGuideCapsule();
    if(settingsNavigation_->isChecked())settingsNavigation_->setChecked(false);
    appSettings_->closeWindows();if(logWindow_)logWindow_->close();
    if(!guideCapsule_){
        // Use the same live capsule widgets as normal mode. During the tour
        // their playback/output signals stay disconnected from the session.
        auto* capsule=practicing?static_cast<QWidget*>(new PracticeMini(this)):static_cast<QWidget*>(new MiniPlayer(library_));
        if(!practicing)capsule->setParent(this,capsule->windowFlags());
        guideCapsule_=capsule;
        if(auto* mini=qobject_cast<PracticeMini*>(capsule))connect(mini,&PracticeMini::restoreRequested,this,&MainWindow::finishHelpGuide);
        else if(auto* mini=qobject_cast<MiniPlayer*>(capsule))connect(mini,&MiniPlayer::restoreRequested,this,&MainWindow::finishHelpGuide);
    }
    const Session* session=current_>=0?&sessions_[current_]:nullptr;
    const auto title=session?QFileInfo(session->path).completeBaseName():QString("尚无曲目");
    const auto duration=session&&session->result?session->result->duration:0;
    const bool canPlay=session&&session->result&&!session->result->notes.empty();
    QString name;
    if(auto* mini=qobject_cast<PracticeMini*>(guideCapsule_.data())){
        if(!mini->isVisible())mini->setSong(title,session?session->song:nullptr,session?session->result:nullptr,session?session->settings:Settings{});
        PracticeMiniState state;state.title=title;state.status="引导中 · 已暂停";state.following=true;state.canPlay=canPlay;state.duration=duration;state.playbackPosition=position_;state.position=position_;state.volume=0;
        if(canPlay){
            const auto groups=makePracticeGroups(*session->result);
            if(!groups.empty()){
                auto group=std::lower_bound(groups.begin(),groups.end(),position_,[](const auto& item,double position){return item.start<position;});
                if(group==groups.end())group=std::prev(groups.end());
                state.keys=group->keys;state.practiceGroup=int(std::distance(groups.begin(),group));state.position=group->start;
                state.progress=QString("%1 / %2 组").arg(state.practiceGroup+1).arg(groups.size());
            }
        }
        mini->setState(state);mini->setGuideView(key=="practiceMiniNumbers",key=="practiceMiniAppearance");
        const QHash<QString,QString> targets{
            {"practiceMiniCapsule","practiceMiniCapsule"},{"practiceMiniKeys","practiceMiniKeys"},{"practiceMiniNumbers","practiceMiniMeasure"},
            {"practiceMiniPlay","practiceMiniPlay"},{"practiceMiniRestart","practiceMiniRestart"},{"practiceMiniMode","practiceMiniMode"},
            {"practiceMiniSeek","practiceMiniProgressSlider"},{"practiceMiniVolume","practiceMiniVolume"},
            {"practiceMiniAppearance","practiceMiniAppearance"},{"practiceMiniRestore","practiceMiniRestore"}};
        name=targets.value(key);
    }else if(auto* mini=qobject_cast<MiniPlayer*>(guideCapsule_.data())){
        MiniPlayerState state;state.title=title;state.status="引导中 · 已暂停";state.position=position_;state.duration=duration;state.volume=volume_->value();state.canPlay=canPlay;state.seekEnabled=canPlay;state.modeEnabled=true;state.mode=performance_->playMode();state.outputReady=performance_->outputReady();
        std::tie(state.rangeFirst,state.rangeLast)=selectedRange();state.performance=key=="playerMiniPerformance";
        mini->setState(state);mini->setGuideView(key=="playerMiniQueue",key=="playerMiniAppearance");
        const QHash<QString,QString> targets{
            {"playerMiniCapsule","miniCapsule"},{"playerMiniAudition","miniAudition"},{"playerMiniPerformance","miniPerformance"},
            {"playerMiniPlay","miniPlay"},{"playerMiniNavigation","miniNext"},{"playerMiniMode","miniMode"},{"playerMiniQueue","miniQueue"},
            {"playerMiniSeek","miniProgress"},{"playerMiniVolume","miniVolume"},{"playerMiniAppearance","miniOpacityPanel"},
            {"playerMiniResize","miniResizeHandle"},{"playerMiniRestore","miniRestore"},{"playerMiniQuit","miniQuit"}};
        name=targets.value(key);
    }
    if(name.isEmpty())return nullptr;
    if(isVisible()){guideHidMain_=true;hide();}
    if(!guideCapsule_->isVisible()){
        if(auto* mini=qobject_cast<PracticeMini*>(guideCapsule_.data()))mini->present(screen());
        else if(auto* mini=qobject_cast<MiniPlayer*>(guideCapsule_.data()))mini->present(screen());
    }
    auto* target=guideCapsule_->findChild<QWidget*>(name);
    guideCapsule_->raise();return target;
}
void MainWindow::closeGuideCapsule(){
    if(guideCapsule_){auto* capsule=guideCapsule_.data();guideCapsule_.clear();disconnect(capsule,nullptr,this,nullptr);capsule->hide();capsule->deleteLater();}
    const bool restore=guideHidMain_;guideHidMain_=false;
    if(restore&&!closing_){show();raise();activateWindow();}
}
void MainWindow::showHelpStep(int index){
    const auto& topics=guideTopics();const auto topic=std::find_if(topics.begin(),topics.end(),[this](const auto& item){return item.id==guideTopicId_;});
    if(topic==topics.end()||index<0||index>=static_cast<int>(topic->steps.size()))return;
    if(guideOverlay_){auto* old=guideOverlay_.data();guideOverlay_=nullptr;disconnect(old,nullptr,this,nullptr);old->dismiss();old->deleteLater();}
    guideStepIndex_=index;const auto& step=topic->steps[index];auto* target=prepareGuideTarget(step.target);
    if(!target||!target->isVisible()){finishHelpGuide();status_->setText("当前功能区域暂不可见，请稍后从帮助列表重试。");return;}
    auto* overlay=new GuideOverlay(target->window(),guideCapsule_&&target->window()==guideCapsule_);guideOverlay_=overlay;
    connect(overlay,&GuideOverlay::previousRequested,this,[this]{showHelpStep(guideStepIndex_-1);});
    connect(overlay,&GuideOverlay::nextRequested,this,[this]{
        const auto& topics=guideTopics();const auto topic=std::find_if(topics.begin(),topics.end(),[this](const auto& item){return item.id==guideTopicId_;});
        if(topic!=topics.end()&&guideStepIndex_+1<static_cast<int>(topic->steps.size()))showHelpStep(guideStepIndex_+1);else finishHelpGuide();
    });
    connect(overlay,&GuideOverlay::exitRequested,this,&MainWindow::finishHelpGuide);
    overlay->present(target,topic->title,step.title,step.description,index,static_cast<int>(topic->steps.size()));
}
void MainWindow::finishHelpGuide(){
    const bool active=!guideTopicId_.isEmpty();guideTopicId_.clear();guideStepIndex_=0;
    if(guideOverlay_){auto* old=guideOverlay_.data();guideOverlay_=nullptr;disconnect(old,nullptr,this,nullptr);old->dismiss();old->deleteLater();}
    closeGuideCapsule();
    if(active&&!closing_)updateGlobalShortcuts();
}
}
