#pragma once
#include "core/music.h"
#include "audio/audio_player.h"
#include <QFutureWatcher>
#include <QMainWindow>
#include <QTimer>
#include <memory>
#include <optional>

class QLabel; class QPushButton; class QComboBox; class QDoubleSpinBox;
class QSpinBox; class QListWidget; class QTreeWidget; class QTabWidget;
class QProgressBar; class QSplitter; class QVBoxLayout;
namespace rock {
class PianoRoll;
class PerformancePanel;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent=nullptr);
    ~MainWindow() override;
    void importFiles(const QStringList& paths);
    const Conversion* currentResult() const;
protected:
    bool eventFilter(QObject*,QEvent*) override;
    void closeEvent(QCloseEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;
private:
    struct EditChange {int source{};std::optional<NoteEdit> before, after;};
    struct TimelineState {Song song;NoteEdits edits;int first{},last{-1};};
    struct HistoryEntry {
        HistoryEntry()=default;
        std::vector<EditChange> changes;
        std::optional<TimelineState> before,after;
        HistoryEntry(std::vector<EditChange> c):changes(std::move(c)){}
        HistoryEntry(TimelineState b,TimelineState a):before(std::move(b)),after(std::move(a)){}
    };
    struct Session {
        QString path;
        std::shared_ptr<Song> song;
        Settings settings;
        std::shared_ptr<Conversion> result;
        NoteEdits edits;
        std::vector<HistoryEntry> history;
        size_t historyCursor{};
        int rangeFirst{},rangeLast{-1}; // -1 follows the whole song, including new notes.
    };
    struct Loaded {Session session; QString error;};
    std::vector<Session> sessions_;
    QFutureWatcher<std::vector<Loaded>> importWatcher_;
    std::shared_ptr<std::atomic_bool> cancel_;
    int current_{-1}; bool busy_{}, updating_{};
    bool settingsPending_{};
    QListWidget* library_{}; QTreeWidget* tracks_{};
    PianoRoll* roll_{}; QWidget* workspace_{};
    QPushButton *resetRange_{},*deleteRange_{},*createRange_{};
    QWidget *editorPanel_{},*transportPanel_{},*statusPanel_{},*trackWindow_{},*editorPlaceholder_{};
    QSplitter* workspaceSplit_{}; QVBoxLayout* outerLayout_{};
    QList<int> workspaceSizes_;
    QPushButton* expandTrack_{};
    PerformancePanel* performance_{};
    QTabWidget* tabs_{};
    QLabel *songTitle_{},*subtitle_{},*status_{},*details_{},*clock_{},*dirty_{},*summary_{},*zoomText_{};
    QComboBox *strategy_{},*tempoMode_{},*filter_{},*octaveMode_{};
    QLabel* octaveInfo_{};
    QDoubleSpinBox *bpm_{},*speed_{};
    QSpinBox *hold_{},*gap_{};
    QPushButton *import_{},*exportMidi_{},*apply_{},*play_{},*stop_{},*cancelButton_{};
    QPushButton *addNote_{},*deleteNote_{},*deleteMode_{},*undo_{},*redo_{};
    QProgressBar* progress_{};
    AudioPlayer audio_;
    QTimer timer_; double position_{};
    void buildUi();
    void openTrackWindow();
    void restoreTrackPanel();
    void selectSong(int index);
    void removeSong(int index);
    void moveSong(int from,int to);
    void clearSong();
    void promptImportConflicts(int firstImported);
    void applySettings();
    void recalculate(bool fit=false);
    void refreshResult(bool fit=false);
    void markDirty();
    bool validateParameters();
    void showWarning(const QString& title,const QString& message);
    void showNote(int source);
    void showDiagnostics();
    void exportCurrentMidi();
    void setAllTracks(bool enabled);
    void togglePlayback();
    void startPreview();
    void pausePreview(bool reset=false);
    void refreshClock();
    void editNotes(const std::vector<MappedNote>& notes);
    void addNote(double start,int target);
    void deleteNotes();
    void commitEdits(const NoteEdits& edits);
    void stepHistory(bool redo);
    void updateEditActions();
    std::pair<double,double> selectedRange() const;
    void refreshRange();
    void changeRange(double start,double end);
    void deleteRange();
    void createRange();
};
}
