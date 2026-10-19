#include "help_guide.h"
#include "theme.h"
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>

namespace rock {
const std::vector<GuideTopic>& guideTopics(){
    static const std::vector<GuideTopic> topics{
        {"midi","导入 MIDI 与九键转换","从曲目导入、音轨选择到应用转换参数。",{
            {"导入自己的 MIDI","通过“导入 MIDI”选择文件，也可以将 MIDI 拖入工作台。支持一次添加多首歌曲，原始文件不会被音符编辑改写。","importMidi"},
            {"管理曲目库","在曲目库选择当前歌曲，使用排序、上一首和下一首管理播放顺序。移除曲目只会将它移出曲目库。","songLibrary"},
            {"选择参与转换的音轨","勾选需要保留的音轨。音轨名前的色标对应音符填充色，方便辨认旋律、伴奏与其他声部。","tracks"},
            {"调整音高映射","默认优先保留同音名，并折叠八度。可以调整移调及近似音处理方式，选择如何映射到固定九个音。","pitchSettings"},
            {"应用后检查结果","修改参数后点击“应用设置 · 重新转换”。检查转换结果和冲突提示，再继续试听、编辑或导出。","applySettings"}
        }},
        {"notes","音符编辑与复制粘贴","认识音符状态，学习选择、移动和编辑。",{
            {"读懂音符颜色与边框","填充色区分音轨；同音名无边框、近似音为黄框、已编辑为蓝框、冲突为红框。跳过音符保持灰色且无边框。","noteGrid"},
            {"选择编辑工具","使用轨道工具添加音符、框选或删除。选中音符后可移动、拉伸时长，或使用上下方向键调整音级。","noteTools"},
            {"复制、剪切与粘贴","在轨道中框选音符，按 Ctrl+C 复制或 Ctrl+X 剪切。将蓝色游标定位到目标时间，再按 Ctrl+V 粘贴，保留所选音符之间的节奏。","noteGrid"},
            {"撤销、重做与浏览","Ctrl+Z 撤销，Ctrl+Y 或 Ctrl+Shift+Z 重做。滚轮横向浏览，Ctrl+滚轮缩放，也可用“查看全部”适配整曲。","noteTools"}
        }},
        {"range","区间编辑与导出","设置绿色边界，了解整曲与区间导出。",{
            {"划定播放区间","拖动绿色的左右边界，选择需要反复查看的时间段。默认快捷键 Alt+[ / Alt+] 可将左右边界移到播放标。","selectionRange"},
            {"调整区间时间","区间工具可删除整段时间或插入等长空白，操作支持撤销。先检查绿色边界的位置，避免改动其他段落。","noteTools"},
            {"导出 MIDI 或文字谱","左键“导出 MIDI”保存完整曲目，右键仅导出绿色边界之间的区间。复制键谱、复制手弹谱使用完整转换结果，右键可导出 TXT。","exports"}
        }},
        {"preview","手碟试听与节奏设置","试听歌曲、定位时间，调整播放节奏。",{
            {"使用手碟试听","点击“手碟试听”或按默认快捷键 Space 播放、暂停，使用内置手碟音色检查当前转换结果。试听不需要 Interception 驱动。","preview"},
            {"精确跳转到某一时刻","点击或拖动进度条定位，也可以点击时间显示左侧输入 01:30.50 或 90.5，回车跳转，Esc 取消输入。","seek"},
            {"调整歌曲节奏","在节奏参数中选择原曲变速或固定 BPM，并调整按下时长与松开间隔。同键过密时，需结合节奏参数和音符编辑排除冲突。","rhythmSettings"},
            {"让参数修改生效","修改转换或节奏参数后，点击“应用设置 · 重新转换”，再试听确认结果。导出和跟练也使用已应用的转换结果。","applySettings"}
        }},
        {"practice","歌曲跟练","了解跟练方式，以及游戏小窗的提示、控制与外观。",{
            {"先确认练习歌曲","在曲目库选择要练习的歌曲。跟练使用当前歌曲已应用的转换结果，建议先试听确认音符和节奏。","songLibrary"},
            {"找到跟练入口","通过“跟练练习”进入歌曲练习。页面提供自动播放和跟弹练习，也可切换时间轴或简谱查看歌曲。","practice"},
            {"循序练习","跟弹练习会等待正确按键，和弦需同时按下。“练习设置 / 键位”可调整 A/B 循环、节拍器与九键。","practice"},
            {"认识跟练胶囊","现在已打开跟练小窗。平时可在跟练页面点击“小窗”，或按默认 F12 进入。胶囊置顶，点击不抢占游戏焦点，跟弹按键仍会传递给游戏。引导中仅展示控件，不会开始练习。","practiceMiniCapsule"},
            {"查看当前按键提示","这里显示当前需要弹奏的按键，已匹配的按键会高亮。跟弹模式中，按对本组后松开按键，再继续下一组；没有曲目时显示等待提示。","practiceMiniKeys"},
            {"切换当前小节简谱","显示切换图标可在按键提示与当前小节数字简谱之间切换。这里已展开简谱视图，随练习进度显示当前小节。","practiceMiniNumbers"},
            {"开始或暂停练习","播放按钮控制开始 / 暂停。自动预览按节奏播放，跟弹练习等待正确按键；引导结束后即可自行操作。","practiceMiniPlay"},
            {"从区间起点重练","重来按钮从当前练习区间的起点重新开始，适合反复练习一段旋律。练习区间可在完整练习页面的“练习设置 / 键位”中调整。","practiceMiniRestart"},
            {"切换练习模式","模式按钮在自动预览与跟弹练习之间切换。自动预览用于听节奏、看提示，跟弹练习会等待正确按键，和弦需同时按下。","practiceMiniMode"},
            {"定位练习进度","点击或拖动进度条可跳到指定位置，旁边显示当前时间和总时长。可先定位到难点，再开始练习或从区间起点重来。","practiceMiniSeek"},
            {"调节跟练小窗音量","自动预览与跟弹练习分别记住音量，跟弹默认静音。拖动这里的滑块调整当前模式音量，喇叭按钮可静音或恢复声音；小窗音量与完整练习页面独立。","practiceMiniVolume"},
            {"调整胶囊外观","外观图标可展开透明度与大小设置，这里已展开对应滑块。拖动标题或按键提示区域可移动胶囊，适合放在不遮挡游戏操作的位置。","practiceMiniAppearance"},
            {"返回完整练习页面","这个返回图标或默认快捷键 F12 会暂停练习，并返回完整练习页面。本次点击“完成”将结束引导并恢复工作台。","practiceMiniRestore"}
        }},
        {"performance","自动演奏","认识驱动、输出目标与演奏控制。",{
            {"了解驱动连接","向游戏自动按键需要 Interception 驱动与可用键盘；驱动需单独安装并重启。可在设置的“驱动连接”查看连接说明，试听和跟练无需驱动。","driver"},
            {"选择输出目标","在演奏区域确认输出键盘和目标窗口。输出未就绪时“开始演奏”显示红色；选择完成后显示绿色，开始前还需解决音符冲突。","performance"},
            {"开始、暂停与终止","开始后按倒计时切换到目标窗口，也可设置启动时激活目标。默认 Ctrl+W 暂停或继续，Ctrl+R 终止；目标失焦会暂停，暂停或终止会释放按键。","performance"}
        }},
        {"project","工程保存与播放小窗","保存工程，了解播放小窗的模式、曲目与外观控制。",{
            {"保存当前工程","右上角“工程”菜单可保存或导入 .rockproj。Ctrl+S 保存进度，Ctrl+Shift+S 另存为；工程保存曲目、音符编辑、转换参数和工作台布局。","project"},
            {"选择合适的保存方式","完整保存曲目数据适合备份和迁移，引用本地 MIDI 路径适合保留文件引用。导入工程会替换当前工作台，已有曲目时会先确认。","project"},
            {"认识播放胶囊","现在已打开置顶播放小窗。平时在工作台点击左上角 Logo 或按默认 F12 即可进入；在跟练页面使用该快捷键则进入跟练胶囊。小窗点击不抢占游戏焦点，引导中不会播放或演奏。","playerMiniCapsule"},
            {"选择本地试听","左侧耳机图标切换本地试听，用内置手碟音色播放当前曲目。试听模式会显示音量滑块，无需安装按键输出驱动。","playerMiniAudition"},
            {"选择自动演奏","键盘图标切换自动演奏。需先设置输出键盘和目标窗口；播放按钮显示红色时，点击可返回主窗口补全输出设置。","playerMiniPerformance"},
            {"播放或暂停","中央播放按钮控制当前模式的播放 / 暂停。试听播放声音，自动演奏向目标窗口输出按键；请先确认歌曲、输出目标和转换结果。","playerMiniPlay"},
            {"切换上一首与下一首","播放按钮两侧的箭头用于切换歌曲，默认全局快捷键 Ctrl+Q / Ctrl+E 也可切歌。曲目库只有一首时，切歌按钮不可用。","playerMiniNavigation"},
            {"切换播放顺序","这个图标可切换顺序播放、单曲循环、随机播放和单曲播放完停止。播放模式由试听和自动演奏共用；自动演奏进行中需先暂停，才能切换模式。","playerMiniMode"},
            {"管理胶囊曲目列表","列表图标可展开曲目库，这里已展开。它与主窗口共用曲目，点击曲目会切歌并播放；上下箭头调整顺序，移除图标只将曲目移出曲目库，不删除原 MIDI。","playerMiniQueue"},
            {"定位播放进度","点击或拖动进度条定位。松开后，原本在播放则继续，原本暂停则保持暂停；自动演奏会在绿色区间内定位。","playerMiniSeek"},
            {"调节试听音量","本地试听模式显示这个音量滑块，拖动即可调整，调到 0 为静音。自动演奏模式不显示试听音量。","playerMiniVolume"},
            {"调整胶囊透明度","透明度图标展开下方滑块，这里已展开。可降低不透明度，让胶囊融入游戏画面；100% 为完全不透明。","playerMiniAppearance"},
            {"移动与缩放胶囊","拖动标题或胶囊背景可移动位置。这个右下角缩放手柄可等比例调整大小，便于适配屏幕和游戏布局。","playerMiniResize"},
            {"返回主窗口","返回图标或默认快捷键 F12 可返回完整工作台，继续编辑、转换或保存工程。引导中的“上一步”也可返回前面的主窗口步骤。","playerMiniRestore"},
            {"识别程序退出按钮","右侧电源图标会退出整个程序；工程有未保存修改时会先询问。点击本卡片的“完成”仅结束引导并恢复工作台。","playerMiniQuit"}
        }},
        {"appearance","主题、配色与壁纸","切换界面模式，挑选配色并导入壁纸。",{
            {"打开外观主题","在设置中心点击“外观主题”，打开独立设置窗口。外观修改即时生效，并自动记住选择。","appearance"},
            {"选择明暗模式","界面模式支持浅色、深色和跟随系统。跟随系统会按系统偏好切换明暗，主题配色仍使用当前选择。","interfaceModes"},
            {"挑选主题与参考图配色","通过“主题库”查看浅色、深色主题预览，点击卡片立即应用。也可导入参考图片提取候选颜色，或调整自定义强调色。","themeLibrary"},
            {"设置壁纸与透明背景","“壁纸库”打开独立窗口，可导入图片、GIF 或视频；悬停缩略图可选择、改名或删除。选择“透明背景”可看到窗口后面的内容，并实时调整背景透明度。“无壁纸”恢复默认背景，音符网格始终保持不透明。","wallpaperLibrary"}
        }},
        {"shortcuts","快捷键与手碟测试","按功能区修改快捷键，检查九键发声。",{
            {"认识设置中心","设置中心以应用图标打开各项独立小窗，包括外观主题、快捷键、驱动连接、手碟测试、运行日志和关于本程序。","settingsHome"},
            {"修改并保存快捷键","快捷键按文件与工程、试听与音符编辑、轨道与区间、全局演奏上下分区。点击输入框录入组合键，修改或恢复默认后需点击保存。","shortcuts"},
            {"试弹九键手碟","“手碟测试”支持键盘或鼠标试听九键与和弦，并查看本地按键日志。演奏会话中暂时锁定快捷键修改和手碟测试入口。","handpanTest"}
        }},
        {"diagnostics","连接状态、日志与版本","查找异常线索，查看程序和作者信息。",{
            {"查看连接状态","“驱动连接”展示 Interception 状态、可用键盘数量和异常说明。需要更新状态时可手动重新检测；演奏期间暂停检测。","driver"},
            {"查找运行记录","“运行日志”记录关键操作和异常，支持搜索、筛选等级、复制、导出 TXT，以及打开本地日志目录。遇到问题时可先查找相关时间的记录。","logs"},
            {"了解当前程序","“关于本程序”显示当前版本、主要功能、技术架构和作者信息，并提供 GitHub 仓库与 bilibili 主页入口。","about"}
        }}
    };
    return topics;
}

namespace {
QLabel* words(const QString& value,const char* role=nullptr){
    auto* label=new QLabel(value);label->setTextFormat(Qt::PlainText);label->setWordWrap(true);label->setAttribute(Qt::WA_TransparentForMouseEvents);
    if(role)label->setProperty("role",role);return label;
}
QPushButton* topicRow(const GuideTopic& topic,int number,QWidget* parent){
    auto* button=new QPushButton(parent);button->setObjectName("helpTopic_"+topic.id);button->setProperty("helpTopic",true);button->setProperty("topicId",topic.id);button->setAutoDefault(false);button->setCursor(Qt::PointingHandCursor);button->setFocusPolicy(Qt::StrongFocus);button->setMinimumHeight(92);button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Minimum);
    button->setAccessibleName(topic.title);button->setAccessibleDescription(topic.description+QString(" 共 %1 步，点击开始引导。").arg(topic.steps.size()));button->setToolTip("开始引导："+topic.title);
    auto* row=new QHBoxLayout(button);row->setContentsMargins(16,14,16,14);row->setSpacing(14);
    auto* index=words(QString::number(number).rightJustified(2,'0'));index->setObjectName("helpTopicNumber_"+topic.id);index->setAlignment(Qt::AlignCenter);index->setFixedSize(36,36);Theme::setStyle(index,"QLabel {background:#e8f4f1;color:#178e80;border-radius:11px;font-size:13px;font-weight:700;}");row->addWidget(index,0,Qt::AlignVCenter);
    auto* text=new QVBoxLayout;text->setSpacing(5);auto* title=words(topic.title,"settingsTitle");Theme::setStyle(title,"font-size:14px;font-weight:700;");text->addWidget(title);auto* description=words(topic.description,"muted");text->addWidget(description);row->addLayout(text,1);
    auto* arrow=words("›");arrow->setAlignment(Qt::AlignCenter);arrow->setFixedWidth(15);Theme::setStyle(arrow,"color:#8397a3;font-size:25px;font-weight:400;");row->addWidget(arrow);
    Theme::setStyle(button,"QPushButton[helpTopic=true] {background:#ffffff;border:1px solid #e0e9ed;border-radius:12px;padding:0;} QPushButton[helpTopic=true]:hover {background:#f5f9fa;border-color:#8ac8bb;} QPushButton[helpTopic=true]:focus {border:2px solid #178e80;} QPushButton[helpTopic=true]:pressed {background:#e5f3ef;} QPushButton[helpTopic=true]:disabled {background:#f5f8fa;border-color:#e6edf0;} QPushButton[helpTopic=true] QLabel:disabled {color:#afbdc5;}");
    return button;
}
}

