#include "wallpaper_library.h"
#include "wallpaper_store.h"
#include "theme.h"
#include "app/preferences.h"
#include <QAbstractButton>
#include <QApplication>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QEnterEvent>
#include <QFileDialog>
#include <QFocusEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHideEvent>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <vector>

namespace rock {
namespace {
QLabel* words(const QString& text,const char* role=nullptr){
    auto* label=new QLabel(text);label->setTextFormat(Qt::PlainText);label->setWordWrap(true);
    if(role)label->setProperty("role",role);return label;
}
QString kindName(WallpaperKind kind){
    switch(kind){case WallpaperKind::Video:return "▶ 视频";case WallpaperKind::Gif:return "▷ 动图";case WallpaperKind::Image:return "▧ 静态";}
    return {};
}
void showWindow(QDialog* dialog){if(dialog->isMinimized())dialog->showNormal();else dialog->show();dialog->raise();dialog->activateWindow();}

class WallpaperCard final:public QAbstractButton {
public:
    WallpaperCard(const WallpaperEntry& entry,std::function<void()> select,std::function<void()> remove,std::function<void()> rename,QWidget* parent,bool transparent=false):QAbstractButton(parent),entry_(entry),transparent_(transparent){
        setObjectName("wallpaperCard_"+entry.id);setProperty("wallpaperId",entry.id);setProperty("wallpaperKind",static_cast<int>(entry.kind));
        setProperty("builtinWallpaper",transparent_);
        setCheckable(true);setFocusPolicy(Qt::StrongFocus);setCursor(Qt::PointingHandCursor);setMinimumSize(188,174);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        setToolTip(entry.name+(transparent_?"\n透视窗口后方，背景透明度可调":"\n点击立即设为壁纸"));
        if(!transparent_){thumbnail_.load(entry.thumbnail);if(thumbnail_.isNull()&&entry.kind!=WallpaperKind::Video)thumbnail_.load(entry.path);}
        select_=action("wallpaperSelect_"+entry.id,"选择壁纸 "+entry.name,"选择壁纸",QStyle::SP_DialogApplyButton);
        if(!transparent_){
            remove_=action("wallpaperDelete_"+entry.id,"删除壁纸 "+entry.name,"从壁纸库删除",QStyle::SP_TrashIcon);
            rename_=action("wallpaperRename_"+entry.id,"重命名壁纸 "+entry.name,"编辑名称",QStyle::SP_FileDialogDetailedView);rename_->setIcon(QIcon());rename_->setText("✎");
            QObject::connect(remove_,&QToolButton::clicked,this,[remove]{remove();});
            QObject::connect(rename_,&QToolButton::clicked,this,[rename]{rename();});
        }
        QObject::connect(this,&QAbstractButton::clicked,this,[select]{select();});
        QObject::connect(select_,&QToolButton::clicked,this,[select]{select();});
        refresh();
    }
    QSize sizeHint()const override{return {224,174};}
    void refresh(){
        const bool selected=transparent_?WallpaperStore::instance().transparent():WallpaperStore::instance().activeId()==entry_.id;setChecked(selected);
        setAccessibleName(entry_.name+" · "+(transparent_?QString("内置"):kindName(entry_.kind))+(selected?" · 使用中":""));setAccessibleDescription(transparent_?"点击或按空格透视窗口后方，然后调整背景透明度。":"点击或按空格应用壁纸；聚焦后可选择、重命名或删除。");
        select_->setToolTip(selected?"正在使用":"选择壁纸");select_->setEnabled(!selected);update();
    }
    void setImportBusy(bool busy){if(remove_){remove_->setEnabled(!busy);remove_->setToolTip(busy?"导入完成后可删除":"从壁纸库删除");}}
protected:
    void paintEvent(QPaintEvent*)override{
        const auto colors=Theme::palette(Preferences::instance().themeId(),Preferences::instance().darkTheme());
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);const QRectF frame=QRectF(rect()).adjusted(2,2,-2,-2);
        painter.setPen(QPen(isChecked()?colors.accent:colors.border,isChecked()?2.5:1));painter.setBrush(colors.surface);painter.drawRoundedRect(frame,12,12);
        const QRectF preview(12,12,width()-24,112);QPainterPath clip;clip.addRoundedRect(preview,8,8);painter.save();painter.setClipPath(clip);
        painter.fillRect(preview,colors.raised);
        if(transparent_){
            const int tile=14;const QColor alternate=colors.raised.lighter(Preferences::instance().darkTheme()?132:96);
            for(int row=0;row*tile<preview.height();++row)for(int column=0;column*tile<preview.width();++column)if((row+column)%2)painter.fillRect(QRectF(preview.left()+column*tile,preview.top()+row*tile,tile,tile),alternate);
            const QRectF behind=preview.adjusted(18,14,-38,-20);painter.setPen(QPen(colors.accent,1.5));QColor tint=colors.accent;tint.setAlpha(90);painter.setBrush(tint);painter.drawRoundedRect(behind,6,6);
            painter.drawLine(behind.left()+10,behind.top()+13,behind.right()-10,behind.top()+13);
            const QRectF glass=preview.adjusted(42,28,-18,-9);QColor surface=colors.surface;surface.setAlpha(145);painter.setBrush(surface);painter.setPen(QPen(colors.border,1));painter.drawRoundedRect(glass,6,6);
            painter.setPen(QPen(colors.text,2));painter.drawLine(glass.left()+12,glass.top()+13,glass.left()+48,glass.top()+13);painter.setPen(QPen(colors.accent,4));painter.drawLine(glass.left()+12,glass.top()+27,glass.right()-12,glass.top()+27);
        }else if(!thumbnail_.isNull()){
            const qreal scale=std::max(preview.width()/thumbnail_.width(),preview.height()/thumbnail_.height());
            const QSizeF size(thumbnail_.width()*scale,thumbnail_.height()*scale);const QRectF target(preview.center()-QPointF(size.width()/2,size.height()/2),size);painter.drawPixmap(target,thumbnail_,QRectF(thumbnail_.rect()));
        }else{
            QFont placeholder=font();placeholder.setPointSize(26);painter.setFont(placeholder);painter.setPen(colors.muted);painter.drawText(preview,Qt::AlignCenter,entry_.kind==WallpaperKind::Video?"▶":"▧");
        }
        // The badge has its own dark surface so it stays readable over bright images.
        QFont badge=font();badge.setPointSize(8);badge.setBold(true);painter.setFont(badge);const QString kind=transparent_?QString("◈ 透明"):kindName(entry_.kind);
        const qreal badgeWidth=painter.fontMetrics().horizontalAdvance(kind)+16;const QRectF badgeRect(preview.left()+7,preview.bottom()-29,badgeWidth,22);
        painter.setPen(Qt::NoPen);painter.setBrush(QColor(10,16,24,196));painter.drawRoundedRect(badgeRect,6,6);painter.setPen(Qt::white);painter.drawText(badgeRect,Qt::AlignCenter,kind);painter.restore();
        QFont title=font();title.setPointSize(10);title.setBold(true);painter.setFont(title);painter.setPen(colors.text);
        painter.drawText(QRectF(14,136,width()-28,23),Qt::AlignVCenter|Qt::AlignLeft,painter.fontMetrics().elidedText(entry_.name,Qt::ElideRight,width()-28));
        if(hasFocus()){painter.setPen(QPen(colors.accent,1,Qt::DashLine));painter.setBrush(Qt::NoBrush);painter.drawRoundedRect(frame.adjusted(4,4,-4,-4),8,8);}
    }
    void resizeEvent(QResizeEvent* event)override{QAbstractButton::resizeEvent(event);select_->move(width()-(transparent_?48:86),18);if(remove_)remove_->move(width()-48,18);if(rename_)rename_->move(width()-124,18);}
    void enterEvent(QEnterEvent* event)override{hovered_=true;syncActions();QAbstractButton::enterEvent(event);}
    void leaveEvent(QEvent* event)override{hovered_=false;syncActions();QAbstractButton::leaveEvent(event);}
    void focusInEvent(QFocusEvent* event)override{QAbstractButton::focusInEvent(event);syncActions();}
    void focusOutEvent(QFocusEvent* event)override{QAbstractButton::focusOutEvent(event);QTimer::singleShot(0,this,[this]{syncActions();});}
    bool eventFilter(QObject* object,QEvent* event)override{
        if(event->type()==QEvent::FocusIn||event->type()==QEvent::FocusOut)QTimer::singleShot(0,this,[this]{syncActions();});return QAbstractButton::eventFilter(object,event);
    }
private:
    QToolButton* action(const QString& name,const QString& accessible,const QString& tooltip,QStyle::StandardPixmap icon){
        auto* button=new QToolButton(this);button->setObjectName(name);button->setAccessibleName(accessible);button->setToolTip(tooltip);button->setIcon(style()->standardIcon(icon));button->setIconSize({18,18});button->setFixedSize(32,32);button->setFocusPolicy(Qt::StrongFocus);button->setCursor(Qt::PointingHandCursor);button->installEventFilter(this);
        // Overlay controls deliberately keep an opaque surface over the image.
        Theme::setStyle(button,"QToolButton {background:#ffffff;border:1px solid #dce6eb;border-radius:8px;padding:4px;font-size:18px;} QToolButton:hover, QToolButton:focus {background:#e8f4f1;border:2px solid #178e80;} QToolButton:disabled {background:#f5f8fa;}");button->hide();return button;
    }
    void syncActions(){const auto* focus=QApplication::focusWidget();const bool visible=hovered_||hasFocus()||(focus&&isAncestorOf(focus));select_->setVisible(visible);if(remove_)remove_->setVisible(visible);if(rename_)rename_->setVisible(visible);update();}
    WallpaperEntry entry_;QPixmap thumbnail_;QToolButton* select_{};QToolButton* remove_{};QToolButton* rename_{};bool hovered_{};bool transparent_{};
};

class WallpaperContent final:public QWidget {
public:
    explicit WallpaperContent(QWidget* parent=nullptr):QWidget(parent){
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,8,0);layout->setSpacing(0);
        grid_=new QGridLayout;grid_->setContentsMargins(0,0,0,0);grid_->setHorizontalSpacing(14);grid_->setVerticalSpacing(14);layout->addLayout(grid_);
        empty_=words("还没有本地壁纸，点击下方按钮导入图片、GIF 或视频。","muted");empty_->setObjectName("wallpaperEmptyState");empty_->setAlignment(Qt::AlignCenter);empty_->setMinimumHeight(64);layout->addWidget(empty_);layout->addStretch(1);
    }
    void watchViewport(QWidget* viewport){viewport_=viewport;viewport_->installEventFilter(this);reflow();}
    void setCards(const std::vector<WallpaperCard*>& cards){
        for(auto* card:cards_)grid_->removeWidget(card);cards_=cards;empty_->setVisible(cards_.size()<=1);columns_=0;reflow();
    }
    const std::vector<WallpaperCard*>& cards()const{return cards_;}
protected:
    void resizeEvent(QResizeEvent* event)override{QWidget::resizeEvent(event);reflow();}
    bool eventFilter(QObject* object,QEvent* event)override{if(object==viewport_&&event->type()==QEvent::Resize)reflow();return QWidget::eventFilter(object,event);}
private:
    void reflow(){
        const int available=viewport_?viewport_->width():width();const int columns=std::clamp((available-8+14)/202,1,4);if(columns==columns_)return;columns_=columns;
        for(auto* card:cards_)grid_->removeWidget(card);
        for(size_t index=0;index<cards_.size();++index)grid_->addWidget(cards_[index],static_cast<int>(index)/columns,static_cast<int>(index)%columns);
        for(int column=0;column<4;++column)grid_->setColumnStretch(column,column<columns?1:0);
    }
    QGridLayout* grid_{};QLabel* empty_{};QWidget* viewport_{};int columns_{};std::vector<WallpaperCard*> cards_;
};
}

