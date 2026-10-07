#pragma once
#include <QImage>
#include <QString>
#include <functional>

namespace rock {
QString scoreFileBase(const QString& title);
bool writeScoreArchive(const QString& path,int pages,
    const std::function<QImage(int)>& renderPage,const QString& title,QString& error,
    const std::function<bool(int,int)>& progress={});
}
