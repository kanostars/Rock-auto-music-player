#pragma once
#include <QColor>
#include <QString>
class QWidget;class QLabel;
namespace rock::Theme {
void initialize();
QColor color(const QString& light,int alpha=255);
QString style(const QString& light);
void setStyle(QWidget* widget,const QString& light);
QString sourceStyle(const QWidget* widget);
void setRichText(QLabel* label,const QString& light);
}
