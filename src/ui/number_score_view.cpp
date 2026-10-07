#include "number_score_view.h"
#include "core/number_score.h"
#include "core/practice.h"
#include "theme.h"
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScrollBar>
#include <QToolTip>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace rock {
namespace {
constexpr double paperWidth=1600,paperHeight=2000;
constexpr double margin=70,contentTop=224,contentBottom=1840;
constexpr double columnGap=28,rowGap=32,pitchStep=48;
constexpr double contentWidth=paperWidth-2*margin;
constexpr double halfWidth=(contentWidth-columnGap)/2;
constexpr double availableHeight=contentBottom-contentTop;
const QColor ink("#233b3c"),muted("#74858a"),rule("#a3b1ae");

QFont scoreFont(double size,bool bold=false){
    QFont value("Microsoft YaHei UI");value.setPixelSize(qRound(size));value.setBold(bold);return value;
}
int chordSize(std::uint16_t mask){return std::max(1,std::popcount(mask));}
bool intersects(double first,double last,const NumberScoreEvent& event){return event.startTick<last-1e-6&&event.endTick>first+1e-6;}
struct VoiceSlice {int index{},chord{};double height{};};
struct Slice {
    int measure{};
    double first{},last{},height{},idealWidth{};
    std::vector<VoiceSlice> voices;
};
struct Glyph {
    QRectF bounds;
    double x{},baseline{},startTick{},endTick{},sourceSeconds{};
    int units{};
    std::uint16_t keys{};
    bool tieIn{},tieOut{},extension{};
};
struct VoiceLayout {int index{};QRectF bounds;std::vector<Glyph> glyphs;};
struct Cell {int measure{};double first{},last{};QRectF bounds;std::vector<VoiceLayout> voices;};
struct Page {std::vector<Cell> cells;};
struct MeasureFocus {int page{};double seconds{};QRectF bounds;};

std::vector<int> activeVoices(const NumberScoreMeasure& measure,double first,double last){
    std::vector<int> voices;
    for(int v=0;v<static_cast<int>(measure.voices.size());++v)
        if(std::any_of(measure.voices[v].begin(),measure.voices[v].end(),[=](const auto& event){return event.keys&&intersects(first,last,event);}))voices.push_back(v);
    if(voices.empty()&&!measure.voices.empty())voices.push_back(0);
    return voices;
}
std::vector<double> columnTicks(const NumberScoreMeasure& measure,double first,double last){
    std::vector<double> ticks{first,last};
    for(const auto& voice:measure.voices)for(const auto& event:voice)if(intersects(first,last,event))ticks.push_back(std::max(first,event.startTick));
    std::sort(ticks.begin(),ticks.end());
    ticks.erase(std::unique(ticks.begin(),ticks.end(),[](double a,double b){return std::abs(a-b)<1e-6;}),ticks.end());
    return ticks;
}
double idealWidth(const NumberScoreMeasure& measure,double first,double last){
    return 68+(columnTicks(measure,first,last).size()-1)*50;
}
void addSlices(std::vector<Slice>& slices,const NumberScore& score,int index,double first,double last,int ppq){
    const auto& measure=score.measures[index];
    const double width=idealWidth(measure,first,last);
    if(width>contentWidth&&last-first>ppq*.5){
        double middle=first+(last-first)/2;
        // Normal cuts fall on a quarter-note boundary so duration symbols do
        // not change merely because a long measure needs another printed row.
        const double aligned=measure.startTick+std::round((middle-measure.startTick)/ppq)*ppq;
        if(aligned>first+1e-6&&aligned<last-1e-6)middle=aligned;
        addSlices(slices,score,index,first,middle,ppq);
        addSlices(slices,score,index,middle,last,ppq);
        return;
    }
    Slice slice{index,first,last,68,width,{}};
    for(int v:activeVoices(measure,first,last)){
        int chord=1;for(const auto& event:measure.voices[v])if(intersects(first,last,event))chord=std::max(chord,chordSize(event.keys));
        const double height=chord*pitchStep+38;
        if(slice.height+height>availableHeight&&!slice.voices.empty()){
            slice.height+=12;slices.push_back(std::move(slice));slice={index,first,last,68,width,{}};
        }
        slice.voices.push_back({v,chord,height});slice.height+=height;
    }
    slice.height+=12;slices.push_back(std::move(slice));
}
Cell placeCell(const Slice& slice,const NumberScore& score,const QRectF& bounds,int ppq){
    Cell cell{slice.measure,slice.first,slice.last,bounds,{}};
    const auto& measure=score.measures[slice.measure];
    const auto ticks=columnTicks(measure,slice.first,slice.last);
    std::vector<double> centers;
    double weight=0;
    for(std::size_t i=0;i+1<ticks.size();++i)weight+=std::sqrt(std::max(.25,(ticks[i+1]-ticks[i])/(ppq/4.0)));
    const double slack=std::max(0.0,bounds.width()-68-(ticks.size()-1)*50);
    double x=bounds.left()+34;
    for(std::size_t i=0;i+1<ticks.size();++i){
        const double width=50+slack*std::sqrt(std::max(.25,(ticks[i+1]-ticks[i])/(ppq/4.0)))/std::max(.01,weight);
        centers.push_back(x+width/2);x+=width;
    }
    double y=bounds.top()+68;
    for(const auto& part:slice.voices){
        VoiceLayout voice{part.index,QRectF(bounds.left()+8,y,bounds.width()-16,part.height),{}};
        for(const auto& event:measure.voices[part.index])if(intersects(slice.first,slice.last,event)){
            const double first=std::max(event.startTick,slice.first),last=std::min(event.endTick,slice.last);
            const double units=(last-first)/(ppq/4.0);
            const int pitches=chordSize(event.keys);
            const double baseline=y+46+(part.chord-pitches)*pitchStep;
            const bool clippedIn=first>event.startTick+1e-6,clippedOut=last<event.endTick-1e-6;
            const int displayUnits=clippedIn||clippedOut?std::max(1,qRound(units)):event.units;
            const auto column=std::lower_bound(ticks.begin(),ticks.end(),first-1e-6)-ticks.begin();
            const double center=centers[std::min(static_cast<std::size_t>(column),centers.size()-1)];
            voice.glyphs.push_back({QRectF(center-23,baseline-43,46,pitches*pitchStep+33),center,baseline,
                first,last,event.sourceSeconds,displayUnits,event.keys,event.tieIn||clippedIn,event.tieOut||clippedOut,
                event.extension||(clippedIn&&event.keys&&displayUnits==4)});
        }
        cell.voices.push_back(std::move(voice));y+=part.height;
    }
    return cell;
}

void arc(QPainter& painter,double x1,double x2,double y){
    if(x2<=x1)return;
    painter.setBrush(Qt::NoBrush);
    QPainterPath path;path.moveTo(x1,y);path.quadTo((x1+x2)/2,y-17,x2,y);
    painter.drawPath(path);
}
void drawGlyph(QPainter& painter,const Glyph& glyph){
    painter.setFont(scoreFont(38,true));painter.setPen(ink);
    std::vector<std::pair<int,int>> pitches;
    if(!glyph.keys)pitches.push_back({0,0});
    else for(int target=8;target>=0;--target)if(glyph.keys&(1u<<target))pitches.push_back(numberDegree(target));
    double baseline=glyph.baseline;
    for(const auto& [degree,octave]:pitches){
        const QString text=glyph.extension?QString::fromUtf8("—"):QString::number(degree);
        const double textWidth=QFontMetricsF(painter.font()).horizontalAdvance(text);
        painter.drawText(QPointF(glyph.x-textWidth/2,baseline),text);
        if(!glyph.extension&&degree){
            painter.setBrush(ink);painter.setPen(Qt::NoPen);
            for(int dot=0;dot<std::abs(octave);++dot){
                const double dotY=octave>0?baseline-40-dot*8:baseline+12+dot*8;
                painter.drawEllipse(QPointF(glyph.x,dotY),2.7,2.7);
            }
            painter.setPen(ink);
        }
        if(!glyph.extension&&(glyph.units==3||glyph.units==6)){
            painter.setBrush(ink);painter.setPen(Qt::NoPen);painter.drawEllipse(QPointF(glyph.x+23,baseline-12),2.8,2.8);painter.setPen(ink);
        }
        baseline+=pitchStep;
    }
    const int underlines=glyph.extension?0:(glyph.units==1?2:(glyph.units==2||glyph.units==3?1:0));
    painter.setPen(QPen(ink,2));
    for(int line=0;line<underlines;++line)painter.drawLine(QPointF(glyph.x-13,baseline-pitchStep+23+line*6),QPointF(glyph.x+13,baseline-pitchStep+23+line*6));
}
void drawCell(QPainter& painter,const Cell& cell,const NumberScore& score){
    const auto& measure=score.measures[cell.measure];
    painter.setFont(scoreFont(22));painter.setPen(muted);
    const bool fragment=cell.first>measure.startTick+1e-6||cell.last<measure.endTick-1e-6;
    const QString title=QString("第 %1 小节%2 · %3/%4").arg(measure.number).arg(fragment?"（续）":"").arg(measure.numerator).arg(measure.denominator);
    painter.drawText(QPointF(cell.bounds.left()+8,cell.bounds.top()+24),title);
    painter.setFont(scoreFont(19));
    painter.drawText(QPointF(cell.bounds.left()+8,cell.bounds.top()+51),QString::fromUtf8("♩ = %1").arg(measure.bpm,0,'f',std::abs(measure.bpm-std::round(measure.bpm))<.05?0:1));
    for(const auto& voice:cell.voices){
        painter.setPen(QPen(rule,1.7));
        painter.drawLine(QPointF(voice.bounds.left(),voice.bounds.top()+10),QPointF(voice.bounds.left(),voice.bounds.bottom()-10));
        painter.drawLine(QPointF(voice.bounds.right(),voice.bounds.top()+10),QPointF(voice.bounds.right(),voice.bounds.bottom()-10));
        if(score.voiceCount>1){
            painter.setFont(scoreFont(16));painter.setPen(muted);
            painter.drawText(QRectF(voice.bounds.left()+3,voice.bounds.top()+15,28,22),Qt::AlignLeft,QString::number(voice.index+1));
        }
        for(const auto& glyph:voice.glyphs)drawGlyph(painter,glyph);
        painter.setPen(QPen(rule,1.7));
        for(std::size_t i=0;i<voice.glyphs.size();++i){
            const auto& glyph=voice.glyphs[i];
            const double y=glyph.bounds.top()-5;
            if(glyph.keys&&glyph.tieIn&&(i==0||!voice.glyphs[i-1].tieOut||voice.glyphs[i-1].keys!=glyph.keys))
                arc(painter,std::max(voice.bounds.left()+1,glyph.x-30),glyph.x-4,y);
            if(glyph.keys&&glyph.tieOut){
                if(i+1<voice.glyphs.size()&&voice.glyphs[i+1].tieIn&&voice.glyphs[i+1].keys==glyph.keys)
                    arc(painter,glyph.x+4,voice.glyphs[i+1].x-4,std::min(y,voice.glyphs[i+1].bounds.top()-5));
                else arc(painter,glyph.x+4,std::min(voice.bounds.right()-1,glyph.x+34),y);
            }
        }
    }
}
void drawPage(QPainter& painter,const Page& page,const NumberScore& score,const QString& name,int pageIndex,int total){
    painter.setRenderHint(QPainter::Antialiasing);painter.setRenderHint(QPainter::TextAntialiasing);
    painter.fillRect(QRectF(0,0,paperWidth,paperHeight),Qt::white);
    painter.setFont(scoreFont(42,true));painter.setPen(ink);
    const QString title=QFontMetricsF(painter.font()).elidedText(name,Qt::ElideMiddle,paperWidth-2*margin);
    painter.drawText(QRectF(margin,42,contentWidth,66),Qt::AlignCenter,title);
    painter.setFont(scoreFont(23));
    painter.drawText(QPointF(margin,143),"九键手碟 · 游戏数字谱（无点 1 = C3）· 1/16 量化");
    painter.drawText(QRectF(paperWidth-margin-230,120,230,32),Qt::AlignRight|Qt::AlignVCenter,QString("第 %1 / %2 页").arg(pageIndex+1).arg(total));
    painter.setFont(scoreFont(20));painter.setPen(muted);
    painter.drawText(QPointF(margin,183),QString("共 %1 小节 · %2 个声部 · 参考速度 %3 BPM").arg(score.measures.size()).arg(score.voiceCount).arg(score.bpm,0,'f',1));
    painter.setPen(QPen(QColor("#e0e8e4"),1));painter.drawLine(QPointF(margin,202),QPointF(paperWidth-margin,202));
    for(const auto& cell:page.cells)drawCell(painter,cell,score);
    painter.setPen(QPen(QColor("#e0e8e4"),1));painter.drawLine(QPointF(margin,1870),QPointF(paperWidth-margin,1870));
    painter.setFont(scoreFont(21));painter.setPen(muted);
    painter.drawText(QPointF(margin,1910),"上 / 下点：高 / 低八度    0：休止    下划线：减时    右点：附点    —：延音");
    painter.drawText(QPointF(margin,1946),"弧线连接同音，后段无需重复按键；竖排数字同时弹奏。节奏已量化，请结合试听练习。");
}
}

