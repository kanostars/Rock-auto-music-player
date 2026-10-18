#include "theme_image.h"
#include <QFileInfo>
#include <QImageReader>
#include <algorithm>
#include <array>
#include <cmath>

namespace rock::Theme {
namespace {
struct Sample {double red{},green{},blue{},weight{};};
double distance(const Sample& a,const Sample& b){
    const auto r=a.red-b.red,g=a.green-b.green,blue=a.blue-b.blue;
    return .30*r*r+.59*g*g+.11*blue*blue;
}
QColor sampleColor(const Sample& value){return QColor(qRound(value.red),qRound(value.green),qRound(value.blue));}
double featureWeight(const Sample& value){
    const auto color=sampleColor(value);
    // Colored features should win over large white margins or dark backdrops.
    const double saturation=color.hsvSaturationF(),brightness=color.valueF();
    const double usable=brightness<.12?.12:brightness>.97&&saturation<.12?.12:1;
    return value.weight*(.04+saturation*saturation)*usable;
}
}
bool extractImageColors(const QString& path,ImageColors& output,QString& error){
    error.clear();const QFileInfo source(path);
    if(!source.isFile()||source.size()<=0){error="图片不存在或为空，请重新选择。";return false;}
    if(source.size()>64*1024*1024){error="请选择小于 64 MB 的参考图片。";return false;}
    QImageReader reader(path);reader.setAutoTransform(true);
    if(!reader.canRead()){error="无法读取这张图片，请选择 PNG、JPEG、BMP 或 GIF 图片。";return false;}
    const auto dimensions=reader.size();
    if(!dimensions.isValid()||dimensions.width()>32768||dimensions.height()>32768||
       qint64(dimensions.width())*dimensions.height()>64000000){
        error="图片尺寸过大，请缩小到 6400 万像素以内后重试。";return false;
    }
    reader.setScaledSize(dimensions.scaled(256,256,Qt::KeepAspectRatio));
    auto decoded=reader.read();
    if(decoded.isNull()){error="图片解码失败："+reader.errorString();return false;}
    if(decoded.width()>256||decoded.height()>256)decoded=decoded.scaled(256,256,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    decoded=decoded.convertToFormat(QImage::Format_ARGB32);
    std::array<Sample,4096> bins{};
    for(int y=0;y<decoded.height();++y){
        const auto* row=reinterpret_cast<const QRgb*>(decoded.constScanLine(y));
        for(int x=0;x<decoded.width();++x){
            const auto pixel=row[x];if(qAlpha(pixel)<64)continue;
            const double weight=qAlpha(pixel)/255.0;
            auto& bin=bins[((qRed(pixel)>>4)<<8)|((qGreen(pixel)>>4)<<4)|(qBlue(pixel)>>4)];
            bin.red+=qRed(pixel)*weight;bin.green+=qGreen(pixel)*weight;bin.blue+=qBlue(pixel)*weight;bin.weight+=weight;
        }
    }
    std::vector<Sample> points;double totalWeight=0;
    for(auto bin:bins)if(bin.weight>0){totalWeight+=bin.weight;bin.red/=bin.weight;bin.green/=bin.weight;bin.blue/=bin.weight;points.push_back(bin);}
    if(points.empty()){error="图片完全透明，没有可提取的颜色，请换一张参考图片。";return false;}
    std::vector<Sample> centers{*std::max_element(points.begin(),points.end(),[](const auto& a,const auto& b){return featureWeight(a)<featureWeight(b);})};
    while(centers.size()<std::min<size_t>(8,points.size())){
        double best=0;const Sample* next=nullptr;
        for(const auto& point:points){
            double nearest=1e9;for(const auto& center:centers)nearest=std::min(nearest,distance(point,center));
            const double score=nearest*std::sqrt(point.weight);
            if(score>best){best=score;next=&point;}
        }
        if(!next||best<1e-6)break;centers.push_back(*next);
    }
    for(int pass=0;pass<12;++pass){
        std::vector<Sample> sums(centers.size());
        for(const auto& point:points){
            size_t nearest=0;for(size_t i=1;i<centers.size();++i)if(distance(point,centers[i])<distance(point,centers[nearest]))nearest=i;
            auto& sum=sums[nearest];sum.red+=point.red*point.weight;sum.green+=point.green*point.weight;sum.blue+=point.blue*point.weight;sum.weight+=point.weight;
        }
        for(size_t i=0;i<centers.size();++i)if(sums[i].weight>0){auto& sum=sums[i];sum.red/=sum.weight;sum.green/=sum.weight;sum.blue/=sum.weight;centers[i]=sum;}else centers[i].weight=0;
    }
    std::stable_sort(centers.begin(),centers.end(),[](const auto& a,const auto& b){return featureWeight(a)>featureWeight(b);});
    ImageColors result;result.thumbnail=decoded;
    for(const auto& center:centers){
        if(center.weight<=0)continue;
        // Ignore thin resize fringes as secondary colors, retaining a small
        // distinctive main feature if it ranked first.
        if(!result.colors.empty()&&center.weight<totalWeight*.01)continue;
        bool duplicate=false;for(const auto& selected:result.colors){Sample sample{double(selected.red()),double(selected.green()),double(selected.blue())};if(distance(center,sample)<400){duplicate=true;break;}}
        if(!duplicate)result.colors.push_back(sampleColor(center));
        if(result.colors.size()==5)break;
    }
    output=std::move(result);return true;
}
}
