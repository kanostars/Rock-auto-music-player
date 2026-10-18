#pragma once

#include <QImage>
#include <QWidget>
#include <memory>

namespace rock {
class WallpaperPlayback;

// Decodes a real video frame using the installed Windows media components.
// Safe to call from an import worker; it creates its own COM/MF scope.
bool wallpaperVideoThumbnail(const QString& path,QImage& image,QString& error);

class WallpaperLayer final : public QWidget {
    Q_OBJECT
public:
    explicit WallpaperLayer(QWidget* parent=nullptr);
    ~WallpaperLayer() override;

signals:
    void playbackError(const QString& message);

protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
    bool eventFilter(QObject* watched,QEvent* event) override;

private:
    friend class WallpaperPlayback;
    std::shared_ptr<WallpaperPlayback> playback_;
    QImage frame_;
    void syncPlayback();
};

}
