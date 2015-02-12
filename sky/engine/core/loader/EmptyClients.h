/*
 * Copyright (C) 2006 Eric Seidel (eric@webkit.org)
 * Copyright (C) 2008, 2009, 2010, 2011, 2012 Apple Inc. All rights reserved.
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 * Copyright (C) 2012 Samsung Electronics. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SKY_ENGINE_CORE_LOADER_EMPTYCLIENTS_H_
#define SKY_ENGINE_CORE_LOADER_EMPTYCLIENTS_H_

#include "sky/engine/core/editing/UndoStep.h"
#include "sky/engine/core/loader/FrameLoaderClient.h"
#include "sky/engine/core/page/ChromeClient.h"
#include "sky/engine/core/page/EditorClient.h"
#include "sky/engine/core/page/FocusType.h"
#include "sky/engine/core/page/Page.h"
#include "sky/engine/core/page/SpellCheckerClient.h"
#include "sky/engine/platform/geometry/FloatRect.h"
#include "sky/engine/platform/network/ResourceError.h"
#include "sky/engine/platform/text/TextCheckerClient.h"
#include "sky/engine/public/platform/WebScreenInfo.h"
#include "sky/engine/wtf/Forward.h"

/*
 This file holds empty Client stubs for use by WebCore.
 Viewless element needs to create a dummy Page->LocalFrame->FrameView tree for use in parsing or executing JavaScript.
 This tree depends heavily on Clients (usually provided by WebKit classes).

 This file was first created for SVGImage as it had no way to access the current Page (nor should it,
 since Images are not tied to a page).
 See http://bugs.webkit.org/show_bug.cgi?id=5971 for the original discussion about this file.

 Ideally, whenever you change a Client class, you should add a stub here.
 Brittle, yes.  Unfortunate, yes.  Hopefully temporary.
*/

namespace blink {

class EmptyFrameLoaderClient : public FrameLoaderClient {
    WTF_MAKE_NONCOPYABLE(EmptyFrameLoaderClient); WTF_MAKE_FAST_ALLOCATED;
public:
    EmptyFrameLoaderClient() { }
    virtual ~EmptyFrameLoaderClient() {  }

    virtual void detachedFromParent() override { }

    virtual void dispatchWillSendRequest(Document*, unsigned long, ResourceRequest&, const ResourceResponse&) override { }
    virtual void dispatchDidReceiveResponse(Document*, unsigned long, const ResourceResponse&) override { }
    virtual void dispatchDidFinishLoading(Document*, unsigned long) override { }
    virtual void dispatchDidLoadResourceFromMemoryCache(const ResourceRequest&, const ResourceResponse&) override { }

    virtual void dispatchDidHandleOnloadEvents() override { }
    virtual void dispatchWillClose() override { }
    virtual void dispatchDidReceiveTitle(const String&) override { }
    virtual void dispatchDidFailLoad(const ResourceError&) override { }

    virtual NavigationPolicy decidePolicyForNavigation(const ResourceRequest&, Document*, NavigationPolicy, bool isTransitionNavigation) override;

    virtual void didStartLoading(LoadStartType) override { }
    virtual void progressEstimateChanged(double) override { }
    virtual void didStopLoading() override { }

    virtual void loadURLExternally(const ResourceRequest&, NavigationPolicy, const String& = String()) override { }
    virtual mojo::View* createChildFrame() override { return nullptr; }

    virtual void transitionToCommittedForNewPage() override { }

    virtual void selectorMatchChanged(const Vector<String>&, const Vector<String>&) override { }

    virtual void documentElementAvailable() override { }
};

class EmptyTextCheckerClient : public TextCheckerClient {
public:
    ~EmptyTextCheckerClient() { }

    virtual bool shouldEraseMarkersAfterChangeSelection(TextCheckingType) const override { return true; }
    virtual void checkSpellingOfString(const String&, int*, int*) override { }
    virtual String getAutoCorrectSuggestionForMisspelledWord(const String&) override { return String(); }
    virtual void checkGrammarOfString(const String&, Vector<GrammarDetail>&, int*, int*) override { }
    virtual void requestCheckingOfString(PassRefPtr<TextCheckingRequest>) override;
};

class EmptySpellCheckerClient : public SpellCheckerClient {
    WTF_MAKE_NONCOPYABLE(EmptySpellCheckerClient); WTF_MAKE_FAST_ALLOCATED;
public:
    EmptySpellCheckerClient() { }
    virtual ~EmptySpellCheckerClient() { }

    virtual bool isContinuousSpellCheckingEnabled() override { return false; }
    virtual void toggleContinuousSpellChecking() override { }
    virtual bool isGrammarCheckingEnabled() override { return false; }

    virtual TextCheckerClient& textChecker() override { return m_textCheckerClient; }

    virtual void updateSpellingUIWithMisspelledWord(const String&) override { }
    virtual void showSpellingUI(bool) override { }
    virtual bool spellingUIIsShowing() override { return false; }

private:
    EmptyTextCheckerClient m_textCheckerClient;
};

class EmptyEditorClient final : public EditorClient {
    WTF_MAKE_NONCOPYABLE(EmptyEditorClient); WTF_MAKE_FAST_ALLOCATED;
public:
    EmptyEditorClient() { }
    virtual ~EmptyEditorClient() { }

    virtual void respondToChangedContents() override { }
    virtual void respondToChangedSelection(LocalFrame*, SelectionType) override { }

    virtual bool canCopyCut(LocalFrame*, bool defaultValue) const override { return defaultValue; }
    virtual bool canPaste(LocalFrame*, bool defaultValue) const override { return defaultValue; }

    virtual bool handleKeyboardEvent() override { return false; }
};

void fillWithEmptyClients(Page::PageClients&);

}

#endif  // SKY_ENGINE_CORE_LOADER_EMPTYCLIENTS_H_
