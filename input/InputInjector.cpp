#include "InputInjector.h"

#include <QDebug>
#include <QMutexLocker>

// ---------------------------------------------------------------------------
// Windows headers – only in the .cpp
// ---------------------------------------------------------------------------
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

// Re-export the typedefs used in the header with real Win32 types so the
// static_casts below are well-formed even though the header uses plain aliases.
static_assert(sizeof(WORD) == 2, "WORD size mismatch");
static_assert(sizeof(DWORD) == 4, "DWORD size mismatch");

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

InputInjector::InputInjector(QObject* parent)
    : QObject(parent)
{
    buildKeyTable();
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

void InputInjector::handleKeyboard(const QString& key,
    const QStringList& modifiers,
    const QString& type)
{
    QMutexLocker lock(&m_mutex);

    const bool isDown = (type == QLatin1String("keydown") ||
        type == QLatin1String("keypress"));
    const bool isUp = (type == QLatin1String("keyup") ||
        type == QLatin1String("keypress"));

    qDebug() << "[InputInjector] keyboard" << type << key << modifiers;

    // Handle modifier keys
    if (isDown) { injectModifiers(modifiers, true); }

    // --- Try VK lookup first ---
    auto it = m_keyTable.find(key.toLower());
    if (it == m_keyTable.end()) {
        it = m_keyTable.find(key); // case-sensitive fallback (e.g. "F1")
    }

    if (it != m_keyTable.end()) {
        const WORD vk = it.value();
        if (isDown) { injectVkEvent(vk, true); }
        if (isUp) { injectVkEvent(vk, false); }
    }
    else {
        // --- Unicode path for any single character not in the VK table ---
        if (!key.isEmpty()) {
            const ushort ucs2 = key.at(0).unicode();
            if (ucs2 > 0x7F || !m_keyTable.contains(key)) {
                // Down + up injected together by injectUnicode
                injectUnicode(ucs2);
            }
        }
    }

    if (isUp) { injectModifiers(modifiers, false); }
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------

void InputInjector::handleMouse(float x, float y,
    const QString& type,
    const QString& button,
    float /*deltaX*/, float deltaY)
{
    QMutexLocker lock(&m_mutex);

    qDebug() << "[InputInjector] mouse" << type << button
        << "pos(" << x << "," << y << ")";

    // Clamp normalised coords
    x = qBound(0.0f, x, 1.0f);
    y = qBound(0.0f, y, 1.0f);

    if (type == QLatin1String("mousemove")) {
        injectMouseMove(x, y);
        return;
    }

    if (type == QLatin1String("wheel")) {
        injectMouseMove(x, y);

        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = MOUSEEVENTF_WHEEL;
        in.mi.mouseData = static_cast<DWORD>(scaledWheelDelta(deltaY));
        SendInput(1, &in, sizeof(INPUT));
        return;
    }

    // Determine button flags
    DWORD downFlag = 0;
    DWORD upFlag = 0;

    if (button == QLatin1String("left")) {
        downFlag = MOUSEEVENTF_LEFTDOWN;
        upFlag = MOUSEEVENTF_LEFTUP;
    }
    else if (button == QLatin1String("right")) {
        downFlag = MOUSEEVENTF_RIGHTDOWN;
        upFlag = MOUSEEVENTF_RIGHTUP;
    }
    else if (button == QLatin1String("middle")) {
        downFlag = MOUSEEVENTF_MIDDLEDOWN;
        upFlag = MOUSEEVENTF_MIDDLEUP;
    }

    injectMouseMove(x, y);

    auto sendBtn = [&](DWORD flags) {
        if (!flags) { return; }
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = flags;
        SendInput(1, &in, sizeof(INPUT));
        };

    if (type == QLatin1String("mousedown")) {
        sendBtn(downFlag);
    }
    else if (type == QLatin1String("mouseup")) {
        sendBtn(upFlag);
    }
    else if (type == QLatin1String("click")) {
        sendBtn(downFlag);
        sendBtn(upFlag);
    }
    else if (type == QLatin1String("dblclick")) {
        sendBtn(downFlag); sendBtn(upFlag);
        sendBtn(downFlag); sendBtn(upFlag);
    }
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void InputInjector::injectModifiers(const QStringList& modifiers, bool down)
{
    for (const QString& mod : modifiers) {
        const QString lmod = mod.toLower();
        WORD vk = 0;
        if (lmod == QLatin1String("ctrl") || lmod == QLatin1String("control"))
            vk = VK_CONTROL;
        else if (lmod == QLatin1String("alt"))
            vk = VK_MENU;
        else if (lmod == QLatin1String("shift"))
            vk = VK_SHIFT;
        else if (lmod == QLatin1String("meta") || lmod == QLatin1String("win"))
            vk = VK_LWIN;

        if (vk) { injectVkEvent(vk, down); }
    }
}

void InputInjector::injectVkEvent(WORD vk, bool down) const
{
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}

void InputInjector::injectUnicode(ushort unicode) const
{
    INPUT events[2]{};

    events[0].type = INPUT_KEYBOARD;
    events[0].ki.wScan = unicode;
    events[0].ki.dwFlags = KEYEVENTF_UNICODE;

    events[1].type = INPUT_KEYBOARD;
    events[1].ki.wScan = unicode;
    events[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

    SendInput(2, events, sizeof(INPUT));
}

void InputInjector::injectMouseMove(float nx, float ny) const
{
    // Win32 absolute coordinates range [0, 65535] across the primary monitor.
    const LONG ax = static_cast<LONG>(nx * 65535.0f);
    const LONG ay = static_cast<LONG>(ny * 65535.0f);

    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = ax;
    in.mi.dy = ay;
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    SendInput(1, &in, sizeof(INPUT));
}

int InputInjector::scaledWheelDelta(float delta)
{
    // WHEEL_DELTA = 120 per notch; positive = scroll up (away from user).
    // Our convention: positive deltaY = scroll down, so negate.
    return static_cast<int>(-delta * WHEEL_DELTA);
}

// ---------------------------------------------------------------------------
// Key lookup table
// ---------------------------------------------------------------------------

void InputInjector::buildKeyTable()
{
    // -----------------------------------------------------------------------
    // Printable ASCII (a-z handled as lower-case key names → VK_A … VK_Z)
    // -----------------------------------------------------------------------
    for (char c = 'a'; c <= 'z'; ++c) {
        m_keyTable.insert(QString(c), static_cast<WORD>(std::toupper(c)));
    }
    // Uppercase variants map to the same VK
    for (char c = 'A'; c <= 'Z'; ++c) {
        m_keyTable.insert(QString(c), static_cast<WORD>(c));
    }
    // Digits
    for (char c = '0'; c <= '9'; ++c) {
        m_keyTable.insert(QString(c), static_cast<WORD>(c));
    }

    // -----------------------------------------------------------------------
    // Named keys – Web KeyboardEvent.key names + common aliases
    // -----------------------------------------------------------------------
    // Navigation
    m_keyTable.insert(QStringLiteral("arrowleft"), VK_LEFT);
    m_keyTable.insert(QStringLiteral("arrowright"), VK_RIGHT);
    m_keyTable.insert(QStringLiteral("arrowup"), VK_UP);
    m_keyTable.insert(QStringLiteral("arrowdown"), VK_DOWN);
    m_keyTable.insert(QStringLiteral("home"), VK_HOME);
    m_keyTable.insert(QStringLiteral("end"), VK_END);
    m_keyTable.insert(QStringLiteral("pageup"), VK_PRIOR);
    m_keyTable.insert(QStringLiteral("pagedown"), VK_NEXT);

    // Editing
    m_keyTable.insert(QStringLiteral("backspace"), VK_BACK);
    m_keyTable.insert(QStringLiteral("delete"), VK_DELETE);
    m_keyTable.insert(QStringLiteral("del"), VK_DELETE);
    m_keyTable.insert(QStringLiteral("insert"), VK_INSERT);
    m_keyTable.insert(QStringLiteral("tab"), VK_TAB);
    m_keyTable.insert(QStringLiteral("enter"), VK_RETURN);
    m_keyTable.insert(QStringLiteral("return"), VK_RETURN);
    m_keyTable.insert(QStringLiteral("escape"), VK_ESCAPE);
    m_keyTable.insert(QStringLiteral("esc"), VK_ESCAPE);
    m_keyTable.insert(QStringLiteral(" "), VK_SPACE);
    m_keyTable.insert(QStringLiteral("space"), VK_SPACE);
    m_keyTable.insert(QStringLiteral("spacebar"), VK_SPACE);

    // Modifier keys (standalone presses)
    m_keyTable.insert(QStringLiteral("shift"), VK_SHIFT);
    m_keyTable.insert(QStringLiteral("control"), VK_CONTROL);
    m_keyTable.insert(QStringLiteral("ctrl"), VK_CONTROL);
    m_keyTable.insert(QStringLiteral("alt"), VK_MENU);
    m_keyTable.insert(QStringLiteral("meta"), VK_LWIN);
    m_keyTable.insert(QStringLiteral("win"), VK_LWIN);
    m_keyTable.insert(QStringLiteral("capslock"), VK_CAPITAL);
    m_keyTable.insert(QStringLiteral("numlock"), VK_NUMLOCK);
    m_keyTable.insert(QStringLiteral("scrolllock"), VK_SCROLL);

    // Function keys
    m_keyTable.insert(QStringLiteral("f1"), VK_F1);
    m_keyTable.insert(QStringLiteral("f2"), VK_F2);
    m_keyTable.insert(QStringLiteral("f3"), VK_F3);
    m_keyTable.insert(QStringLiteral("f4"), VK_F4);
    m_keyTable.insert(QStringLiteral("f5"), VK_F5);
    m_keyTable.insert(QStringLiteral("f6"), VK_F6);
    m_keyTable.insert(QStringLiteral("f7"), VK_F7);
    m_keyTable.insert(QStringLiteral("f8"), VK_F8);
    m_keyTable.insert(QStringLiteral("f9"), VK_F9);
    m_keyTable.insert(QStringLiteral("f10"), VK_F10);
    m_keyTable.insert(QStringLiteral("f11"), VK_F11);
    m_keyTable.insert(QStringLiteral("f12"), VK_F12);
    // Preserve case for "F1"…"F12" variants too
    m_keyTable.insert(QStringLiteral("F1"), VK_F1);
    m_keyTable.insert(QStringLiteral("F2"), VK_F2);
    m_keyTable.insert(QStringLiteral("F3"), VK_F3);
    m_keyTable.insert(QStringLiteral("F4"), VK_F4);
    m_keyTable.insert(QStringLiteral("F5"), VK_F5);
    m_keyTable.insert(QStringLiteral("F6"), VK_F6);
    m_keyTable.insert(QStringLiteral("F7"), VK_F7);
    m_keyTable.insert(QStringLiteral("F8"), VK_F8);
    m_keyTable.insert(QStringLiteral("F9"), VK_F9);
    m_keyTable.insert(QStringLiteral("F10"), VK_F10);
    m_keyTable.insert(QStringLiteral("F11"), VK_F11);
    m_keyTable.insert(QStringLiteral("F12"), VK_F12);

    // Numpad
    m_keyTable.insert(QStringLiteral("numpad0"), VK_NUMPAD0);
    m_keyTable.insert(QStringLiteral("numpad1"), VK_NUMPAD1);
    m_keyTable.insert(QStringLiteral("numpad2"), VK_NUMPAD2);
    m_keyTable.insert(QStringLiteral("numpad3"), VK_NUMPAD3);
    m_keyTable.insert(QStringLiteral("numpad4"), VK_NUMPAD4);
    m_keyTable.insert(QStringLiteral("numpad5"), VK_NUMPAD5);
    m_keyTable.insert(QStringLiteral("numpad6"), VK_NUMPAD6);
    m_keyTable.insert(QStringLiteral("numpad7"), VK_NUMPAD7);
    m_keyTable.insert(QStringLiteral("numpad8"), VK_NUMPAD8);
    m_keyTable.insert(QStringLiteral("numpad9"), VK_NUMPAD9);
    m_keyTable.insert(QStringLiteral("multiply"), VK_MULTIPLY);
    m_keyTable.insert(QStringLiteral("add"), VK_ADD);
    m_keyTable.insert(QStringLiteral("subtract"), VK_SUBTRACT);
    m_keyTable.insert(QStringLiteral("decimal"), VK_DECIMAL);
    m_keyTable.insert(QStringLiteral("divide"), VK_DIVIDE);

    // Punctuation / symbols (OEM keys)
    m_keyTable.insert(QStringLiteral(";"), VK_OEM_1);
    m_keyTable.insert(QStringLiteral("="), VK_OEM_PLUS);
    m_keyTable.insert(QStringLiteral(","), VK_OEM_COMMA);
    m_keyTable.insert(QStringLiteral("-"), VK_OEM_MINUS);
    m_keyTable.insert(QStringLiteral("."), VK_OEM_PERIOD);
    m_keyTable.insert(QStringLiteral("/"), VK_OEM_2);
    m_keyTable.insert(QStringLiteral("`"), VK_OEM_3);
    m_keyTable.insert(QStringLiteral("["), VK_OEM_4);
    m_keyTable.insert(QStringLiteral("\\"), VK_OEM_5);
    m_keyTable.insert(QStringLiteral("]"), VK_OEM_6);
    m_keyTable.insert(QStringLiteral("'"), VK_OEM_7);

    // Misc
    m_keyTable.insert(QStringLiteral("printscreen"), VK_SNAPSHOT);
    m_keyTable.insert(QStringLiteral("pause"), VK_PAUSE);
    m_keyTable.insert(QStringLiteral("contextmenu"), VK_APPS);
    m_keyTable.insert(QStringLiteral("audiovolumeup"), VK_VOLUME_UP);
    m_keyTable.insert(QStringLiteral("audiovolumedown"), VK_VOLUME_DOWN);
    m_keyTable.insert(QStringLiteral("audiovolumemute"), VK_VOLUME_MUTE);
    m_keyTable.insert(QStringLiteral("mediaplaypause"), VK_MEDIA_PLAY_PAUSE);
    m_keyTable.insert(QStringLiteral("mediatracknext"), VK_MEDIA_NEXT_TRACK);
    m_keyTable.insert(QStringLiteral("mediatrackprevious"), VK_MEDIA_PREV_TRACK);
}