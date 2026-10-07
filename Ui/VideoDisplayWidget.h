#pragma once



#include <QLabel>
#include <QRect>
#include <QSize>
#include <QWidget>

class QBoxLayout;



class VideoDisplayWidget : public QWidget

{

    Q_OBJECT



public:

    enum class PreviewMode { RemotePeer, HostLocal, SharingPaused, HostSharingPaused, HostDeparted };

    explicit VideoDisplayWidget(QWidget *parent = nullptr);

    void setFrame(const QImage &frame);
    void clearFrame();
    void showPlaceholder();
    const QImage &frame() const { return m_frame; }
    void setPreviewMode(PreviewMode mode);
    void setStreamSize(const QSize &size);

    void setRemoteControlEnabled(bool enabled);



    bool isFullscreen() const { return m_fullscreenActive; }

    void toggleFullscreen();

    void exitFullscreenIfActive();



signals:

    void mouseMoveNormalized(uint16_t nx, uint16_t ny);

    void mouseButtonNormalized(uint8_t button, bool down, uint16_t nx, uint16_t ny);

    void wheelNormalized(int16_t delta, uint16_t nx, uint16_t ny);

    void keyEvent(uint16_t vk, bool down, bool extended);

    void fullscreenChanged(bool active);



protected:

    void resizeEvent(QResizeEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override;

    void wheelEvent(QWheelEvent *event) override;

    void keyPressEvent(QKeyEvent *event) override;

    void keyReleaseEvent(QKeyEvent *event) override;



private:

    QRect videoContentRect() const;

    bool mapPointerToNormalized(int x, int y, uint16_t *outX, uint16_t *outY) const;



    QLabel *m_label = nullptr;

    QImage m_frame;

    QSize m_streamSize = QSize(1920, 1080);

    bool m_controlEnabled = false;

    PreviewMode m_previewMode = PreviewMode::RemotePeer;



    bool m_fullscreenActive = false;

    QWidget *m_parentBeforeFullscreen = nullptr;
    QBoxLayout *m_layoutBeforeFullscreen = nullptr;
    int m_layoutIndexBefore = -1;
    int m_layoutStretchBefore = 1;
};


