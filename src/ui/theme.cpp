#include "theme.h"
#include "app/preferences.h"
#include <QApplication>
#include <QHash>
#include <QEvent>
#include <QLabel>
#include <QPalette>
#include <QRegularExpression>
#include <QWidget>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif
namespace rock::Theme {
namespace {
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
        {"#526fa8","#a0bbe8"},{"#6582bd","#769ad8"},{"#d5a14a","#c9a355"},{"#a77722","#e0b86b"},{"#bd4f5b","#f18c98"},{"#b64957","#f18c98"},{"#d4656d","#db7f89"},{"#cf7580","#a76270"},{"#fbebed","#4a303b"},{"#b7c1cb","#64788a"}
    };return colors;
}
const QString baseStyle=QString::fromUtf8(R"(

        QMainWindow, QDialog, QWidget#appSettingsPage, QWidget#root, QWidget#trackWindow { background: #eef3f5; color: #203d4e; }
        QWidget { font-family: 'Microsoft YaHei UI'; font-size: 12px; color: #203d4e; }
        QFrame#panel, QFrame#header, QFrame#transport { background: white; border: 1px solid #e0e9ed; border-radius: 10px; }
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
        QPushButton#settingsNavigation:checked {background:#e5f3ef;color:#0e796c;border-color:#8ac8bb;}
        QPlainTextEdit {background:white;border:1px solid #dce6eb;border-radius:6px;padding:6px;}
)");
void titleTheme(QWidget* widget){
#ifdef Q_OS_WIN
    if(widget->isWindow()&&widget->windowHandle()){BOOL dark=Preferences::instance().darkTheme();DwmSetWindowAttribute(reinterpret_cast<HWND>(widget->winId()),20,&dark,sizeof(dark));}
#endif
}
class ThemeEvents:public QObject {
public:using QObject::QObject;
protected:bool eventFilter(QObject* object,QEvent* event) override{if(event->type()==QEvent::Show)if(auto* widget=qobject_cast<QWidget*>(object))titleTheme(widget);return false;}
};
void apply(){
    QPalette palette;palette.setColor(QPalette::Window,color("#eef3f5"));palette.setColor(QPalette::WindowText,color("#203d4e"));palette.setColor(QPalette::Base,color("#ffffff"));palette.setColor(QPalette::AlternateBase,color("#f5f9fa"));
    palette.setColor(QPalette::Text,color("#203d4e"));palette.setColor(QPalette::Button,color("#f7fafb"));palette.setColor(QPalette::ButtonText,color("#203d4e"));palette.setColor(QPalette::Highlight,color("#178e80"));palette.setColor(QPalette::HighlightedText,Qt::white);palette.setColor(QPalette::PlaceholderText,color("#8397a3"));palette.setColor(QPalette::Disabled,QPalette::Text,color("#afbdc5"));palette.setColor(QPalette::Disabled,QPalette::ButtonText,color("#afbdc5"));
    qApp->setPalette(palette);qApp->setStyleSheet(style(baseStyle));
    for(auto* widget:QApplication::allWidgets()){
        if(widget->property("themeSourceStyle").isValid())widget->setStyleSheet(style(widget->property("themeSourceStyle").toString()));
        if(auto* label=qobject_cast<QLabel*>(widget))if(label->property("themeSourceText").isValid())label->setText(style(label->property("themeSourceText").toString()));
        titleTheme(widget);widget->update();
    }
}
}
QColor color(const QString& light,int alpha){QColor value(light);if(Preferences::instance().darkTheme())value=QColor(darkColors().value(value.name(),value.name()));value.setAlpha(alpha);return value;}
QString style(const QString& light){
    if(!Preferences::instance().darkTheme())return light;
    QString input=light;input.replace(QRegularExpression("(background(?:-color)?\\s*:)\\s*white\\b"),"\\1 #ffffff");
    QString output;qsizetype offset=0;auto matches=QRegularExpression("#[0-9a-fA-F]{6}\\b").globalMatch(input);
    while(matches.hasNext()){const auto match=matches.next();const auto original=match.captured();output+=input.mid(offset,match.capturedStart()-offset)+darkColors().value(original.toLower(),original);offset=match.capturedEnd();}
    output+=input.mid(offset);
    output.replace(QRegularExpression("(QToolTip\\s*\\{[^}]*background\\s*:)\\s*#[0-9a-fA-F]{6}"),"\\1 #101a24");
    return output;
}
void setStyle(QWidget* widget,const QString& light){if(widget->property("themeSourceStyle").toString()!=light)widget->setProperty("themeSourceStyle",light);const auto applied=style(light);if(widget->styleSheet()!=applied)widget->setStyleSheet(applied);}
QString sourceStyle(const QWidget* widget){const auto source=widget->property("themeSourceStyle");return source.isValid()?source.toString():widget->styleSheet();}
void setRichText(QLabel* label,const QString& light){label->setProperty("themeSourceText",light);label->setText(style(light));}
void initialize(){static bool initialized=false;if(initialized)return;initialized=true;QObject::connect(&Preferences::instance(),&Preferences::themeChanged,qApp,[]{apply();});qApp->installEventFilter(new ThemeEvents(qApp));apply();}
}
