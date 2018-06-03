/*
 * Copyright (C) 2018 Sony Interactive Entertainment Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "PlatformUtilities.h"

#include <Shlwapi.h>
#include <WebCore/FileSystem.h>
#include <WebCore/URL.h>
#include <wtf/text/win/WCharStringExtras.h>

namespace TestWebKitAPI {
namespace Util {

static String moduleDirectory()
{
    constexpr size_t bufferLength = _MAX_PATH + 1;
    wchar_t filename[bufferLength];
    auto len = GetModuleFileName(nullptr, filename, bufferLength);
    ASSERT(len > 0);
    return WebCore::FileSystem::directoryName(wcharToString(filename, len));
}

WKStringRef createInjectedBundlePath()
{
    auto path = WebCore::FileSystem::pathByAppendingComponent(moduleDirectory(), String(L"TestWebKitAPIInjectedBundle.dll"));
    return WKStringCreateWithUTF8CString(path.utf8().data());
}

WKURLRef createURLForResource(const char* resource, const char* extension)
{
    String filename = String::format("..\\..\\..\\Tools\\TestWebKitAPI\\Tests\\WebKit\\%s.%s", resource, extension);
    auto url = WebCore::URL::fileURLWithFileSystemPath(WebCore::FileSystem::pathByAppendingComponent(moduleDirectory(), filename));
    return WKURLCreateWithUTF8CString(url.string().utf8().data());
}

WKURLRef URLForNonExistentResource()
{
    return WKURLCreateWithUTF8CString("file:///does-not-exist.html");
}

bool isKeyDown(WKNativeEventPtr event)
{
    // FIXME
    return false;
}

} // namespace Util
} // namespace TestWebKitAPI