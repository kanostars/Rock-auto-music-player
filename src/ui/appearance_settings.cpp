#include "appearance_settings.h"
#include "theme.h"
#include "theme_image.h"
#include "wallpaper_library.h"
#include "app/preferences.h"
#include <QAbstractButton>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace rock {
namespace {
QLabel* label(const QString& value,const char* role=nullptr){
    auto* result=new QLabel(value);result->setTextFormat(Qt::PlainText);result->setWordWrap(true);
    if(role)result->setProperty("role",role);return result;
}
void configureWindow(QDialog* window,const char* name,const QString& title,int width,int height,int minWidth,int minHeight){
    window->setObjectName(name);window->setProperty("settingsDialog",true);window->setWindowTitle(title);
    window->setWindowFlag(Qt::WindowContextHelpButtonHint,false);window->setModal(false);
    Theme::prepareWindow(window);
    window->setMinimumSize(minWidth,minHeight);
    if(auto* display=window->screen()){
        const auto available=display->availableGeometry();width=std::min(width,std::max(minWidth,available.width()-48));height=std::min(height,std::max(minHeight,available.height()-80));
    }
    window->resize(width,height);
}
void showWindow(QDialog* window){if(window->isMinimized())window->showNormal();else window->show();window->raise();window->activateWindow();}
void addClose(QDialog* window,QBoxLayout* layout){
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close);buttons->button(QDialogButtonBox::Close)->setText("关闭");
    buttons->button(QDialogButtonBox::Close)->setAutoDefault(false);QObject::connect(buttons,&QDialogButtonBox::rejected,window,&QDialog::close);layout->addWidget(buttons);
}
void drawPreview(QPainter& painter,const QRectF& bounds,const Theme::Palette& colors){
    painter.save();painter.setPen(QPen(colors.border,1));painter.setBrush(colors.background);painter.drawRoundedRect(bounds,9,9);
    const double inset=std::max(8.0,bounds.width()*.04),leftWidth=bounds.width()*.24;
    const QRectF sidebar(bounds.left()+inset,bounds.top()+inset,leftWidth,bounds.height()-inset*2);
    painter.setPen(Qt::NoPen);painter.setBrush(colors.raised);painter.drawRoundedRect(sidebar,5,5);
    const auto bar=[&](QRectF rect,const QColor& color,double radius=2){painter.setBrush(color);painter.drawRoundedRect(rect,radius,radius);};
    bar(sidebar.adjusted(5,7,-5,-sidebar.height()+17),colors.accent);
    bar(QRectF(sidebar.left()+5,sidebar.top()+23,sidebar.width()-10,5),colors.muted);
    bar(QRectF(sidebar.left()+5,sidebar.top()+33,sidebar.width()-15,5),colors.muted);
    const double mainLeft=sidebar.right()+inset,mainWidth=bounds.right()-inset-mainLeft;
    bar(QRectF(mainLeft,bounds.top()+inset,mainWidth,8),colors.muted,3);
    bar(QRectF(mainLeft,bounds.top()+inset+15,mainWidth*.63,6),colors.border,3);
    const QRectF work(mainLeft,bounds.top()+inset+30,mainWidth,std::max(16.0,bounds.height()-inset*2-30));
    bar(work,colors.surface,4);
    const double noteHeight=std::max(5.0,std::min(9.0,work.height()*.25));
    for(int i=0;i<3;++i)bar(QRectF(work.left()+5+i*mainWidth*.22,work.top()+5+(i%2)*noteHeight,mainWidth*.25,noteHeight),colors.accent,2);
    painter.restore();
}
class PreviewCanvas final:public QWidget {
public:
    explicit PreviewCanvas(std::function<Theme::Palette()> colors,QWidget* parent=nullptr):QWidget(parent),colors_(std::move(colors)){setMinimumSize(250,150);}
protected:
    void paintEvent(QPaintEvent*) override{QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);drawPreview(painter,rect().adjusted(1,1,-1,-1),colors_());}