struct NumberScoreView::Layout {
    QString name;
    NumberScore score;
    std::vector<Page> pages;
    std::vector<PracticeGroup> groups;
    std::vector<std::vector<MeasureFocus>> measureFocus;
    std::shared_ptr<const Song> song;
    Settings settings;
    int page{},cachedPage{-1};
    int focusedMeasure{-1},focusedFragment{-1};
    bool following{};
    double position{},tick{};
    std::size_t group{std::numeric_limits<std::size_t>::max()};
    QImage cachedImage;
    const Glyph* hit(const QPointF& point) const{
        if(page<0||page>=static_cast<int>(pages.size()))return nullptr;
        for(const auto& cell:pages[page].cells)for(const auto& voice:cell.voices)for(const auto& glyph:voice.glyphs)
            if(glyph.keys&&!glyph.tieIn&&!glyph.extension&&glyph.bounds.adjusted(-8,0,8,0).contains(point))return &glyph;
        return nullptr;
    }
    bool highlighted(const Glyph& glyph) const{
        if(group>=groups.size()||!glyph.keys||std::abs(glyph.sourceSeconds-groups[group].start)>=1e-7||!(glyph.keys&groups[group].keys))return false;
        return (tick>=glyph.startTick-1e-6&&tick<glyph.endTick-1e-6)||
            (!glyph.tieIn&&!glyph.extension&&std::abs(position-groups[group].start)<1e-7);
    }
    std::pair<int,int> focusAt(double seconds) const{
        if(score.measures.empty())return {-1,-1};
        // Use source seconds rather than rounded ticks or quantized onsets:
        // neither may switch the page before the actual measure boundary.
        auto measure=std::upper_bound(score.measures.begin(),score.measures.end(),seconds,
            [](double time,const NumberScoreMeasure& value){return time<value.startSeconds;});
        const int index=measure==score.measures.begin()?0:static_cast<int>(measure-score.measures.begin()-1);
        const auto& fragments=measureFocus[index];
        if(fragments.empty())return {-1,-1};
        auto fragment=std::upper_bound(fragments.begin(),fragments.end(),seconds,
            [](double time,const MeasureFocus& value){return time<value.seconds;});
        return {index,fragment==fragments.begin()?0:static_cast<int>(fragment-fragments.begin()-1)};
    }
};

