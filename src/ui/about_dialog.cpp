#include "about_dialog.h"
#include "theme.h"
#include "app/branding.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSysInfo>
#include <QVBoxLayout>
#include <algorithm>

namespace rock {
namespace {
QLabel* text(const QString& value,const char* name=nullptr,const char* role=nullptr) {
    auto* label=new QLabel(value);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);
    if(name)label->setObjectName(name);
    if(role)label->setProperty("role",role);
    return label;
}

QVBoxLayout* section(QBoxLayout* parent,const QString& title) {
    auto* card=new QFrame;
    card->setObjectName("aboutSectionCard");
    Theme::setStyle(card,"QFrame#aboutSectionCard {background:#ffffff;border:1px solid #e0e9ed;border-radius:10px;}");
    auto* body=new QVBoxLayout(card);
    body->setContentsMargins(18,15,18,16);
    body->setSpacing(10);
    if(!title.isEmpty())body->addWidget(text(title,nullptr,"section"));
    parent->addWidget(card);
    return body;
}

QLabel* link(const QString& title,const QString& url,const char* name) {
    auto* label=new QLabel;
    label->setObjectName(name);
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(false);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard|Qt::LinksAccessibleByMouse|Qt::LinksAccessibleByKeyboard);
    label->setOpenExternalLinks(true);
    label->setAccessibleName(title);
    label->setToolTip(url);
    Theme::setRichText(label,QString("<a href=\"%1\" style=\"color:#178e80;text-decoration:none;\">%2 ↗</a>").arg(url.toHtmlEscaped(),title.toHtmlEscaped()));
    return label;
}
}

AboutDialog::AboutDialog(QWidget* parent):QDialog(parent) {
    setObjectName("aboutSettingsDialog");
    setProperty("settingsDialog",true);
    setWindowTitle("关于 ROCK");
    setWindowFlag(Qt::WindowContextHelpButtonHint,false);
    setModal(false);
    setMinimumSize(600,480);
    QSize preferred(740,760);
    if(auto* display=screen()) {
        const auto available=display->availableGeometry();
        preferred.setWidth(std::min(preferred.width(),std::max(minimumWidth(),available.width()-48)));
        preferred.setHeight(std::min(preferred.height(),std::max(minimumHeight(),available.height()-80)));
    }
    resize(preferred);

    auto* outer=new QVBoxLayout(this);
    outer->setContentsMargins(22,22,22,18);
    outer->setSpacing(14);

    auto* hero=new QFrame;
    hero->setObjectName("aboutHero");
    Theme::setStyle(hero,"QFrame#aboutHero {background:#ffffff;border:1px solid #e0e9ed;border-radius:12px;}");
    auto* heading=new QVBoxLayout(hero);
    heading->setContentsMargins(20,18,20,18);
    heading->setSpacing(8);
    auto* name=text(QString::fromUtf8(appDisplayName),"aboutAppNameLabel");
    Theme::setStyle(name,"font-size:22px;font-weight:700;color:#183d4d;");
    heading->addWidget(name);
    heading->addWidget(text("桌面手碟演奏与 MIDI 创作工具",nullptr,"muted"));
    auto* versionRow=new QHBoxLayout;
    versionRow->setSpacing(14);
    auto version=QCoreApplication::applicationVersion().trimmed();
    if(version.isEmpty())version="开发版本";
    else if(!version.startsWith('v',Qt::CaseInsensitive))version.prepend('v');
    versionRow->addWidget(text("当前版本："+version,"aboutVersionLabel"));
    versionRow->addStretch();
    versionRow->addWidget(text(QString("Qt %1 · %2").arg(QString::fromLatin1(qVersion()),QSysInfo::currentCpuArchitecture()),"aboutRuntimeLabel","muted"));
    heading->addLayout(versionRow);
    outer->addWidget(hero);

    auto* scroll=new QScrollArea;
    scroll->setObjectName("aboutScroll");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    Theme::setStyle(scroll,"QScrollArea#aboutScroll, QScrollArea#aboutScroll > QWidget > QWidget {background:transparent;border:none;}");
    auto* content=new QWidget;
    content->setObjectName("aboutContent");
    auto* body=new QVBoxLayout(content);
    body->setContentsMargins(0,0,8,0);
    body->setSpacing(12);
    scroll->setWidget(content);
    outer->addWidget(scroll,1);

    auto* architecture=section(body,"技术架构");
    architecture->addWidget(text(
        "C++20 + Qt 6 Widgets / Concurrent\n"
        "曲谱转换核心、音频引擎、桌面界面与演奏控制分层协作。\n\n"
        "midifile · MIDI 解析与写出\n"
        "miniaudio · 音频播放与手碟试听\n"
        "Interception · 自动演奏按键输出\n"
        "Windows Media Foundation · 视频壁纸\n"
        "QMovie · GIF 动态壁纸",
        "aboutArchitectureLabel"));

    auto* features=section(body,"主要功能");
    features->addWidget(text(
        "· 多轨 MIDI 解析与九键音符映射\n"
        "· 音符编辑、复制剪切粘贴、撤销与重做\n"
        "· 区间 MIDI 导出、键谱与手弹谱导出\n"
        "· 手碟试听、演奏跟练与简谱\n"
        "· 工程保存恢复、自动演奏与小窗控制\n"
        "· 主题配色与图片取色、静态/动态壁纸、快捷键与日志",
        "aboutFeaturesLabel"));

    auto* author=section(body,QString());
    auto* signature=text("By  北辰_kano","aboutAuthorLabel");
    Theme::setStyle(signature,"font-size:15px;font-weight:700;color:#183d4d;");
    author->addWidget(signature);
    auto* links=new QHBoxLayout;
    links->setSpacing(24);
    links->addWidget(link("GitHub 项目主页","https://github.com/kanostars/Rock-auto-music-player","aboutGithubLink"));
    links->addWidget(link("哔哩哔哩个人空间","https://space.bilibili.com/481122212","aboutBilibiliLink"));
    links->addStretch();
    author->addLayout(links);
    body->addStretch();

    auto* notice=text("此软件永久免费，一切商用皆是骗子。","aboutFreeNoticeLabel");
    notice->setAlignment(Qt::AlignCenter);
    Theme::setStyle(notice,"QLabel#aboutFreeNoticeLabel {background:#e8f4f1;color:#10695f;border:1px solid #badfd5;border-radius:10px;padding:14px 16px;font-size:15px;font-weight:700;}");
    outer->addWidget(notice);

    auto* actions=new QHBoxLayout;
    actions->addStretch();
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close);
    auto* close=buttons->button(QDialogButtonBox::Close);
    close->setObjectName("aboutCloseButton");
    close->setText("关闭");
    close->setAutoDefault(false);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::close);
    actions->addWidget(buttons);
    outer->addLayout(actions);
}
}