WallpaperLibraryDialog::WallpaperLibraryDialog(QWidget* parent):QDialog(parent){
    setObjectName("wallpaperLibraryDialog");setProperty("settingsDialog",true);setWindowTitle("壁纸库");setModal(false);setWindowFlag(Qt::WindowContextHelpButtonHint,false);setMinimumSize(620,470);
    Theme::prepareWindow(this);
    QSize preferred(1040,720);if(auto* display=screen()){const auto available=display->availableGeometry();preferred.setWidth(std::min(preferred.width(),std::max(minimumWidth(),available.width()-48)));preferred.setHeight(std::min(preferred.height(),std::max(minimumHeight(),available.height()-80)));}resize(preferred);
    auto* outer=new QVBoxLayout(this);outer->setContentsMargins(22,22,22,18);outer->setSpacing(14);auto* title=words("壁纸库","settingsTitle");Theme::setStyle(title,"font-size:21px;font-weight:700;");outer->addWidget(title);
    outer->addWidget(words("选择透明背景透视窗口后方，或导入图片、GIF 和视频。点击卡片立即应用。","muted"));
    transparencyControls_=new QWidget;transparencyControls_->setObjectName("wallpaperTransparencyControls");
    auto* controls=new QVBoxLayout(transparencyControls_);controls->setContentsMargins(0,0,0,0);controls->setSpacing(7);
    auto* transparencyHeading=new QHBoxLayout;auto* transparencyLabel=words("背景透明度","section");transparencyHeading->addWidget(transparencyLabel);transparencyHeading->addStretch(1);
    transparencyValue_=words({});transparencyValue_->setObjectName("wallpaperTransparencyValue");transparencyValue_->setMinimumWidth(42);transparencyValue_->setAlignment(Qt::AlignRight|Qt::AlignVCenter);transparencyHeading->addWidget(transparencyValue_);controls->addLayout(transparencyHeading);
    auto* transparencyRow=new QHBoxLayout;transparencyRow->setSpacing(12);transparencyRow->addWidget(words("不透明","muted"));
    transparency_=new QSlider(Qt::Horizontal);transparency_->setObjectName("wallpaperTransparencySlider");transparency_->setRange(0,100);transparency_->setSingleStep(1);transparency_->setPageStep(10);transparency_->setAccessibleName("背景透明度");transparency_->setAccessibleDescription("0% 不透明，100% 完全透明。数值越高，越能看清窗口后方；文字和轨道保持清晰。");transparency_->setToolTip("数值越高，越能看清窗口后方");transparencyLabel->setBuddy(transparency_);transparencyRow->addWidget(transparency_,1);transparencyRow->addWidget(words("透明","muted"));controls->addLayout(transparencyRow);outer->addWidget(transparencyControls_);
    scroll_=new QScrollArea;scroll_->setObjectName("wallpaperLibraryScroll");scroll_->setWidgetResizable(true);scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);outer->addWidget(scroll_,1);
    auto* content=new WallpaperContent;content_=content;content->setObjectName("wallpaperLibraryContent");scroll_->setWidget(content);content->watchViewport(scroll_->viewport());
    auto* divider=new QFrame;divider->setFrameShape(QFrame::HLine);outer->addWidget(divider);
    status_=words({},"muted");status_->setObjectName("wallpaperLibraryStatus");status_->setMinimumHeight(18);status_->setAccessibleName("壁纸状态");outer->addWidget(status_);
    progress_=new QProgressBar;progress_->setObjectName("wallpaperImportProgress");progress_->setRange(0,0);progress_->setTextVisible(false);progress_->setFixedHeight(4);progress_->hide();outer->addWidget(progress_);
    auto* footer=new QHBoxLayout;footer->setSpacing(10);import_=new QPushButton("＋ 导入本地壁纸");import_->setObjectName("wallpaperImportButton");import_->setProperty("primary",true);import_->setAutoDefault(false);footer->addWidget(import_);
    none_=new QPushButton("无壁纸");none_->setObjectName("wallpaperNoneButton");none_->setCheckable(true);none_->setAutoDefault(false);none_->setToolTip("恢复纯色主题背景");footer->addWidget(none_);footer->addStretch(1);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close);buttons->button(QDialogButtonBox::Close)->setText("关闭");buttons->button(QDialogButtonBox::Close)->setAutoDefault(false);footer->addWidget(buttons);outer->addLayout(footer);
    auto& store=WallpaperStore::instance();connect(import_,&QPushButton::clicked,this,&WallpaperLibraryDialog::openImport);connect(none_,&QPushButton::clicked,this,[this]{importReport_.clear();WallpaperStore::instance().clearSelection();syncSelection();});connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::close);
    connect(&store,&WallpaperStore::changed,this,&WallpaperLibraryDialog::scheduleRebuild);connect(&store,&WallpaperStore::activeChanged,this,&WallpaperLibraryDialog::syncSelection);connect(&store,&WallpaperStore::busyChanged,this,&WallpaperLibraryDialog::syncBusy);
    connect(transparency_,&QSlider::valueChanged,this,[](int value){WallpaperStore::instance().setTransparency(value);});connect(&store,&WallpaperStore::transparencyChanged,this,&WallpaperLibraryDialog::syncSelection);
    connect(&store,&WallpaperStore::importFinished,this,[this](int added,const QStringList& errors){
        importReport_=added>0?QString("已导入 %1 张壁纸，点击缩略图即可应用。").arg(added):QString("没有导入新的壁纸。");
        if(!errors.isEmpty()){importReport_+="\n"+errors.mid(0,2).join("\n");if(errors.size()>2)importReport_+=QString("\n另有 %1 个文件未导入，将鼠标移到此处查看详情。").arg(errors.size()-2);}
        status_->setText(importReport_);status_->setToolTip(errors.join("\n"));
    });
    connect(&Preferences::instance(),&Preferences::themeChanged,this,[this]{for(auto* card:static_cast<WallpaperContent*>(content_)->cards())card->refresh();});
    rebuild();syncBusy(store.busy());
}
void WallpaperLibraryDialog::openImport(){
    if(!picker_){
        auto* picker=new QFileDialog(this,"导入本地壁纸");picker_=picker;picker->setObjectName("wallpaperFileDialog");picker->setProperty("settingsDialog",true);picker->setFileMode(QFileDialog::ExistingFiles);picker->setAcceptMode(QFileDialog::AcceptOpen);picker->setModal(false);
        picker->setNameFilters({"壁纸 (*.png *.jpg *.jpeg *.bmp *.gif *.mp4 *.wmv *.avi *.mov *.m4v)","静态图片 (*.png *.jpg *.jpeg *.bmp)","GIF 动图 (*.gif)","视频 (*.mp4 *.wmv *.avi *.mov *.m4v)","所有文件 (*)"});connect(picker,&QFileDialog::filesSelected,this,&WallpaperLibraryDialog::importFiles);
    }
    showWindow(picker_);
}
void WallpaperLibraryDialog::importFiles(const QStringList& paths){if(paths.isEmpty())return;importReport_.clear();status_->setToolTip({});WallpaperStore::instance().importFiles(paths);}
void WallpaperLibraryDialog::renameWallpaper(const QString& id){
    const auto& entries=WallpaperStore::instance().entries();const auto entry=std::find_if(entries.begin(),entries.end(),[&](const auto& item){return item.id==id;});if(entry==entries.end())return;
    if(!renameWindow_){
        auto* dialog=new QInputDialog(this);renameWindow_=dialog;dialog->setObjectName("wallpaperRenameDialog");dialog->setProperty("settingsDialog",true);dialog->setWindowTitle("编辑壁纸名称");dialog->setInputMode(QInputDialog::TextInput);dialog->setLabelText("壁纸名称");dialog->setOkButtonText("保存");dialog->setCancelButtonText("取消");dialog->setModal(false);dialog->setMinimumWidth(360);Theme::prepareWindow(dialog);
        auto* input=dialog->findChild<QLineEdit*>();input->setObjectName("wallpaperRenameInput");input->setMaxLength(512);
        connect(dialog,&QInputDialog::textValueChanged,this,[dialog](const QString& value){if(auto* buttons=dialog->findChild<QDialogButtonBox*>())buttons->button(QDialogButtonBox::Ok)->setEnabled(!value.trimmed().isEmpty());});
        connect(dialog,&QInputDialog::textValueSelected,this,[this](const QString& value){
            importReport_.clear();if(WallpaperStore::instance().rename(renamingId_,value))syncSelection();else status_->setText("名称无效或壁纸已被移除，请重新选择。");
        });
    }
    renamingId_=id;renameWindow_->setTextValue(entry->name);showWindow(renameWindow_);if(auto* input=renameWindow_->findChild<QLineEdit*>()){input->setFocus();input->selectAll();}
}
void WallpaperLibraryDialog::scheduleRebuild(){if(rebuildPending_)return;rebuildPending_=true;QTimer::singleShot(0,this,[this]{rebuildPending_=false;rebuild();});}
void WallpaperLibraryDialog::rebuild(){
    // Selection also emits changed(). Keep existing cards and decoded thumbnails
    // unless their content changed, so switching wallpaper stays immediate.
    QStringList keys{"builtin-transparent"};for(const auto& entry:WallpaperStore::instance().entries())keys<<entry.id<<entry.name<<entry.path<<entry.thumbnail<<QString::number(static_cast<int>(entry.kind));
    if(keys==entryKeys_){syncSelection();return;}entryKeys_=keys;
    auto* content=static_cast<WallpaperContent*>(content_);const auto previous=content->cards();content->setCards({});for(auto* card:previous){card->hide();card->deleteLater();}
    WallpaperEntry transparentEntry;transparentEntry.id="builtin-transparent";transparentEntry.name="透明背景";
    std::vector<WallpaperCard*> cards;cards.push_back(new WallpaperCard(transparentEntry,[this]{importReport_.clear();WallpaperStore::instance().selectTransparent();syncSelection();},{},{},content,true));
    for(const auto& entry:WallpaperStore::instance().entries()){
        auto* card=new WallpaperCard(entry,[this,id=entry.id]{importReport_.clear();if(!WallpaperStore::instance().select(id))status_->setText("壁纸文件不可用，请重新导入。");else syncSelection();},[this,id=entry.id]{importReport_.clear();if(!WallpaperStore::instance().remove(id))status_->setText("壁纸未能删除，请稍后重试。");},[this,id=entry.id]{renameWallpaper(id);},content);cards.push_back(card);
    }
    content->setCards(cards);for(auto* card:cards)card->setImportBusy(WallpaperStore::instance().busy());syncSelection();
}
void WallpaperLibraryDialog::syncSelection(){
    auto& store=WallpaperStore::instance();for(auto* card:static_cast<WallpaperContent*>(content_)->cards())card->refresh();none_->setChecked(store.activeId().isEmpty()&&!store.transparent());
    transparencyControls_->setVisible(store.transparent());{const QSignalBlocker blocker(transparency_);transparency_->setValue(store.transparency());}transparencyValue_->setText(QString::number(store.transparency())+"%");
    if(store.busy())return;if(!importReport_.isEmpty()){status_->setText(importReport_);return;}status_->setToolTip({});if(store.transparent())status_->setText(QString("当前：透明背景 · 背景透明度 %1% · 拖动滑块即时调整。").arg(store.transparency()));else if(const auto* active=store.active())status_->setText("当前壁纸："+active->name+" · "+kindName(active->kind));else status_->setText("当前：无壁纸 · 导入后可悬停选择或删除。删除仅移除壁纸库副本。");
}
void WallpaperLibraryDialog::syncBusy(bool busy){import_->setEnabled(!busy);progress_->setVisible(busy);for(auto* card:static_cast<WallpaperContent*>(content_)->cards())card->setImportBusy(busy);if(busy)status_->setText("正在导入壁纸并生成缩略图…");else syncSelection();}
void WallpaperLibraryDialog::closeEvent(QCloseEvent* event){if(picker_)picker_->close();if(renameWindow_)renameWindow_->close();QDialog::closeEvent(event);}
void WallpaperLibraryDialog::hideEvent(QHideEvent* event){if(picker_)picker_->close();if(renameWindow_)renameWindow_->close();QDialog::hideEvent(event);}
}
