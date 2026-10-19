#include "theme.h"
#include "wallpaper_store.h"
#include "wallpaper_layer.h"
#include "app/preferences.h"
#include <QApplication>
#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QEvent>
#include <QLabel>
#include <QPalette>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QWidget>
#include <QDialog>
#include <QFileDialog>
#include <QColorDialog>
#include <QScrollArea>
#include <algorithm>
#include <cmath>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif
namespace rock::Theme {
namespace {
void configureScrollWallpaper(QWidget* widget){
    auto* scroll=qobject_cast<QScrollArea*>(widget);if(!scroll)return;
    const auto& store=WallpaperStore::instance();const bool enabled=store.transparent()||store.active();
    for(auto* surface:{scroll->viewport(),scroll->widget()})if(surface){
        const auto original=surface->property("wallpaperAutoFillBackground");
        if(enabled){if(!original.isValid())surface->setProperty("wallpaperAutoFillBackground",surface->autoFillBackground());surface->setAutoFillBackground(false);}
        else if(original.isValid()){surface->setAutoFillBackground(original.toBool());surface->setProperty("wallpaperAutoFillBackground",QVariant());}
    }
}
void ensureDialogWallpaper(QWidget* widget){
    const auto& store=WallpaperStore::instance();
    if(!qobject_cast<QDialog*>(widget)||qobject_cast<QFileDialog*>(widget)||qobject_cast<QColorDialog*>(widget)||(!store.transparent()&&!store.active()))return;
    if(store.transparent()&&!widget->property("wallpaperWindow").toBool())return;
    if(widget->findChild<WallpaperLayer*>(QString(),Qt::FindDirectChildrenOnly))return;
    widget->setProperty("wallpaperTint",.72);auto* layer=new WallpaperLayer(widget);layer->setObjectName("dialogWallpaperLayer");layer->lower();layer->show();
}
QString wallpaperSurfaces(const QString& input){
    const auto& store=WallpaperStore::instance();const bool transparent=store.transparent();
    if(!transparent&&!store.active())return input;
    static const QRegularExpression rules("([^{}]+)\\{([^{}]*)\\}");
    static const QRegularExpression backgrounds("(\\bbackground(?:-color)?\\s*:\\s*)(#[0-9a-fA-F]{6})(?![0-9a-fA-F])",QRegularExpression::CaseInsensitiveOption);
    QString result;qsizetype offset=0;auto matches=rules.globalMatch(input);
    while(matches.hasNext()){
        const auto rule=matches.next();result+=input.mid(offset,rule.capturedStart()-offset);
        for(const auto& selectorPart:rule.captured(1).split(',')){
            auto selector=selectorPart.trimmed();auto body=rule.captured(2);
            const bool windowSurface=selector=="QMainWindow"||selector=="QDialog"||(selector.startsWith("QDialog#")&&!selector.contains(' ')&&!selector.contains(':'))||selector=="QWidget#root"||selector=="QWidget#trackWindow";
            if(transparent&&windowSurface&&selector.startsWith("QDialog")){
                // Native pickers and system prompts keep an opaque background.
                result+=selector+" {"+body+"}\n";selector+="[wallpaperWindow=true]";
            }
            const bool opaque=selector.contains("opaqueWorkspace")||(!transparent&&windowSurface)||selector.contains("QAbstractItemView")||selector.contains("QToolTip")||selector.contains("QMenu");
            if(!opaque){
                const bool clear=(transparent&&windowSurface)||selector.contains("QScrollArea")||selector.contains("QWidget#settingsPage")||selector.contains("QTabWidget::pane");
                const bool control=selector.contains("QPushButton")||selector.contains("QToolButton")||selector.contains("QLineEdit")||selector.contains("SpinBox")||selector.contains("QComboBox")||selector.contains("QTextEdit")||selector.contains("QPlainTextEdit");
                const double opacity=transparent?std::max(control?.72:0.,1.-store.transparency()/100.):
                    selector.contains("#appSettingsPage")?.66:selector.contains("#header")?.76:selector.contains("#transport")?.78:control?.90:.70;
                QString rewritten;qsizetype position=0;auto fills=backgrounds.globalMatch(body);
                while(fills.hasNext()){
                    const auto fill=fills.next();const QColor color(fill.captured(2));
                    QString replacement=fill.captured(0);
                    if(clear)replacement=fill.captured(1)+"transparent";
                    else if(color.hsvSaturationF()<.34||color.valueF()<.35)replacement=fill.captured(1)+QString("rgba(%1,%2,%3,%4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(qRound(opacity*255));
                    rewritten+=body.mid(position,fill.capturedStart()-position)+replacement;position=fill.capturedEnd();
                }
                body=rewritten+body.mid(position);
            }
            result+=selector+" {"+body+"}\n";
        }
        offset=rule.capturedEnd();
    }
    return result+input.mid(offset);
}
QColor blend(const QColor& left,const QColor& right,double amount){
    return QColor::fromRgbF(left.redF()*(1-amount)+right.redF()*amount,left.greenF()*(1-amount)+right.greenF()*amount,left.blueF()*(1-amount)+right.blueF()*amount);
}
double luminance(const QColor& color){
    auto channel=[](double value){return value<=.04045?value/12.92:std::pow((value+.055)/1.055,2.4);};
    return .2126*channel(color.redF())+.7152*channel(color.greenF())+.0722*channel(color.blueF());
}
QColor readableForeground(const QColor& background){return luminance(background)>.179?QColor(Qt::black):QColor(Qt::white);}
const QHash<QString,QString>& darkColors(){
    static const QHash<QString,QString> colors{
        {"#eef3f5","#111923"},{"#ffffff","#1b2733"},{"#fcfdfd","#1c2935"},{"#f5f8fa","#202f3d"},{"#f6f9fa","#23313f"},{"#f5f9fa","#243441"},
        {"#f7fafb","#263644"},{"#f0f4f6","#17232e"},{"#f5f7f8","#202c37"},{"#203d4e","#e3edf5"},{"#183d4d","#edf4fa"},{"#204557","#d0e6f2"},
        {"#315162","#bdd4e5"},{"#142e3e","#e3efff"},{"#526c7c","#aabfce"},{"#516c7c","#b1c5d3"},{"#627c8a","#9db3c4"},
        {"#8397a3","#9dafc0"},{"#83949f","#9dafc0"},{"#8495a0","#9dafc0"},{"#8699a3","#9dafc0"},{"#879aa6","#9dafc0"},{"#93a0aa","#8ca1b4"},{"#748593","#9dafc0"},
        {"#dce6eb","#354858"},{"#dce5eb","#354858"},{"#d9e4e9","#354858"},{"#e0e9ed","#304252"},{"#e2eaed","#304252"},{"#e3ebee","#304252"},{"#e6edef","#293c4c"},{"#e4ebef","#354858"},{"#e7eef1","#304252"},
        {"#e6edf0","#293c4c"},{"#e7eef0","#415564"},{"#c4d3da","#4b6276"},{"#afbdc5","#657d92"},{"#a8bfc5","#7a9aab"},{"#aecfc9","#325952"},{"#c5dcd7","#325952"},
        {"#e8f4f1","#284b43"},{"#e5f3ef","#23443d"},{"#eaf5f1","#23443d"},{"#d9eeea","#315c50"},{"#d4e9e4","#31564c"},{"#d8eee7","#31564c"},{"#e9f3f7","#294655"},{"#d8f2ea","#31564c"},{"#badfd5","#4c8173"},{"#8ac8bb","#5b9d8a"},
        {"#178e80","#39bfab"},{"#138777","#39bfab"},{"#0e796c","#72d2bd"},{"#107566","#6ccbb5"},{"#10695f","#72d2bd"},{"#189e91","#2ead9d"},
        {"#526fa8","#a0bbe8"},{"#6582bd","#769ad8"},{"#d5a14a","#c9a355"},{"#a77722","#e0b86b"},{"#bd4f5b","#f18c98"},{"#b64957","#f18c98"},{"#d4656d","#db7f89"},{"#cf7580","#a76270"},{"#fbebed","#4a303b"},{"#b7c1cb","#64788a"},{"#16865a","#47c995"}
    };return colors;
}
const QHash<QString,QString>& activeColors(){
    static QHash<QString,QString> colors;static QString previousId;static QColor previousAccent;static bool previousDark{};
    const auto& preferences=Preferences::instance();const auto id=preferences.themeId();const bool dark=preferences.darkTheme();const auto custom=preferences.customThemeAccent();
    if(previousId==id&&previousDark==dark&&previousAccent==custom)return colors;
    previousId=id;previousDark=dark;previousAccent=custom;colors=dark?darkColors():QHash<QString,QString>{};
    if(id=="deep-teal")return colors;
    const auto p=palette(id,dark);
    auto assign=[&](std::initializer_list<const char*> tokens,const QColor& value){for(const auto* token:tokens)colors[QLatin1String(token)]=value.name();};
    assign({"#eef3f5"},p.background);assign({"#ffffff"},p.surface);assign({"#fcfdfd"},blend(p.surface,p.raised,.16));
    assign({"#f5f8fa","#f6f9fa","#f5f9fa"},blend(p.surface,p.raised,.72));assign({"#f7fafb"},p.raised);
    assign({"#f0f4f6"},blend(p.background,p.surface,.45));assign({"#f5f7f8"},blend(p.background,p.raised,.65));
    assign({"#203d4e","#183d4d","#204557","#142e3e"},p.text);assign({"#315162"},blend(p.text,p.muted,.18));
    assign({"#526c7c","#516c7c","#627c8a"},blend(p.text,p.muted,.65));
    assign({"#8397a3","#83949f","#8495a0","#8699a3","#879aa6","#93a0aa","#748593"},p.muted);
    assign({"#dce6eb","#dce5eb","#d9e4e9","#e0e9ed","#e2eaed","#e3ebee","#e4ebef","#e7eef1"},p.border);
    assign({"#e6edef","#e6edf0"},blend(p.surface,p.border,.62));assign({"#e7eef0"},blend(p.border,p.muted,.18));
    assign({"#c4d3da"},blend(p.border,p.muted,.45));assign({"#afbdc5","#a8bfc5"},blend(p.muted,p.raised,.48));
    assign({"#178e80","#138777","#189e91"},p.accent);assign({"#0e796c","#10695f"},dark?p.accent.lighter(116):p.accent.darker(108));
    assign({"#107566"},dark?p.accent.lighter(108):p.accent.darker(112));
    assign({"#e8f4f1","#e5f3ef","#eaf5f1","#e9f3f7"},blend(p.surface,p.accent,dark?.18:.10));
    assign({"#d9eeea","#d4e9e4","#d8eee7","#d8f2ea"},blend(p.surface,p.accent,dark?.29:.20));
    assign({"#badfd5","#8ac8bb"},blend(p.surface,p.accent,dark?.56:.46));
    assign({"#aecfc9","#c5dcd7"},blend(p.surface,p.accent,dark?.33:.29));
    return colors;
}
const QString baseStyle=QString::fromUtf8(R"(

        QMainWindow, QDialog, QWidget#appSettingsPage, QWidget#root, QWidget#trackWindow { background: #eef3f5; color: #203d4e; }
        QWidget { font-family: 'Microsoft YaHei UI'; font-size: 12px; color: #203d4e; }
        QFrame#panel, QFrame#header, QFrame#transport { background: white; border: 1px solid #e0e9ed; border-radius: 10px; }
        QAbstractScrollArea[opaqueWorkspace=true] { background: #ffffff; }
        QAbstractScrollArea[opaqueWorkspace=true] QScrollBar:horizontal, QAbstractScrollArea[opaqueWorkspace=true] QScrollBar:vertical { background: #f0f4f6; }
        QAbstractScrollArea[opaqueWorkspace=true] QScrollBar::handle:horizontal, QAbstractScrollArea[opaqueWorkspace=true] QScrollBar::handle:vertical { background: #c4d3da; }
        QLabel { background: transparent; border: none; }
        QLabel[role=brand] {font-size: 22px; font-weight: 700; color: #183d4d;}
        QLabel[role=title] {font-size: 18px; font-weight: 700;}
        QLabel[role=section] {font-size: 13px; font-weight: 700;}
        QLabel[role=muted] {color: #8397a3; font-size: 11px;}
        QPushButton {background: #f7fafb; border: 1px solid #dce6eb; border-radius: 6px; padding: 9px 13px; font-weight: 600;}
        QPushButton:hover {background: #e8f4f1; border-color: #8ac8bb;}
        QPushButton:pressed {background: #d4e9e4;}
        QPushButton#deleteMode:checked {background:#fbebed; border-color:#cf7580; color:#b64957;}
        QPushButton#addNoteButton:checked {background:#d8f2ea; border-color:#178e80; color:#10695f;}
        QPushButton:disabled {color: #afbdc5; background: #f5f7f8; border-color: #e6edf0;}
        QPushButton[primary=true], QPushButton#primary, QPushButton#importButton, QPushButton#playButton {background: #178e80; border-color: #178e80; color: white;}
        QPushButton[primary=true]:hover, QPushButton#primary:hover, QPushButton#importButton:hover, QPushButton#playButton:hover {background: #107566;}
        QPushButton#playButton:disabled {background: #aecfc9; border-color: #aecfc9;}
        QListWidget, QTreeWidget {border: none; background: white; outline: none;}
        QListWidget::item {border-radius: 6px; padding: 6px; margin: 2px;}
        QListWidget::item:selected {background: #e5f3ef; color: #0e796c;}
        QTreeWidget::item {padding: 9px 2px;}
        QTreeWidget::item:selected {background: #e9f3f7; color: #204557;}
        QHeaderView::section {background: #f6f9fa; color: #8397a3; border: none; padding: 7px 3px; font-size: 10px;}
        QComboBox, QDoubleSpinBox, QSpinBox, QKeySequenceEdit, QLineEdit {background: white; border: 1px solid #dce5eb; border-radius: 6px; padding: 8px; min-height: 20px;}
        QComboBox::drop-down {border: none; width: 20px;}
        QComboBox QAbstractItemView {background: #ffffff; color: #203d4e; border: 1px solid #dce5eb; outline: none; selection-background-color: #e5f3ef; selection-color: #0e796c;}
        QComboBox QAbstractItemView::item {background: #ffffff; color: #203d4e; padding: 6px 10px;}
        QComboBox QAbstractItemView::item:selected, QComboBox QAbstractItemView::item:hover {background: #e5f3ef; color: #0e796c;}
        QTabWidget::pane {border: none; background: white;}
        QTabBar::tab {padding: 12px 20px; color: #8699a3; border-bottom: 2px solid transparent;}
        QTabBar::tab:selected {color: #138777; border-bottom: 2px solid #138777;}
        QScrollArea {border: none; background: white;}
        QScrollBar:horizontal {height: 11px; background: #f0f4f6;}
        QScrollBar::handle:horizontal {background: #c4d3da; border-radius: 5px; min-width: 30px;}
        QScrollBar:vertical {width: 9px; background: #f0f4f6;}
        QScrollBar::handle:vertical {background: #c4d3da; border-radius: 4px; min-height: 30px;}
        QScrollBar::add-line, QScrollBar::sub-line {width: 0; height: 0;}
        QSplitter::handle {background: transparent;}
        QTableWidget {background: white; border: 1px solid #e0e9ed; gridline-color: #eef3f5;}
        QToolTip {background: #203d4e; color: white; padding: 6px; border: none;}
        QScrollArea#appSettingsScroll, QScrollArea#appSettingsScroll > QWidget > QWidget {background:#eef3f5;}
        QFrame[shortcutGroup=true] {background:#f7fafb;border:1px solid #e0e9ed;border-radius:8px;}
        QLabel[role=settingsTitle] {font-size:15px;font-weight:700;}
        QLabel[role=shortcutName] {font-weight:600;}
        QWidget#appSettingsPage QKeySequenceEdit, QDialog[settingsDialog=true] QKeySequenceEdit {background:transparent;border:none;padding:0;min-height:0;}
        QWidget#appSettingsPage QKeySequenceEdit QLineEdit, QDialog[settingsDialog=true] QKeySequenceEdit QLineEdit {background:white;border:1px solid #dce5eb;border-radius:6px;padding:5px 7px;min-height:20px;}
        QWidget#appSettingsPage QKeySequenceEdit QLineEdit:focus, QDialog[settingsDialog=true] QKeySequenceEdit QLineEdit:focus {border-color:#178e80;}
        QWidget#appSettingsPage QKeySequenceEdit QLineEdit:disabled, QDialog[settingsDialog=true] QKeySequenceEdit QLineEdit:disabled {background:#f5f7f8;color:#afbdc5;border-color:#e6edf0;}
        QWidget#appSettingsPage QPushButton[primary=true]:disabled, QDialog[settingsDialog=true] QPushButton[primary=true]:disabled {background:#c5dcd7;color:#8397a3;border-color:#c5dcd7;}
        QPushButton#settingsNavigation:checked, QPushButton#practiceBackButton {background:#e5f3ef;color:#0e796c;border-color:#8ac8bb;}
        QFrame#practiceTransport, QFrame#practicePrompt {background:white;border:1px solid #e0e9ed;border-radius:10px;}
        QWidget#practicePage QPushButton:checked {background:#e5f3ef;color:#0e796c;border-color:#8ac8bb;}
        QWidget#practicePage QPushButton#practicePlayButton {border-radius:23px;padding:8px;}
        QWidget#practicePage QSlider::groove:horizontal {height:6px;background:#e6edf0;border-radius:3px;}
        QWidget#practicePage QSlider::sub-page:horizontal {background:#178e80;border-radius:3px;}
        QWidget#practicePage QSlider::handle:horizontal {background:#178e80;border:1px solid #178e80;width:14px;margin:-4px 0;border-radius:7px;}
        QLabel[role=practiceKey] {font-size:12px;font-weight:600;}
        QPlainTextEdit {background:white;border:1px solid #dce6eb;border-radius:6px;padding:6px;}
)");
void titleTheme(QWidget* widget){
#ifdef Q_OS_WIN
    if(widget->isWindow()&&widget->windowHandle()){BOOL dark=Preferences::instance().darkTheme();DwmSetWindowAttribute(reinterpret_cast<HWND>(widget->winId()),20,&dark,sizeof(dark));}
#endif
}
void windowTransparency(QWidget* widget){
#ifdef Q_OS_WIN
    if(!widget->property("wallpaperWindow").toBool()||!widget->internalWinId()||!widget->isWindow())return;
    const bool enabled=WallpaperStore::instance().transparent();
    const auto applied=widget->property("wallpaperNativeTransparency");if(applied.isValid()&&applied.toBool()==enabled)return;
    // Let DWM composite the raster backing store's per-pixel alpha while
    // retaining the native title bar and resize frame. Never fade the window:
    // text and the note grid must keep their original opacity.
    DWM_BLURBEHIND effect{};effect.dwFlags=DWM_BB_ENABLE;effect.fEnable=enabled;
    if(enabled){effect.dwFlags|=DWM_BB_BLURREGION;effect.hRgnBlur=CreateRectRgn(0,0,-1,-1);}
    const auto result=DwmEnableBlurBehindWindow(reinterpret_cast<HWND>(widget->internalWinId()),&effect);
    if(effect.hRgnBlur)DeleteObject(effect.hRgnBlur);
    if(SUCCEEDED(result))widget->setProperty("wallpaperNativeTransparency",enabled);
#else
    Q_UNUSED(widget);
#endif
}
class ThemeEvents:public QObject,public QAbstractNativeEventFilter {
public:using QObject::QObject;
    bool nativeEventFilter(const QByteArray&,void* message,qintptr*) override{
#ifdef Q_OS_WIN
        const auto* native=static_cast<MSG*>(message);
        if(native->message==WM_DWMCOMPOSITIONCHANGED)if(auto* widget=QWidget::find(reinterpret_cast<WId>(native->hwnd))){
            widget->setProperty("wallpaperNativeTransparency",QVariant());windowTransparency(widget);
        }
#else
        Q_UNUSED(message);
#endif
        return false;
    }
protected:bool eventFilter(QObject* object,QEvent* event) override{
    if(auto* widget=qobject_cast<QWidget*>(object)){
        if(event->type()==QEvent::Show){titleTheme(widget);windowTransparency(widget);ensureDialogWallpaper(widget);configureScrollWallpaper(widget);}
        else if(event->type()==QEvent::WinIdChange){widget->setProperty("wallpaperNativeTransparency",QVariant());windowTransparency(widget);}
    }
    return false;
}
};
void apply(){
    static bool applying=false;if(applying||!qApp)return;const QScopedValueRollback<bool> guard(applying,true);
    const auto appearance=Theme::palette(currentThemeId(),Preferences::instance().darkTheme());
    QPalette palette;palette.setColor(QPalette::Window,color("#eef3f5"));palette.setColor(QPalette::WindowText,color("#203d4e"));palette.setColor(QPalette::Base,color("#ffffff"));palette.setColor(QPalette::AlternateBase,color("#f5f9fa"));
    palette.setColor(QPalette::Text,color("#203d4e"));palette.setColor(QPalette::Button,color("#f7fafb"));palette.setColor(QPalette::ButtonText,color("#203d4e"));palette.setColor(QPalette::Highlight,color("#178e80"));palette.setColor(QPalette::HighlightedText,appearance.accentText);palette.setColor(QPalette::PlaceholderText,color("#8397a3"));palette.setColor(QPalette::Disabled,QPalette::Text,color("#afbdc5"));palette.setColor(QPalette::Disabled,QPalette::ButtonText,color("#afbdc5"));
    palette.setColor(QPalette::Link,appearance.accent);palette.setColor(QPalette::ToolTipBase,Preferences::instance().darkTheme()?QColor("#101a24"):appearance.text);palette.setColor(QPalette::ToolTipText,Qt::white);
    qApp->setPalette(palette);qApp->setStyleSheet(style(baseStyle));
    const auto widgets=QApplication::allWidgets();
    for(auto* widget:widgets){
        if(widget->isVisible())ensureDialogWallpaper(widget);
        configureScrollWallpaper(widget);
        if(widget->property("themeSourceStyle").isValid())widget->setStyleSheet(style(widget->property("themeSourceStyle").toString()));
        if(auto* label=qobject_cast<QLabel*>(widget))if(label->property("themeSourceText").isValid())label->setText(style(label->property("themeSourceText").toString()));
        titleTheme(widget);windowTransparency(widget);widget->update();
    }
}
}
const std::vector<Preset>& presets(){
    static const std::vector<Preset> values{
        {"deep-teal","深空青","沉静青绿，延续熟悉的工作台。",QColor("#178e80")},
        {"midnight-blue","极夜蓝","清晰蓝调，专注每一次编辑。",QColor("#356fc0")},
        {"aurora-purple","极光紫","柔和紫色，为灵感留出空间。",QColor("#8255b8")},
        {"sunrise-orange","晨曦橙","温暖橙调，点亮演奏时刻。",QColor("#ad601b")},
        {"rose-pink","玫瑰粉","细腻玫瑰色，轻盈而有层次。",QColor("#b04e79")},
        {"moss-green","苍藓绿","自然绿调，舒适耐看。",QColor("#557943")},
        {"ember-red","余烬红","深暖红色，保留鲜明重点。",QColor("#b2483e")},
        {"gilded-gold","鎏金","克制金色，搭配暖灰层次。",QColor("#927020")},
        {"high-contrast","高对比","鲜明的文字与边界，便于辨识。",QColor("#0057a8")},
        {"workstation","专业工作站","中性石墨与琥珀，长时间专注。",QColor("#976b30")},
        {"classic-light","经典浅色","简洁中性色，保留传统桌面质感。",QColor("#26735f")}
    };return values;
}
Palette paletteForAccent(const QColor& requested,bool dark){
    auto accent=requested.isValid()?requested:QColor("#8b6bd6");accent.setAlpha(255);
    float hue,saturation,lightness;accent.getHslF(&hue,&saturation,&lightness);const bool neutral=saturation<.08;if(hue<0)hue=0;
    if(dark){accent=QColor::fromHslF(hue,neutral?0:std::clamp(double(saturation),.36,.78),std::clamp(double(lightness)+.19,.62,.76));}
    else{for(int i=0;i<30&&luminance(accent)>.18;++i)accent=accent.darker(106);}
    Palette p;
    if(dark){
        p.background=QColor::fromHslF(hue,neutral?0:.19,.072);p.surface=QColor::fromHslF(hue,neutral?0:.17,.12);p.raised=QColor::fromHslF(hue,neutral?0:.16,.16);
        p.text=QColor::fromHslF(hue,neutral?0:.18,.93);p.muted=QColor::fromHslF(hue,neutral?0:.12,.66);p.border=QColor::fromHslF(hue,neutral?0:.14,.27);
    }else{
        p.background=blend(QColor(neutral?"#f3f3f3":"#f1f3f5"),accent,.075);p.surface=blend(QColor(Qt::white),accent,.015);p.raised=blend(QColor(neutral?"#fbfbfb":"#fafbfc"),accent,.055);
        p.text=QColor::fromHslF(hue,neutral?0:.22,.18);p.muted=QColor::fromHslF(hue,neutral?0:.10,.46);p.border=blend(QColor(neutral?"#dedede":"#dbe0e5"),accent,.13);
    }
    p.accent=accent;p.accentText=readableForeground(accent);return p;
}
Palette palette(const QString& id,bool dark){
    if(id=="deep-teal")return dark?Palette{QColor("#111923"),QColor("#1b2733"),QColor("#263644"),QColor("#e3edf5"),QColor("#9dafc0"),QColor("#304252"),QColor("#39bfab"),QColor(Qt::black)}:
        Palette{QColor("#eef3f5"),QColor(Qt::white),QColor("#f7fafb"),QColor("#203d4e"),QColor("#8397a3"),QColor("#e0e9ed"),QColor("#178e80"),QColor(Qt::black)};
    if(id=="custom")return paletteForAccent(Preferences::instance().customThemeAccent(),dark);
    if(id=="high-contrast")return dark?Palette{QColor("#000000"),QColor("#090909"),QColor("#181818"),QColor(Qt::white),QColor("#d0d0d0"),QColor("#888888"),QColor("#ffdc3c"),QColor(Qt::black)}:
        Palette{QColor(Qt::white),QColor(Qt::white),QColor("#ededed"),QColor(Qt::black),QColor("#333333"),QColor("#555555"),QColor("#0057a8"),QColor(Qt::white)};
    if(id=="workstation")return dark?Palette{QColor("#151518"),QColor("#202024"),QColor("#2b2b31"),QColor("#eeeeef"),QColor("#a0a0ad"),QColor("#41414a"),QColor("#d1aa68"),QColor(Qt::black)}:
        Palette{QColor("#edece9"),QColor("#fafaf8"),QColor("#f2f1ed"),QColor("#33323a"),QColor("#737079"),QColor("#d0cdc7"),QColor("#8b6326"),QColor(Qt::white)};
    if(id=="classic-light")return dark?Palette{QColor("#171a1d"),QColor("#23272b"),QColor("#2c3137"),QColor("#e7ebef"),QColor("#a3aab2"),QColor("#434b54"),QColor("#59bfa3"),QColor(Qt::black)}:
        Palette{QColor("#f0f0f0"),QColor(Qt::white),QColor("#fafafa"),QColor("#24292f"),QColor("#68717c"),QColor("#d0d5da"),QColor("#26735f"),QColor(Qt::white)};
    for(const auto& preset:presets())if(preset.id==id)return paletteForAccent(preset.accent,dark);
    return palette("deep-teal",dark);
}
QString currentThemeId(){return Preferences::instance().themeId();}
QColor color(const QString& light,int alpha){if(light.trimmed().compare("transparent",Qt::CaseInsensitive)==0)return QColor(Qt::transparent);QColor value(light);if(value.isValid())value=QColor(activeColors().value(value.name(),value.name()));value.setAlpha(std::clamp(alpha,0,255));return value;}
QString style(const QString& light){
    const auto& colors=activeColors();const bool dark=Preferences::instance().darkTheme();
    static const QRegularExpression tokens("#[0-9a-fA-F]{6}\\b|\\b(?:background(?:-color)?|color)\\s*:\\s*white\\b",QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression background("\\bbackground(?:-color)?\\s*:\\s*(#[0-9a-fA-F]{6}|white)\\b",QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression foreground("(?:^|[;:{])\\s*color\\s*:\\s*$",QRegularExpression::CaseInsensitiveOption);
    QString output;qsizetype offset=0;auto matches=tokens.globalMatch(light);
    while(matches.hasNext()){
        const auto match=matches.next();const auto original=match.captured();const bool namedWhite=!original.startsWith('#');
        const auto token=namedWhite?QString("#ffffff"):original.toLower();
        const auto open=light.lastIndexOf('{',match.capturedStart());const auto close=light.indexOf('}',match.capturedEnd());
        const auto rule=light.mid(open<0?0:open+1,(close<0?light.size():close)-(open<0?0:open+1));
        const auto previousClose=light.lastIndexOf('}',open);const bool tooltip=open>=0&&light.mid(previousClose+1,open-previousClose-1).contains("QToolTip");
        auto replacement=colors.value(token,token);
        const bool textWhite=token=="#ffffff"&&(namedWhite?original.left(original.indexOf(':')).trimmed().compare("color",Qt::CaseInsensitive)==0:foreground.match(light.left(match.capturedStart())).hasMatch());
        if(textWhite){
            const auto fill=background.match(rule);
            if(tooltip)replacement="#ffffff";
            else if(fill.hasMatch()){const auto canonical=fill.captured(1).compare("white",Qt::CaseInsensitive)==0?QString("#ffffff"):fill.captured(1).toLower();replacement=readableForeground(QColor(colors.value(canonical,canonical))).name();}
            else replacement="#ffffff";
        }else if(tooltip&&token=="#203d4e")replacement=dark?"#101a24":palette(currentThemeId(),false).text.name();
        if(namedWhite)replacement=original.left(original.indexOf(':')+1)+replacement;
        output+=light.mid(offset,match.capturedStart()-offset)+replacement;offset=match.capturedEnd();
    }
    return wallpaperSurfaces(output+light.mid(offset));
}
void setStyle(QWidget* widget,const QString& light){if(widget->property("themeSourceStyle").toString()!=light)widget->setProperty("themeSourceStyle",light);const auto applied=style(light);if(widget->styleSheet()!=applied)widget->setStyleSheet(applied);}
QString sourceStyle(const QWidget* widget){const auto source=widget->property("themeSourceStyle");return source.isValid()?source.toString():widget->styleSheet();}
void setRichText(QLabel* label,const QString& light){label->setProperty("themeSourceText",light);label->setText(style(light));}
void prepareWindow(QWidget* widget){
    if(!widget||!widget->isWindow()||qobject_cast<QFileDialog*>(widget)||qobject_cast<QColorDialog*>(widget))return;
    // Allocate an alpha backing store before the native handle is created.
    // Keep it for all wallpapers so switching never recreates a visible window.
    widget->setAttribute(Qt::WA_TranslucentBackground);widget->setProperty("wallpaperWindow",true);
}
void initialize(){static bool initialized=false;if(initialized)return;initialized=true;QObject::connect(&Preferences::instance(),&Preferences::themeChanged,qApp,[]{apply();});QObject::connect(&WallpaperStore::instance(),&WallpaperStore::activeChanged,qApp,[]{apply();});QObject::connect(&WallpaperStore::instance(),&WallpaperStore::transparencyChanged,qApp,[]{if(WallpaperStore::instance().transparent())apply();});auto* events=new ThemeEvents(qApp);qApp->installEventFilter(events);qApp->installNativeEventFilter(events);apply();}
}
