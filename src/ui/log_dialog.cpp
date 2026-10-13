#include "log_dialog.h"
#include "theme.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace rock {
namespace {
QLabel* label(const QString& text,const char* role=nullptr){
    auto* value=new QLabel(text);value->setTextFormat(Qt::PlainText);value->setWordWrap(true);
    if(role)value->setProperty("role",role);return value;
}
QPushButton* button(const QString& text,const char* name){
    auto* value=new QPushButton(text);value->setObjectName(name);value->setAutoDefault(false);
    value->setCursor(Qt::PointingHandCursor);return value;
}
}
LogDialog::LogDialog(QWidget* parent):QDialog(parent){
    setObjectName("logDialog");setWindowTitle("全局日志");resize(980,600);setMinimumSize(680,440);
    Theme::setStyle(this,R"(
        QDialog#logDialog {background:#eef3f5;}
        QPlainTextEdit#logText {background:#ffffff;border:1px solid #dce6eb;border-radius:8px;padding:10px;font-family:'Consolas','Microsoft YaHei UI';font-size:12px;}
        QLabel#logStorageError {color:#bd4f5b;}
        QDialog#logDialog QComboBox, QDialog#logDialog QLineEdit {padding:6px 8px;}
        QDialog#logDialog QPushButton {padding:7px 12px;}
    )");
    auto* content=new QVBoxLayout(this);content->setContentsMargins(20,18,20,18);content->setSpacing(10);
    content->addWidget(label("全局日志","title"));
    content->addWidget(label("查看当前会话的运行记录；关闭此窗口后仍会继续记录。复制和保存仅包含当前筛选结果。","muted"));
    auto* filters=new QHBoxLayout;filters->setSpacing(8);filters->addWidget(label("等级"));
    level_=new QComboBox;level_->setObjectName("logLevelFilter");level_->setAccessibleName("日志等级");
    level_->addItem("全部等级",-1);level_->addItem("调试",static_cast<int>(LogLevel::Debug));
    level_->addItem("信息",static_cast<int>(LogLevel::Info));level_->addItem("警告",static_cast<int>(LogLevel::Warning));level_->addItem("错误",static_cast<int>(LogLevel::Error));
    level_->setMinimumWidth(110);filters->addWidget(level_);
    search_=new QLineEdit;search_->setObjectName("logSearch");search_->setPlaceholderText("搜索时间、模块或日志内容…");search_->setClearButtonEnabled(true);search_->setAccessibleName("搜索日志");filters->addWidget(search_,1);
    follow_=new QCheckBox("跟随新日志");follow_->setObjectName("logAutoScroll");follow_->setChecked(true);follow_->setToolTip("在底部时自动跟随；向上查看旧日志时保留当前位置。");filters->addWidget(follow_);content->addLayout(filters);
    text_=new QPlainTextEdit;text_->setObjectName("logText");text_->setReadOnly(true);text_->setUndoRedoEnabled(false);
    text_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));text_->setLineWrapMode(QPlainTextEdit::NoWrap);text_->setMaximumBlockCount(3000);
    text_->setPlaceholderText("暂无符合条件的日志。");text_->setAccessibleName("当前会话日志");content->addWidget(text_,1);
    auto* details=new QHBoxLayout;details->setSpacing(10);
    summary_=label({},"muted");summary_->setWordWrap(false);details->addWidget(summary_);
    path_=label({},"muted");path_->setTextInteractionFlags(Qt::TextSelectableByMouse);details->addWidget(path_,1);content->addLayout(details);
    error_=label({});error_->setObjectName("logStorageError");error_->hide();content->addWidget(error_);
    auto* actions=new QHBoxLayout;actions->setSpacing(8);
    folder_=button("打开日志文件夹","openLogFolderButton");folder_->setToolTip("打开保存 app.log 和历史日志的文件夹。");actions->addWidget(folder_);
    auto* clear=button("清空当前日志","clearLogsButton");clear->setToolTip("清空当前会话日志与 app.log，保留历史轮换文件。");actions->addWidget(clear);actions->addStretch();
    copy_=button("复制日志","copyLogsButton");save_=button("保存 TXT","saveLogsButton");save_->setProperty("primary",true);actions->addWidget(copy_);actions->addWidget(save_);
    auto* close=button("关闭","closeLogDialogButton");actions->addWidget(close);content->addLayout(actions);
    connect(close,&QPushButton::clicked,this,&QDialog::close);
    connect(level_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{refreshLogs(true);});
    connect(search_,&QLineEdit::textChanged,this,[this]{refreshLogs(true);});
    connect(follow_,&QCheckBox::toggled,this,[this](bool enabled){if(enabled)text_->verticalScrollBar()->setValue(text_->verticalScrollBar()->maximum());});
    connect(copy_,&QPushButton::clicked,this,[this]{refreshLogs();QApplication::clipboard()->setText(text_->toPlainText());});
    connect(save_,&QPushButton::clicked,this,&LogDialog::saveLogs);
    connect(clear,&QPushButton::clicked,this,[this]{AppLog::instance().clear();refreshLogs(true);});
    connect(folder_,&QPushButton::clicked,this,[this]{
        const auto folder=QFileInfo(AppLog::instance().filePath()).absolutePath();
        if(!QDesktopServices::openUrl(QUrl::fromLocalFile(folder)))QMessageBox::warning(this,"无法打开日志文件夹","无法打开日志文件夹："+folder);
    });
    auto& log=AppLog::instance();
    connect(&log,&AppLog::entryAdded,this,[this](const LogEntry&){dirty_=true;},Qt::QueuedConnection);
    connect(&log,&AppLog::cleared,this,[this]{refreshLogs(true);},Qt::QueuedConnection);
    refreshTimer_.setInterval(100);connect(&refreshTimer_,&QTimer::timeout,this,[this]{if(dirty_)refreshLogs();});refreshTimer_.start();refreshLogs(true);
}
void LogDialog::refreshLogs(bool force){
    dirty_=false;
    // The backend snapshot is already bounded and ordered, even when notifications arrive out of order.
    const auto entries=AppLog::instance().entries();
    QList<quint64> sequences;QStringList lines;
    const auto needle=search_->text().trimmed();const int level=level_->currentData().toInt();
    for(const auto& entry:entries){
        if(level>=0&&static_cast<int>(entry.level)!=level)continue;
        const auto line=formatLogEntry(entry);
        if(!needle.isEmpty()&&!line.contains(needle,Qt::CaseInsensitive))continue;
        sequences.push_back(entry.sequence);lines.push_back(line);
    }
    auto* vertical=text_->verticalScrollBar();auto* horizontal=text_->horizontalScrollBar();
    const int oldVertical=vertical->value(),oldHorizontal=horizontal->value();
    const bool atBottom=oldVertical>=vertical->maximum()-2;
    const bool append=!force&&sequences.size()>=displayed_.size()&&std::equal(displayed_.begin(),displayed_.end(),sequences.begin());
    if(append){
        if(sequences.size()>displayed_.size()){
            const auto added=lines.mid(displayed_.size()).join('\n');
            QTextCursor cursor(text_->document());cursor.movePosition(QTextCursor::End);
            cursor.insertText((displayed_.isEmpty()?QString():QString("\n"))+added);
        }
    }else text_->setPlainText(lines.join('\n'));
    displayed_=std::move(sequences);
    vertical->setValue(follow_->isChecked()&&atBottom?vertical->maximum():std::min(oldVertical,vertical->maximum()));horizontal->setValue(oldHorizontal);
    summary_->setText(QString("显示 %1 / %2 条 · 最多保留 3000 条").arg(displayed_.size()).arg(entries.size()));
    const auto file=AppLog::instance().filePath();path_->setText(file.isEmpty()?"日志文件尚未创建":"日志文件："+QDir::toNativeSeparators(file));path_->setToolTip(path_->text());
    const auto error=AppLog::instance().storageError();error_->setText(error.isEmpty()?QString():"日志保存异常："+error);error_->setVisible(!error.isEmpty());
    folder_->setEnabled(!file.isEmpty());copy_->setEnabled(!displayed_.isEmpty());save_->setEnabled(!displayed_.isEmpty());
}
void LogDialog::saveLogs(){
    refreshLogs();const auto contents=text_->toPlainText();if(contents.isEmpty())return;
    const auto destination=QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).filePath("Rock_日志_"+QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")+".txt");
    const auto path=QFileDialog::getSaveFileName(this,"保存当前日志",destination,"文本文件 (*.txt)");if(path.isEmpty())return;
    QSaveFile file(path);const auto bytes=(contents+"\n").toUtf8();
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()){
        const auto error=file.errorString();file.cancelWriting();AppLog::instance().write(LogLevel::Error,"日志",QString("保存日志 TXT 失败：%1；%2").arg(path,error));
        QMessageBox::warning(this,"保存日志失败",error);return;
    }
    AppLog::instance().write(LogLevel::Info,"日志","已保存日志 TXT："+path);
}
}
