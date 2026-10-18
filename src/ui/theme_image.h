#pragma once
#include <QColor>
#include <QImage>
#include <QString>
#include <vector>

namespace rock::Theme {
struct ImageColors {
    QImage thumbnail;
    std::vector<QColor> colors;
};
// Decode a bounded preview and extract representative colors locally.
// A failed read leaves output unchanged.
bool extractImageColors(const QString& path,ImageColors& output,QString& error);
}
