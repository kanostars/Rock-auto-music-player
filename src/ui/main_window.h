#pragma once
#include "app/project.h"
#include "audio/audio_player.h"
#include <QFutureWatcher>
#include <QMainWindow>
#include <QTimer>
#include <memory>
#include <optional>

class QLabel; class QPushButton; class QComboBox; class QDoubleSpinBox;
class QSpinBox; class QListWidget; class QTreeWidget; class QTabWidget;
class QProgressBar; class QSplitter; class QVBoxLayout;
class QSlider;
class QShortcut;
namespace rock {
class PianoRoll;
class PerformancePanel;
class MiniPlayer;
class SettingsPage;
class PracticePage;
class TimeSeekEdit;
class GlobalShortcut;
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
    enum class TextScoreFormat { Hand, Keys };
    using EditChange=ProjectEditChange;
    using TimelineState=ProjectTimelineState;
    using Session=ProjectSession;
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
    MiniPlayer* mini_{};QTimer miniRefresh_;bool miniPerformance_{true};
    bool miniSeekResume_{},miniSeekPerformance_{};std::shared_ptr<Song> miniSeekSong_;
    QTabWidget* tabs_{};
    SettingsPage* appSettings_{};QPushButton* settingsNavigation_{};
    QPushButton *headerLogo_{},*practiceBack_{};
    QPushButton* projectMenu_{};
    QString projectPath_;
    ProjectStorage projectStorage_{ProjectStorage::Embedded};
    QByteArray savedProjectFingerprint_;
    bool projectNeedsSave_{},closePromptOpen_{},closeAfterImport_{};
    QLabel* libraryCount_{};
    QLabel *headerPageTitle_{},*headerSubtitle_{};
    PracticePage* practicePage_{};QPushButton* practiceButton_{};
    std::array<QShortcut*,3> editorShortcuts_{};QShortcut* fullscreenShortcut_{};
    std::array<GlobalShortcut*,3> globalShortcuts_{};bool closing_{};
    QLabel *songTitle_{},*subtitle_{},*status_{},*details_{},*dirty_{},*summary_{},*zoomText_{};
    TimeSeekEdit* clock_{};
    QComboBox *strategy_{},*tempoMode_{},*filter_{},*octaveMode_{};
    QLabel* octaveInfo_{};
    QDoubleSpinBox *bpm_{},*speed_{};
    QSpinBox *hold_{},*gap_{};
    QPushButton *import_{},*exportMidi_{},*copyHandScore_{},*copyKeyScore_{},*apply_{},*play_{},*stop_{},*cancelButton_{};
    QPushButton *addNote_{},*deleteNote_{},*deleteMode_{},*undo_{},*redo_{};
    QProgressBar* progress_{};
    AudioPlayer audio_;
    QSlider* volume_{};
    QTimer timer_; double position_{};
    void buildUi();
    void showAppSettings(bool show);
    void setPageHeader(const QString& title={},const QString& subtitle={});
    void openPractice();
    void leavePractice();
    void updateShortcuts();
    void updateGlobalShortcuts();
    void toggleMiniPlayer();
    void openMiniPlayer();
    void restoreMainWindow();
    void syncMiniPlayer();
    void togglePerformance();
    void quitFromMiniPlayer();
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
    ProjectState captureProject() const;
    void restoreProject(ProjectState state);
    void exportWorkbenchProject();
    bool saveWorkbenchProject(bool saveAs);
    bool writeWorkbenchProject(const QString& path,ProjectState state,ProjectStorage storage);
    bool hasUnsavedProjectChanges() const;
    void importWorkbenchProject();
    bool importProjectFile(const QString& path);
    bool currentScoreText(TextScoreFormat format,QString& text);
    void copyCurrentScore(TextScoreFormat format);
    void exportCurrentScore(TextScoreFormat format);
    void setAllTracks(bool enabled);
    void togglePlayback();
    void startPreview();
    void pausePreview(bool reset=false);
    void seekPreviewTime(double seconds);
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
    void moveRangeBoundaryToPlayhead(bool left,double seconds);
    void deleteRange();
    void createRange();
};
}
