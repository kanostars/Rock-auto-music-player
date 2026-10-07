#include "piano_roll.h"
#include "theme.h"
#include "app/preferences.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <QToolTip>
#include <algorithm>
#include <cmath>
#include <limits>
#include <iterator>

namespace rock {
namespace {
// Bar boundaries may fall between integer ticks (for example at low PPQ in
// compound meters), so the ruler keeps the fractional tick until painting.
double barSeconds(const Song& song,const Settings& settings,double tick){
    if(settings.fixedTempo)return tick/song.ppq*60/settings.bpm/settings.speed;
    auto tempo=std::upper_bound(song.tempos.begin(),song.tempos.end(),tick,[](double t,const Tempo& value){return t<value.tick;});
    if(tempo==song.tempos.begin())return tick*.5/song.ppq/settings.speed;
    --tempo;return (tempo->seconds+(tick-tempo->tick)*tempo->micros/(song.ppq*1000000.0))/settings.speed;
}
double barTick(const Song& song,const Settings& settings,double seconds){
    seconds=std::max(0.0,seconds)*settings.speed;
    double tick;
    if(settings.fixedTempo)tick=seconds*song.ppq*settings.bpm/60;
    else {
        auto tempo=std::upper_bound(song.tempos.begin(),song.tempos.end(),seconds,[](double t,const Tempo& value){return t<value.seconds;});
        if(tempo==song.tempos.begin())tick=seconds*song.ppq*2;
        else {--tempo;tick=tempo->tick+(seconds-tempo->seconds)*song.ppq*1000000.0/tempo->micros;}
    }
    return std::clamp(tick,0.0,double(std::numeric_limits<int>::max()));
}
}
PianoRoll::PianoRoll(QWidget* parent):QAbstractScrollArea(parent) {
    std::copy(keys.begin(),keys.end(),practiceKeys_.begin());
    setObjectName("pianoRoll"); setFrameShape(QFrame::NoFrame);
    setMouseTracking(true); viewport()->setMouseTracking(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMinimumHeight(200);setFocusPolicy(Qt::StrongFocus);
    connect(horizontalScrollBar(),&QScrollBar::valueChanged,viewport(),qOverload<>(&QWidget::update));
    dragScroll_.setInterval(30);
    connect(&dragScroll_,&QTimer::timeout,this,[this]{
        if(!isEditing()||!dragMoved_)return;
        int delta=lastPointer_.x()>viewport()->width()-18?12:lastPointer_.x()<gutter_+18?-12:0;
        if(!delta)return;
        auto* bar=horizontalScrollBar();
        if(gesture_!=Gesture::Box&&!draggingRange()&&delta>0&&bar->value()+delta>bar->maximum())bar->setMaximum(std::min(2000000000,bar->value()+delta));
        bar->setValue(bar->value()+delta);updateGesture(lastPointer_);
    });
}
void PianoRoll::setMusic(std::shared_ptr<const Song> song,std::shared_ptr<const Conversion> result) {
    cancelGesture();
    song_=std::move(song);result_=std::move(result);selection_.clear();selected_=-1;rebuildBars();updateRange();viewport()->update();
}
void PianoRoll::setPracticeMode(bool enabled,const Settings& settings){
    if(practiceMode_!=enabled){
        cancelGesture();selection_.clear();selected_=-1;
        if(enabled)editingBeforePractice_=editingEnabled_;
        practiceMode_=enabled;
        setEditingEnabled(enabled?false:editingBeforePractice_);
        if(enabled){filter_=-1;deleteMode_=false;setAddMode(false);}
    }
    practiceSettings_=settings;rebuildBars();updateRange();viewport()->update();
}
void PianoRoll::setPracticeKeys(const std::array<int,9>& keyLabels){
    if(practiceKeys_==keyLabels)return;
    practiceKeys_=keyLabels;viewport()->update();
}
void PianoRoll::setTrackFilter(int track) {if(practiceMode_)return;cancelGesture();filter_=track;selection_.clear();notifySelection();}
int PianoRoll::rowHeight() const {return std::max(14,(viewport()->height()-top_)/(practiceMode_?9:10));}
void PianoRoll::rebuildBars(){
    bars_.clear();if(!song_||song_->ppq<=0)return;
    BarSegment segment{0,std::numeric_limits<double>::infinity(),song_->ppq*4.0,1};
    int numerator=4,denominator=4;
    for(const auto& signature:song_->timeSignatures){
        if(signature.tick<0||signature.numerator<=0||signature.denominator<=0||signature.tick<segment.first)continue;
        if(signature.numerator==numerator&&signature.denominator==denominator)continue;
        if(signature.tick>segment.first){
            segment.last=signature.tick;bars_.push_back(segment);
            const auto count=static_cast<quint64>(std::max(1.0,std::ceil((segment.last-segment.first)/segment.length-1e-9)));
            segment.number+=count;
        }
        numerator=signature.numerator;denominator=signature.denominator;
        segment.first=signature.tick;segment.last=std::numeric_limits<double>::infinity();
        segment.length=double(song_->ppq)*4*numerator/denominator;
    }
    bars_.push_back(segment);
}
void PianoRoll::drawPracticeRuler(QPainter& painter,double left,int width,int height){
    if(!song_||bars_.empty()||!std::isfinite(practiceSettings_.speed)||!std::isfinite(practiceSettings_.bpm)||
       practiceSettings_.speed<=0||practiceSettings_.bpm<=0)return;
    double tick=barTick(*song_,practiceSettings_,left);
    const int limit=std::max(1,(width-gutter_)/24+3);
    double lastLabel=-1000;
    painter.save();painter.setClipRect(gutter_,0,std::max(0,width-gutter_),height);
    // Jump through screen positions rather than iterating over all measures.
    // Even hour-long or densely metered songs only paint a viewport of labels.
    for(int line=0;line<limit;++line){
        auto segment=std::upper_bound(bars_.begin(),bars_.end(),tick,[](double value,const BarSegment& bar){return value<bar.first;});
        if(segment==bars_.begin())segment=bars_.begin();else --segment;
        double index=std::max(0.0,std::ceil((tick-segment->first)/segment->length-1e-9));
        double boundary=segment->first+index*segment->length;
        if(boundary>=segment->last-1e-9&&std::next(segment)!=bars_.end()){
            ++segment;boundary=segment->first;index=0;
        }
        const double time=barSeconds(*song_,practiceSettings_,boundary),x=gutter_+(time-left)*pixels_;
        if(!std::isfinite(x)||x>width)break;
        painter.setPen(Theme::color("#dce6eb"));painter.drawLine(QPointF(x,top_),QPointF(x,height));
        if(x-lastLabel>=90){
            painter.setPen(Theme::color("#627c8a"));painter.setFont(QFont("Microsoft YaHei UI",9));
            painter.drawText(QRectF(x+7,9,110,22),QString("第 %1 小节").arg(segment->number+static_cast<quint64>(index)));
            lastLabel=x;
        }
        tick=std::max(boundary+std::max(1e-6,segment->length*1e-8),barTick(*song_,practiceSettings_,time+24/pixels_));
        if(tick>std::numeric_limits<int>::max())break;
    }
    painter.restore();
}
void PianoRoll::updateRange() {
    double width=(result_?result_->duration:12)*pixels_+40;
    horizontalScrollBar()->setPageStep(std::max(1,viewport()->width()-gutter_));
    horizontalScrollBar()->setRange(0,static_cast<int>(std::clamp(width-(viewport()->width()-gutter_),0.0,2e9)));
}
void PianoRoll::setZoom(double p) {
    cancelGesture();
    double left=horizontalScrollBar()->value()/pixels_;
    pixels_=std::clamp(p,0.001,1200.0);updateRange();
    horizontalScrollBar()->setValue(static_cast<int>(std::min(left*pixels_,2e9)));
    viewport()->update();emit zoomChanged(pixels_);
}
void PianoRoll::fitAll() {
    setZoom(std::max(1,viewport()->width()-gutter_-35)/std::max(1.0,result_?result_->duration:12.0));
    horizontalScrollBar()->setValue(0);
}
void PianoRoll::setPlayhead(double seconds,bool follow) {
    playhead_=seconds;
    double x=seconds*pixels_-horizontalScrollBar()->value();
    if(follow&&(x<0||x>viewport()->width()-gutter_-20)) horizontalScrollBar()->setValue(static_cast<int>(std::min(seconds*pixels_,2e9)));
    viewport()->update();
}
void PianoRoll::selectSource(int source,bool reveal) {
    selectSources({source},reveal);
}
void PianoRoll::selectSources(const std::set<int>& sources,bool reveal) {
    selection_.clear();
    if(practiceMode_){selected_=-1;viewport()->update();return;}
    for(int source:sources)if(result_&&source>=0&&source<static_cast<int>(result_->notes.size())&&visibleNote(result_->notes[source]))selection_.insert(source);
    selected_=selection_.empty()?-1:*selection_.begin();
    if(reveal&&selected_>=0) {
        double start=result_->notes[selected_].start;
        horizontalScrollBar()->setValue(static_cast<int>(std::min(std::max(0.0,start*pixels_-100),2e9)));
    }
    viewport()->update();
}
void PianoRoll::notifySelection() {
    if(!selection_.contains(selected_))selected_=selection_.empty()?-1:*selection_.begin();
    emit noteSelected(selected_);viewport()->update();
}
QRectF PianoRoll::selectionRect() const {
    QPointF anchor(pressPosition_.x()+pressOffset_-horizontalScrollBar()->value(),pressPosition_.y());
    return QRectF(anchor,lastPointer_).normalized();
}
bool PianoRoll::visibleNote(const MappedNote& n) const {
    if(practiceMode_)return n.target>=0&&n.target<static_cast<int>(keys.size())&&
        n.mapping!=Mapping::Excluded&&n.mapping!=Mapping::Deleted&&n.mapping!=Mapping::Skipped&&n.mapping!=Mapping::ConflictSkipped;
    return n.mapping!=Mapping::Excluded && n.mapping!=Mapping::Deleted && (filter_<0||song_->notes[n.source].track==filter_);
}
QRectF PianoRoll::noteRect(const MappedNote& n) const {
    int row=n.target>=0?8-n.target:9;
    int rh=rowHeight();
    double inset=std::min(7.0,rh*.2);
    return {gutter_+n.start*pixels_-horizontalScrollBar()->value(),top_+row*rh+inset,
        std::max(4.0,n.duration*pixels_),rh-2*inset};
}
void PianoRoll::paintEvent(QPaintEvent*) {
    QPainter p(viewport()); p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(viewport()->rect(),Theme::color("#ffffff"));
    const int w=viewport()->width(),h=viewport()->height(),rh=rowHeight();
    for(int r=0;r<(practiceMode_?9:10);++r) {
        p.fillRect(gutter_,top_+r*rh,w-gutter_,rh,Theme::color(r%2?"#f5f8fa":"#fcfdfd"));
        p.setPen(Theme::color("#e6edef"));p.drawLine(gutter_,top_+(r+1)*rh,w,top_+(r+1)*rh);
    }
    const double left=horizontalScrollBar()->value()/pixels_;
    double step=std::pow(10,std::floor(std::log10(85/pixels_)));
    while(step*pixels_<70) step*=2;
    p.setFont(QFont("Segoe UI",9));
    if(practiceMode_)drawPracticeRuler(p,left,w,h);
    else for(double t=std::ceil(left/step)*step;t<left+(w-gutter_)/pixels_;t+=step) {
        double x=gutter_+(t-left)*pixels_;
        p.setPen(Theme::color("#e3ebee"));p.drawLine(QPointF(x,top_),QPointF(x,h));
        p.setPen(Theme::color("#83949f"));p.drawText(QRectF(x+6,9,100,22),QString::number(t,'f',step<1?1:0)+" s");
    }
    p.save();p.setClipRect(gutter_,top_,w-gutter_,h-top_);
    int count=0;
    auto drawNote=[&](const MappedNote& n) {
        auto rect=noteRect(n);
        if(rect.right()<gutter_||rect.left()>w) return;
        QColor color=practiceMode_?Theme::color("#189e91"):n.mapping==Mapping::Edited?Theme::color("#6582bd"):n.mapping==Mapping::Exact?Theme::color("#189e91"):n.mapping==Mapping::Approximate?Theme::color("#d5a14a"):Theme::color("#b7c1cb");
        if(n.conflict&&!practiceMode_)color=Theme::color("#d4656d");
        p.setBrush(color);p.setPen(selection_.contains(n.source)?QPen(Theme::color("#142e3e"),2):Qt::NoPen);
        p.drawRoundedRect(rect,4,4);
        if(rect.width()>19) {
            p.setPen(Qt::white);p.setFont(QFont("Segoe UI",9,QFont::DemiBold));
            QString text=n.target>=0?QString(QChar(practiceMode_?practiceKeys_[n.target]:keys[n.target])):QString::fromStdString(pitchName(song_->notes[n.source].pitch));
            p.drawText(rect.adjusted(selection_.contains(n.source)?10:5,0,-6,0),Qt::AlignVCenter|Qt::AlignLeft,text);
        }
        if(selection_.contains(n.source)&&n.target>=0&&!deleteMode_&&editingEnabled_) {
            auto [leftHandle,rightHandle]=handles(n);
            p.setBrush(Qt::white);p.setPen(QPen(Theme::color("#203d4e"),1));
            p.drawRoundedRect(leftHandle,1,1);p.drawRoundedRect(rightHandle,1,1);
        }
    };
    if(result_) for(const auto& n:result_->notes) {
        if(!visibleNote(n))continue;
        ++count;if(!selection_.contains(n.source))drawNote(n);
    }
    if(result_)for(int id:selection_)if(visibleNote(result_->notes[id]))
        if(!isEditing()||gesture_==Gesture::Box||draggingRange()||!editable(id))drawNote(result_->notes[id]);
    if(isEditing()&&gesture_!=Gesture::Box&&!draggingRange())for(const auto& n:previews_)drawNote(n);
    if(gesture_==Gesture::Box&&dragMoved_) {
        p.setBrush(QColor(53,94,232,30));p.setPen(QPen(Theme::color("#355ee8"),1,Qt::DashLine));p.drawRect(selectionRect());
    }
    if(!practiceMode_&&result_&&rangeEnd_>=0){
        const double a=gutter_+(rangeStart_-left)*pixels_,b=gutter_+(rangeEnd_-left)*pixels_;
        p.fillRect(QRectF(gutter_,top_,std::max(0.0,a-gutter_),h-top_),QColor(120,140,152,55));
        p.fillRect(QRectF(std::max(double(gutter_),b),top_,std::max(0.0,w-std::max(double(gutter_),b)),h-top_),QColor(120,140,152,55));
    }
    if(result_&&count) {
        double x=gutter_+(playhead_-left)*pixels_;
        p.setPen(QPen(Theme::color("#355ee8"),1.5));p.drawLine(QPointF(x,top_),QPointF(x,h));
    }
    p.restore();
    if(!practiceMode_&&result_&&result_->duration>0&&rangeEnd_>=0){
        p.save();p.setClipRect(gutter_,top_-13,w-gutter_,h-top_+13);
        p.setPen(QPen(Theme::color("#20a45b"),1.5));
        for(double t:{rangeStart_,rangeEnd_}){
            const double x=gutter_+(t-left)*pixels_;
            p.drawLine(QPointF(x,top_),QPointF(x,h));
            p.setBrush(Theme::color("#20a45b"));p.drawRoundedRect(QRectF(x-3,top_-8,6,11),2,2);
        }
        p.restore();
    }
    if(practiceMode_&&result_&&result_->duration>0&&rangeEnd_>=0){
        p.save();p.setClipRect(gutter_,top_-14,w-gutter_,h-top_+14);
        const QColor boundary=Theme::color("#178e80");
        for(int endpoint=0;endpoint<2;++endpoint){
            const double x=gutter_+((endpoint?rangeEnd_:rangeStart_)-left)*pixels_;
            p.setPen(QPen(boundary,1.5,Qt::DashLine));p.drawLine(QPointF(x,top_),QPointF(x,h));
            const QRectF label(x+(endpoint?-21:1),top_-14,20,14);
            p.setPen(Qt::NoPen);p.setBrush(Theme::color("#e5f3ef"));p.drawRoundedRect(label,3,3);
            p.setPen(boundary);p.setFont(QFont("Segoe UI",9,QFont::DemiBold));
            p.drawText(label,Qt::AlignCenter,endpoint?"B":"A");
        }
        p.restore();
    }
    p.fillRect(0,0,gutter_,h,Theme::color("#f6f9fa"));
    p.setPen(Theme::color("#83949f"));p.setFont(QFont("Microsoft YaHei UI",9));
    p.drawText(QRect(16,9,70,24),Qt::AlignVCenter,"按键 / 音高");
    for(int r=0;r<(practiceMode_?9:10);++r) {
        QRect rr(0,top_+r*rh,gutter_,rh);
        if(r<9) {
            int i=8-r;
            p.setPen(Theme::color("#203d4e"));p.setFont(QFont("Segoe UI",12,QFont::Bold));
            p.drawText(rr.adjusted(20,0,0,0),Qt::AlignVCenter,QString(QChar(practiceMode_?practiceKeys_[i]:keys[i])));
            p.setPen(Theme::color("#8495a0"));p.setFont(QFont("Segoe UI",9));
            p.drawText(rr.adjusted(50,0,0,0),Qt::AlignVCenter,QString::fromStdString(pitchName(pitches[i])));
        } else {p.setPen(Theme::color("#93a0aa"));p.setFont(QFont("Microsoft YaHei UI",9));p.drawText(rr,Qt::AlignCenter,"已跳过");}
    }
    p.setPen(Theme::color("#e2eaed"));p.drawLine(gutter_-1,0,gutter_-1,h);
    if(!result_ || !count) {
        QRectF box(gutter_+20,h/2.0-53,w-gutter_-40,108);
        p.fillRect(box,Theme::color("#ffffff",236));
        p.setPen(Theme::color("#315162"));p.setFont(QFont("Microsoft YaHei UI",14,QFont::DemiBold));
        p.drawText(box.adjusted(0,0,0,-42),Qt::AlignCenter,addMode_?"点击九键任一行添加音符":result_?"当前没有可显示的音符":"把 MIDI 变成九键旋律");
        p.setPen(Theme::color("#879aa6"));p.setFont(QFont("Microsoft YaHei UI",10));
        p.drawText(box.adjusted(0,45,0,0),Qt::AlignCenter,addMode_?QString("默认一拍 · 添加后可拖动或拉伸 · %1 退出").arg(Preferences::instance().shortcutText(ShortcutAction::CancelEdit)):result_?"检查音轨勾选、显示范围或转换选项":"点击右上方「导入 MIDI」，或拖入本地文件");
    }
}
bool PianoRoll::event(QEvent* e) {
    if(e->type()==QEvent::WindowDeactivate||e->type()==QEvent::Hide||
       (e->type()==QEvent::EnabledChange&&!isEnabled()))cancelGesture();
    return QAbstractScrollArea::event(e);
}
void PianoRoll::resizeEvent(QResizeEvent* e) {cancelGesture();QAbstractScrollArea::resizeEvent(e);updateRange();}
int PianoRoll::hit(const QPointF& point) const {
    if(!result_ || point.x()<gutter_) return -1;
    for(int id:selection_)if(visibleNote(result_->notes[id])) {
        if(noteRect(result_->notes[id]).contains(point))return id;
        if(editable(id)&&!deleteMode_&&editingEnabled_) {
            auto [left,right]=handles(result_->notes[id]);
            if(left.adjusted(-2,-2,2,2).contains(point)||right.adjusted(-2,-2,2,2).contains(point))return id;
        }
    }
    for(auto it=result_->notes.rbegin();it!=result_->notes.rend();++it)
        if(visibleNote(*it)&&noteRect(*it).contains(point)) return it->source;
    return -1;
}
void PianoRoll::mousePressEvent(QMouseEvent* e) {
    if(e->button()!=Qt::LeftButton) return;
    setFocus(Qt::MouseFocusReason);cancelGesture();
    if(practiceMode_){
        if(e->position().x()>=gutter_&&result_)emit seekRequested(std::clamp((e->position().x()-gutter_+horizontalScrollBar()->value())/pixels_,0.0,result_->duration));
        e->accept();return;
    }
    const auto boundary=(e->modifiers()&(Qt::ControlModifier|Qt::ShiftModifier))?Gesture::None:rangePartAt(e->position());
    if(boundary!=Gesture::None){
        emit editStarted();gesture_=boundary;savedRangeStart_=rangeStart_;savedRangeEnd_=rangeEnd_;
        pressPosition_=lastPointer_=e->position();pressOffset_=horizontalScrollBar()->value();dragMoved_=false;
        dragScroll_.start();viewport()->setCursor(Qt::SizeHorCursor);return;
    }
    int id=hit(e->position());
    if(addMode_&&editingEnabled_&&result_&&id<0) {
        const auto point=e->position();
        if(point.x()>=gutter_&&point.x()<viewport()->width()&&point.y()>=top_&&point.y()<top_+9*rowHeight()) {
            int target=8-static_cast<int>((point.y()-top_)/rowHeight());
            emit addRequested((point.x()-gutter_+horizontalScrollBar()->value())/pixels_,target);
        }
        return;
    }
    if(id>=0) {
        if(e->modifiers()&Qt::ControlModifier) {
            if(selection_.contains(id))selection_.erase(id);else selection_.insert(id);
            selected_=id;notifySelection();return;
        }
        if(e->modifiers()&Qt::ShiftModifier)selection_.insert(id);
        else if(!selection_.contains(id))selection_={id};
        selected_=id;notifySelection();
        if(!editingEnabled_){viewport()->update();return;}
        emit editStarted();
        if(deleteMode_) {emit deleteRequested();return;}
        if(editable(id)) {
            gesture_=partAt(id,e->position());originals_.clear();
            for(int source:selection_)if(editable(source))originals_.push_back(result_->notes[source]);
            previews_=originals_;
            pressPosition_=lastPointer_=e->position();pressOffset_=horizontalScrollBar()->value();dragMoved_=false;
            dragScroll_.start();
        }
        viewport()->update();
    }
    else {
        if(e->position().x()>=gutter_&&e->position().y()>=top_&&result_&&editingEnabled_) {
            selectionBefore_=selection_;additiveBox_=e->modifiers()&(Qt::ControlModifier|Qt::ShiftModifier);
            if(!additiveBox_)selection_.clear();
            gesture_=Gesture::Box;pressPosition_=lastPointer_=e->position();pressOffset_=horizontalScrollBar()->value();dragMoved_=false;
            notifySelection();dragScroll_.start();
        } else {
            selection_.clear();notifySelection();
            if(e->position().x()>=gutter_&&result_)emit seekRequested(std::clamp((e->position().x()-gutter_+horizontalScrollBar()->value())/pixels_,0.0,result_->duration));
        }
    }
}
void PianoRoll::mouseMoveEvent(QMouseEvent* e) {
    if(practiceMode_){
        viewport()->setCursor(Qt::ArrowCursor);
        const int id=hit(e->position());
        if(id<0){QToolTip::hideText();return;}
        const auto& note=result_->notes[id];
        QToolTip::showText(e->globalPosition().toPoint(),QString("按键 %1\n开始 %2 s · 持续 %3 s")
            .arg(QChar(keys[note.target])).arg(note.start,0,'f',3).arg(note.duration,0,'f',3),viewport());
        return;
    }
    if(isEditing()) {lastPointer_=e->position();updateGesture(lastPointer_);return;}
    if(rangePartAt(e->position())!=Gesture::None){
        viewport()->setCursor(Qt::SizeHorCursor);
        auto& shortcuts=Preferences::instance();
        QToolTip::showText(e->globalPosition().toPoint(),QString("拖动绿色边界调整区间\n%1 s — %2 s · %3 s\n%4 左边界 / %5 右边界移至蓝色播放标\n%6 取消拖动").arg(rangeStart_,0,'f',3).arg(rangeEnd_,0,'f',3).arg(rangeEnd_-rangeStart_,0,'f',3).arg(shortcuts.shortcutText(ShortcutAction::RangeLeftToPlayhead),shortcuts.shortcutText(ShortcutAction::RangeRightToPlayhead),shortcuts.shortcutText(ShortcutAction::CancelEdit)),viewport());return;
    }
    int id=hit(e->position());
    if(id<0&&addMode_&&editingEnabled_) {
        const auto point=e->position();
        bool valid=result_&&point.x()>=gutter_&&point.y()>=top_&&point.y()<top_+9*rowHeight();
        viewport()->setCursor(valid?Qt::CrossCursor:Qt::ForbiddenCursor);QToolTip::hideText();return;
    }
    if(id<0) {viewport()->unsetCursor();QToolTip::hideText();return;}
    auto part=partAt(id,e->position());
    viewport()->setCursor(!editingEnabled_?Qt::ArrowCursor:deleteMode_?Qt::PointingHandCursor:!editable(id)?Qt::ArrowCursor:part==Gesture::Move?Qt::SizeAllCursor:Qt::SizeHorCursor);
    const auto& n=result_->notes[id];const auto& src=song_->notes[id];
    QString target=n.target<0?"跳过":QString("%1 · %2").arg(QChar(keys[n.target])).arg(QString::fromStdString(pitchName(pitches[n.target])));
    QToolTip::showText(e->globalPosition().toPoint(),QString("%1 → %2\n开始 %3 s · 持续 %4 s\n音轨 %5 / 通道 %6\n%7")
        .arg(QString::fromStdString(pitchName(src.pitch)),target).arg(n.start,0,'f',3).arg(n.duration,0,'f',3)
        .arg(song_->tracks[src.track].source+1).arg(song_->tracks[src.track].channel+1)
        .arg(!editingEnabled_?"试听期间音符锁定，可点击查看":deleteMode_?"点击删除音符":n.target<0?Preferences::instance().shortcutText(ShortcutAction::DeleteNotes)+" 删除 / 右键菜单":"拖动中间移动 · 拖动两端调整时长 · "+Preferences::instance().shortcutText(ShortcutAction::DeleteNotes)+" 删除"),viewport());
}
void PianoRoll::wheelEvent(QWheelEvent* e) {
    if(isEditing()) {e->accept();return;}
    if(e->modifiers()&Qt::ControlModifier) {setZoom(pixels_*std::pow(1.2,e->angleDelta().y()/120.0));e->accept();}
    else {horizontalScrollBar()->setValue(horizontalScrollBar()->value()-e->angleDelta().y());e->accept();}
}
bool PianoRoll::editable(int source) const {
    return !practiceMode_&&editingEnabled_&&result_&&source>=0&&source<static_cast<int>(result_->notes.size())&&visibleNote(result_->notes[source])&&result_->notes[source].target>=0;
}
std::pair<QRectF,QRectF> PianoRoll::handles(const MappedNote& note) const {
    auto rect=noteRect(note);double y=rect.center().y()-4;
    if(rect.width()<18)return {{rect.left()-7,y,5,8},{rect.right()+2,y,5,8}};
    return {{rect.left()+1,y,4,8},{rect.right()-5,y,4,8}};
}
PianoRoll::Gesture PianoRoll::partAt(int source,const QPointF& point) const {
    if(!editable(source))return Gesture::None;
    const auto& note=result_->notes[source];auto rect=noteRect(note);
    if(selection_.contains(source)) {
        auto [left,right]=handles(note);
        if(left.adjusted(-2,-3,2,3).contains(point))return Gesture::LeftEdge;
        if(right.adjusted(-2,-3,2,3).contains(point))return Gesture::RightEdge;
    }
    if(rect.width()>=18) {
        if(std::abs(point.x()-rect.left())<=6)return Gesture::LeftEdge;
        if(std::abs(point.x()-rect.right())<=6)return Gesture::RightEdge;
    }
    return Gesture::Move;
}
bool PianoRoll::isEditing() const {return gesture_!=Gesture::None;}
PianoRoll::Gesture PianoRoll::rangePartAt(const QPointF& point) const {
    if(practiceMode_||!editingEnabled_||!result_||result_->duration<=0||rangeEnd_<0||point.y()<top_-13||point.x()<gutter_)return Gesture::None;
    const double a=gutter_+rangeStart_*pixels_-horizontalScrollBar()->value(),b=gutter_+rangeEnd_*pixels_-horizontalScrollBar()->value();
    const double radius=point.y()<top_+6?7:3;
    const double da=std::abs(point.x()-a),db=std::abs(point.x()-b);
    if(std::min(da,db)>radius)return Gesture::None;
    if(point.y()>=top_+6&&hit(point)>=0)return Gesture::None; // Preserve note edge editing under a boundary.
    return da<=db?Gesture::RangeLeft:Gesture::RangeRight;
}
void PianoRoll::updateGesture(const QPointF& point) {
    if(!isEditing())return;
    if(!dragMoved_&&(point-pressPosition_).manhattanLength()<QApplication::startDragDistance())return;
    dragMoved_=true;QToolTip::hideText();
    if(draggingRange()){
        const double delta=(point.x()-pressPosition_.x()+horizontalScrollBar()->value()-pressOffset_)/pixels_;
        if(gesture_==Gesture::RangeLeft)rangeStart_=std::clamp(savedRangeStart_+delta,0.0,std::max(0.0,rangeEnd_-1e-6));
        else rangeEnd_=std::clamp(savedRangeEnd_+delta,std::min(result_->duration,rangeStart_+1e-6),result_->duration);
        viewport()->setCursor(Qt::SizeHorCursor);viewport()->update();return;
    }
    if(gesture_==Gesture::Box) {
        lastPointer_=point;selection_=additiveBox_?selectionBefore_:std::set<int>{};
        const auto box=selectionRect();
        for(const auto& n:result_->notes)if(visibleNote(n)&&box.intersects(noteRect(n)))selection_.insert(n.source);
        notifySelection();return;
    }
    previews_=originals_;
    double delta=(point.x()-pressPosition_.x()+horizontalScrollBar()->value()-pressOffset_)/pixels_;
    double earliest=originals_.front().start,shrink=originals_.front().duration;
    int lowest=8,highest=0;
    for(const auto& n:originals_) {
        earliest=std::min(earliest,n.start);
        shrink=std::min(shrink,n.duration-std::min(.01,n.duration));
        lowest=std::min(lowest,n.target);highest=std::max(highest,n.target);
    }
    if(gesture_==Gesture::Move) {
        delta=std::max(-earliest,delta);
        int pitchDelta=std::clamp(-static_cast<int>(std::round((point.y()-pressPosition_.y())/rowHeight())),-lowest,8-highest);
        for(auto& n:previews_){n.start+=delta;n.target+=pitchDelta;}
        viewport()->setCursor(Qt::ClosedHandCursor);
    } else if(gesture_==Gesture::LeftEdge) {
        delta=std::clamp(delta,-earliest,shrink);
        for(auto& n:previews_){n.start+=delta;n.duration-=delta;}
        viewport()->setCursor(Qt::SizeHorCursor);
    } else {
        delta=std::max(-shrink,delta);
        for(auto& n:previews_)n.duration+=delta;
        viewport()->setCursor(Qt::SizeHorCursor);
    }
    viewport()->update();
}
void PianoRoll::cancelGesture() {
    if(draggingRange()){rangeStart_=savedRangeStart_;rangeEnd_=savedRangeEnd_;}
    if(gesture_==Gesture::Box){selection_=selectionBefore_;notifySelection();}
    dragScroll_.stop();gesture_=Gesture::None;dragMoved_=false;viewport()->unsetCursor();viewport()->update();
}
void PianoRoll::mouseReleaseEvent(QMouseEvent* e) {
    if(e->button()!=Qt::LeftButton||!isEditing())return;
    updateGesture(e->position());
    if(draggingRange()){
        const double first=rangeStart_,last=rangeEnd_;const bool changed=dragMoved_;
        gesture_=Gesture::None;cancelGesture();if(changed)emit rangeEdited(first,last);return;
    }
    if(gesture_==Gesture::Box) {
        bool seek=!dragMoved_&&!additiveBox_;gesture_=Gesture::None;cancelGesture();
        if(seek)emit seekRequested(std::clamp((e->position().x()-gutter_+horizontalScrollBar()->value())/pixels_,0.0,result_->duration));
        return;
    }
    auto notes=previews_;bool commit=false;
    for(size_t i=0;i<notes.size();++i)if(dragMoved_&&
        (std::abs(notes[i].start-originals_[i].start)>1e-9||std::abs(notes[i].duration-originals_[i].duration)>1e-9||notes[i].target!=originals_[i].target))commit=true;
    cancelGesture();
    if(commit)emit notesEdited(notes);
    updateRange();
}
void PianoRoll::keyPressEvent(QKeyEvent* e) {
    if(practiceMode_){QAbstractScrollArea::keyPressEvent(e);return;}
    const bool left=matchesShortcut(ShortcutAction::RangeLeftToPlayhead,e),right=matchesShortcut(ShortcutAction::RangeRightToPlayhead,e);
    if(left||right){
        if(!e->isAutoRepeat()&&result_&&result_->duration>0&&rangeEnd_>=0&&!isEditing())emit rangeBoundaryToPlayheadRequested(left,playhead_);
        e->accept();return;
    }
    if(matchesShortcut(ShortcutAction::CancelEdit,e)&&isEditing()){cancelGesture();updateRange();e->accept();return;}
    if(matchesShortcut(ShortcutAction::CancelEdit,e)&&addMode_){setAddMode(false);e->accept();return;}
    if(matchesShortcut(ShortcutAction::CancelEdit,e)){selection_.clear();notifySelection();e->accept();return;}
    if(matchesShortcut(ShortcutAction::SelectAll,e)&&result_&&!isEditing()) {
        selection_.clear();for(const auto& n:result_->notes)if(visibleNote(n))selection_.insert(n.source);
        notifySelection();e->accept();return;
    }
    const bool up=matchesShortcut(ShortcutAction::PitchUp,e),down=matchesShortcut(ShortcutAction::PitchDown,e);
    if(matchesShortcut(ShortcutAction::DeleteNotes,e)&&editingEnabled_&&!selection_.empty()&&!isEditing()) {emit deleteRequested();e->accept();return;}
    if((up||down)&&editingEnabled_&&!isEditing()) {
        transposeSelection(up?1:-1);e->accept();return;
    }
    QAbstractScrollArea::keyPressEvent(e);
}
void PianoRoll::transposeSelection(int delta) {
    std::vector<MappedNote> notes;int lowest=8,highest=0;
    for(int id:selection_)if(editable(id)) {
        const auto& n=result_->notes[id];notes.push_back(n);lowest=std::min(lowest,n.target);highest=std::max(highest,n.target);
    }
    if(notes.empty())return;
    delta=std::clamp(delta,-lowest,8-highest);if(!delta)return;
    for(auto& n:notes)n.target+=delta;
    emit editStarted();emit notesEdited(notes);
}
void PianoRoll::setDeleteMode(bool enabled) {cancelGesture();deleteMode_=enabled&&!practiceMode_;if(deleteMode_)setAddMode(false);viewport()->update();}
void PianoRoll::setAddMode(bool enabled) {
    enabled=enabled&&editingEnabled_&&!practiceMode_;
    if(addMode_==enabled)return;
    cancelGesture();addMode_=enabled;
    if(enabled){deleteMode_=false;viewport()->setCursor(Qt::CrossCursor);}
    emit addModeChanged(enabled);viewport()->update();
}
void PianoRoll::setEditingEnabled(bool enabled) {
    enabled=enabled&&!practiceMode_;
    if(editingEnabled_==enabled)return;
    cancelGesture();editingEnabled_=enabled;
    if(!enabled){deleteMode_=false;setAddMode(false);}
    viewport()->update();
}
void PianoRoll::contextMenuEvent(QContextMenuEvent* e) {
    if(practiceMode_||!editingEnabled_){e->accept();return;}
    int id=hit(viewport()->mapFromGlobal(e->globalPos()));if(id<0)return;
    cancelGesture();if(!selection_.contains(id))selection_={id};selected_=id;notifySelection();emit editStarted();
    QMenu menu(this);auto* remove=menu.addAction(QString("删除选中 %1 个音符\t%2").arg(selection_.size()).arg(Preferences::instance().shortcutText(ShortcutAction::DeleteNotes)));
    if(menu.exec(e->globalPos())==remove)emit deleteRequested();
}
}
