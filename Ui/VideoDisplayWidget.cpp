#include "VideoDisplayWidget.h"



#include <QBoxLayout>
#include <QHBoxLayout>

#include <QKeyEvent>

#include <QMouseEvent>

#include <QPixmap>

#include <QResizeEvent>

#include <QSizePolicy>

#include <QVBoxLayout>

#include <QWheelEvent>



VideoDisplayWidget::VideoDisplayWidget(QWidget *parent)

    : QWidget(parent)

    , m_label(new QLabel(this))

{

    setFocusPolicy(Qt::StrongFocus);

    m_label->setAlignment(Qt::AlignCenter);

    m_label->setMinimumSize(480, 320);

    m_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_label->setScaledContents(false);

    m_label->setStyleSheet(QStringLiteral("background-color: #0a0e13; border: 1px solid #243044; border-radius: 8px; color: #8b99a8;"));

    showPlaceholder();
    m_label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setMouseTracking(true);

    auto *layout = new QVBoxLayout(this);

    layout->setContentsMargins(0, 0, 0, 0);

    layout->addWidget(m_label);

}



QRect VideoDisplayWidget::videoContentRect() const

{

    const QSize area = m_label->size();

    if (area.width() < 1 || area.height() < 1) {

        return QRect(0, 0, qMax(1, width()), qMax(1, height()));

    }

    if (m_frame.isNull()) {

        return QRect(0, 0, area.width(), area.height());

    }



    const QSize scaled = m_frame.size().scaled(area, Qt::KeepAspectRatio);

    const int x = (area.width() - scaled.width()) / 2;

    const int y = (area.height() - scaled.height()) / 2;

    return QRect(x, y, scaled.width(), scaled.height());

}



bool VideoDisplayWidget::mapPointerToNormalized(int x, int y, uint16_t *outX, uint16_t *outY) const

{

    const QRect content = videoContentRect();

    if (content.width() < 2 || content.height() < 2) {

        return false;

    }



    const int cx = qBound(content.left(), x, content.right());

    const int cy = qBound(content.top(), y, content.bottom());

    const int localX = cx - content.left();

    const int localY = cy - content.top();

    const int w = qMax(1, content.width() - 1);

    const int h = qMax(1, content.height() - 1);



    *outX = uint16_t(qBound(0, int(double(localX) / double(w) * 65535.0), 65535));

    *outY = uint16_t(qBound(0, int(double(localY) / double(h) * 65535.0), 65535));

    return true;

}



void VideoDisplayWidget::setFrame(const QImage &frame)

{

    if (frame.isNull()) {

        return;

    }

    m_frame = frame;

    m_label->setText(QString());



    QSize target = m_label->size();

    if (target.width() < 16 || target.height() < 16) {

        target = size();

    }

    if (target.width() < 16 || target.height() < 16) {

        target = QSize(640, 360);

    }



    QPixmap pix = QPixmap::fromImage(m_frame);

    if (pix.isNull()) {

        return;

    }

    m_label->setPixmap(pix.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));

}



void VideoDisplayWidget::clearFrame()

{

    m_frame = QImage();

    m_label->clear();

    showPlaceholder();

}



void VideoDisplayWidget::setPreviewMode(PreviewMode mode)

{

    m_previewMode = mode;

}



void VideoDisplayWidget::showPlaceholder()

{

    if (m_previewMode == PreviewMode::HostLocal) {

        m_label->setText(tr("Your screen preview will appear here"));

    } else if (m_previewMode == PreviewMode::SharingPaused) {

        m_label->setText(tr("Host paused screen sharing"));

    } else if (m_previewMode == PreviewMode::HostSharingPaused) {

        m_label->setText(tr("Screen sharing paused — press Start screen sharing to resume"));

    } else if (m_previewMode == PreviewMode::HostDeparted) {

        m_label->setText(tr("Host left the room.\nLeave and rejoin the same room code to become the new host."));

    } else {

        m_label->setText(tr("Remote screen will appear here"));

    }

}



void VideoDisplayWidget::setStreamSize(const QSize &size)

{

    if (size.isValid()) {

        m_streamSize = size;

    }

}



void VideoDisplayWidget::setRemoteControlEnabled(bool enabled)

{

    m_controlEnabled = enabled;

    if (enabled) {

        setFocus();

    }

}



void VideoDisplayWidget::toggleFullscreen()

