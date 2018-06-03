/*
 * Copyright (C) 2018 Apple Inc. All rights reserved.
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

#pragma once

#if ENABLE(LAYOUT_FORMATTING_CONTEXT)

#include "FormattingContext.h"
#include <wtf/IsoMalloc.h>

namespace WebCore {

class LayoutUnit;

namespace Layout {

class BlockFormattingState;
class Box;

// This class implements the layout logic for block formatting contexts.
// https://www.w3.org/TR/CSS22/visuren.html#block-formatting
class BlockFormattingContext : public FormattingContext {
    WTF_MAKE_ISO_ALLOCATED(BlockFormattingContext);
public:
    BlockFormattingContext(const Box& formattingContextRoot);

    void layout(LayoutContext&, FormattingState&) const override;
    std::unique_ptr<FormattingState> createFormattingState(Ref<FloatingState>&&) const override;
    Ref<FloatingState> createOrFindFloatingState(LayoutContext&) const override;

private:
    void computeWidth(LayoutContext&, const Box&, Display::Box&) const;
    void computeHeight(LayoutContext&, const Box&, Display::Box&) const;

    void computeStaticPosition(LayoutContext&, const Box&, Display::Box&) const override;
    void computeInFlowPositionedPosition(LayoutContext&, const Box&, Display::Box&) const override;
    void computeInFlowWidth(LayoutContext&, const Box&, Display::Box&) const;
    void computeInFlowHeight(LayoutContext&, const Box&, Display::Box&) const;
    void computeMargin(LayoutContext&, const Box&, Display::Box&) const;

    // This class implements positioning and sizing for boxes participating in a block formatting context.
    class Geometry {
    public:
        static LayoutUnit inFlowHeight(LayoutContext&, const Box&);
        static LayoutUnit inFlowWidth(LayoutContext&, const Box&);

        static LayoutPoint staticPosition(LayoutContext&, const Box&);
        static LayoutPoint inFlowPositionedPosition(LayoutContext&, const Box&);

        static Display::Box::Edges computedMargin(LayoutContext&, const Box&);

    private:
        static LayoutUnit inFlowNonReplacedHeight(LayoutContext&, const Box&);
        static LayoutUnit inFlowNonReplacedWidth(LayoutContext&, const Box&);
    };
    
    // This class implements margin collapsing for block formatting context.
    class MarginCollapse {
    public:
        static LayoutUnit marginTop(const Box&);
        static LayoutUnit marginBottom(const Box&);

        static bool isMarginBottomCollapsedWithParent(const Box&);
        static bool isMarginTopCollapsedWithParentMarginBottom(const Box&);
    
    private:
        static LayoutUnit collapsedMarginBottomFromLastChild(const Box&);
        static LayoutUnit nonCollapsedMarginBottom(const Box&);
    };
};

}
}
#endif