private:std::function<Theme::Palette()> colors_;
};
class ThemeCard final:public QAbstractButton {
public:
    ThemeCard(Theme::Preset preset,bool dark,QWidget* parent=nullptr):QAbstractButton(parent),preset_(std::move(preset)),dark_(dark){
        setObjectName("themeCard_"+preset_.id+(dark?"_dark":"_light"));setProperty("themeId",preset_.id);setProperty("darkMode",dark);
        setCheckable(true);setCursor(Qt::PointingHandCursor);setFocusPolicy(Qt::StrongFocus);setMinimumSize(188,184);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        setToolTip(preset_.description+"\n点击立即应用"+(dark?QString("深色"):QString("浅色"))+"主题");refresh();
    }
    QSize sizeHint() const override{return {220,184};}
    void refresh(){
        const auto& preferences=Preferences::instance();const bool selected=preferences.themeId()==preset_.id&&preferences.darkTheme()==dark_;
        setChecked(selected);setAccessibleName(preset_.name+" · "+(dark_?"深色":"浅色")+(selected?" · 使用中":""));setAccessibleDescription(preset_.description);update();
    }
protected:
    void paintEvent(QPaintEvent*) override{
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        const auto current=Theme::palette(Preferences::instance().themeId(),Preferences::instance().darkTheme());
        const auto preview=Theme::palette(preset_.id,dark_);const auto frame=QRectF(rect()).adjusted(2,2,-2,-2);
        painter.setBrush(current.surface);painter.setPen(QPen(isChecked()?current.accent:current.border,isChecked()?2.5:1));painter.drawRoundedRect(frame,12,12);
        drawPreview(painter,QRectF(14,14,width()-28,87),preview);
        QFont heading=font();heading.setPointSize(11);heading.setBold(true);painter.setFont(heading);painter.setPen(current.text);
        const QString name=painter.fontMetrics().elidedText(preset_.name,Qt::ElideRight,width()-93);
        painter.drawText(QRectF(15,111,width()-30,24),Qt::AlignVCenter|Qt::AlignLeft,name);
        const double dotX=std::min(double(width()-28),15.0+painter.fontMetrics().horizontalAdvance(name)+12);
        painter.setPen(Qt::NoPen);painter.setBrush(preview.accent);painter.drawEllipse(QPointF(dotX,123),4,4);
        if(isChecked()){
            QFont status=font();status.setPointSize(8);painter.setFont(status);painter.setPen(current.accent);painter.drawText(QRectF(width()-65,113,50,20),Qt::AlignRight|Qt::AlignVCenter,"使用中");
        }
        painter.setPen(current.muted);QFont description=font();description.setPointSize(9);painter.setFont(description);
        painter.drawText(QRectF(15,141,width()-30,25),Qt::AlignLeft|Qt::AlignTop,painter.fontMetrics().elidedText(preset_.description,Qt::ElideRight,width()-30));
        if(hasFocus()){painter.setPen(QPen(current.accent,1,Qt::DashLine));painter.setBrush(Qt::NoBrush);painter.drawRoundedRect(frame.adjusted(4,4,-4,-4),8,8);}
    }
private:Theme::Preset preset_;bool dark_{};
};
class GalleryContent final:public QWidget {
public:
    struct Section {QGridLayout* grid{};std::vector<ThemeCard*> cards;};
    std::vector<Section> sections;
    void watchViewport(QWidget* viewport){viewport_=viewport;viewport_->installEventFilter(this);reflow(viewport_->width());}
protected:
    void resizeEvent(QResizeEvent* event) override{
        QWidget::resizeEvent(event);reflow(viewport_?viewport_->width():width());
    }
    bool eventFilter(QObject* watched,QEvent* event) override{
        if(watched==viewport_&&event->type()==QEvent::Resize)reflow(viewport_->width());return QWidget::eventFilter(watched,event);
    }
private:
    void reflow(int width){
        // The viewport sets the available width. The grid's own minimum width
        // must not keep an oversized column count alive in a smaller window.
        const int columns=std::clamp((width-8+14)/202,1,4);
        if(columns==columns_)return;columns_=columns;
        for(auto& section:sections){
            for(auto* card:section.cards)section.grid->removeWidget(card);
            for(size_t i=0;i<section.cards.size();++i)section.grid->addWidget(section.cards[i],static_cast<int>(i)/columns,static_cast<int>(i)%columns);
            for(int i=0;i<4;++i)section.grid->setColumnStretch(i,i<columns?1:0);
        }
    }
    QWidget* viewport_{};int columns_{};
};
QString presetName(const QString& id){for(const auto& preset:Theme::presets())if(preset.id==id)return preset.name;return "自定义配色";}
}
AppearanceSettingsDialog::AppearanceSettingsDialog(QWidget* parent):QDialog(parent){
    configureWindow(this,"appearanceSettingsDialog","外观主题",760,460,620,410);
    auto* outer=new QVBoxLayout(this);outer->setContentsMargins(24,23,24,18);outer->setSpacing(17);
    auto* title=label("外观主题","settingsTitle");Theme::setStyle(title,"font-size:19px;font-weight:700;");outer->addWidget(title);
    outer->addWidget(label("选择喜欢的界面模式与配色，让工作台更合你的习惯。","muted"));
    auto* options=new QFrame;options->setObjectName("appearanceOptions");Theme::setStyle(options,"QFrame#appearanceOptions {background:#ffffff;border:1px solid #e0e9ed;border-radius:10px;}");outer->addWidget(options);
    auto* rows=new QVBoxLayout(options);rows->setContentsMargins(18,8,18,8);rows->setSpacing(0);
    auto* modeRow=new QHBoxLayout;modeRow->setSpacing(16);auto* modeWords=new QVBoxLayout;modeWords->setSpacing(5);modeWords->addWidget(label("界面模式","section"));modeWords->addWidget(label("浅色明亮 · 深色护眼 · 跟随系统偏好","muted"));modeRow->addLayout(modeWords,1);
    auto* modes=new QWidget;modes->setObjectName("interfaceModeGroup");modes->setAttribute(Qt::WA_StyledBackground,true);auto* segmented=new QHBoxLayout(modes);segmented->setContentsMargins(4,4,4,4);segmented->setSpacing(2);auto* group=new QButtonGroup(this);group->setExclusive(true);
    const std::array<const char*,3> names{"light","dark","system"};const std::array<const char*,3> captions{"浅色","深色","跟随系统"};
    for(int i=0;i<3;++i){auto* button=new QPushButton(captions[i]);button->setObjectName("interfaceMode_"+QString::fromLatin1(names[i]));button->setCheckable(true);button->setAutoDefault(false);button->setMinimumWidth(i==2?92:66);button->setFixedHeight(34);Theme::setStyle(button,"QPushButton {background:transparent;border:0px solid transparent;border-radius:17px;padding:7px 10px;} QPushButton:checked {background:#178e80;color:#ffffff;} QPushButton:hover:!checked {background:#e8f4f1;}");group->addButton(button,i);modeButtons_[i]=button;segmented->addWidget(button);}
    Theme::setStyle(modes,"QWidget#interfaceModeGroup {background:#f5f8fa;border:1px solid #dce6eb;border-radius:20px;}");
    modeRow->addWidget(modes);rows->addLayout(modeRow);rows->addSpacing(12);auto* divider=new QFrame;divider->setFrameShape(QFrame::HLine);Theme::setStyle(divider,"color:#e0e9ed;");rows->addWidget(divider);rows->addSpacing(12);
    auto* themeRow=new QHBoxLayout;themeRow->setSpacing(12);auto* themeWords=new QVBoxLayout;themeWords->setSpacing(5);themeWords->addWidget(label("界面主题","section"));themeWords->addWidget(label("从主题库挑选，或在此切换","muted"));themeRow->addLayout(themeWords,1);
    preset_=new QComboBox;preset_->setObjectName("themePresetSelector");preset_->setAccessibleName("界面主题");preset_->setMinimumWidth(175);
    for(const auto& preset:Theme::presets())preset_->addItem(preset.name,preset.id);preset_->addItem("自定义配色","custom");themeRow->addWidget(preset_);
    auto* library=new QPushButton("主题库");library->setObjectName("openThemeLibraryButton");library->setAutoDefault(false);library->setMinimumWidth(94);themeRow->addWidget(library);rows->addLayout(themeRow);rows->addSpacing(12);
    auto* wallpaperDivider=new QFrame;wallpaperDivider->setFrameShape(QFrame::HLine);Theme::setStyle(wallpaperDivider,"color:#e0e9ed;");rows->addWidget(wallpaperDivider);rows->addSpacing(12);
    auto* wallpaperRow=new QHBoxLayout;auto* wallpaperWords=new QVBoxLayout;wallpaperWords->setSpacing(5);wallpaperWords->addWidget(label("界面壁纸","section"));wallpaperWords->addWidget(label("透明背景 · 图片 · GIF · 静音视频","muted"));wallpaperRow->addLayout(wallpaperWords,1);
    auto* wallpaper=new QPushButton("壁纸库");wallpaper->setObjectName("openWallpaperLibraryButton");wallpaper->setAccessibleName("打开壁纸库，选择透明背景或导入壁纸");wallpaper->setAutoDefault(false);wallpaper->setMinimumWidth(175);wallpaperRow->addWidget(wallpaper);rows->addLayout(wallpaperRow);rows->addSpacing(8);outer->addStretch();
    auto* footer=new QHBoxLayout;footer->addWidget(label("设置即时生效并自动保存。","muted"),1);addClose(this,footer);outer->addLayout(footer);
    connect(group,&QButtonGroup::idClicked,this,[](int id){Preferences::instance().setInterfaceMode(static_cast<InterfaceMode>(id));});
    connect(preset_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){if(index>=0)Preferences::instance().setThemeId(preset_->itemData(index).toString());});
    connect(library,&QPushButton::clicked,this,&AppearanceSettingsDialog::openLibrary);connect(&Preferences::instance(),&Preferences::themeChanged,this,&AppearanceSettingsDialog::syncFromPreferences);syncFromPreferences();
    connect(wallpaper,&QPushButton::clicked,this,&AppearanceSettingsDialog::openWallpapers);
}
void AppearanceSettingsDialog::openWallpapers(){if(!wallpaperWindow_)wallpaperWindow_=new WallpaperLibraryDialog(this);showWindow(wallpaperWindow_);}
void AppearanceSettingsDialog::syncFromPreferences(){
    const auto& preferences=Preferences::instance();const int mode=static_cast<int>(preferences.interfaceMode());
    for(int i=0;i<3;++i){const QSignalBlocker blocker(modeButtons_[i]);modeButtons_[i]->setChecked(i==mode);}
    {const QSignalBlocker blocker(preset_);preset_->setCurrentIndex(preset_->findData(preferences.themeId()));}
    for(auto* card:cards_)static_cast<ThemeCard*>(card)->refresh();
    if(libraryStatus_)libraryStatus_->setText(QString("当前：%1 · %2").arg(presetName(preferences.themeId()),preferences.interfaceMode()==InterfaceMode::System?QString("跟随系统"):preferences.darkTheme()?QString("深色"):QString("浅色")));
    if(imageStatus_)imageStatus_->setText("当前强调色 "+preferences.customThemeAccent().name().toUpper()+" · 点击色块即时应用");
    if(imageWindow_){
        if(auto* preview=imageWindow_->findChild<QWidget*>("extractedThemePreview"))preview->update();
        const auto border=Theme::palette(preferences.themeId(),preferences.darkTheme()).text.name();
        for(auto* swatch:imageWindow_->findChildren<QPushButton*>())if(swatch->objectName().startsWith("extractedColor_")){
            const auto color=swatch->property("color").value<QColor>();swatch->setChecked(color==preferences.customThemeAccent());
            swatch->setAccessibleDescription(swatch->isChecked()?"当前强调色":"点击应用此颜色");
            swatch->setStyleSheet("QPushButton {background:"+color.name()+";border:2px solid #ffffff;border-radius:7px;} QPushButton:checked, QPushButton:focus {border:3px solid "+border+";}");
        }
    }
}
void AppearanceSettingsDialog::openLibrary(){
    if(!library_){
        library_=new QDialog(this);configureWindow(library_,"themeLibraryDialog","主题库",1040,760,620,500);
        auto* outer=new QVBoxLayout(library_);outer->setContentsMargins(22,22,22,18);outer->setSpacing(14);auto* heading=label("主题库","settingsTitle");Theme::setStyle(heading,"font-size:21px;font-weight:700;");outer->addWidget(heading);outer->addWidget(label("点击卡片立即换肤。每个主题均有深色与浅色基底，也可以自定义强调色。","muted"));
        auto* scroll=new QScrollArea;scroll->setObjectName("themeLibraryScroll");scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);outer->addWidget(scroll,1);auto* content=new GalleryContent;auto* sections=new QVBoxLayout(content);sections->setContentsMargins(0,0,8,0);sections->setSpacing(16);scroll->setWidget(content);
        for(bool dark:{true,false}){
            auto* title=label(dark?"深色主题":"浅色主题","section");title->setObjectName(dark?"darkThemeGroupTitle":"lightThemeGroupTitle");sections->addWidget(title);auto* grid=new QGridLayout;grid->setContentsMargins(0,0,0,0);grid->setSpacing(14);sections->addLayout(grid);GalleryContent::Section section;section.grid=grid;int count=0;
            for(const auto& preset:Theme::presets())if(dark?preset.dark:preset.light){auto* card=new ThemeCard(preset,dark,content);cards_.push_back(card);section.cards.push_back(card);grid->addWidget(card,count/4,count%4);++count;connect(card,&QAbstractButton::clicked,this,[this,id=preset.id,dark]{auto& preferences=Preferences::instance();preferences.setThemeId(id);preferences.setInterfaceMode(dark?InterfaceMode::Dark:InterfaceMode::Light);syncFromPreferences();});}
            title->setText(title->text()+QString("  %1").arg(count));content->sections.push_back(std::move(section));
        }
        content->watchViewport(scroll->viewport());
        auto* divider=new QFrame;divider->setFrameShape(QFrame::HLine);outer->addWidget(divider);libraryStatus_=label({},"muted");libraryStatus_->setObjectName("themeLibraryStatus");outer->addWidget(libraryStatus_);
        auto* footer=new QHBoxLayout;footer->setSpacing(10);auto* image=new QPushButton("选择图片生成主题");image->setObjectName("importThemeImageButton");image->setAutoDefault(false);image->setProperty("primary",true);footer->addWidget(image);auto* color=new QPushButton("自定义强调色");color->setObjectName("customThemeColorButton");color->setAutoDefault(false);footer->addWidget(color);footer->addStretch();addClose(library_,footer);outer->addLayout(footer);
        connect(image,&QPushButton::clicked,this,&AppearanceSettingsDialog::importImage);connect(color,&QPushButton::clicked,this,&AppearanceSettingsDialog::openCustomColor);connect(library_,&QDialog::finished,this,[this]{closeLibraryChildren();});
    }
    syncFromPreferences();showWindow(library_);
}
void AppearanceSettingsDialog::applyCustomAccent(const QColor& color){
    if(!color.isValid())return;auto& preferences=Preferences::instance();preferences.setCustomThemeAccent(color);preferences.setThemeId("custom");syncFromPreferences();
}
void AppearanceSettingsDialog::openCustomColor(){
    if(!colorPicker_){
        auto* picker=new QColorDialog(this);colorPicker_=picker;picker->setObjectName("customThemeColorDialog");picker->setProperty("settingsDialog",true);picker->setWindowTitle("自定义强调色");picker->setOption(QColorDialog::DontUseNativeDialog,true);picker->setModal(false);connect(picker,&QColorDialog::colorSelected,this,&AppearanceSettingsDialog::applyCustomAccent);
    }
    static_cast<QColorDialog*>(colorPicker_.data())->setCurrentColor(Preferences::instance().customThemeAccent());showWindow(colorPicker_);
}
void AppearanceSettingsDialog::importImage(){
    if(!imagePicker_){
        auto* picker=new QFileDialog(this,"选择图片生成主题");imagePicker_=picker;picker->setObjectName("themeImageFileDialog");picker->setProperty("settingsDialog",true);picker->setFileMode(QFileDialog::ExistingFile);picker->setAcceptMode(QFileDialog::AcceptOpen);picker->setNameFilter("图片 (*.png *.jpg *.jpeg *.bmp *.gif *.ico);;所有文件 (*)");picker->setModal(false);
        connect(picker,&QFileDialog::fileSelected,this,&AppearanceSettingsDialog::importImageFile);
    }
    showWindow(imagePicker_);
}
void AppearanceSettingsDialog::importImageFile(const QString& path){
    Theme::ImageColors colors;QString error;
    if(!Theme::extractImageColors(path,colors,error)||colors.colors.empty()){
        if(libraryStatus_)libraryStatus_->setText(error.isEmpty()?QString("图片中没有可用颜色，请换一张图片。"):error);return;
    }
    applyCustomAccent(colors.colors.front());showImageColors(colors,QFileInfo(path).fileName());
}
void AppearanceSettingsDialog::showImageColors(const Theme::ImageColors& colors,const QString& filename){
    if(imageWindow_){imageWindow_->close();delete imageWindow_.data();}
    imageWindow_=new QDialog(this);configureWindow(imageWindow_,"themeImageImportDialog","图片主题",730,470,620,410);auto* outer=new QVBoxLayout(imageWindow_);outer->setContentsMargins(22,22,22,18);outer->setSpacing(14);outer->addWidget(label("图片主题已应用","settingsTitle"));outer->addWidget(label(filename,"muted"));
    auto* previews=new QHBoxLayout;previews->setSpacing(18);auto* reference=new QLabel;reference->setObjectName("themeReferenceImage");reference->setAlignment(Qt::AlignCenter);reference->setMinimumSize(260,180);reference->setPixmap(QPixmap::fromImage(colors.thumbnail).scaled(290,190,Qt::KeepAspectRatio,Qt::SmoothTransformation));previews->addWidget(reference,1);auto* preview=new PreviewCanvas([]{return Theme::palette("custom",Preferences::instance().darkTheme());});preview->setObjectName("extractedThemePreview");previews->addWidget(preview,1);outer->addLayout(previews,1);
    auto* swatches=new QHBoxLayout;swatches->setSpacing(10);swatches->addWidget(label("提取的颜色","section"));
    for(size_t i=0;i<colors.colors.size();++i){const auto color=colors.colors[i];auto* swatch=new QPushButton;swatch->setObjectName("extractedColor_"+QString::number(i));swatch->setAccessibleName("使用颜色 "+color.name());swatch->setToolTip(color.name().toUpper());swatch->setProperty("color",color);swatch->setCheckable(true);swatch->setFixedSize(42,34);swatch->setAutoDefault(false);swatch->setCursor(Qt::PointingHandCursor);swatches->addWidget(swatch);connect(swatch,&QPushButton::clicked,this,[this,color]{applyCustomAccent(color);});}swatches->addStretch();outer->addLayout(swatches);
    imageStatus_=label({},"muted");imageStatus_->setObjectName("imageThemeStatus");outer->addWidget(imageStatus_);auto* footer=new QHBoxLayout;footer->addWidget(label("参考图用于提取配色，界面保持清晰易读。","muted"),1);addClose(imageWindow_,footer);outer->addLayout(footer);connect(imageWindow_,&QObject::destroyed,this,[this]{imageStatus_=nullptr;});syncFromPreferences();showWindow(imageWindow_);
}
void AppearanceSettingsDialog::closeLibraryChildren(){for(auto* popup:{imageWindow_.data(),imagePicker_.data(),colorPicker_.data()})if(popup)popup->close();}
void AppearanceSettingsDialog::closeWindows(){closeLibraryChildren();if(library_)library_->close();if(wallpaperWindow_)wallpaperWindow_->close();}
void AppearanceSettingsDialog::closeEvent(QCloseEvent* event){closeWindows();QDialog::closeEvent(event);}
void AppearanceSettingsDialog::hideEvent(QHideEvent* event){closeWindows();QDialog::hideEvent(event);}
}
