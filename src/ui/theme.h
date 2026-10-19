#pragma once
#include <QColor>
#include <QString>
#include <vector>
class QWidget;class QLabel;
namespace rock::Theme {
struct Palette {QColor background,surface,raised,text,muted,border,accent,accentText;};
struct Preset {QString id,name,description;QColor accent;bool light{true},dark{true};};
const std::vector<Preset>& presets();
Palette palette(const QString& id,bool dark);
Palette paletteForAccent(const QColor& accent,bool dark);
QString currentThemeId();
void initialize();
void prepareWindow(QWidget* widget);
QColor color(const QString& light,int alpha=255);
QString style(const QString& light);
void setStyle(QWidget* widget,const QString& light);
QString sourceStyle(const QWidget* widget);
void setRichText(QLabel* label,const QString& light);
}
