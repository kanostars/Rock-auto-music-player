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
    WallpaperCard(const WallpaperEntry& entry,std::function<void()> select,std::function<void()> remove,std::function<void()> rename,QWidget* parent):QAbstractButton(parent),entry_(entry){
        setObjectName("wallpaperCard_"+entry.id);setProperty("wallpaperId",entry.id);setProperty("wallpaperKind",static_cast<int>(entry.kind));
        setCheckable(true);setFocusPolicy(Qt::StrongFocus);setCursor(Qt::PointingHandCursor);setMinimumSize(188,174);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        setToolTip(entry.name+"\n点击立即设为壁纸");thumbnail_.load(entry.thumbnail);
        if(thumbnail_.isNull()&&entry.kind!=WallpaperKind::Video)thumbnail_.load(entry.path);
        select_=action("wallpaperSelect_"+entry.id,"选择壁纸 "+entry.name,"选择壁纸",QStyle::SP_DialogApplyButton);
        remove_=action("wallpaperDelete_"+entry.id,"删除壁纸 "+entry.name,"从壁纸库删除",QStyle::SP_TrashIcon);
        rename_=action("wallpaperRename_"+entry.id,"重命名壁纸 "+entry.name,"编辑名称",QStyle::SP_FileDialogDetailedView);rename_->setIcon(QIcon());rename_->setText("✎");
        QObject::connect(this,&QAbstractButton::clicked,this,[select]{select();});
        QObject::connect(select_,&QToolButton::clicked,this,[select]{select();});
        QObject::connect(remove_,&QToolButton::clicked,this,[remove]{remove();});refresh();
        QObject::connect(rename_,&QToolButton::clicked,this,[rename]{rename();});
    }
    QSize sizeHint()const override{return {224,174};}
    void refresh(){
        const bool selected=WallpaperStore::instance().activeId()==entry_.id;setChecked(selected);
        setAccessibleName(entry_.name+" · "+kindName(entry_.kind)+(selected?" · 使用中":""));setAccessibleDescription("点击或按空格应用壁纸；聚焦后可选择、重命名或删除。");
        select_->setToolTip(selected?"正在使用":"选择壁纸");select_->setEnabled(!selected);update();
    }
    void setImportBusy(bool busy){remove_->setEnabled(!busy);remove_->setToolTip(busy?"导入完成后可删除":"从壁纸库删除");}
protected:
    void paintEvent(QPaintEvent*)override{
        const auto colors=Theme::palette(Preferences::instance().themeId(),Preferences::instance().darkTheme());
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);const QRectF frame=QRectF(rect()).adjusted(2,2,-2,-2);
        painter.setPen(QPen(isChecked()?colors.accent:colors.border,isChecked()?2.5:1));painter.setBrush(colors.surface);painter.drawRoundedRect(frame,12,12);
        const QRectF preview(12,12,width()-24,112);QPainterPath clip;clip.addRoundedRect(preview,8,8);painter.save();painter.setClipPath(clip);
        painter.fillRect(preview,colors.raised);
        if(!thumbnail_.isNull()){
            const qreal scale=std::max(preview.width()/thumbnail_.width(),preview.height()/thumbnail_.height());
            const QSizeF size(thumbnail_.width()*scale,thumbnail_.height()*scale);const QRectF target(preview.center()-QPointF(size.width()/2,size.height()/2),size);painter.drawPixmap(target,thumbnail_,QRectF(thumbnail_.rect()));
        }else{
            QFont placeholder=font();placeholder.setPointSize(26);painter.setFont(placeholder);painter.setPen(colors.muted);painter.drawText(preview,Qt::AlignCenter,entry_.kind==WallpaperKind::Video?"▶":"▧");
        }
        // The badge has its own dark surface so it stays readable over bright images.
        QFont badge=font();badge.setPointSize(8);badge.setBold(true);painter.setFont(badge);const QString kind=kindName(entry_.kind);
        const qreal badgeWidth=painter.fontMetrics().horizontalAdvance(kind)+16;const QRectF badgeRect(preview.left()+7,preview.bottom()-29,badgeWidth,22);
        painter.setPen(Qt::NoPen);painter.setBrush(QColor(10,16,24,196));painter.drawRoundedRect(badgeRect,6,6);painter.setPen(Qt::white);painter.drawText(badgeRect,Qt::AlignCenter,kind);painter.restore();
        QFont title=font();title.setPointSize(10);title.setBold(true);painter.setFont(title);painter.setPen(colors.text);
        painter.drawText(QRectF(14,136,width()-28,23),Qt::AlignVCenter|Qt::AlignLeft,painter.fontMetrics().elidedText(entry_.name,Qt::ElideRight,width()-28));
        if(hasFocus()){painter.setPen(QPen(colors.accent,1,Qt::DashLine));painter.setBrush(Qt::NoBrush);painter.drawRoundedRect(frame.adjusted(4,4,-4,-4),8,8);}
    }
    void resizeEvent(QResizeEvent* event)override{QAbstractButton::resizeEvent(event);remove_->move(width()-48,18);select_->move(width()-86,18);rename_->move(width()-124,18);}
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
    void syncActions(){const auto* focus=QApplication::focusWidget();const bool visible=hovered_||hasFocus()||(focus&&isAncestorOf(focus));select_->setVisible(visible);remove_->setVisible(visible);rename_->setVisible(visible);update();}
    WallpaperEntry entry_;QPixmap thumbnail_;QToolButton* select_{};QToolButton* remove_{};QToolButton* rename_{};bool hovered_{};
};

