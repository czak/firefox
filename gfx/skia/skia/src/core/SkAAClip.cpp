/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkAAClip.h"

#include "include/core/SkClipOp.h"
#include "include/core/SkPath.h"
#include "include/core/SkRegion.h"
#include "include/core/SkTypes.h"
#include "include/private/base/SkCPUTypes.h"
#include "include/private/base/SkDebug.h"
#include "include/private/base/SkMacros.h"
#include "include/private/base/SkMalloc.h"
#include "include/private/base/SkMath.h"
#include "include/private/base/SkTDArray.h"
#include "include/private/base/SkTo.h"
#include "src/core/SkBlitter.h"
#include "src/core/SkColorData.h"
#include "src/core/SkMask.h"
#include "src/core/SkScan.h"

#include <algorithm>
#include <atomic>
#include <cstring>

namespace {

class AutoAAClipValidate {
public:
    AutoAAClipValidate(const SkAAClip& clip) : fClip(clip) {
        fClip.validate();
    }
    ~AutoAAClipValidate() {
        fClip.validate();
    }
private:
    const SkAAClip& fClip;
};

#ifdef SK_DEBUG
    #define AUTO_AACLIP_VALIDATE(clip)  AutoAAClipValidate acv(clip)
#else
    #define AUTO_AACLIP_VALIDATE(clip)
#endif

///////////////////////////////////////////////////////////////////////////////

static constexpr int32_t kMaxInt32 = 0x7FFFFFFF;

#ifdef SK_DEBUG
// assert we're exactly width-wide, and then return the number of bytes used
static size_t compute_row_length(const uint8_t row[], int width) {
    const uint8_t* origRow = row;
    while (width > 0) {
        int n = row[0];
        SkASSERT(n > 0);
        SkASSERT(n <= width);
        row += 2;
        width -= n;
    }
    SkASSERT(0 == width);
    return row - origRow;
}
#endif

/*
 *  Data runs are packed [count, alpha]
 */
struct YOffset {
    int32_t  fY;
    uint32_t fOffset;
};

class RowIter {
public:
    RowIter(const uint8_t* row, const SkIRect& bounds) {
        fRow = row;
        fLeft = bounds.fLeft;
        fBoundsRight = bounds.fRight;
        if (row) {
            fRight = bounds.fLeft + row[0];
            SkASSERT(fRight <= fBoundsRight);
            fAlpha = row[1];
            fDone = false;
        } else {
            fDone = true;
            fRight = kMaxInt32;
            fAlpha = 0;
        }
    }

    bool done() const { return fDone; }
    int left() const { return fLeft; }
    int right() const { return fRight; }
    U8CPU alpha() const { return fAlpha; }
    void next() {
        if (!fDone) {
            fLeft = fRight;
            if (fRight == fBoundsRight) {
                fDone = true;
                fRight = kMaxInt32;
                fAlpha = 0;
            } else {
                fRow += 2;
                fRight += fRow[0];
                fAlpha = fRow[1];
                SkASSERT(fRight <= fBoundsRight);
            }
        }
    }

private:
    const uint8_t*  fRow;
    int             fLeft;
    int             fRight;
    int             fBoundsRight;
    bool            fDone;
    uint8_t         fAlpha;
};

class Iter {
public:
    Iter() = default;

    Iter(int y, const uint8_t* data, const YOffset* start, const YOffset* end)
            : fCurrYOff(start)
            , fStopYOff(end)
            , fData(data + start->fOffset)
            , fTop(y)
            , fBottom(y + start->fY + 1)
            , fDone(false) {}

    bool done() const { return fDone; }
    int top() const { return fTop; }
    int bottom() const { return fBottom; }
    const uint8_t* data() const { return fData; }

    void next() {
        if (!fDone) {
            const YOffset* prev = fCurrYOff;
            const YOffset* curr = prev + 1;
            SkASSERT(curr <= fStopYOff);

            fTop = fBottom;
            if (curr >= fStopYOff) {
                fDone = true;
                fBottom = kMaxInt32;
                fData = nullptr;
            } else {
                fBottom += curr->fY - prev->fY;
                fData += curr->fOffset - prev->fOffset;
                fCurrYOff = curr;
            }
        }
    }

private:
    const YOffset* fCurrYOff = nullptr;
    const YOffset* fStopYOff = nullptr;
    const uint8_t* fData = nullptr;

    int fTop = kMaxInt32;
    int fBottom = kMaxInt32;
    bool fDone = true;
};

}  // namespace

///////////////////////////////////////////////////////////////////////////////

struct SkAAClip::RunHead {
    std::atomic<int32_t> fRefCnt;
    int32_t fRowCount;
    size_t  fDataSize;

    YOffset* yoffsets() {
        return (YOffset*)((char*)this + sizeof(RunHead));
    }
    const YOffset* yoffsets() const {
        return (const YOffset*)((const char*)this + sizeof(RunHead));
    }
    uint8_t* data() {
        return (uint8_t*)(this->yoffsets() + fRowCount);
    }
    const uint8_t* data() const {
        return (const uint8_t*)(this->yoffsets() + fRowCount);
    }

    static RunHead* Alloc(int rowCount, size_t dataSize) {
        size_t size = sizeof(RunHead) + rowCount * sizeof(YOffset) + dataSize;
        RunHead* head = (RunHead*)sk_malloc_throw(size);
        head->fRefCnt.store(1);
        head->fRowCount = rowCount;
        head->fDataSize = dataSize;
        return head;
    }

    static int ComputeRowSizeForWidth(int width) {
        // 2 bytes per segment, where each segment can store up to 255 for count
        int segments = 0;
        while (width > 0) {
            segments += 1;
            int n = std::min(width, 255);
            width -= n;
        }
        return segments * 2;    // each segment is row[0] + row[1] (n + alpha)
    }

    static RunHead* AllocRect(const SkIRect& bounds) {
        SkASSERT(!bounds.isEmpty());
        int width = bounds.width();
        size_t rowSize = ComputeRowSizeForWidth(width);
        RunHead* head = RunHead::Alloc(1, rowSize);
        YOffset* yoff = head->yoffsets();
        yoff->fY = bounds.height() - 1;
        yoff->fOffset = 0;
        uint8_t* row = head->data();
        while (width > 0) {
            int n = std::min(width, 255);
            row[0] = n;
            row[1] = 0xFF;
            width -= n;
            row += 2;
        }
        return head;
    }

    static Iter Iterate(const SkAAClip& clip) {
        const RunHead* head = clip.fRunHead;
        if (!clip.fRunHead) {
            // A null run head is an empty clip, so return aan already finished iterator.
            return Iter();
        }

        return Iter(clip.getBounds().fTop, head->data(), head->yoffsets(),
                    head->yoffsets() + head->fRowCount);
    }
};

///////////////////////////////////////////////////////////////////////////////

class SkAAClip::Builder {
    class Blitter;

    SkIRect fBounds;
    struct Row {
        int fY;
        int fWidth;
        SkTDArray<uint8_t>* fData;
    };
    SkTDArray<Row>  fRows;
    Row* fCurrRow;
    int fPrevY;
    int fWidth;
    int fMinY;

public:
    Builder(const SkIRect& bounds) : fBounds(bounds) {
        fPrevY = -1;
        fWidth = bounds.width();
        fCurrRow = nullptr;
        fMinY = bounds.fTop;
    }

    ~Builder() {
        Row* row = fRows.begin();
        Row* stop = fRows.end();
        while (row < stop) {
            delete row->fData;
            row += 1;
        }
    }

