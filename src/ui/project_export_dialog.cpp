#include "project_export_dialog.h"
#include "theme.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace rock {
ProjectExportDialog::ProjectExportDialog(const ProjectState& state,QWidget* parent):QDialog(parent){
    setObjectName("projectExportDialog");setWindowTitle("导出工作台工程");resize(720,480);setMinimumSize(600,360);
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(22,20,22,20);layout->setSpacing(14);
    auto* title=new QLabel("保存当前工作台");title->setProperty("role","title");layout->addWidget(title);
    auto* description=new QLabel("保存曲目顺序、音符编辑、撤销记录、转换参数、片段区间与当前播放位置。");description->setWordWrap(true);description->setProperty("role","muted");layout->addWidget(description);
    auto* mode=new QHBoxLayout;mode->addWidget(new QLabel("曲目保存方式"));
    storage_=new QComboBox;storage_->setObjectName("projectStorageMode");storage_->addItems({"完整保存曲目数据","引用本地 MIDI 文件路径"});mode->addWidget(storage_,1);layout->addLayout(mode);
    auto* hint=new QLabel;hint->setWordWrap(true);hint->setProperty("role","muted");layout->addWidget(hint);
    auto updateHint=[this,hint]{hint->setText(storage_->currentIndex()==0?
        "完整保存：工程包含编辑后的曲目数据，无需原 MIDI 文件，可直接迁移。下方路径仅记录曲目来源。":
        "路径引用：导入依赖原 MIDI 文件，移动后可重新定位。编辑与撤销记录仍保存在工程中；请保留原文件内容。");};
    connect(storage_,qOverload<int>(&QComboBox::currentIndexChanged),this,updateHint);updateHint();
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
    auto* contents=new QWidget;auto* rows=new QVBoxLayout(contents);rows->setContentsMargins(0,0,0,0);rows->setSpacing(12);
    for(size_t i=0;i<state.sessions.size();++i){
        const auto& session=state.sessions[i];auto* row=new QVBoxLayout;row->setSpacing(5);
        auto* name=new QLabel(QString("%1. %2").arg(i+1).arg(QFileInfo(session.path).completeBaseName()));name->setTextFormat(Qt::PlainText);row->addWidget(name);
        auto* pathRow=new QHBoxLayout;auto* edit=new QLineEdit(session.path);edit->setObjectName(QString("projectSongPath%1").arg(i));edit->setClearButtonEnabled(true);edit->setToolTip("曲目的本地 MIDI 路径");paths_.push_back(edit);pathRow->addWidget(edit,1);
        auto* browse=new QPushButton("选择 MIDI");browse->setObjectName(QString("projectBrowseSong%1").arg(i));browse->setCursor(Qt::PointingHandCursor);pathRow->addWidget(browse);
        connect(browse,&QPushButton::clicked,this,[this,edit]{const auto path=QFileDialog::getOpenFileName(this,"选择曲目引用的 MIDI",edit->text(),"MIDI 文件 (*.mid *.midi)");if(!path.isEmpty())edit->setText(path);});
        row->addLayout(pathRow);rows->addLayout(row);
    }
    if(state.sessions.empty()){auto* empty=new QLabel("当前没有曲目，将保存空工作台的布局与播放设置。");empty->setProperty("role","muted");rows->addWidget(empty);}
    rows->addStretch();scroll->setWidget(contents);layout->addWidget(scroll,1);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Save)->setText("继续导出");buttons->button(QDialogButtonBox::Save)->setObjectName("confirmProjectExport");buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    connect(buttons,&QDialogButtonBox::accepted,this,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);layout->addWidget(buttons);
    Theme::setStyle(this,"QDialog {background:#eef3f5;}");
}
ProjectStorage ProjectExportDialog::storage()const{return storage_->currentIndex()==0?ProjectStorage::Embedded:ProjectStorage::Linked;}
QStringList ProjectExportDialog::songPaths()const{QStringList paths;for(auto* edit:paths_)paths.append(edit->text().trimmed());return paths;}
}
