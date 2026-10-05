#include "playback_icons.h"
#include "theme.h"
#include "app/preferences.h"
#include <QIconEngine>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
namespace rock {
namespace {
QIcon renderedIcon(PlaylistIcon kind,bool light){
    static QHash<int,QIcon> cache;const int key=static_cast<int>(kind)*4+(light?2:0)+(Preferences::instance().darkTheme()?1:0);
    if(cache.contains(key))return cache.value(key);
    QIcon icon;
    for(int size:{24,48})for(bool disabled:{false,true}){
        QPixmap pixmap(size*2,size*2);pixmap.setDevicePixelRatio(2);pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);p.setRenderHint(QPainter::Antialiasing);p.scale(size/24.0,size/24.0);
        const bool mode=kind>=PlaylistIcon::Loop&&kind<=PlaylistIcon::Once;
        p.setPen(QPen(light&&!disabled?QColor(Qt::white):Theme::color(disabled?"#afbdc5":mode?"#178e80":"#526c7c"),1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        auto line=[&](qreal x1,qreal y1,qreal x2,qreal y2){p.drawLine(QPointF(x1,y1),QPointF(x2,y2));};
        if(kind==PlaylistIcon::Up||kind==PlaylistIcon::Down){
            const bool up=kind==PlaylistIcon::Up;line(12,5,12,19);
            line(6,up?11:13,12,up?5:19);line(12,up?5:19,18,up?11:13);
        }else if(kind==PlaylistIcon::Remove){
            line(5,6,19,6);line(9,6,9,3);line(9,3,15,3);line(15,3,15,6);
            QPainterPath bin;bin.moveTo(7,9);bin.lineTo(8,21);bin.lineTo(16,21);bin.lineTo(17,9);p.drawPath(bin);
            line(10,10,10.5,17);line(14,10,13.5,17);
        }else if(kind==PlaylistIcon::Once){
            QPainterPath play;play.moveTo(5,4);play.lineTo(15,12);play.lineTo(5,20);play.closeSubpath();p.drawPath(play);
            line(19,4,19,20);
        }else if(kind==PlaylistIcon::Shuffle){
            QPainterPath a;a.moveTo(3,6);a.cubicTo(11,6,12,18,20,18);p.drawPath(a);
            QPainterPath b;b.moveTo(3,18);b.cubicTo(11,18,12,6,20,6);p.drawPath(b);
            line(17,3,20,6);line(20,6,17,9);line(17,15,20,18);line(20,18,17,21);
        }else if(kind==PlaylistIcon::Loop||kind==PlaylistIcon::Single){
            QPainterPath top;top.moveTo(4,10);top.lineTo(4,8);top.quadTo(4,5,7,5);top.lineTo(20,5);p.drawPath(top);
            line(17,2,20,5);line(20,5,17,8);
            QPainterPath bottom;bottom.moveTo(20,14);bottom.lineTo(20,16);bottom.quadTo(20,19,17,19);bottom.lineTo(4,19);p.drawPath(bottom);
            line(7,16,4,19);line(4,19,7,22);
            if(kind==PlaylistIcon::Single){
                QFont font=p.font();font.setPixelSize(10);font.setBold(true);p.setFont(font);p.drawText(QRectF(8,6,8,12),Qt::AlignCenter,"1");
            }else{for(int y:{10,13})line(9,y,15,y);}
        }else if(kind==PlaylistIcon::Previous||kind==PlaylistIcon::Next){
            const bool prev=kind==PlaylistIcon::Previous;line(prev?5:19,5,prev?5:19,19);
            QPainterPath path;path.moveTo(prev?19:5,5);path.lineTo(prev?8:16,12);path.lineTo(prev?19:5,19);path.closeSubpath();p.drawPath(path);
        }else if(kind==PlaylistIcon::Play){
            QPainterPath path;path.moveTo(8,5);path.lineTo(19,12);path.lineTo(8,19);path.closeSubpath();p.fillPath(path,p.pen().color());
        }else if(kind==PlaylistIcon::Pause){line(8,5,8,19);line(16,5,16,19);
        }else if(kind==PlaylistIcon::Headphones){
            QPainterPath path;path.moveTo(4,14);path.lineTo(4,11);path.cubicTo(4,0,20,0,20,11);path.lineTo(20,14);p.drawPath(path);
            p.drawRoundedRect(QRectF(3,12,4,8),2,2);p.drawRoundedRect(QRectF(17,12,4,8),2,2);
        }else if(kind==PlaylistIcon::Keyboard){
            p.drawRoundedRect(QRectF(2,5,20,14),3,3);for(int x:{6,10,14,18})for(int y:{9,12})p.drawPoint(x,y);line(7,16,17,16);
        }else if(kind==PlaylistIcon::List){line(4,6,20,6);line(4,12,15,12);line(4,18,15,18);line(18,15,21,18);line(21,18,18,21);
        }else if(kind==PlaylistIcon::Restore){
            p.drawRoundedRect(QRectF(3,6,15,15),2,2);line(12,3,21,3);line(21,3,21,12);line(21,3,12,12);
        }else if(kind==PlaylistIcon::Power){
            p.drawArc(QRectF(3,3,18,18),130*16,280*16);line(12,2,12,11);
        }else if(kind==PlaylistIcon::Volume){
            QPainterPath path;path.moveTo(3,9);path.lineTo(7,9);path.lineTo(12,5);path.lineTo(12,19);path.lineTo(7,15);path.lineTo(3,15);path.closeSubpath();p.drawPath(path);
            p.drawArc(QRectF(10,6,10,12),-60*16,120*16);p.drawArc(QRectF(10,3,16,18),-60*16,120*16);
        }else if(kind==PlaylistIcon::Opacity){
            p.drawEllipse(QRectF(4,4,16,16));
            QPainterPath half;half.moveTo(12,4);half.arcTo(QRectF(4,4,16,16),90,180);half.closeSubpath();p.fillPath(half,p.pen().color());
        }
        p.end();icon.addPixmap(pixmap,disabled?QIcon::Disabled:QIcon::Normal);
    }
    cache.insert(key,icon);return icon;
}
class ThemeIconEngine:public QIconEngine {
    PlaylistIcon kind_;bool light_;
public:
    ThemeIconEngine(PlaylistIcon kind,bool light):kind_(kind),light_(light){}
    QIconEngine* clone() const override{return new ThemeIconEngine(kind_,light_);}
    void paint(QPainter* painter,const QRect& rect,QIcon::Mode mode,QIcon::State state) override{renderedIcon(kind_,light_).paint(painter,rect,Qt::AlignCenter,mode,state);}
    QPixmap pixmap(const QSize& size,QIcon::Mode mode,QIcon::State state) override{return renderedIcon(kind_,light_).pixmap(size,mode,state);}
    QPixmap scaledPixmap(const QSize& size,QIcon::Mode mode,QIcon::State state,qreal scale) override{return renderedIcon(kind_,light_).pixmap(size,scale,mode,state);}
};
}
QIcon playlistIcon(PlaylistIcon kind,bool light){return QIcon(new ThemeIconEngine(kind,light));}
}
