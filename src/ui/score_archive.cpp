#include "score_archive.h"
#include <QBuffer>
#include <QDateTime>
#include <QRegularExpression>
#include <QSaveFile>
#include <array>
#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <vector>

namespace rock {
namespace {
void u16(QByteArray& out,std::uint16_t value){out.append(char(value));out.append(char(value>>8));}
void u32(QByteArray& out,std::uint32_t value){u16(out,value&0xffff);u16(out,value>>16);}
std::uint32_t crc32(const QByteArray& bytes){
    static const auto table=[] {
        std::array<std::uint32_t,256> result{};
        for(std::uint32_t i=0;i<result.size();++i){
            auto value=i;for(int bit=0;bit<8;++bit)value=(value>>1)^((value&1)?0xedb88320u:0u);
            result[i]=value;
        }
        return result;
    }();
    std::uint32_t crc=0xffffffffu;
    for(const auto byte:bytes)crc=table[(crc^static_cast<unsigned char>(byte))&0xff]^(crc>>8);
    return crc^0xffffffffu;
}
struct Entry {QByteArray name;std::uint32_t crc{},size{},offset{};};
}
QString scoreFileBase(const QString& title){
    QString name=title.trimmed();name.replace(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1f]"),"_");
    name=name.left(80);if(!name.isEmpty()&&name.back().isHighSurrogate())name.chop(1);
    while(!name.isEmpty()&&(name.back()=='.'||name.back()==' '))name.chop(1);
    if(name.isEmpty())name="歌曲";
    static const QRegularExpression reserved("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)",QRegularExpression::CaseInsensitiveOption);
    if(reserved.match(name).hasMatch())name.prepend('_');
    return name;
}
bool writeScoreArchive(const QString& path,int pages,const std::function<QImage(int)>& renderPage,
                      const QString& title,QString& error,const std::function<bool(int,int)>& progress){
    error.clear();
    if(pages<=0||pages>65535||!renderPage){error="简谱页数无效，无法打包。";return false;}
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly)){error="无法创建 ZIP："+file.errorString();return false;}
    auto fail=[&](const QString& message){error=message;file.cancelWriting();return false;};
    auto write=[&](const QByteArray& bytes){return file.write(bytes)==bytes.size();};
    auto report=[&](int done){return !progress||progress(done,pages);};
    const auto now=QDateTime::currentDateTime();const auto date=now.date();const auto time=now.time();
    const std::uint16_t dosDate=((std::clamp(date.year(),1980,2107)-1980)<<9)|(date.month()<<5)|date.day();
    const std::uint16_t dosTime=(time.hour()<<11)|(time.minute()<<5)|(time.second()/2);
    constexpr std::uint16_t utf8=0x0800;
    constexpr auto maximum=std::numeric_limits<std::uint32_t>::max();
    const auto base=scoreFileBase(title);const int digits=std::max(3,static_cast<int>(QString::number(pages).size()));
    std::vector<Entry> entries;entries.reserve(pages);
    try{
        if(!report(0))return fail("已取消下载简谱。");
        for(int page=0;page<pages;++page){
            const auto image=renderPage(page);if(image.isNull())return fail(QString("第 %1 页简谱生成失败。").arg(page+1));
            QByteArray png;QBuffer buffer(&png);
            if(!buffer.open(QIODevice::WriteOnly)||!image.save(&buffer,"PNG"))return fail(QString("第 %1 页 PNG 编码失败。").arg(page+1));
            Entry entry;entry.name=(base+QString("_简谱_%1.png").arg(page+1,digits,10,QChar('0'))).toUtf8();
            if(entry.name.size()>65535||static_cast<quint64>(png.size())>maximum||
               static_cast<quint64>(file.pos())+30+entry.name.size()+png.size()>maximum)
                return fail("ZIP 文件超过 4 GB，请分别下载页面。");
            entry.crc=crc32(png);entry.size=static_cast<std::uint32_t>(png.size());entry.offset=static_cast<std::uint32_t>(file.pos());
            QByteArray header;u32(header,0x04034b50);u16(header,20);u16(header,utf8);u16(header,0);
            u16(header,dosTime);u16(header,dosDate);u32(header,entry.crc);u32(header,entry.size);u32(header,entry.size);
            u16(header,entry.name.size());u16(header,0);header+=entry.name;
            if(!write(header)||!write(png))return fail("ZIP 写入失败："+file.errorString());
            entries.push_back(std::move(entry));
            if(!report(page+1))return fail("已取消下载简谱。");
        }
        const auto centralOffset=static_cast<std::uint32_t>(file.pos());
        for(const auto& entry:entries){
            QByteArray header;u32(header,0x02014b50);u16(header,20);u16(header,20);u16(header,utf8);u16(header,0);
            u16(header,dosTime);u16(header,dosDate);u32(header,entry.crc);u32(header,entry.size);u32(header,entry.size);
            u16(header,entry.name.size());u16(header,0);u16(header,0);u16(header,0);u16(header,0);u32(header,0);u32(header,entry.offset);header+=entry.name;
            if(static_cast<quint64>(file.pos())+header.size()+22>maximum)return fail("ZIP 文件超过 4 GB，请分别下载页面。");
            if(!write(header))return fail("ZIP 目录写入失败："+file.errorString());
        }
        const auto centralSize=static_cast<std::uint32_t>(file.pos()-centralOffset);
        QByteArray end;u32(end,0x06054b50);u16(end,0);u16(end,0);u16(end,pages);u16(end,pages);
        u32(end,centralSize);u32(end,centralOffset);u16(end,0);
        if(!write(end)||!file.commit())return fail("ZIP 保存失败："+file.errorString());
    }catch(const std::exception& e){return fail("简谱打包失败："+QString::fromUtf8(e.what()));}
    return true;
}
}