{

    if (m_fullscreenActive) {

        exitFullscreenIfActive();

        return;

    }



    m_parentBeforeFullscreen = parentWidget();
    m_layoutBeforeFullscreen = nullptr;
    m_layoutIndexBefore = -1;
    m_layoutStretchBefore = 1;
    if (m_parentBeforeFullscreen) {
        m_layoutBeforeFullscreen = qobject_cast<QBoxLayout *>(m_parentBeforeFullscreen->layout());
        if (m_layoutBeforeFullscreen) {
            m_layoutIndexBefore = m_layoutBeforeFullscreen->indexOf(this);
            if (m_layoutIndexBefore >= 0) {
                m_layoutStretchBefore = m_layoutBeforeFullscreen->stretch(m_layoutIndexBefore);
            }
        }
    }



    setParent(nullptr);

    setWindowFlag(Qt::Window, true);

    setWindowTitle(tr("Darpan — remote view"));

    showFullScreen();

    setFocus();

    m_fullscreenActive = true;

    emit fullscreenChanged(true);

}



void VideoDisplayWidget::exitFullscreenIfActive()

{

    if (!m_fullscreenActive) {

        return;

    }



    hide();

    setWindowFlag(Qt::Window, false);

    if (m_parentBeforeFullscreen) {
        setParent(m_parentBeforeFullscreen);
        if (m_layoutBeforeFullscreen && m_layoutIndexBefore >= 0) {
            m_layoutBeforeFullscreen->insertWidget(m_layoutIndexBefore, this, m_layoutStretchBefore);
        } else if (m_layoutBeforeFullscreen) {
            m_layoutBeforeFullscreen->addWidget(this, m_layoutStretchBefore);
        }
    }

    show();

    m_fullscreenActive = false;
    m_parentBeforeFullscreen = nullptr;
    m_layoutBeforeFullscreen = nullptr;
    m_layoutIndexBefore = -1;
    emit fullscreenChanged(false);



    if (!m_frame.isNull()) {

        setFrame(m_frame);

    }

}



void VideoDisplayWidget::resizeEvent(QResizeEvent *event)

{

    QWidget::resizeEvent(event);

    if (!m_frame.isNull()) {

        setFrame(m_frame);

    }

}



void VideoDisplayWidget::mouseMoveEvent(QMouseEvent *event)

{

    if (m_controlEnabled) {

        uint16_t nx = 0;

        uint16_t ny = 0;

        const QPoint p = m_label->mapFrom(this, event->position().toPoint());

        if (mapPointerToNormalized(p.x(), p.y(), &nx, &ny)) {

            emit mouseMoveNormalized(nx, ny);

        }

    }

    QWidget::mouseMoveEvent(event);

}



void VideoDisplayWidget::mousePressEvent(QMouseEvent *event)

{

    if (m_controlEnabled) {

        const uint8_t btn = event->button() == Qt::RightButton  ? 1

                              : event->button() == Qt::MiddleButton ? 2

                                                                    : 0;

        uint16_t nx = 0;

        uint16_t ny = 0;

        const QPoint p = m_label->mapFrom(this, event->position().toPoint());

        if (mapPointerToNormalized(p.x(), p.y(), &nx, &ny)) {

            emit mouseButtonNormalized(btn, true, nx, ny);

        }

    }

    QWidget::mousePressEvent(event);

}



void VideoDisplayWidget::mouseReleaseEvent(QMouseEvent *event)

{

    if (m_controlEnabled) {

        const uint8_t btn = event->button() == Qt::RightButton  ? 1

                              : event->button() == Qt::MiddleButton ? 2

                                                                    : 0;

        uint16_t nx = 0;

        uint16_t ny = 0;

        const QPoint p = m_label->mapFrom(this, event->position().toPoint());

        if (mapPointerToNormalized(p.x(), p.y(), &nx, &ny)) {

            emit mouseButtonNormalized(btn, false, nx, ny);

        }

    }

    QWidget::mouseReleaseEvent(event);

}



void VideoDisplayWidget::wheelEvent(QWheelEvent *event)

{

    if (m_controlEnabled) {

        uint16_t nx = 0;

        uint16_t ny = 0;

        const QPoint p = m_label->mapFrom(this, event->position().toPoint());

        if (mapPointerToNormalized(p.x(), p.y(), &nx, &ny)) {

            emit wheelNormalized(int16_t(event->angleDelta().y() / 8), nx, ny);

        }

    }

    QWidget::wheelEvent(event);

}



void VideoDisplayWidget::keyPressEvent(QKeyEvent *event)

{

    if (event->key() == Qt::Key_Escape && m_fullscreenActive) {

        exitFullscreenIfActive();

        event->accept();

        return;

    }

    if (m_controlEnabled) {

        emit keyEvent(uint16_t(event->nativeVirtualKey()), true, event->nativeScanCode() > 0);

    }

    QWidget::keyPressEvent(event);

}



void VideoDisplayWidget::keyReleaseEvent(QKeyEvent *event)

{

    if (m_controlEnabled) {

        emit keyEvent(uint16_t(event->nativeVirtualKey()), false, event->nativeScanCode() > 0);

    }

    QWidget::keyReleaseEvent(event);

}


