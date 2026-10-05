#pragma once
#include "core/music.h"
#include <QAbstractScrollArea>
#include <QTimer>
#include <functional>
#include <memory>
#include <set>

namespace rock {
class PianoRoll : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit PianoRoll(QWidget* parent=nullptr);
    void setMusic(std::shared_ptr<const Song> song, std::shared_ptr<const Conversion> result);
    void setTrackFilter(int track);
    void setZoom(double pixelsPerSecond);
    void fitAll();
    void setPlayhead(double seconds, bool follow=false);
    void setPlaybackRange(double first,double last){rangeStart_=first;rangeEnd_=last;viewport()->update();}
    void selectSource(int source, bool reveal=true);
    void selectSources(const std::set<int>& sources, bool reveal=false);
    const std::set<int>& selectedSources() const {return selection_;}
    void setDeleteMode(bool enabled);
    void setAddMode(bool enabled);
    bool addMode() const {return addMode_;}
    void setEditingEnabled(bool enabled);
    bool isEditing() const;
    bool editingEnabled() const {return editingEnabled_;}
    int selectedSource() const {return selected_;}
    double zoom() const {return pixels_;}
signals:
    void noteSelected(int source);
    void seekRequested(double seconds);
    void zoomChanged(double pixels);
    void editStarted();
    void notesEdited(const std::vector<rock::MappedNote>& notes);
    void deleteRequested();
    void addRequested(double start, int target);
    void addModeChanged(bool enabled);
    void rangeEdited(double first,double last);
    void rangeBoundaryToPlayheadRequested(bool left,double seconds);
protected:
    bool event(QEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void wheelEvent(QWheelEvent*) override;
private:
    std::shared_ptr<const Song> song_;
    std::shared_ptr<const Conversion> result_;
    int filter_{-1}, selected_{-1};
    double pixels_{80}, playhead_{};
    double rangeStart_{},rangeEnd_{-1},savedRangeStart_{},savedRangeEnd_{};
    enum class Gesture {None, Move, LeftEdge, RightEdge, Box, RangeLeft, RangeRight};
    Gesture gesture_{Gesture::None};
    std::set<int> selection_, selectionBefore_;
    std::vector<MappedNote> originals_, previews_;
    bool additiveBox_{};
    QPointF pressPosition_, lastPointer_;
    double pressOffset_{};
    bool dragMoved_{}, deleteMode_{}, addMode_{}, editingEnabled_{true};
    QTimer dragScroll_;
    static constexpr int gutter_=92, top_=44;
    int rowHeight() const;
    QRectF noteRect(const MappedNote& n) const;
    int hit(const QPointF& point) const;
    bool visibleNote(const MappedNote& n) const;
    bool editable(int source) const;
    Gesture partAt(int source, const QPointF& point) const;
    Gesture rangePartAt(const QPointF& point) const;
    bool draggingRange() const{return gesture_==Gesture::RangeLeft||gesture_==Gesture::RangeRight;}
    std::pair<QRectF,QRectF> handles(const MappedNote& note) const;
    void updateGesture(const QPointF& point);
    void cancelGesture();
    void notifySelection();
    QRectF selectionRect() const;
    void transposeSelection(int delta);
    void updateRange();
};
}
