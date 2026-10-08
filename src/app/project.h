#pragma once
#include "core/music.h"
#include <QByteArray>
#include <QList>
#include <QString>
#include <functional>
#include <memory>
#include <optional>

namespace rock {
struct ProjectEditChange {int source{};std::optional<NoteEdit> before,after;};
struct ProjectTimelineState {Song song;NoteEdits edits;int first{},last{-1};};
struct ProjectHistoryEntry {
    std::vector<ProjectEditChange> changes;
    std::optional<ProjectTimelineState> before,after;
    ProjectHistoryEntry()=default;
    ProjectHistoryEntry(std::vector<ProjectEditChange> c):changes(std::move(c)){}
    ProjectHistoryEntry(ProjectTimelineState b,ProjectTimelineState a):before(std::move(b)),after(std::move(a)){}
};
struct ProjectSession {
    QString path;
    std::shared_ptr<Song> song;
    Settings settings;
    std::shared_ptr<Conversion> result;
    NoteEdits edits;
    std::vector<ProjectHistoryEntry> history;
    size_t historyCursor{};
    int rangeFirst{},rangeLast{-1};
};
struct ProjectState {
    std::vector<ProjectSession> sessions;
    int current{-1};
    double position{},zoom{80};
    int volume{60},trackFilter{-1},tab{},horizontalScroll{},verticalScroll{},playMode{3};
    int countdown{5};bool activateTarget{true};
    std::vector<int> selectedSources;
    QList<int> splitterSizes;
    std::optional<Settings> pendingSettings;
};
enum class ProjectStorage {Embedded,Linked};
enum class MissingMidiAction {Locate,Skip,Cancel};
struct MissingMidiResolution {MissingMidiAction action{MissingMidiAction::Cancel};QString path;};
struct ProjectLoadInfo {
    ProjectStorage storage{ProjectStorage::Embedded};
    std::vector<int> skippedOriginalIndexes;
    bool relocated{};
};
bool saveProject(const QString& path,const ProjectState&,ProjectStorage,QString& error);
bool loadProject(const QString& path,ProjectState& output,QString& error,
                 const std::function<MissingMidiResolution(const QString&)>& locateMissing={},
                 ProjectLoadInfo* info=nullptr);
QByteArray projectFingerprint(const ProjectState&);
}
