// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <QtGlobal>

#if defined(Q_OS_WIN32)
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <Carbon/Carbon.h>
#elif defined(Q_OS_LINUX)
#include <X11/XKBlib.h>
#endif

namespace keyboard {
bool isCapsLockOn()
{
#if defined(Q_OS_WIN32)
    return (GetKeyState(VK_CAPITAL) & 0x0001) == 1;
#elif defined(Q_OS_MACOS)
    uint modifiers = GetCurrentKeyModifiers();
    return modifiers & alphaLock;
#elif defined(Q_OS_LINUX)
    Display* d = XOpenDisplay(static_cast<char*>(0));
    bool caps_state = false;
    if (d) {
        uint n;
        XkbGetIndicatorState(d, XkbUseCoreKbd, &n);
        caps_state = (n & 0x0001) == 1;
    }
    return caps_state;
#else
    return false;
#endif
}
}
