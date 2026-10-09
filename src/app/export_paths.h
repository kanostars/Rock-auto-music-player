#pragma once
#include <QString>

namespace rock {
enum class ExportKind {Midi,Project};
bool prepareExportDirectory(ExportKind kind,QString& directory,QString& error);
void rememberExportDirectory(ExportKind kind,const QString& savedFilePath);
}
