#pragma once
#include "core/music.h"
#include <QAbstractScrollArea>
#include <QImage>
#include <QWidget>
#include <cstdint>
#include <memory>

namespace rock {
// A page of printable notation. Display scaling follows the available width;
// playback timing always remains in the original MIDI's source seconds.
class NumberScoreView final : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit NumberScoreView(QWidget* parent=nullptr);
    ~NumberScoreView() override;
    void setSong(const QString& name,std::shared_ptr<const Song> song,
                 std::shared_ptr<const Conversion> result,const Settings& settings);
    // Playback follows source time. Group practice follows its printed strike,
    // which can quantize across a measure boundary without changing MIDI time.
    void setPosition(double seconds,bool follow=true,bool forceFocus=false,int practiceGroup=-1);
    void setPage(int pageIndex);
    int currentPage() const;
    int pageCount() const;
    QImage renderPage(int pageIndex) const;
signals:
    void pageChanged(int pageIndex,int total);
    void seekRequested(double seconds);
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
private:
    struct Layout;
    std::unique_ptr<Layout> layout_;
    void updateScrollRange();
    double displayScale() const;
    QPointF paperPoint(const QPointF& point) const;
};

// A complete measure for the practice mini window. Its natural size follows
// the notation density; painting scales the whole measure to the given space.
class NumberMeasureView final : public QWidget {
    Q_OBJECT
public:
    explicit NumberMeasureView(QWidget* parent=nullptr);
    ~NumberMeasureView() override;
    void setSong(std::shared_ptr<const Song> song,std::shared_ptr<const Conversion> result,
                 const Settings& settings);
    void setPosition(double seconds,std::uint16_t expected=0,std::uint16_t matched=0,int practiceGroup=-1);
    QSize sizeHint() const override;
signals:
    void preferredSizeChanged();
protected:
    void paintEvent(QPaintEvent*) override;
private:
    struct Layout;
    std::unique_ptr<Layout> layout_;
};
}