    bool applyClipOp(SkAAClip* target, const SkAAClip& other, SkClipOp op);
    bool blitPath(SkAAClip* target, const SkPath& path, bool doAA);

private:
    using AlphaProc = U8CPU (*)(U8CPU alphaA, U8CPU alphaB);
    void operateX(int lastY, RowIter& iterA, RowIter& iterB, AlphaProc proc);
    void operateY(const SkAAClip& A, const SkAAClip& B, SkClipOp op);

    void addRun(int x, int y, U8CPU alpha, int count) {
        SkASSERT(count > 0);
        SkASSERT(fBounds.contains(x, y));
        SkASSERT(fBounds.contains(x + count - 1, y));

        x -= fBounds.left();
        y -= fBounds.top();

        Row* row = fCurrRow;
        if (y != fPrevY) {
            SkASSERT(y > fPrevY);
            fPrevY = y;
            row = this->flushRow(true);
            row->fY = y;
            row->fWidth = 0;
            SkASSERT(row->fData);
            SkASSERT(row->fData->empty());
            fCurrRow = row;
        }

        SkASSERT(row->fWidth <= x);
        SkASSERT(row->fWidth < fBounds.width());

        SkTDArray<uint8_t>& data = *row->fData;

        int gap = x - row->fWidth;
        if (gap) {
            AppendRun(data, 0, gap);
            row->fWidth += gap;
            SkASSERT(row->fWidth < fBounds.width());
        }

        AppendRun(data, alpha, count);
        row->fWidth += count;
        SkASSERT(row->fWidth <= fBounds.width());
    }

    void addColumn(int x, int y, U8CPU alpha, int height) {
        SkASSERT(fBounds.contains(x, y + height - 1));

        this->addRun(x, y, alpha, 1);
        this->flushRowH(fCurrRow);
        y -= fBounds.fTop;
        SkASSERT(y == fCurrRow->fY);
        fCurrRow->fY = y + height - 1;
    }

    void addRectRun(int x, int y, int width, int height) {
        SkASSERT(fBounds.contains(x + width - 1, y + height - 1));
        this->addRun(x, y, 0xFF, width);

        // we assum the rect must be all we'll see for these scanlines
        // so we ensure our row goes all the way to our right
        this->flushRowH(fCurrRow);

        y -= fBounds.fTop;
        SkASSERT(y == fCurrRow->fY);
        fCurrRow->fY = y + height - 1;
    }

    void addAntiRectRun(int x, int y, int width, int height,
                        SkAlpha leftAlpha, SkAlpha rightAlpha) {
        // According to SkBlitter.cpp, no matter whether leftAlpha is 0 or positive,
        // we should always consider [x, x+1] as the left-most column and [x+1, x+1+width]
        // as the rect with full alpha.
        SkASSERT(fBounds.contains(x + width + (rightAlpha > 0 ? 1 : 0),
                 y + height - 1));
        SkASSERT(width >= 0);

        // Conceptually we're always adding 3 runs, but we should
        // merge or omit them if possible.
        if (leftAlpha == 0xFF) {
            width++;
        } else if (leftAlpha > 0) {
          this->addRun(x++, y, leftAlpha, 1);
        } else {
          // leftAlpha is 0, ignore the left column
          x++;
        }
        if (rightAlpha == 0xFF) {
            width++;
        }
        if (width > 0) {
            this->addRun(x, y, 0xFF, width);
        }
        if (rightAlpha > 0 && rightAlpha < 255) {
            this->addRun(x + width, y, rightAlpha, 1);
        }

        // if we never called addRun, we might not have a fCurrRow yet
        if (fCurrRow) {
            // we assume the rect must be all we'll see for these scanlines
            // so we ensure our row goes all the way to our right
            this->flushRowH(fCurrRow);

            y -= fBounds.fTop;
            SkASSERT(y == fCurrRow->fY);
            fCurrRow->fY = y + height - 1;
        }
    }

    bool finish(SkAAClip* target) {
        this->flushRow(false);

        const Row* row = fRows.begin();
        const Row* stop = fRows.end();

        size_t dataSize = 0;
        while (row < stop) {
            dataSize += row->fData->size();
            row += 1;
        }

        if (0 == dataSize) {
            return target->setEmpty();
        }

        SkASSERT(fMinY >= fBounds.fTop);
        SkASSERT(fMinY < fBounds.fBottom);
        int adjustY = fMinY - fBounds.fTop;
        fBounds.fTop = fMinY;

        RunHead* head = RunHead::Alloc(fRows.size(), dataSize);
        YOffset* yoffset = head->yoffsets();
        uint8_t* data = head->data();
        uint8_t* baseData = data;

        row = fRows.begin();
        SkDEBUGCODE(int prevY = row->fY - 1;)
        while (row < stop) {
            SkASSERT(prevY < row->fY);  // must be monotonic
            SkDEBUGCODE(prevY = row->fY);

            yoffset->fY = row->fY - adjustY;
            yoffset->fOffset = SkToU32(data - baseData);
            yoffset += 1;

            size_t n = row->fData->size();
            memcpy(data, row->fData->begin(), n);
            SkASSERT(compute_row_length(data, fBounds.width()) == n);
            data += n;

            row += 1;
        }

        target->freeRuns();
        target->fBounds = fBounds;
        target->fRunHead = head;
        return target->trimBounds();
    }

    void dump() {
        this->validate();
        int y;
        for (y = 0; y < fRows.size(); ++y) {
            const Row& row = fRows[y];
            SkDebugf("Y:%3d W:%3d", row.fY, row.fWidth);
            const SkTDArray<uint8_t>& data = *row.fData;
            int count = data.size();
            SkASSERT(!(count & 1));
            const uint8_t* ptr = data.begin();
            for (int x = 0; x < count; x += 2) {
                SkDebugf(" [%3d:%02X]", ptr[0], ptr[1]);
                ptr += 2;
            }
            SkDebugf("\n");
        }
    }

    void validate() {
#ifdef SK_DEBUG
        int prevY = -1;
        for (int i = 0; i < fRows.size(); ++i) {
            const Row& row = fRows[i];
            SkASSERT(prevY < row.fY);
            SkASSERT(fWidth == row.fWidth);
            int count = row.fData->size();
            const uint8_t* ptr = row.fData->begin();
            SkASSERT(!(count & 1));
            int w = 0;
            for (int x = 0; x < count; x += 2) {
                int n = ptr[0];
                SkASSERT(n > 0);
                w += n;
                SkASSERT(w <= fWidth);
                ptr += 2;
            }
            SkASSERT(w == fWidth);
            prevY = row.fY;
        }
#endif
    }

    void flushRowH(Row* row) {
        // flush current row if needed
        if (row->fWidth < fWidth) {
            AppendRun(*row->fData, 0, fWidth - row->fWidth);
            row->fWidth = fWidth;
        }
    }

    Row* flushRow(bool readyForAnother) {
        Row* next = nullptr;
        int count = fRows.size();
        if (count > 0) {
            this->flushRowH(&fRows[count - 1]);
        }
        if (count > 1) {
            // are our last two runs the same?
            Row* prev = &fRows[count - 2];
            Row* curr = &fRows[count - 1];
            SkASSERT(prev->fWidth == fWidth);
            SkASSERT(curr->fWidth == fWidth);
            if (*prev->fData == *curr->fData) {
                prev->fY = curr->fY;
                if (readyForAnother) {
                    curr->fData->clear();
                    next = curr;
                } else {
                    delete curr->fData;
                    fRows.removeShuffle(count - 1);
                }
            } else {
                if (readyForAnother) {
                    next = fRows.append();
                    next->fData = new SkTDArray<uint8_t>;
                }
            }
        } else {
            if (readyForAnother) {
                next = fRows.append();
                next->fData = new SkTDArray<uint8_t>;
            }
        }
        return next;
    }

