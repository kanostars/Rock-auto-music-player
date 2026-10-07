#pragma once
#include "core/music.h"
#include <QAbstractScrollArea>
#include <QImage>
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
    // Playback follows each measure once; explicit seeking can refocus it.
    void setPosition(double seconds,bool follow=true,bool forceFocus=false);
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
}