NumberScoreView::NumberScoreView(QWidget* parent):QAbstractScrollArea(parent),layout_(std::make_unique<Layout>()){
    setObjectName("practiceNumberScore");setAccessibleName("分页数字简谱");setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);setMinimumSize(240,180);
    viewport()->setMouseTracking(true);viewport()->setAutoFillBackground(false);
    connect(verticalScrollBar(),&QScrollBar::valueChanged,viewport(),qOverload<>(&QWidget::update));
}
NumberScoreView::~NumberScoreView()=default;
int NumberScoreView::currentPage() const{return layout_->page;}
int NumberScoreView::pageCount() const{return static_cast<int>(layout_->pages.size());}
void NumberScoreView::setSong(const QString& name,std::shared_ptr<const Song> song,std::shared_ptr<const Conversion> result,const Settings& settings){
    // Build before replacement, so a failed score cannot leave half a page.
    auto next=std::make_unique<Layout>();next->name=name.simplified();next->song=std::move(song);next->settings=settings;
    if(next->song&&result){
        next->score=makeNumberScore(*next->song,*result,settings);next->groups=makePracticeGroups(*result);
        std::vector<Slice> slices;
        for(int i=0;i<static_cast<int>(next->score.measures.size());++i){const auto& measure=next->score.measures[i];addSlices(slices,next->score,i,measure.startTick,measure.endTick,next->song->ppq);}
        double y=contentTop;
        for(std::size_t i=0;i<slices.size();){
            const bool pair=i+1<slices.size()&&slices[i].idealWidth<=halfWidth&&slices[i+1].idealWidth<=halfWidth&&slices[i].measure!=slices[i+1].measure;
            const double height=pair?std::max(slices[i].height,slices[i+1].height):slices[i].height;
            if(next->pages.empty()||y+height>contentBottom+1e-6){next->pages.emplace_back();y=contentTop;}
            auto& page=next->pages.back();
            page.cells.push_back(placeCell(slices[i],next->score,QRectF(margin,y,pair?halfWidth:contentWidth,height),next->song->ppq));
            if(pair)page.cells.push_back(placeCell(slices[i+1],next->score,QRectF(margin+halfWidth+columnGap,y,halfWidth,height),next->song->ppq));
            i+=pair?2:1;y+=height+rowGap;
        }
        next->measureFocus.resize(next->score.measures.size());
        for(int p=0;p<static_cast<int>(next->pages.size());++p)
            for(const auto& cell:next->pages[p].cells){
                auto& fragments=next->measureFocus[cell.measure];
                const auto& measure=next->score.measures[cell.measure];
                double seconds=measure.startSeconds;
                if(std::abs(cell.first-measure.startTick)>=1e-6){
                    const int whole=static_cast<int>(std::floor(cell.first));
                    seconds=secondsAtTick(*next->song,settings,whole);
                    if(cell.first>whole)seconds+=(cell.first-whole)*
                        (secondsAtTick(*next->song,settings,whole+1)-seconds);
                }
                // A tall measure may split its voices across pages. Keep its
                // first cell as the anchor instead of chasing each voice.
                if(fragments.empty()||seconds>fragments.back().seconds+1e-7)
                    fragments.push_back({p,seconds,cell.bounds});
            }
    }
    layout_=std::move(next);verticalScrollBar()->setValue(0);updateScrollRange();viewport()->update();emit pageChanged(currentPage(),pageCount());
}
void NumberScoreView::setPage(int index){
    if(layout_->pages.empty())return;
    index=std::clamp(index,0,pageCount()-1);if(index==layout_->page)return;
    layout_->page=index;layout_->cachedPage=-1;layout_->cachedImage={};verticalScrollBar()->setValue(0);updateScrollRange();viewport()->update();emit pageChanged(index,pageCount());
}
void NumberScoreView::setPosition(double seconds,bool follow,bool forceFocus){
    if(!std::isfinite(seconds))return;
    const auto oldGroup=layout_->group;const double oldTick=layout_->tick;
    layout_->position=std::max(0.0,seconds);layout_->tick=layout_->song?tickAtSeconds(*layout_->song,layout_->settings,layout_->position):0;
    layout_->group=std::numeric_limits<std::size_t>::max();
    auto it=std::upper_bound(layout_->groups.begin(),layout_->groups.end(),seconds+1e-7,[](double time,const PracticeGroup& group){return time<group.start;});
    if(it!=layout_->groups.begin()){--it;if(seconds<it->end+1e-7)layout_->group=static_cast<std::size_t>(it-layout_->groups.begin());}
    if(follow&&!layout_->pages.empty()){
        const auto [measure,fragment]=layout_->focusAt(layout_->position);
        if(measure>=0){
            const auto& target=layout_->measureFocus[measure][fragment];
            if(forceFocus||!layout_->following||measure!=layout_->focusedMeasure||
               fragment!=layout_->focusedFragment||target.page!=layout_->page){
                setPage(target.page);
                verticalScrollBar()->setValue(qRound(12+target.bounds.top()*displayScale()-24));
                layout_->focusedMeasure=measure;layout_->focusedFragment=fragment;
            }
        }
    }
    layout_->following=follow;
    if(oldGroup!=layout_->group||oldTick!=layout_->tick)viewport()->update();
}
QImage NumberScoreView::renderPage(int index) const{
    if(index<0||index>=pageCount())return {};
    QImage image(qRound(paperWidth),qRound(paperHeight),QImage::Format_RGB32);image.fill(Qt::white);
    image.setDotsPerMeterX(7559);image.setDotsPerMeterY(7559);
    QPainter painter(&image);drawPage(painter,layout_->pages[index],layout_->score,layout_->name,index,pageCount());return image;
}
double NumberScoreView::displayScale() const{
    // Keep the paper width stable when a page just fits vertically. Otherwise
    // showing the scrollbar could shrink the paper enough to hide it again.
    const int paperSpace=std::min(viewport()->width(),maximumViewportSize().width()-verticalScrollBar()->sizeHint().width());
    return std::max(1,paperSpace-24)/paperWidth;
}
QPointF NumberScoreView::paperPoint(const QPointF& point) const{return QPointF((point.x()-12)/displayScale(),(point.y()+verticalScrollBar()->value()-12)/displayScale());}
void NumberScoreView::updateScrollRange(){
    const int height=layout_->pages.empty()?0:qCeil(paperHeight*displayScale())+24;
    verticalScrollBar()->setPageStep(viewport()->height());verticalScrollBar()->setRange(0,std::max(0,height-viewport()->height()));
}
void NumberScoreView::resizeEvent(QResizeEvent* event){
    QAbstractScrollArea::resizeEvent(event);updateScrollRange();
    if(layout_->following)setPosition(layout_->position,true,true);
}
void NumberScoreView::paintEvent(QPaintEvent*){
    QPainter painter(viewport());painter.fillRect(viewport()->rect(),Theme::color("#f5f7f8"));
    if(layout_->pages.empty()){
        painter.setPen(Theme::color("#8397a3"));painter.setFont(scoreFont(13));painter.drawText(viewport()->rect().adjusted(24,24,-24,-24),Qt::AlignCenter|Qt::TextWordWrap,"导入 MIDI 后可查看分页数字简谱。");return;
    }
    if(layout_->cachedPage!=layout_->page){layout_->cachedImage=renderPage(layout_->page);layout_->cachedPage=layout_->page;}
    const double scale=displayScale(),y=12-verticalScrollBar()->value();
    painter.setPen(Qt::NoPen);painter.setBrush(Theme::color("#203d4e",20));painter.drawRoundedRect(QRectF(14,y+3,paperWidth*scale,paperHeight*scale),4,4);
    painter.drawImage(QRectF(12,y,paperWidth*scale,paperHeight*scale),layout_->cachedImage);
    painter.translate(12,y);painter.scale(scale,scale);painter.setRenderHint(QPainter::Antialiasing);
    for(const auto& cell:layout_->pages[layout_->page].cells)for(const auto& voice:cell.voices)for(const auto& glyph:voice.glyphs)if(layout_->highlighted(glyph)){
        painter.setBrush(QColor(37,159,126,43));painter.setPen(QPen(QColor("#159e7d"),2));painter.drawRoundedRect(glyph.bounds.adjusted(-5,-2,5,2),7,7);
    }
}
void NumberScoreView::mousePressEvent(QMouseEvent* event){
    if(event->button()==Qt::LeftButton)if(const auto* glyph=layout_->hit(paperPoint(event->position()))){const double seconds=glyph->sourceSeconds;emit seekRequested(seconds);event->accept();return;}
    QAbstractScrollArea::mousePressEvent(event);
}
void NumberScoreView::mouseMoveEvent(QMouseEvent* event){
    if(const auto* glyph=layout_->hit(paperPoint(event->position()))){
        viewport()->setCursor(Qt::PointingHandCursor);
        const int cents=qRound(glyph->sourceSeconds*100);
        const QString time=QString("%1:%2.%3").arg(cents/6000,2,10,QChar('0')).arg(cents/100%60,2,10,QChar('0')).arg(cents%100,2,10,QChar('0'));
        QToolTip::showText(event->globalPosition().toPoint(),"点击定位到 "+time,viewport());
    }else{viewport()->setCursor(Qt::ArrowCursor);QToolTip::hideText();}
    QAbstractScrollArea::mouseMoveEvent(event);
}
}