    static void AppendRun(SkTDArray<uint8_t>& data, U8CPU alpha, int count) {
        do {
            int n = count;
            if (n > 255) {
                n = 255;
            }
            uint8_t* ptr = data.append(2);
            ptr[0] = n;
            ptr[1] = alpha;
            count -= n;
        } while (count > 0);
    }
};

void SkAAClip::Builder::operateX(int lastY, RowIter& iterA, RowIter& iterB, AlphaProc proc) {
    auto advanceRowIter = [](RowIter& iter, int& iterLeft, int& iterRite, int rite) {
        if (rite == iterRite) {
            iter.next();
            iterLeft = iter.left();
            iterRite = iter.right();
        }
    };

    int leftA = iterA.left();
    int riteA = iterA.right();
    int leftB = iterB.left();
    int riteB = iterB.right();

    int prevRite = fBounds.fLeft;

    do {
        U8CPU alphaA = 0;
        U8CPU alphaB = 0;
        int left, rite;

        if (leftA < leftB) {
            left = leftA;
            alphaA = iterA.alpha();
            if (riteA <= leftB) {
                rite = riteA;
            } else {
                rite = leftA = leftB;
            }
        } else if (leftB < leftA) {
            left = leftB;
            alphaB = iterB.alpha();
            if (riteB <= leftA) {
                rite = riteB;
            } else {
                rite = leftB = leftA;
            }
        } else {
            left = leftA;   // or leftB, since leftA == leftB
            rite = leftA = leftB = std::min(riteA, riteB);
            alphaA = iterA.alpha();
            alphaB = iterB.alpha();
        }

        if (left >= fBounds.fRight) {
            break;
        }
        if (rite > fBounds.fRight) {
            rite = fBounds.fRight;
        }

        if (left >= fBounds.fLeft) {
            SkASSERT(rite > left);
            this->addRun(left, lastY, proc(alphaA, alphaB), rite - left);
            prevRite = rite;
        }

        advanceRowIter(iterA, leftA, riteA, rite);
        advanceRowIter(iterB, leftB, riteB, rite);
    } while (!iterA.done() || !iterB.done());

    if (prevRite < fBounds.fRight) {
        this->addRun(prevRite, lastY, 0, fBounds.fRight - prevRite);
    }
}

void SkAAClip::Builder::operateY(const SkAAClip& A, const SkAAClip& B, SkClipOp op) {
    static const AlphaProc kDiff = [](U8CPU a, U8CPU b) { return SkMulDiv255Round(a, 0xFF - b); };
    static const AlphaProc kIntersect = [](U8CPU a, U8CPU b) { return SkMulDiv255Round(a, b); };
    AlphaProc proc = (op == SkClipOp::kDifference) ? kDiff : kIntersect;

    Iter iterA = RunHead::Iterate(A);
    Iter iterB = RunHead::Iterate(B);

    SkASSERT(!iterA.done());
    int topA = iterA.top();
    int botA = iterA.bottom();
    SkASSERT(!iterB.done());
    int topB = iterB.top();
    int botB = iterB.bottom();

    auto advanceIter = [](Iter& iter, int& iterTop, int& iterBot, int bot) {
        if (bot == iterBot) {
            iter.next();
            iterTop = iterBot;
            SkASSERT(iterBot == iter.top());
            iterBot = iter.bottom();
        }
    };

#if defined(SK_BUILD_FOR_FUZZER)
    if ((botA - topA) > 100000 || (botB - topB) > 100000) {
        return;
    }
#endif

    do {
        const uint8_t* rowA = nullptr;
        const uint8_t* rowB = nullptr;
        int top, bot;

        if (topA < topB) {
            top = topA;
            rowA = iterA.data();
            if (botA <= topB) {
                bot = botA;
            } else {
                bot = topA = topB;
            }

        } else if (topB < topA) {
            top = topB;
            rowB = iterB.data();
            if (botB <= topA) {
                bot = botB;
            } else {
                bot = topB = topA;
            }
        } else {
            top = topA;   // or topB, since topA == topB
            bot = topA = topB = std::min(botA, botB);
            rowA = iterA.data();
            rowB = iterB.data();
        }

        if (top >= fBounds.fBottom) {
            break;
        }

        if (bot > fBounds.fBottom) {
            bot = fBounds.fBottom;
        }
        SkASSERT(top < bot);

        if (!rowA && !rowB) {
            this->addRun(fBounds.fLeft, bot - 1, 0, fBounds.width());
        } else if (top >= fBounds.fTop) {
            SkASSERT(bot <= fBounds.fBottom);
            RowIter rowIterA(rowA, rowA ? A.getBounds() : fBounds);
            RowIter rowIterB(rowB, rowB ? B.getBounds() : fBounds);
            this->operateX(bot - 1, rowIterA, rowIterB, proc);
        }

        advanceIter(iterA, topA, botA, bot);
        advanceIter(iterB, topB, botB, bot);
    } while (!iterA.done() || !iterB.done());
}

class SkAAClip::Builder::Blitter final : public SkBlitter {
    int fLastY;

    /*
        If we see a gap of 1 or more empty scanlines while building in Y-order,
        we inject an explicit empty scanline (alpha==0)

        See AAClipTest.cpp : test_path_with_hole()
     */
    void checkForYGap(int y) {
        SkASSERT(y >= fLastY);
        if (fLastY > -SK_MaxS32) {
            int gap = y - fLastY;
            if (gap > 1) {
                fBuilder->addRun(fLeft, y - 1, 0, fRight - fLeft);
            }
        }
        fLastY = y;
    }

public:
    Blitter(Builder* builder) {
        fBuilder = builder;
        fLeft = builder->fBounds.fLeft;
        fRight = builder->fBounds.fRight;
        fMinY = SK_MaxS32;
        fLastY = -SK_MaxS32;    // sentinel
    }

    void finish() {
        if (fMinY < SK_MaxS32) {
            fBuilder->fMinY = fMinY;
        }
    }

    /**
       Must evaluate clips in scan-line order, so don't want to allow blitV(),
       but an AAClip can be clipped down to a single pixel wide, so we
       must support it (given AntiRect semantics: minimum width is 2).
       Instead we'll rely on the runtime asserts to guarantee Y monotonicity;
       any failure cases that misses may have minor artifacts.
    */
    void blitV(int x, int y, int height, SkAlpha alpha) override {
        if (height == 1) {
            // We're still in scan-line order if height is 1
            // This is useful for Analytic AA
            const SkAlpha alphas[2] = {alpha, 0};
            const int16_t runs[2] = {1, 0};
            this->blitAntiH(x, y, alphas, runs);
        } else {
            this->recordMinY(y);
            fBuilder->addColumn(x, y, alpha, height);
            fLastY = y + height - 1;
        }
    }

    void blitRect(int x, int y, int width, int height) override {
        this->recordMinY(y);
        this->checkForYGap(y);
        fBuilder->addRectRun(x, y, width, height);
        fLastY = y + height - 1;
    }

    void blitAntiRect(int x, int y, int width, int height,
                      SkAlpha leftAlpha, SkAlpha rightAlpha) override {
        this->recordMinY(y);
        this->checkForYGap(y);
        fBuilder->addAntiRectRun(x, y, width, height, leftAlpha, rightAlpha);
        fLastY = y + height - 1;
    }