class WallpaperContent final:public QWidget {
public:
    explicit WallpaperContent(QWidget* parent=nullptr):QWidget(parent){
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,8,0);layout->setSpacing(0);
        empty_=words("▧\n\n还没有壁纸\n导入喜欢的图片、GIF 或视频，打造自己的演奏空间。","muted");empty_->setObjectName("wallpaperEmptyState");empty_->setAlignment(Qt::AlignCenter);empty_->setMinimumHeight(260);layout->addWidget(empty_);
        grid_=new QGridLayout;grid_->setContentsMargins(0,0,0,0);grid_->setHorizontalSpacing(14);grid_->setVerticalSpacing(14);layout->addLayout(grid_);layout->addStretch(1);
    }
    void watchViewport(QWidget* viewport){viewport_=viewport;viewport_->installEventFilter(this);reflow();}
    void setCards(const std::vector<WallpaperCard*>& cards){
        for(auto* card:cards_)grid_->removeWidget(card);cards_=cards;empty_->setVisible(cards_.empty());columns_=0;reflow();
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
    QSize preferred(1040,720);if(auto* display=screen()){const auto available=display->availableGeometry();preferred.setWidth(std::min(preferred.width(),std::max(minimumWidth(),available.width()-48)));preferred.setHeight(std::min(preferred.height(),std::max(minimumHeight(),available.height()-80)));}resize(preferred);
    auto* outer=new QVBoxLayout(this);outer->setContentsMargins(22,22,22,18);outer->setSpacing(14);auto* title=words("壁纸库","settingsTitle");Theme::setStyle(title,"font-size:21px;font-weight:700;");outer->addWidget(title);
    outer->addWidget(words("让喜欢的风景陪你演奏。支持静态图片、GIF 动图和视频，点击卡片立即应用。","muted"));
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
        auto* dialog=new QInputDialog(this);renameWindow_=dialog;dialog->setObjectName("wallpaperRenameDialog");dialog->setProperty("settingsDialog",true);dialog->setWindowTitle("编辑壁纸名称");dialog->setInputMode(QInputDialog::TextInput);dialog->setLabelText("壁纸名称");dialog->setOkButtonText("保存");dialog->setCancelButtonText("取消");dialog->setModal(false);dialog->setMinimumWidth(360);
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
    QStringList keys;for(const auto& entry:WallpaperStore::instance().entries())keys<<entry.id<<entry.name<<entry.path<<entry.thumbnail<<QString::number(static_cast<int>(entry.kind));
    if(keys==entryKeys_){syncSelection();return;}entryKeys_=keys;
    auto* content=static_cast<WallpaperContent*>(content_);const auto previous=content->cards();content->setCards({});for(auto* card:previous){card->hide();card->deleteLater();}
    std::vector<WallpaperCard*> cards;for(const auto& entry:WallpaperStore::instance().entries()){
        auto* card=new WallpaperCard(entry,[this,id=entry.id]{importReport_.clear();if(!WallpaperStore::instance().select(id))status_->setText("壁纸文件不可用，请重新导入。");else syncSelection();},[this,id=entry.id]{importReport_.clear();if(!WallpaperStore::instance().remove(id))status_->setText("壁纸未能删除，请稍后重试。");},[this,id=entry.id]{renameWallpaper(id);},content);cards.push_back(card);
    }
    content->setCards(cards);for(auto* card:cards)card->setImportBusy(WallpaperStore::instance().busy());syncSelection();
}
void WallpaperLibraryDialog::syncSelection(){
    auto& store=WallpaperStore::instance();for(auto* card:static_cast<WallpaperContent*>(content_)->cards())card->refresh();none_->setChecked(store.activeId().isEmpty());
    if(store.busy())return;if(!importReport_.isEmpty()){status_->setText(importReport_);return;}status_->setToolTip({});if(const auto* active=store.active())status_->setText("当前壁纸："+active->name+" · "+kindName(active->kind));else status_->setText("当前：无壁纸 · 导入后可悬停选择或删除。删除仅移除壁纸库副本。");
}
void WallpaperLibraryDialog::syncBusy(bool busy){import_->setEnabled(!busy);progress_->setVisible(busy);for(auto* card:static_cast<WallpaperContent*>(content_)->cards())card->setImportBusy(busy);if(busy)status_->setText("正在导入壁纸并生成缩略图…");else syncSelection();}
void WallpaperLibraryDialog::closeEvent(QCloseEvent* event){if(picker_)picker_->close();if(renameWindow_)renameWindow_->close();QDialog::closeEvent(event);}
void WallpaperLibraryDialog::hideEvent(QHideEvent* event){if(picker_)picker_->close();if(renameWindow_)renameWindow_->close();QDialog::hideEvent(event);}
}
