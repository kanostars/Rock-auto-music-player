#include "project.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace rock {
namespace {
constexpr qint64 maxProjectBytes=128*1024*1024;
constexpr int maxNotes=200000;
[[noreturn]] void invalid(const QString& text){throw std::runtime_error(text.toUtf8().constData());}
QJsonObject object(const QJsonValue& v){if(!v.isObject())invalid("工程中的对象格式无效。");return v.toObject();}
QJsonArray array(const QJsonValue& v,int limit){if(!v.isArray()||v.toArray().size()>limit)invalid("工程中的列表无效或超过限制。");return v.toArray();}
QString string(const QJsonValue& v){if(!v.isString()||v.toString().size()>32768)invalid("工程中的文本无效或过长。");return v.toString();}
double number(const QJsonValue& v,double low,double high){
    if(!v.isDouble()||!std::isfinite(v.toDouble())||v.toDouble()<low||v.toDouble()>high)invalid("工程中的数值超出有效范围。");
    return v.toDouble();
}
int integer(const QJsonValue& v,int low=0,int high=std::numeric_limits<int>::max()){
    const double n=number(v,low,high);if(n!=std::floor(n))invalid("工程中的整数格式无效。");return static_cast<int>(n);
}
bool boolean(const QJsonValue& v){if(!v.isBool())invalid("工程中的开关格式无效。");return v.toBool();}
QJsonArray tuple(const QJsonValue& v,int count){auto a=array(v,count);if(a.size()!=count)invalid("工程中的音符或事件不完整。");return a;}
QByteArray readFile(const QString& path,qint64 limit){
    QFile file(path);if(!file.open(QIODevice::ReadOnly))invalid("无法读取文件："+path+"\n"+file.errorString());
    if(file.size()>limit)invalid("文件超过大小限制："+path);
    const auto bytes=file.read(limit+1);
    if(file.error()!=QFile::NoError||bytes.size()>limit)invalid("文件读取失败或超过大小限制："+path);
    return bytes;
}
QJsonObject songJson(const Song& s){
    QJsonArray tracks,notes,tempos,meters,warnings;
    for(const auto& t:s.tracks)tracks.append(QJsonArray{QString::fromLatin1(QByteArray::fromStdString(t.name).toBase64()),t.source,t.channel,t.count});
    for(const auto& n:s.notes)notes.append(QJsonArray{n.track,n.pitch,n.velocity,n.start,n.end,n.added,n.derived});
    for(const auto& t:s.tempos)tempos.append(QJsonArray{t.tick,t.micros});
    for(const auto& m:s.timeSignatures)meters.append(QJsonArray{m.tick,m.numerator,m.denominator});
    for(const auto& w:s.warnings)warnings.append(QString::fromUtf8(w));
    return {{"ppq",s.ppq},{"format",s.format},{"endTick",s.endTick},{"tracks",tracks},{"notes",notes},{"tempos",tempos},{"meters",meters},{"warnings",warnings}};
}
Song readSong(const QJsonValue& value){
    const auto o=object(value);Song s;s.ppq=integer(o["ppq"],1,32767);s.format=integer(o["format"],0,1);s.endTick=integer(o["endTick"]);
    for(const auto& v:array(o["tracks"],4096)){
        const auto a=tuple(v,4);const auto name=QByteArray::fromBase64Encoding(string(a[0]).toLatin1(),QByteArray::AbortOnBase64DecodingErrors);
        if(!name)invalid("工程音轨名称编码无效。");
        s.tracks.push_back({name.decoded.toStdString(),integer(a[1],0,255),integer(a[2],0,15),integer(a[3],0,maxNotes)});
    }
    for(const auto& v:array(o["notes"],maxNotes)){
        const auto a=tuple(v,7);Note n{integer(a[0],0,static_cast<int>(s.tracks.size())-1),integer(a[1],0,127),integer(a[2],1,127),integer(a[3]),integer(a[4]),boolean(a[5]),boolean(a[6])};
        if(n.end<=n.start)invalid("工程音符的结束时间必须晚于开始时间。");s.notes.push_back(n);
    }
    int previous=-1;
    for(const auto& v:array(o["tempos"],2000000)){
        const auto a=tuple(v,2);const int tick=integer(a[0]),micros=integer(a[1],1,0xffffff);
        if(tick<=previous||(previous<0&&tick!=0))invalid("工程速度事件的顺序无效。");
        const double seconds=s.secondsAt(tick);s.tempos.push_back({tick,micros,seconds});previous=tick;
    }
    if(s.tempos.empty())invalid("工程缺少初始速度。");
    s.timeSignatures.clear();previous=-1;
    for(const auto& v:array(o["meters"],2000000)){
        const auto a=tuple(v,3);TimeSignature m{integer(a[0]),integer(a[1],1,255),integer(a[2],1,1<<30)};
        if(m.tick<=previous||(previous<0&&m.tick!=0)||(m.denominator&(m.denominator-1)))invalid("工程拍号事件无效。");
        s.timeSignatures.push_back(m);previous=m.tick;
    }
    if(s.timeSignatures.empty())invalid("工程缺少初始拍号。");
    for(const auto& w:array(o["warnings"],200000))s.warnings.push_back(string(w).toUtf8().toStdString());
    return s;
}
QJsonObject settingsJson(const Settings& s){
    QJsonArray enabled,solo;for(bool b:s.enabled)enabled.append(b);for(bool b:s.solo)solo.append(b);
    return {{"nearest",s.nearest},{"fixedTempo",s.fixedTempo},{"autoTranspose",s.autoTranspose},{"bpm",s.bpm},{"speed",s.speed},{"holdMs",s.holdMs},{"gapMs",s.gapMs},{"enabled",enabled},{"solo",solo}};
}
Settings readSettings(const QJsonValue& v,size_t tracks){
    const auto o=object(v);Settings s;
    s.nearest=boolean(o["nearest"]);s.fixedTempo=boolean(o["fixedTempo"]);s.autoTranspose=boolean(o["autoTranspose"]);
    s.bpm=number(o["bpm"],20,400);s.speed=number(o["speed"],.25,3);s.holdMs=integer(o["holdMs"],1,1000);s.gapMs=integer(o["gapMs"],0,1000);
    for(const auto& b:array(o["enabled"],4096))s.enabled.push_back(boolean(b));
    for(const auto& b:array(o["solo"],4096))s.solo.push_back(boolean(b));
    if(s.enabled.size()!=tracks||s.solo.size()!=tracks)invalid("工程音轨设置与曲目不一致。");return s;
}
QJsonArray editJson(const NoteEdit& e){return {e.startTick,e.endTick,e.target,e.deleted,e.skipped};}
NoteEdit readEdit(const QJsonValue& v){
    const auto a=tuple(v,5);NoteEdit e{integer(a[0]),integer(a[1]),integer(a[2],-1,8),boolean(a[3]),boolean(a[4])};
    if(e.endTick<=e.startTick||(!e.deleted&&e.target<0))invalid("工程音符编辑无效。");return e;
}
QJsonArray editsJson(const NoteEdits& edits){QJsonArray a;for(const auto& [id,e]:edits)a.append(QJsonObject{{"source",id},{"edit",editJson(e)}});return a;}
NoteEdits readEdits(const QJsonValue& v,size_t notes){
    NoteEdits e;for(const auto& item:array(v,maxNotes)){
        const auto o=object(item);const int id=integer(o["source"],0,static_cast<int>(notes)-1);
        if(!e.emplace(id,readEdit(o["edit"])).second)invalid("工程中存在重复的音符编辑编号。");
    }return e;
}
void readRange(const QJsonObject& o,int& first,int& last){first=integer(o["first"]);last=integer(o["last"],-1);if(last!=-1&&last<first)invalid("工程片段区间无效。");}
QJsonObject timelineJson(const ProjectTimelineState& t){return {{"song",songJson(t.song)},{"edits",editsJson(t.edits)},{"first",t.first},{"last",t.last}};}
ProjectTimelineState readTimeline(const QJsonValue& v){
    const auto o=object(v);ProjectTimelineState t;t.song=readSong(o["song"]);t.edits=readEdits(o["edits"],t.song.notes.size());readRange(o,t.first,t.last);return t;
}
QJsonArray historyJson(const std::vector<ProjectHistoryEntry>& entries){
    QJsonArray history;
    for(const auto& h:entries){
        QJsonObject o;if(h.before&&h.after){o["before"]=timelineJson(*h.before);o["after"]=timelineJson(*h.after);}
        else {QJsonArray changes;for(const auto& c:h.changes)changes.append(QJsonObject{{"source",c.source},{"before",c.before?QJsonValue(editJson(*c.before)):QJsonValue::Null},{"after",c.after?QJsonValue(editJson(*c.after)):QJsonValue::Null}});o["changes"]=changes;}
        history.append(o);
    }return history;
}
std::vector<ProjectHistoryEntry> readHistory(const QJsonValue& v,int ppq,size_t tracks,size_t notes){
    std::vector<ProjectHistoryEntry> entries;size_t maxSource=notes;
    for(const auto& item:array(v,256)){
        const auto o=object(item);ProjectHistoryEntry h;
        if(o.contains("before")||o.contains("after")){
            if(o.contains("changes"))invalid("工程撤销记录格式无效。");
            h.before=readTimeline(o["before"]);h.after=readTimeline(o["after"]);
            // Undo may precede a later manual track addition; the editor retains
            // those extra tracks and source IDs when restoring a timeline.
            if(h.before->song.tracks.size()>tracks||h.after->song.tracks.size()>tracks||
               h.before->song.ppq!=h.after->song.ppq||
               (ppq>0&&h.before->song.ppq!=ppq))invalid("工程撤销记录的音轨或时间制不一致。");
            maxSource=std::max({maxSource,h.before->song.notes.size(),h.after->song.notes.size()});
        }else {
            std::set<int> seen;
            for(const auto& change:array(o["changes"],maxNotes)){
                const auto c=object(change);ProjectEditChange e;e.source=integer(c["source"],0,maxNotes-1);
                if(!seen.insert(e.source).second)invalid("工程撤销记录中有重复音符编号。");
                if(!c.contains("before")||!c.contains("after"))invalid("工程撤销记录不完整。");
                if(!c["before"].isNull())e.before=readEdit(c["before"]);if(!c["after"].isNull())e.after=readEdit(c["after"]);h.changes.push_back(e);
            }
        }entries.push_back(std::move(h));
    }
    for(const auto& h:entries)for(const auto& c:h.changes)if(c.source>=static_cast<int>(maxSource))invalid("工程撤销记录引用了不存在的音符。");
    return entries;
}
QJsonObject viewJson(const ProjectState& p){
    QJsonArray selected,sizes;for(int i:p.selectedSources)selected.append(i);for(int i:p.splitterSizes)sizes.append(i);
    QJsonObject v{{"current",p.current},{"position",p.position},{"zoom",p.zoom},{"volume",p.volume},{"trackFilter",p.trackFilter},{"tab",p.tab},{"horizontalScroll",p.horizontalScroll},{"verticalScroll",p.verticalScroll},{"playMode",p.playMode},{"countdown",p.countdown},{"activateTarget",p.activateTarget},{"selection",selected},{"splitterSizes",sizes}};
    if(p.pendingSettings)v["pendingSettings"]=settingsJson(*p.pendingSettings);return v;
}
struct SessionShape {size_t tracks{},notes{};};
void readView(const QJsonValue& value,ProjectState& p,const std::vector<SessionShape>& shapes={}){
    const auto v=object(value);p.current=integer(v["current"],-1,static_cast<int>(p.sessions.size())-1);
    if(!p.sessions.empty()&&p.current<0)invalid("工程未指定当前曲目。");
    const SessionShape currentShape=p.current<0?SessionShape{}:
        shapes.empty()?SessionShape{p.sessions[p.current].song->tracks.size(),p.sessions[p.current].song->notes.size()}:shapes[p.current];
    p.position=number(v["position"],0,1e12);p.zoom=number(v["zoom"],.001,1200);
    p.volume=integer(v["volume"],0,100);p.trackFilter=integer(v["trackFilter"],-1,static_cast<int>(currentShape.tracks)-1);
    p.tab=integer(v["tab"],0,2);p.horizontalScroll=integer(v["horizontalScroll"],0,2000000000);p.verticalScroll=integer(v["verticalScroll"],0,2000000000);p.playMode=integer(v["playMode"],0,3);
    p.countdown=v.contains("countdown")?integer(v["countdown"],1):5;
    p.activateTarget=v.contains("activateTarget")?boolean(v["activateTarget"]):true;
    std::set<int> selected;
    for(const auto& i:array(v["selection"],maxNotes)){
        const int source=integer(i,0,static_cast<int>(currentShape.notes)-1);
        if(!selected.insert(source).second)invalid("工程选中音符编号重复。");p.selectedSources.push_back(source);
    }
    for(const auto& i:array(v["splitterSizes"],3))p.splitterSizes.append(integer(i,0,100000));
    if(!p.splitterSizes.empty()&&p.splitterSizes.size()!=3)invalid("工程布局尺寸无效。");
    if(v.contains("pendingSettings")){if(p.current<0)invalid("空工程包含无效转换参数。");p.pendingSettings=readSettings(v["pendingSettings"],currentShape.tracks);}
}
QJsonObject sessionJson(const ProjectSession& s){
    if(!s.song)invalid("工程曲目数据为空。");
    return {{"sourcePath",s.path},{"song",songJson(*s.song)},{"noteCount",static_cast<int>(s.song->notes.size())},
        {"trackCount",static_cast<int>(s.song->tracks.size())},{"ppq",s.song->ppq},{"settings",settingsJson(s.settings)},
        {"edits",editsJson(s.edits)},{"history",historyJson(s.history)},{"historyCursor",static_cast<double>(s.historyCursor)},{"first",s.rangeFirst},{"last",s.rangeLast}};
}
ProjectSession readSession(const QJsonObject& o,Song song){
    ProjectSession s;s.path=string(o["sourcePath"]);s.song=std::make_shared<Song>(std::move(song));s.settings=readSettings(o["settings"],s.song->tracks.size());
    if(s.path.trimmed().isEmpty())invalid("工程曲目来源路径为空。");
    if((o.contains("noteCount")&&integer(o["noteCount"],0,maxNotes)!=static_cast<int>(s.song->notes.size()))||
       (o.contains("trackCount")&&integer(o["trackCount"],0,4096)!=static_cast<int>(s.song->tracks.size()))||
       (o.contains("ppq")&&integer(o["ppq"],1,32767)!=s.song->ppq))invalid("工程曲目大小或时间制与保存记录不一致。");
    s.edits=readEdits(o["edits"],s.song->notes.size());s.history=readHistory(o["history"],s.song->ppq,s.song->tracks.size(),s.song->notes.size());s.historyCursor=integer(o["historyCursor"],0,static_cast<int>(s.history.size()));readRange(o,s.rangeFirst,s.rangeLast);
    s.result=std::make_shared<Conversion>(convert(*s.song,s.settings,s.edits));return s;
}
SessionShape readSkippedSession(const QJsonObject& o,ProjectSession& session){
    if(o.contains("song")){
        session=readSession(o,readSong(o["song"]));return {session.song->tracks.size(),session.song->notes.size()};
    }
    const auto settings=object(o["settings"]);
    const size_t tracks=o.contains("trackCount")?integer(o["trackCount"],0,4096):array(settings["enabled"],4096).size();
    // Version 1 linked projects originally omitted source dimensions. Validate
    // their missing notes against the importer limit when no snapshot exists.
    const size_t notes=o.contains("noteCount")?integer(o["noteCount"],0,maxNotes):maxNotes;
    const int ppq=o.contains("ppq")?integer(o["ppq"],1,32767):-1;
    session.path=string(o["sourcePath"]);if(session.path.trimmed().isEmpty())invalid("工程曲目来源路径为空。");
    session.settings=readSettings(o["settings"],tracks);session.edits=readEdits(o["edits"],notes);
    session.history=readHistory(o["history"],ppq,tracks,notes);
    session.historyCursor=integer(o["historyCursor"],0,static_cast<int>(session.history.size()));
    readRange(o,session.rangeFirst,session.rangeLast);return {tracks,notes};
}
}
bool saveProject(const QString& path,const ProjectState& state,ProjectStorage storage,QString& error){
    try{
        if(state.sessions.size()>1024)invalid("工程曲目数超过 1024 首限制。");
        QJsonArray songs;ProjectState checked;
        for(const auto& s:state.sessions){
            if(QFileInfo(path).absoluteFilePath().compare(QFileInfo(s.path).absoluteFilePath(),Qt::CaseInsensitive)==0)
                invalid("工程文件不能覆盖原 MIDI，请使用 .rockproj 文件名。");
            auto o=sessionJson(s);checked.sessions.push_back(readSession(o,readSong(o["song"])));
            if(storage==ProjectStorage::Linked){
                const QString source=QFileInfo(s.path).absoluteFilePath();const auto bytes=readFile(source,32*1024*1024);const auto original=parseMidi(bytes.toStdString());
                o["sourcePath"]=source;o["relativePath"]=QDir(QFileInfo(path).absolutePath()).relativeFilePath(source);o["sha256"]=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
                if(songJson(original)==o["song"].toObject())o.remove("song");
            }songs.append(o);
        }
        readView(viewJson(state),checked);
        QJsonObject root{{"format","RockAutoMusicPlayProject"},{"version",1},{"storage",storage==ProjectStorage::Linked?"linked":"embedded"},{"sessions",songs},{"view",viewJson(state)}};
        const auto bytes=QJsonDocument(root).toJson(QJsonDocument::Compact);if(bytes.size()>maxProjectBytes)invalid("工程超过 128 MB，请减少撤销记录或曲目数量。");
        QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())invalid("无法保存工程："+file.errorString());
        error.clear();return true;
    }catch(const std::exception& e){error=QString::fromUtf8(e.what());return false;}
    catch(...){error="保存工程时发生未知错误。";return false;}
}
bool loadProject(const QString& path,ProjectState& output,QString& error,
                 const std::function<MissingMidiResolution(const QString&)>& locateMissing,ProjectLoadInfo* info){
    try{
        QJsonParseError parseError;const auto doc=QJsonDocument::fromJson(readFile(path,maxProjectBytes),&parseError);
        if(parseError.error!=QJsonParseError::NoError||!doc.isObject())invalid("工程 JSON 格式损坏或不完整。");
        const auto root=doc.object();if(string(root["format"])!="RockAutoMusicPlayProject")invalid("这不是九键音乐工作台工程。");
        if(integer(root["version"],1)!=1)invalid("该工程版本暂不支持，请更新软件后再试。");
        const auto storage=string(root["storage"]);if(storage!="embedded"&&storage!="linked")invalid("工程曲目保存方式无效。");
        ProjectState p;ProjectLoadInfo report;report.storage=storage=="linked"?ProjectStorage::Linked:ProjectStorage::Embedded;
        std::vector<SessionShape> shapes;std::vector<bool> skipped;
        for(const auto& value:array(root["sessions"],1024)){
            auto o=object(value);Song song;bool skip=false;
            if(storage=="embedded")song=readSong(o["song"]);
            else{
                const auto hash=string(o["sha256"]).toLower();if(hash.size()!=64||QByteArray::fromHex(hash.toLatin1()).size()!=32)invalid("工程 MIDI 校验值无效。");
                const auto original=string(o["sourcePath"]),relative=string(o["relativePath"]);
                if(original.trimmed().isEmpty()||relative.trimmed().isEmpty())invalid("工程 MIDI 引用路径为空。");
                QString source;QByteArray bytes;
                auto matches=[&](const QString& candidate){
                    if(!QFileInfo(candidate).isFile())return false;
                    try{auto data=readFile(candidate,32*1024*1024);if(QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex())!=hash)return false;bytes=std::move(data);source=QFileInfo(candidate).absoluteFilePath();return true;}catch(const std::exception&){return false;}
                };
                if(!matches(QDir(QFileInfo(path).absolutePath()).absoluteFilePath(relative))&&!matches(original)){
                    const auto resolution=locateMissing?locateMissing(original):MissingMidiResolution{};
                    if(resolution.action==MissingMidiAction::Cancel)
                        invalid("已取消导入工程，当前工作台保留。");
                    if(resolution.action==MissingMidiAction::Skip)skip=true;
                    else if(resolution.action==MissingMidiAction::Locate){
                        if(resolution.path.isEmpty())invalid("未选择引用 MIDI，工程未导入，当前工作台保留。");
                        if(!matches(resolution.path))invalid("所选 MIDI 与工程引用的文件不一致："+resolution.path);
                        report.relocated=true;
                    }else invalid("工程 MIDI 处理方式无效。");
                }
                if(!skip){
                    const auto base=parseMidi(bytes.toStdString());song=o.contains("song")?readSong(o["song"]):base;o["sourcePath"]=source;
                    if(QFileInfo(source).absoluteFilePath().compare(QFileInfo(original).absoluteFilePath(),Qt::CaseInsensitive)!=0)report.relocated=true;
                }
            }
            if(skip){
                ProjectSession session;shapes.push_back(readSkippedSession(o,session));
                report.skippedOriginalIndexes.push_back(static_cast<int>(p.sessions.size()));
                p.sessions.push_back(std::move(session));
            }else{
                p.sessions.push_back(readSession(o,std::move(song)));const auto& session=p.sessions.back();
                shapes.push_back({session.song->tracks.size(),session.song->notes.size()});
            }
            skipped.push_back(skip);
        }
        // Validate view IDs against the original playlist, including skipped
        // sources. Filtering first would hide corrupt current/selection fields.
        readView(root["view"],p,shapes);
        if(!report.skippedOriginalIndexes.empty()){
            std::vector<int> remap(p.sessions.size(),-1);std::vector<ProjectSession> kept;
            for(size_t index=0;index<p.sessions.size();++index)if(!skipped[index]){
                remap[index]=static_cast<int>(kept.size());kept.push_back(std::move(p.sessions[index]));
            }
            const int previousCurrent=p.current;
            if(previousCurrent>=0&&remap[previousCurrent]>=0)p.current=remap[previousCurrent];
            else{
                p.current=-1;
                for(int index=previousCurrent+1;index<static_cast<int>(remap.size());++index)if(remap[index]>=0){p.current=remap[index];break;}
                if(p.current<0)for(int index=previousCurrent-1;index>=0;--index)if(remap[index]>=0){p.current=remap[index];break;}
                p.position=0;p.trackFilter=-1;p.selectedSources.clear();p.pendingSettings.reset();
                p.horizontalScroll=0;p.verticalScroll=0;
            }
            p.sessions=std::move(kept);
        }
        if(info)*info=std::move(report);output=std::move(p);error.clear();return true;
    }catch(const std::exception& e){error=QString::fromUtf8(e.what());return false;}
    catch(...){error="导入工程时发生未知错误，当前工作台保留。";return false;}
}
QByteArray projectFingerprint(const ProjectState& state){
    QJsonArray sessions;
    for(size_t index=0;index<state.sessions.size();++index){
        auto session=sessionJson(state.sessions[index]);
        if(static_cast<int>(index)==state.current&&state.pendingSettings&&
           settingsJson(*state.pendingSettings)!=settingsJson(state.sessions[index].settings))
            session["pendingSettings"]=settingsJson(*state.pendingSettings);
        sessions.append(session);
    }
    const QJsonObject content{{"sessions",sessions},{"volume",state.volume},{"playMode",state.playMode},
        {"countdown",state.countdown},{"activateTarget",state.activateTarget}};
    return QCryptographicHash::hash(QJsonDocument(content).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256);
}
}