    void blitMask(const SkMask&, const SkIRect& clip) override
        { unexpected(); }

    void blitH(int x, int y, int width) override {
        this->recordMinY(y);
        this->checkForYGap(y);
        fBuilder->addRun(x, y, 0xFF, width);
    }

    void blitAntiH(int x, int y, const SkAlpha alpha[], const int16_t runs[]) override {
        this->recordMinY(y);
        this->checkForYGap(y);
        for (;;) {
            int count = *runs;
            if (count <= 0) {
                return;
            }

            // The supersampler's buffer can be the width of the device, so
            // we may have to trim the run to our bounds. Previously, we assert that
            // the extra spans are always alpha==0.
            // However, the analytic AA is too sensitive to precision errors
            // so it may have extra spans with very tiny alpha because after several
            // arithmatic operations, the edge may bleed the path boundary a little bit.
            // Therefore, instead of always asserting alpha==0, we assert alpha < 0x10.
            int localX = x;
            int localCount = count;
            if (x < fLeft) {
                SkASSERT(0x10 > *alpha);
                int gap = fLeft - x;
                SkASSERT(gap <= count);
                localX += gap;
                localCount -= gap;
            }
            int right = x + count;
            if (right > fRight) {
                SkASSERT(0x10 > *alpha);
                localCount -= right - fRight;
                SkASSERT(localCount >= 0);
            }

            if (localCount) {
                fBuilder->addRun(localX, y, *alpha, localCount);
            }
            // Next run
            runs += count;
            alpha += count;
            x += count;
        }
    }

private:
    Builder* fBuilder;
    int      fLeft; // cache of builder's bounds' left edge
    int      fRight;
    int      fMinY;

    /*
     *  We track this, in case the scan converter skipped some number of
     *  scanlines at the (relative to the bounds it was given). This allows
     *  the builder, during its finish, to trip its bounds down to the "real"
     *  top.
     */
    void recordMinY(int y) {
        if (y < fMinY) {
            fMinY = y;
        }
    }

    void unexpected() {
        SK_ABORT("---- did not expect to get called here");
    }
};

bool SkAAClip::Builder::applyClipOp(SkAAClip* target, const SkAAClip& other, SkClipOp op) {
    this->operateY(*target, other, op);
    return this->finish(target);
}

bool SkAAClip::Builder::blitPath(SkAAClip* target, const SkPath& path, bool doAA) {
    Blitter blitter(this);
    SkRegion clip(fBounds);

    if (doAA) {
        SkScan::AntiFillPath(path, clip, &blitter, true);
    } else {
        SkScan::FillPath(path, clip, &blitter);
    }

    blitter.finish();
    return this->finish(target);
}

///////////////////////////////////////////////////////////////////////////////

void SkAAClip::copyToMask(SkMaskBuilder* mask) const {
    auto expandRowToMask = [](uint8_t* dst, const uint8_t* row, int width) {
        while (width > 0) {
            int n = row[0];
            SkASSERT(width >= n);
            memset(dst, row[1], n);
            dst += n;
            row += 2;
            width -= n;
        }
        SkASSERT(0 == width);
    };

    mask->format() = SkMask::kA8_Format;
    if (this->isEmpty()) {
        mask->bounds().setEmpty();
        mask->image() = nullptr;
        mask->rowBytes() = 0;
        return;
    }

    mask->bounds() = fBounds;
    mask->rowBytes() = fBounds.width();
    size_t size = mask->computeImageSize();
    mask->image() = SkMaskBuilder::AllocImage(size);

    Iter iter = RunHead::Iterate(*this);
    uint8_t* dst = mask->image();
    const int width = fBounds.width();

    int y = fBounds.fTop;
    while (!iter.done()) {
        do {
            expandRowToMask(dst, iter.data(), width);
            dst += mask->fRowBytes;
        } while (++y < iter.bottom());
        iter.next();
    }
}

#ifdef SK_DEBUG

void SkAAClip::validate() const {
    if (nullptr == fRunHead) {
        SkASSERT(fBounds.isEmpty());
        return;
    }
    SkASSERT(!fBounds.isEmpty());

    const RunHead* head = fRunHead;
    SkASSERT(head->fRefCnt.load() > 0);
    SkASSERT(head->fRowCount > 0);

    const YOffset* yoff = head->yoffsets();
    const YOffset* ystop = yoff + head->fRowCount;
    const int lastY = fBounds.height() - 1;

    // Y and offset must be monotonic
    int prevY = -1;
    int32_t prevOffset = -1;
    while (yoff < ystop) {
        SkASSERT(prevY < yoff->fY);
        SkASSERT(yoff->fY <= lastY);
        prevY = yoff->fY;
        SkASSERT(prevOffset < (int32_t)yoff->fOffset);
        prevOffset = yoff->fOffset;
        const uint8_t* row = head->data() + yoff->fOffset;
        size_t rowLength = compute_row_length(row, fBounds.width());
        SkASSERT(yoff->fOffset + rowLength <= head->fDataSize);
        yoff += 1;
    }
    // check the last entry;
    --yoff;
    SkASSERT(yoff->fY == lastY);
}

static void dump_one_row(const uint8_t* SK_RESTRICT row,
                         int width, int leading_num) {
    if (leading_num) {
        SkDebugf( "%03d ", leading_num );
    }
    while (width > 0) {
        int n = row[0];
        int val = row[1];
        char out = '.';
        if (val == 0xff) {
            out = '*';
        } else if (val > 0) {
            out = '+';
        }
        for (int i = 0 ; i < n ; i++) {
            SkDebugf( "%c", out );
        }
        row += 2;
        width -= n;
    }
    SkDebugf( "\n" );
}

void SkAAClip::debug(bool compress_y) const {
    Iter iter = RunHead::Iterate(*this);
    const int width = fBounds.width();

    int y = fBounds.fTop;
    while (!iter.done()) {
        if (compress_y) {
            dump_one_row(iter.data(), width, iter.bottom() - iter.top() + 1);
        } else {
            do {
                dump_one_row(iter.data(), width, 0);
            } while (++y < iter.bottom());
        }
        iter.next();
    }
}
#endif

///////////////////////////////////////////////////////////////////////////////

// Count the number of zeros on the left and right edges of the passed in
// RLE row. If 'row' is all zeros return 'width' in both variables.
static void count_left_right_zeros(const uint8_t* row, int width,
                                   int* leftZ, int* riteZ) {
    int zeros = 0;
    do {
        if (row[1]) {
            break;
        }
        int n = row[0];
        SkASSERT(n > 0);
        SkASSERT(n <= width);
        zeros += n;
        row += 2;
        width -= n;
    } while (width > 0);
    *leftZ = zeros;

    if (0 == width) {
        // this line is completely empty return 'width' in both variables
        *riteZ = *leftZ;
        return;
    }

    zeros = 0;
    while (width > 0) {
        int n = row[0];
        SkASSERT(n > 0);
        if (0 == row[1]) {
            zeros += n;
        } else {
            zeros = 0;
        }
        row += 2;
        width -= n;
    }
    *riteZ = zeros;
}