HelpGuideDialog::HelpGuideDialog(QWidget* parent):QDialog(parent){
    Theme::prepareWindow(this);
    setObjectName("helpGuideDialog");setProperty("settingsDialog",true);setWindowTitle("功能帮助");setModal(false);setWindowFlag(Qt::WindowContextHelpButtonHint,false);setMinimumSize(500,420);
    QSize preferred(620,720);if(const auto* display=screen()){const auto available=display->availableGeometry();preferred.setWidth(std::min(preferred.width(),std::max(minimumWidth(),available.width()-48)));preferred.setHeight(std::min(preferred.height(),std::max(minimumHeight(),available.height()-80)));}resize(preferred);
    Theme::setStyle(this,"QDialog#helpGuideDialog {background:#eef3f5;} QScrollArea#helpGuideScroll, QScrollArea#helpGuideScroll > QWidget > QWidget {background:transparent;border:none;}");
    auto* outer=new QVBoxLayout(this);outer->setContentsMargins(22,22,22,18);outer->setSpacing(14);auto* title=words("功能帮助","settingsTitle");Theme::setStyle(title,"font-size:21px;font-weight:700;");outer->addWidget(title);outer->addWidget(words("选择一个功能，跟随界面高亮逐步了解操作。","muted"));
    auto* scroll=new QScrollArea;scroll->setObjectName("helpGuideScroll");scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);outer->addWidget(scroll,1);
    auto* content=new QWidget;content->setObjectName("helpGuideContent");auto* list=new QVBoxLayout(content);list->setContentsMargins(0,0,8,0);list->setSpacing(10);scroll->setWidget(content);
    int number=1;for(const auto& topic:guideTopics()){auto* button=topicRow(topic,number++,content);topics_.push_back(button);list->addWidget(button);connect(button,&QPushButton::clicked,this,[this,id=topic.id]{emit topicRequested(id);});}list->addStretch(1);
    auto* footer=new QHBoxLayout;footer->setSpacing(14);status_=words({},"muted");status_->setObjectName("helpGuideStatus");status_->setMinimumHeight(30);status_->setAccessibleName("帮助引导状态");footer->addWidget(status_,1);
    auto* close=new QDialogButtonBox(QDialogButtonBox::Close);auto* closeButton=close->button(QDialogButtonBox::Close);closeButton->setText("关闭");closeButton->setObjectName("closeHelpGuideButton");closeButton->setAutoDefault(false);connect(close,&QDialogButtonBox::rejected,this,&QDialog::close);footer->addWidget(close);outer->addLayout(footer);setStartEnabled(true);
}
void HelpGuideDialog::setStartEnabled(bool enabled,const QString& reason){
    for(auto* button:topics_)button->setEnabled(enabled);
    status_->setText(!reason.isEmpty()?reason:enabled?QString("可随时结束引导，再回来查看其他功能。"):QString("当前暂不能开始引导，请完成正在进行的操作。"));
}
}
