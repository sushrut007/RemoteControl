#pragma once

#include <QObject>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QMutex>

// Keep Windows headers out of the public interface.
using WORD = unsigned short;
using DWORD = unsigned long;

// ---------------------------------------------------------------------------
// InputInjector
//
// Thread-safe Win32 SendInput wrapper.  All public methods may be called from
// any thread (e.g. the socket event thread that receives remote input events).
//
// Coordinate convention
// ---------------------
//  Mouse x/y are normalised [0.0, 1.0] relative to the primary monitor's
//  logical size and are converted to the absolute MOUSEINPUT range
//  [0, 65535] that Win32 SendInput expects with MOUSEEVENTF_ABSOLUTE.
//
// Key name convention
// -------------------
//  Key names follow the Web / JavaScript KeyboardEvent.key convention
//  (e.g. "Enter", "ArrowLeft", "F1", "a", "A", " ") plus some Qt aliases.
//  Non-ASCII characters are injected via KEYEVENTF_UNICODE so they work
//  regardless of the current keyboard layout.
// ---------------------------------------------------------------------------
class InputInjector : public QObject
{
    Q_OBJECT

public:
    explicit InputInjector(QObject* parent = nullptr);
    ~InputInjector() override = default;

    InputInjector(const InputInjector&) = delete;
    InputInjector& operator=(const InputInjector&) = delete;

    // -----------------------------------------------------------------------
    // Keyboard
    // -----------------------------------------------------------------------

    /// Inject a keyboard event.
    /// @param key        JS/Qt key name, e.g. "Enter", "a", "F5", "ArrowUp".
    /// @param modifiers  List of active modifiers: "ctrl","alt","shift","meta".
    /// @param type       "keydown", "keyup", or "keypress".
    void handleKeyboard(const QString& key,
        const QStringList& modifiers,
        const QString& type);

    // -----------------------------------------------------------------------
    // Mouse
    // -----------------------------------------------------------------------

    /// Inject a mouse event.
    /// @param x       Normalised horizontal position [0.0, 1.0].
    /// @param y       Normalised vertical   position [0.0, 1.0].
    /// @param type    "mousemove","mousedown","mouseup","click","dblclick","wheel".
    /// @param button  "left", "right", or "middle" (ignored for move/wheel).
    /// @param deltaX  Horizontal scroll delta (wheel events, positive = right).
    /// @param deltaY  Vertical   scroll delta (wheel events, positive = down).
    void handleMouse(float x, float y,
        const QString& type,
        const QString& button,
        float deltaX, float deltaY);

public slots:
    /// Set the captured monitor's region in virtual-desktop pixel coordinates.
    /// Call this once after ScreenCapturer emits captureRegionReady().
    /// Defaults to the primary monitor if never called.
    void setCaptureRegion(int originX, int originY, int width, int height);

private:
    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    /// Press or release a set of modifier keys before/after the main key.
    void injectModifiers(const QStringList& modifiers, bool down);

    /// Inject a single virtual-key event (down or up).
    void injectVkEvent(WORD vk, bool down) const;

    /// Inject a Unicode character key-down event.
    void injectUnicodeDown(ushort unicode) const;

    /// Inject a Unicode character key-up event.
    void injectUnicodeUp(ushort unicode) const;

    /// Move the mouse to the absolute position corresponding to (nx, ny).
    void injectMouseMove(float nx, float ny) const;

    // -----------------------------------------------------------------------
    // Capture region (virtual-desktop coordinates)
    // -----------------------------------------------------------------------

    int m_captureOriginX{ 0 };
    int m_captureOriginY{ 0 };
    int m_captureWidth{ 0 };   ///< 0 = unknown; fall back to primary monitor
    int m_captureHeight{ 0 };

    /// Return the MOUSEINPUT dwData wheel delta for a normalised scroll value.
    static int scaledWheelDelta(float delta);

    // -----------------------------------------------------------------------
    // Key-name → VK lookup table (built once in the constructor)
    // -----------------------------------------------------------------------

    void buildKeyTable();
    QHash<QString, WORD> m_keyTable;

    // -----------------------------------------------------------------------
    // Thread safety
    // -----------------------------------------------------------------------

    mutable QMutex m_mutex; ///< Serialises all SendInput calls
};