// modify row in place, trimming off (zeros) from the left and right sides.
// return the number of bytes that were completely eliminated from the left
static int trim_row_left_right(uint8_t* row, int width, int leftZ, int riteZ) {
    int trim = 0;
    while (leftZ > 0) {
        SkASSERT(0 == row[1]);
        int n = row[0];
        SkASSERT(n > 0);
        SkASSERT(n <= width);
        width -= n;
        row += 2;
        if (n > leftZ) {
            row[-2] = n - leftZ;
            break;
        }
        trim += 2;
        leftZ -= n;
        SkASSERT(leftZ >= 0);
    }

    if (riteZ) {
        // walk row to the end, and then we'll back up to trim riteZ
        while (width > 0) {
            int n = row[0];
            SkASSERT(n <= width);
            width -= n;
            row += 2;
        }
        // now skip whole runs of zeros
        do {
            row -= 2;
            SkASSERT(0 == row[1]);
            int n = row[0];
            SkASSERT(n > 0);
            if (n > riteZ) {
                row[0] = n - riteZ;
                break;
            }
            riteZ -= n;
            SkASSERT(riteZ >= 0);
        } while (riteZ > 0);
    }

    return trim;
}

bool SkAAClip::trimLeftRight() {
    if (this->isEmpty()) {
        return false;
    }

    AUTO_AACLIP_VALIDATE(*this);

    const int width = fBounds.width();
    RunHead* head = fRunHead;
    YOffset* yoff = head->yoffsets();
    YOffset* stop = yoff + head->fRowCount;
    uint8_t* base = head->data();

    // After this loop, 'leftZeros' & 'rightZeros' will contain the minimum
    // number of zeros on the left and right of the clip. This information
    // can be used to shrink the bounding box.
    int leftZeros = width;
    int riteZeros = width;
    while (yoff < stop) {
        int L, R;
        count_left_right_zeros(base + yoff->fOffset, width, &L, &R);
        SkASSERT(L + R < width || (L == width && R == width));
        if (L < leftZeros) {
            leftZeros = L;
        }
        if (R < riteZeros) {
            riteZeros = R;
        }
        if (0 == (leftZeros | riteZeros)) {
            // no trimming to do
            return true;
        }
        yoff += 1;
    }

    SkASSERT(leftZeros || riteZeros);
    if (width == leftZeros) {
        SkASSERT(width == riteZeros);
        return this->setEmpty();
    }

    this->validate();

    fBounds.fLeft += leftZeros;
    fBounds.fRight -= riteZeros;
    SkASSERT(!fBounds.isEmpty());

    // For now we don't realloc the storage (for time), we just shrink in place
    // This means we don't have to do any memmoves either, since we can just
    // play tricks with the yoff->fOffset for each row
    yoff = head->yoffsets();
    while (yoff < stop) {
        uint8_t* row = base + yoff->fOffset;
        SkDEBUGCODE((void)compute_row_length(row, width);)
        yoff->fOffset += trim_row_left_right(row, width, leftZeros, riteZeros);
        SkDEBUGCODE((void)compute_row_length(base + yoff->fOffset, width - leftZeros - riteZeros);)
        yoff += 1;
    }
    return true;
}

static bool row_is_all_zeros(const uint8_t* row, int width) {
    SkASSERT(width > 0);
    do {
        if (row[1]) {
            return false;
        }
        int n = row[0];
        SkASSERT(n <= width);
        width -= n;
        row += 2;
    } while (width > 0);
    SkASSERT(0 == width);
    return true;
}

bool SkAAClip::trimTopBottom() {
    if (this->isEmpty()) {
        return false;
    }

    this->validate();

    const int width = fBounds.width();
    RunHead* head = fRunHead;
    YOffset* yoff = head->yoffsets();
    YOffset* stop = yoff + head->fRowCount;
    const uint8_t* base = head->data();

    //  Look to trim away empty rows from the top.
    //
    int skip = 0;
    while (yoff < stop) {
        const uint8_t* data = base + yoff->fOffset;
        if (!row_is_all_zeros(data, width)) {
            break;
        }
        skip += 1;
        yoff += 1;
    }
    SkASSERT(skip <= head->fRowCount);
    if (skip == head->fRowCount) {
        return this->setEmpty();
    }
    if (skip > 0) {
        // adjust fRowCount and fBounds.fTop, and slide all the data up
        // as we remove [skip] number of YOffset entries
        yoff = head->yoffsets();
        int dy = yoff[skip - 1].fY + 1;
        for (int i = skip; i < head->fRowCount; ++i) {
            SkASSERT(yoff[i].fY >= dy);
            yoff[i].fY -= dy;
        }
        YOffset* dst = head->yoffsets();
        size_t size = head->fRowCount * sizeof(YOffset) + head->fDataSize;
        memmove(dst, dst + skip, size - skip * sizeof(YOffset));

        fBounds.fTop += dy;
        SkASSERT(!fBounds.isEmpty());
        head->fRowCount -= skip;
        SkASSERT(head->fRowCount > 0);

        this->validate();
        // need to reset this after the memmove
        base = head->data();
    }

    //  Look to trim away empty rows from the bottom.
    //  We know that we have at least one non-zero row, so we can just walk
    //  backwards without checking for running past the start.
    //
    stop = yoff = head->yoffsets() + head->fRowCount;
    do {
        yoff -= 1;
    } while (row_is_all_zeros(base + yoff->fOffset, width));
    skip = SkToInt(stop - yoff - 1);
    SkASSERT(skip >= 0 && skip < head->fRowCount);
    if (skip > 0) {
        // removing from the bottom is easier than from the top, as we don't
        // have to adjust any of the Y values, we just have to trim the array
        memmove(stop - skip, stop, head->fDataSize);

        fBounds.fBottom = fBounds.fTop + yoff->fY + 1;
        SkASSERT(!fBounds.isEmpty());
        head->fRowCount -= skip;
        SkASSERT(head->fRowCount > 0);
    }
    this->validate();

    return true;
}

// can't validate before we're done, since trimming is part of the process of
// making us valid after the Builder. Since we build from top to bottom, its
// possible our fBounds.fBottom is bigger than our last scanline of data, so
// we trim fBounds.fBottom back up.
//
// TODO: check for duplicates in X and Y to further compress our data
//
bool SkAAClip::trimBounds() {
    if (this->isEmpty()) {
        return false;
    }

    const RunHead* head = fRunHead;
    const YOffset* yoff = head->yoffsets();

    SkASSERT(head->fRowCount > 0);
    const YOffset& lastY = yoff[head->fRowCount - 1];
    SkASSERT(lastY.fY + 1 <= fBounds.height());
    fBounds.fBottom = fBounds.fTop + lastY.fY + 1;
    SkASSERT(lastY.fY + 1 == fBounds.height());
    SkASSERT(!fBounds.isEmpty());

    return this->trimTopBottom() && this->trimLeftRight();
}

///////////////////////////////////////////////////////////////////////////////

SkAAClip::SkAAClip() {
    fBounds.setEmpty();
    fRunHead = nullptr;
}

SkAAClip::SkAAClip(const SkAAClip& src) {
    SkDEBUGCODE(fBounds.setEmpty();)    // need this for validate
    fRunHead = nullptr;
    *this = src;
}

SkAAClip::~SkAAClip() {
    this->freeRuns();
}

SkAAClip& SkAAClip::operator=(const SkAAClip& src) {
    AUTO_AACLIP_VALIDATE(*this);
    src.validate();

    if (this != &src) {
        this->freeRuns();
        fBounds = src.fBounds;
        fRunHead = src.fRunHead;
        if (fRunHead) {
            fRunHead->fRefCnt++;
        }
    }
    return *this;
}

bool SkAAClip::setEmpty() {
    this->freeRuns();
    fBounds.setEmpty();
    fRunHead = nullptr;
    return false;
}

bool SkAAClip::setRect(const SkIRect& bounds) {
    if (bounds.isEmpty()) {
        return this->setEmpty();
    }

    AUTO_AACLIP_VALIDATE(*this);

    this->freeRuns();
    fBounds = bounds;
    fRunHead = RunHead::AllocRect(bounds);
    SkASSERT(!this->isEmpty());
    return true;
}

bool SkAAClip::isRect() const {
    if (this->isEmpty()) {
        return false;
    }

    const RunHead* head = fRunHead;
    if (head->fRowCount != 1) {
        return false;
    }
    const YOffset* yoff = head->yoffsets();
    if (yoff->fY != fBounds.fBottom - 1) {
        return false;
    }

    const uint8_t* row = head->data() + yoff->fOffset;
    int width = fBounds.width();
    do {
        if (row[1] != 0xFF) {
            return false;
        }
        int n = row[0];
        SkASSERT(n <= width);
        width -= n;
        row += 2;
    } while (width > 0);
    return true;
}

bool SkAAClip::setRegion(const SkRegion& rgn) {
    if (rgn.isEmpty()) {
        return this->setEmpty();
    }
    if (rgn.isRect()) {
        return this->setRect(rgn.getBounds());
    }


    const SkIRect& bounds = rgn.getBounds();
    const int offsetX = bounds.fLeft;
    const int offsetY = bounds.fTop;

    SkTDArray<YOffset> yArray;
    SkTDArray<uint8_t> xArray;

    yArray.reserve(std::min(bounds.height(), 1024));
    xArray.reserve(std::min(bounds.width(), 512) * 128);

    auto appendXRun = [&xArray](uint8_t value, int count) {
        SkASSERT(count >= 0);
        while (count > 0) {
            int n = count;
            if (n > 255) {
                n = 255;
            }
            uint8_t* data = xArray.append(2);
            data[0] = n;
            data[1] = value;
            count -= n;
        }
    };

    SkRegion::Iterator iter(rgn);
    int prevRight = 0;
    int prevBot = 0;
    YOffset* currY = nullptr;

    for (; !iter.done(); iter.next()) {
        const SkIRect& r = iter.rect();
        SkASSERT(bounds.contains(r));

        int bot = r.fBottom - offsetY;
        SkASSERT(bot >= prevBot);
        if (bot > prevBot) {
            if (currY) {
                // flush current row
                appendXRun(0, bounds.width() - prevRight);
            }
            // did we introduce an empty-gap from the prev row?
            int top = r.fTop - offsetY;
            if (top > prevBot) {
                currY = yArray.append();
                currY->fY = top - 1;
                currY->fOffset = xArray.size();
                appendXRun(0, bounds.width());
            }
            // create a new record for this Y value
            currY = yArray.append();
            currY->fY = bot - 1;
            currY->fOffset = xArray.size();
            prevRight = 0;
            prevBot = bot;
        }

        int x = r.fLeft - offsetX;
        appendXRun(0, x - prevRight);

        int w = r.fRight - r.fLeft;
        appendXRun(0xFF, w);
        prevRight = x + w;
        SkASSERT(prevRight <= bounds.width());
    }
    // flush last row
    appendXRun(0, bounds.width() - prevRight);

    // now pack everything into a RunHead
    RunHead* head = RunHead::Alloc(yArray.size(), xArray.size_bytes());
    memcpy(head->yoffsets(), yArray.begin(), yArray.size_bytes());
    memcpy(head->data(), xArray.begin(), xArray.size_bytes());

    this->setEmpty();
    fBounds = bounds;
    fRunHead = head;
    this->validate();
    return true;
}

bool SkAAClip::setPath(const SkPath& path, const SkIRect& clip, bool doAA) {
    AUTO_AACLIP_VALIDATE(*this);

    if (clip.isEmpty()) {
        return this->setEmpty();
    }

    SkIRect ibounds;
    // Since we assert that the BuilderBlitter will never blit outside the intersection
    // of clip and ibounds, we create the builder with the snug bounds.
    if (path.isInverseFillType()) {
        ibounds = clip;
    } else {
        path.getBounds().roundOut(&ibounds);
        // It's possible the bounds of our path might exceed SK_MaxS32 in width
        // but since our clip is within that width (otherwise isEmpty() above
        // would catch it), we can use isEmpty64() safely here. blitPath will
        // interesect the two bounds before drawing.
        if (ibounds.isEmpty64() || !ibounds.intersect(clip)) {
            return this->setEmpty();
        }
    }

    Builder builder(ibounds);
    return builder.blitPath(this, path, doAA);
}

///////////////////////////////////////////////////////////////////////////////

bool SkAAClip::op(const SkAAClip& other, SkClipOp op) {
    AUTO_AACLIP_VALIDATE(*this);

    if (this->isEmpty()) {
        // Once the clip is empty, it cannot become un-empty.
        return false;
    }

    SkIRect bounds = fBounds;
    switch(op) {
        case SkClipOp::kDifference:
            if (other.isEmpty() || !SkIRect::Intersects(fBounds, other.fBounds)) {
                // this remains unmodified and isn't empty
                return true;
            }
            break;

        case SkClipOp::kIntersect:
            if (other.isEmpty() || !bounds.intersect(other.fBounds)) {
                // the intersected clip becomes empty
                return this->setEmpty();
            }
            break;
    }


    SkASSERT(SkIRect::Intersects(bounds, fBounds));
    SkASSERT(SkIRect::Intersects(bounds, other.fBounds));

    Builder builder(bounds);
    return builder.applyClipOp(this, other, op);
}

bool SkAAClip::op(const SkIRect& rect, SkClipOp op) {
    // It can be expensive to build a local aaclip before applying the op, so
    // we first see if we can restrict the bounds of new rect to our current
    // bounds, or note that the new rect subsumes our current clip.
    SkIRect pixelBounds = fBounds;
    if (!pixelBounds.intersect(rect)) {
        // No change or clip becomes empty depending on 'op'
        switch(op) {
            case SkClipOp::kDifference: return !this->isEmpty();
            case SkClipOp::kIntersect:  return this->setEmpty();
        }
        SkUNREACHABLE;
    } else if (pixelBounds == fBounds) {
        // Wholly inside 'rect', so clip becomes empty or remains unchanged
        switch(op) {
            case SkClipOp::kDifference: return this->setEmpty();
            case SkClipOp::kIntersect:  return !this->isEmpty();
        }
        SkUNREACHABLE;
    } else if (op == SkClipOp::kIntersect && this->quickContains(pixelBounds)) {
        // We become just the remaining rectangle
        return this->setRect(pixelBounds);
    } else {
        SkAAClip clip;
        clip.setRect(rect);
        return this->op(clip, op);
    }
}

bool SkAAClip::op(const SkRect& rect, SkClipOp op, bool doAA) {
    if (!doAA) {
        return this->op(rect.round(), op);
    } else {
        // Tighten bounds for "path" aaclip of the rect
        SkIRect pixelBounds = fBounds;
        if (!pixelBounds.intersect(rect.roundOut())) {
            // No change or clip becomes empty depending on 'op'
            switch(op) {
                case SkClipOp::kDifference: return !this->isEmpty();
                case SkClipOp::kIntersect:  return this->setEmpty();
            }
            SkUNREACHABLE;
        } else if (rect.contains(SkRect::Make(fBounds))) {
            // Wholly inside 'rect', so clip becomes empty or remains unchanged
            switch(op) {
                case SkClipOp::kDifference: return this->setEmpty();
                case SkClipOp::kIntersect:  return !this->isEmpty();
            }
            SkUNREACHABLE;
        } else if (op == SkClipOp::kIntersect && this->quickContains(pixelBounds)) {
            // We become just the rect intersected with pixel bounds (preserving fractional coords
            // for AA edges).
            return this->setPath(SkPath::Rect(rect), pixelBounds, /*doAA=*/true);
        } else {
            SkAAClip rectClip;
            rectClip.setPath(SkPath::Rect(rect),
                             op == SkClipOp::kDifference ? fBounds : pixelBounds,
                             /*doAA=*/true);
            return this->op(rectClip, op);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////

bool SkAAClip::translate(int dx, int dy, SkAAClip* dst) const {
    if (nullptr == dst) {
        return !this->isEmpty();
    }

    if (this->isEmpty()) {
        return dst->setEmpty();
    }

    if (this != dst) {
        fRunHead->fRefCnt++;
        dst->freeRuns();
        dst->fRunHead = fRunHead;
        dst->fBounds = fBounds;
    }
    dst->fBounds.offset(dx, dy);
    return true;
}

void SkAAClip::freeRuns() {
    if (fRunHead) {
        SkASSERT(fRunHead->fRefCnt.load() >= 1);
        if (1 == fRunHead->fRefCnt--) {
            sk_free(fRunHead);
        }
    }
}

const uint8_t* SkAAClip::findRow(int y, int* lastYForRow) const {
    SkASSERT(fRunHead);

    if (y < fBounds.fTop || y >= fBounds.fBottom) {
        return nullptr;
    }
    y -= fBounds.y();  // our yoffs values are relative to the top

    const YOffset* yoff = fRunHead->yoffsets();
    while (yoff->fY < y) {
        yoff += 1;
        SkASSERT(yoff - fRunHead->yoffsets() < fRunHead->fRowCount);
    }

    if (lastYForRow) {
        *lastYForRow = fBounds.y() + yoff->fY;
    }
    return fRunHead->data() + yoff->fOffset;
}

const uint8_t* SkAAClip::findX(const uint8_t data[], int x, int* initialCount) const {
    SkASSERT(x >= fBounds.fLeft && x < fBounds.fRight);
    x -= fBounds.x();

    // first skip up to X
    for (;;) {
        int n = data[0];
        if (x < n) {
            if (initialCount) {
                *initialCount = n - x;
            }
            break;
        }
        data += 2;
        x -= n;
    }
    return data;
}

bool SkAAClip::quickContains(int left, int top, int right, int bottom) const {
    if (this->isEmpty()) {
        return false;
    }
    if (!fBounds.contains(SkIRect{left, top, right, bottom})) {
        return false;
    }

    int lastY SK_INIT_TO_AVOID_WARNING;
    const uint8_t* row = this->findRow(top, &lastY);
    if (lastY < bottom) {
        return false;
    }
    // now just need to check in X
    int count;
    row = this->findX(row, left, &count);

    int rectWidth = right - left;
    while (0xFF == row[1]) {
        if (count >= rectWidth) {
            return true;
        }
        rectWidth -= count;
        row += 2;
        count = row[0];
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////

static void expandToRuns(const uint8_t* SK_RESTRICT data, int initialCount, int width,
                         int16_t* SK_RESTRICT runs, SkAlpha* SK_RESTRICT aa) {
    // we don't read our initial n from data, since the caller may have had to
    // clip it, hence the initialCount parameter.
    int n = initialCount;
    for (;;) {
        if (n > width) {
            n = width;
        }
        SkASSERT(n > 0);
        runs[0] = n;
        runs += n;

        aa[0] = data[1];
        aa += n;

        data += 2;
        width -= n;
        if (0 == width) {
            break;
        }
        // load the next count
        n = data[0];
    }
    runs[0] = 0;    // sentinel
}

SkAAClipBlitter::~SkAAClipBlitter() {
    sk_free(fScanlineScratch);
}

void SkAAClipBlitter::ensureRunsAndAA() {
    if (nullptr == fScanlineScratch) {
        // add 1 so we can store the terminating run count of 0
        int count = fAAClipBounds.width() + 1;
        // we use this either for fRuns + fAA, or a scaline of a mask
        // which may be as deep as 32bits
        fScanlineScratch = sk_malloc_throw(count * sizeof(SkPMColor));
        fRuns = (int16_t*)fScanlineScratch;
        fAA = (SkAlpha*)(fRuns + count);
    }
}

void SkAAClipBlitter::blitH(int x, int y, int width) {
    SkASSERT(width > 0);
    SkASSERT(fAAClipBounds.contains(x, y));
    SkASSERT(fAAClipBounds.contains(x + width  - 1, y));

    const uint8_t* row = fAAClip->findRow(y);
    int initialCount;
    row = fAAClip->findX(row, x, &initialCount);

    if (initialCount >= width) {
        SkAlpha alpha = row[1];
        if (0 == alpha) {
            return;
        }
        if (0xFF == alpha) {
            fBlitter->blitH(x, y, width);
            return;
        }
    }

    this->ensureRunsAndAA();
    expandToRuns(row, initialCount, width, fRuns, fAA);

    fBlitter->blitAntiH(x, y, fAA, fRuns);
}

static void merge(const uint8_t* SK_RESTRICT row, int rowN,
                  const SkAlpha* SK_RESTRICT srcAA,
                  const int16_t* SK_RESTRICT srcRuns,
                  SkAlpha* SK_RESTRICT dstAA,
                  int16_t* SK_RESTRICT dstRuns,
                  int width) {
    SkDEBUGCODE(int accumulated = 0;)
    int srcN = srcRuns[0];
    // do we need this check?
    if (0 == srcN) {
        return;
    }

    for (;;) {
        SkASSERT(rowN > 0);
        SkASSERT(srcN > 0);

        unsigned newAlpha = SkMulDiv255Round(srcAA[0], row[1]);
        int minN = std::min(srcN, rowN);
        dstRuns[0] = minN;
        dstRuns += minN;
        dstAA[0] = newAlpha;
        dstAA += minN;

        if (0 == (srcN -= minN)) {
            srcN = srcRuns[0];  // refresh
            srcRuns += srcN;
            srcAA += srcN;
            srcN = srcRuns[0];  // reload
            if (0 == srcN) {
                break;
            }
        }
        if (0 == (rowN -= minN)) {
            row += 2;
            rowN = row[0];  // reload
        }

        SkDEBUGCODE(accumulated += minN;)
        SkASSERT(accumulated <= width);
    }
    dstRuns[0] = 0;
}

void SkAAClipBlitter::blitAntiH(int x, int y, const SkAlpha aa[],
                                const int16_t runs[]) {

    const uint8_t* row = fAAClip->findRow(y);
    int initialCount;
    row = fAAClip->findX(row, x, &initialCount);

    this->ensureRunsAndAA();

    merge(row, initialCount, aa, runs, fAA, fRuns, fAAClipBounds.width());
    fBlitter->blitAntiH(x, y, fAA, fRuns);
}

void SkAAClipBlitter::blitV(int x, int y, int height, SkAlpha alpha) {
    if (fAAClip->quickContains(x, y, x + 1, y + height)) {
        fBlitter->blitV(x, y, height, alpha);
        return;
    }

    for (;;) {
        int lastY SK_INIT_TO_AVOID_WARNING;
        const uint8_t* row = fAAClip->findRow(y, &lastY);
        int dy = lastY - y + 1;
        if (dy > height) {
            dy = height;
        }
        height -= dy;

        row = fAAClip->findX(row, x);
        SkAlpha newAlpha = SkMulDiv255Round(alpha, row[1]);
        if (newAlpha) {
            fBlitter->blitV(x, y, dy, newAlpha);
        }
        SkASSERT(height >= 0);
        if (height <= 0) {
            break;
        }
        y = lastY + 1;
    }
}

void SkAAClipBlitter::blitRect(int x, int y, int width, int height) {
    if (fAAClip->quickContains(x, y, x + width, y + height)) {
        fBlitter->blitRect(x, y, width, height);
        return;
    }

    while (--height >= 0) {
        this->blitH(x, y, width);
        y += 1;
    }
}

typedef void (*MergeAAProc)(const void* src, int width, const uint8_t* row,
                            int initialRowCount, void* dst);

static void small_memcpy(void* dst, const void* src, size_t n) {
    memcpy(dst, src, n);
}

static void small_bzero(void* dst, size_t n) {
    sk_bzero(dst, n);
}

static inline uint8_t mergeOne(uint8_t value, unsigned alpha) {
    return SkMulDiv255Round(value, alpha);
}

static inline uint16_t mergeOne(uint16_t value, unsigned alpha) {
    unsigned r = SkGetPackedR16(value);
    unsigned g = SkGetPackedG16(value);
    unsigned b = SkGetPackedB16(value);
    return SkPackRGB16(SkMulDiv255Round(r, alpha),
                       SkMulDiv255Round(g, alpha),
                       SkMulDiv255Round(b, alpha));
}

template <typename T>
void mergeT(const void* inSrc, int srcN, const uint8_t* SK_RESTRICT row, int rowN, void* inDst) {
    const T* SK_RESTRICT src = static_cast<const T*>(inSrc);
    T* SK_RESTRICT       dst = static_cast<T*>(inDst);
    for (;;) {
        SkASSERT(rowN > 0);
        SkASSERT(srcN > 0);

        int n = std::min(rowN, srcN);
        unsigned rowA = row[1];
        if (0xFF == rowA) {
            small_memcpy(dst, src, n * sizeof(T));
        } else if (0 == rowA) {
            small_bzero(dst, n * sizeof(T));
        } else {
            for (int i = 0; i < n; ++i) {
                dst[i] = mergeOne(src[i], rowA);
            }
        }

        if (0 == (srcN -= n)) {
            break;
        }

        src += n;
        dst += n;

        SkASSERT(rowN == n);
        row += 2;
        rowN = row[0];
    }
}

static MergeAAProc find_merge_aa_proc(SkMask::Format format) {
    switch (format) {
        case SkMask::kBW_Format:
            SkDEBUGFAIL("unsupported");
            return nullptr;
        case SkMask::kA8_Format:
        case SkMask::k3D_Format:
            return mergeT<uint8_t> ;
        case SkMask::kLCD16_Format:
            return mergeT<uint16_t>;
        default:
            SkDEBUGFAIL("unsupported");
            return nullptr;
    }
}

static U8CPU bit2byte(int bitInAByte) {
    SkASSERT(bitInAByte <= 0xFF);
    // negation turns any non-zero into 0xFFFFFF??, so we just shift down
    // some value >= 8 to get a full FF value
    return -bitInAByte >> 8;
}

static void upscaleBW2A8(SkMask* dstMask, const SkMask& srcMask) {
    SkASSERT(SkMask::kBW_Format == srcMask.fFormat);
    SkASSERT(SkMask::kA8_Format == dstMask->fFormat);

    const int width = srcMask.fBounds.width();
    const int height = srcMask.fBounds.height();

    const uint8_t* SK_RESTRICT src = (const uint8_t*)srcMask.fImage;
    const size_t srcRB = srcMask.fRowBytes;
    uint8_t* SK_RESTRICT dst = const_cast<uint8_t*>(dstMask->fImage);
    const size_t dstRB = dstMask->fRowBytes;

    const int wholeBytes = width >> 3;
    const int leftOverBits = width & 7;

    for (int y = 0; y < height; ++y) {
        uint8_t* SK_RESTRICT d = dst;
        for (int i = 0; i < wholeBytes; ++i) {
            int srcByte = src[i];
            d[0] = bit2byte(srcByte & (1 << 7));
            d[1] = bit2byte(srcByte & (1 << 6));
            d[2] = bit2byte(srcByte & (1 << 5));
            d[3] = bit2byte(srcByte & (1 << 4));
            d[4] = bit2byte(srcByte & (1 << 3));
            d[5] = bit2byte(srcByte & (1 << 2));
            d[6] = bit2byte(srcByte & (1 << 1));
            d[7] = bit2byte(srcByte & (1 << 0));
            d += 8;
        }
        if (leftOverBits) {
            int srcByte = src[wholeBytes];
            for (int x = 0; x < leftOverBits; ++x) {
                *d++ = bit2byte(srcByte & 0x80);
                srcByte <<= 1;
            }
        }
        src += srcRB;
        dst += dstRB;
    }
}

void SkAAClipBlitter::blitMask(const SkMask& origMask, const SkIRect& clip) {
    SkASSERT(fAAClip->getBounds().contains(clip));

    if (fAAClip->quickContains(clip)) {
        fBlitter->blitMask(origMask, clip);
        return;
    }

    const SkMask* mask = &origMask;

    // if we're BW, we need to upscale to A8 (ugh)
    SkMaskBuilder  grayMask;
    if (SkMask::kBW_Format == origMask.fFormat) {
        grayMask.format() = SkMask::kA8_Format;
        grayMask.bounds() = origMask.fBounds;
        grayMask.rowBytes() = origMask.fBounds.width();
        size_t size = grayMask.computeImageSize();
        grayMask.image() = reinterpret_cast<uint8_t*>(
            fGrayMaskScratch.reset(size, SkAutoMalloc::kReuse_OnShrink));

        upscaleBW2A8(&grayMask, origMask);
        mask = &grayMask;
    }

    this->ensureRunsAndAA();

    // HACK -- we are devolving 3D into A8, need to copy the rest of the 3D
    // data into a temp block to support it better (ugh)

    const void* src = mask->getAddr(clip.fLeft, clip.fTop);
    const size_t srcRB = mask->fRowBytes;
    const int width = clip.width();
    MergeAAProc mergeProc = find_merge_aa_proc(mask->fFormat);

    SkMaskBuilder rowMask;
    rowMask.format() = SkMask::k3D_Format == mask->fFormat ? SkMask::kA8_Format : mask->fFormat;
    rowMask.bounds().fLeft = clip.fLeft;
    rowMask.bounds().fRight = clip.fRight;
    rowMask.rowBytes() = mask->fRowBytes; // doesn't matter, since our height==1
    rowMask.image() = (uint8_t*)fScanlineScratch;

    int y = clip.fTop;
    const int stopY = y + clip.height();

    do {
        int localStopY SK_INIT_TO_AVOID_WARNING;
        const uint8_t* row = fAAClip->findRow(y, &localStopY);
        // findRow returns last Y, not stop, so we add 1
        localStopY = std::min(localStopY + 1, stopY);

        int initialCount;
        row = fAAClip->findX(row, clip.fLeft, &initialCount);
        do {
            mergeProc(src, width, row, initialCount, rowMask.image());
            rowMask.bounds().fTop = y;
            rowMask.bounds().fBottom = y + 1;
            fBlitter->blitMask(rowMask, rowMask.fBounds);
            src = (const void*)((const char*)src + srcRB);
        } while (++y < localStopY);
    } while (y < stopY);
}
