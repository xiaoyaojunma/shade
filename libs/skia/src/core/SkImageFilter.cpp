/*
 * Copyright 2012 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkImageFilter.h"

#include "SkBitmap.h"
#include "SkReadBuffer.h"
#include "SkWriteBuffer.h"
#include "SkRect.h"
#include "SkValidationUtils.h"
#if SK_SUPPORT_GPU
#include "GrContext.h"
#include "SkGrPixelRef.h"
#include "SkGr.h"
#endif

SkImageFilter::SkImageFilter(int inputCount, SkImageFilter** inputs, const CropRect* cropRect)
  : fInputCount(inputCount),
    fInputs(new SkImageFilter*[inputCount]),
    fCropRect(cropRect ? *cropRect : CropRect(SkRect(), 0x0)) {
    for (int i = 0; i < inputCount; ++i) {
        fInputs[i] = inputs[i];
        SkSafeRef(fInputs[i]);
    }
}

SkImageFilter::SkImageFilter(SkImageFilter* input, const CropRect* cropRect)
  : fInputCount(1),
    fInputs(new SkImageFilter*[1]),
    fCropRect(cropRect ? *cropRect : CropRect(SkRect(), 0x0)) {
    fInputs[0] = input;
    SkSafeRef(fInputs[0]);
}

SkImageFilter::SkImageFilter(SkImageFilter* input1, SkImageFilter* input2, const CropRect* cropRect)
  : fInputCount(2), fInputs(new SkImageFilter*[2]),
    fCropRect(cropRect ? *cropRect : CropRect(SkRect(), 0x0)) {
    fInputs[0] = input1;
    fInputs[1] = input2;
    SkSafeRef(fInputs[0]);
    SkSafeRef(fInputs[1]);
}

SkImageFilter::~SkImageFilter() {
    for (int i = 0; i < fInputCount; i++) {
        SkSafeUnref(fInputs[i]);
    }
    delete[] fInputs;
}

SkImageFilter::SkImageFilter(int inputCount, SkReadBuffer& buffer) {
    fInputCount = buffer.readInt();
    if (buffer.validate((fInputCount >= 0) && ((inputCount < 0) || (fInputCount == inputCount)))) {
        fInputs = new SkImageFilter*[fInputCount];
        for (int i = 0; i < fInputCount; i++) {
            if (buffer.readBool()) {
                fInputs[i] = buffer.readImageFilter();
            } else {
                fInputs[i] = NULL;
            }
            if (!buffer.isValid()) {
                fInputCount = i; // Do not use fInputs past that point in the destructor
                break;
            }
        }
        SkRect rect;
        buffer.readRect(&rect);
        if (buffer.isValid() && buffer.validate(SkIsValidRect(rect))) {
            uint32_t flags = buffer.readUInt();
            fCropRect = CropRect(rect, flags);
        }
    } else {
        fInputCount = 0;
        fInputs = NULL;
    }
}

void SkImageFilter::flatten(SkWriteBuffer& buffer) const {
    buffer.writeInt(fInputCount);
    for (int i = 0; i < fInputCount; i++) {
        SkImageFilter* input = getInput(i);
        buffer.writeBool(input != NULL);
        if (input != NULL) {
            buffer.writeFlattenable(input);
        }
    }
    buffer.writeRect(fCropRect.rect());
    buffer.writeUInt(fCropRect.flags());
}

bool SkImageFilter::filterImage(Proxy* proxy, const SkBitmap& src,
                                const SkMatrix& ctm,
                                SkBitmap* result, SkIPoint* offset) const {
    SkASSERT(result);
    SkASSERT(offset);
    /*
     *  Give the proxy first shot at the filter. If it returns false, ask
     *  the filter to do it.
     */
    return (proxy && proxy->filterImage(this, src, ctm, result, offset)) ||
           this->onFilterImage(proxy, src, ctm, result, offset);
}

bool SkImageFilter::filterBounds(const SkIRect& src, const SkMatrix& ctm,
                                 SkIRect* dst) const {
    SkASSERT(&src);
    SkASSERT(dst);
    return this->onFilterBounds(src, ctm, dst);
}

void SkImageFilter::computeFastBounds(const SkRect& src, SkRect* dst) const {
    if (0 == fInputCount) {
        *dst = src;
        return;
    }
    if (this->getInput(0)) {
        this->getInput(0)->computeFastBounds(src, dst);
    } else {
        *dst = src;
    }
    for (int i = 1; i < fInputCount; i++) {
        SkImageFilter* input = this->getInput(i);
        if (input) {
            SkRect bounds;
            input->computeFastBounds(src, &bounds);
            dst->join(bounds);
        } else {
            dst->join(src);
        }
    }
}

bool SkImageFilter::onFilterImage(Proxy*, const SkBitmap&, const SkMatrix&,
                                  SkBitmap*, SkIPoint*) const {
    return false;
}

bool SkImageFilter::canFilterImageGPU() const {
    return this->asNewEffect(NULL, NULL, SkMatrix::I(), SkIRect());
}

bool SkImageFilter::filterImageGPU(Proxy* proxy, const SkBitmap& src, const SkMatrix& ctm,
                                   SkBitmap* result, SkIPoint* offset) const {
#if SK_SUPPORT_GPU
    SkBitmap input = src;
    SkASSERT(fInputCount == 1);
    SkIPoint srcOffset = SkIPoint::Make(0, 0);
    if (this->getInput(0) &&
        !this->getInput(0)->getInputResultGPU(proxy, src, ctm, &input, &srcOffset)) {
        return false;
    }
    GrTexture* srcTexture = input.getTexture();
    SkIRect bounds;
    src.getBounds(&bounds);
    bounds.offset(srcOffset);
    if (!this->applyCropRect(&bounds, ctm)) {
        return false;
    }
    SkRect srcRect = SkRect::Make(bounds);
    SkRect dstRect = SkRect::MakeWH(srcRect.width(), srcRect.height());
    GrContext* context = srcTexture->getContext();

    GrTextureDesc desc;
    desc.fFlags = kRenderTarget_GrTextureFlagBit,
    desc.fWidth = bounds.width();
    desc.fHeight = bounds.height();
    desc.fConfig = kRGBA_8888_GrPixelConfig;

    GrAutoScratchTexture dst(context, desc);
    GrContext::AutoMatrix am;
    am.setIdentity(context);
    GrContext::AutoRenderTarget art(context, dst.texture()->asRenderTarget());
    GrContext::AutoClip acs(context, dstRect);
    GrEffectRef* effect;
    offset->fX = bounds.left();
    offset->fY = bounds.top();
    bounds.offset(-srcOffset);
    SkMatrix matrix(ctm);
    matrix.postTranslate(SkIntToScalar(-bounds.left()), SkIntToScalar(-bounds.top()));
    this->asNewEffect(&effect, srcTexture, matrix, bounds);
    SkASSERT(effect);
    SkAutoUnref effectRef(effect);
    GrPaint paint;
    paint.addColorEffect(effect);
    context->drawRectToRect(paint, dstRect, srcRect);

    SkAutoTUnref<GrTexture> resultTex(dst.detach());
    WrapTexture(resultTex, bounds.width(), bounds.height(), result);
    return true;
#else
    return false;
#endif
}

bool SkImageFilter::applyCropRect(SkIRect* rect, const SkMatrix& matrix) const {
    SkRect cropRect;
    matrix.mapRect(&cropRect, fCropRect.rect());
    SkIRect cropRectI;
    cropRect.roundOut(&cropRectI);
    uint32_t flags = fCropRect.flags();
    // If the original crop rect edges were unset, max out the new crop edges
    if (!(flags & CropRect::kHasLeft_CropEdge)) cropRectI.fLeft = SK_MinS32;
    if (!(flags & CropRect::kHasTop_CropEdge)) cropRectI.fTop = SK_MinS32;
    if (!(flags & CropRect::kHasRight_CropEdge)) cropRectI.fRight = SK_MaxS32;
    if (!(flags & CropRect::kHasBottom_CropEdge)) cropRectI.fBottom = SK_MaxS32;
    return rect->intersect(cropRectI);
}

bool SkImageFilter::onFilterBounds(const SkIRect& src, const SkMatrix& ctm,
                                   SkIRect* dst) const {
    if (fInputCount < 1) {
        return false;
    }

    SkIRect bounds;
    for (int i = 0; i < fInputCount; ++i) {
        SkImageFilter* filter = this->getInput(i);
        SkIRect rect = src;
        if (filter && !filter->filterBounds(src, ctm, &rect)) {
            return false;
        }
        if (0 == i) {
            bounds = rect;
        } else {
            bounds.join(rect);
        }
    }

    // don't modify dst until now, so we don't accidentally change it in the
    // loop, but then return false on the next filter.
    *dst = bounds;
    return true;
}

bool SkImageFilter::asNewEffect(GrEffectRef**, GrTexture*, const SkMatrix&, const SkIRect&) const {
    return false;
}

bool SkImageFilter::asColorFilter(SkColorFilter**) const {
    return false;
}

#if SK_SUPPORT_GPU

void SkImageFilter::WrapTexture(GrTexture* texture, int width, int height, SkBitmap* result) {
    SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
    result->setConfig(info);
    result->setPixelRef(SkNEW_ARGS(SkGrPixelRef, (info, texture)))->unref();
}

bool SkImageFilter::getInputResultGPU(SkImageFilter::Proxy* proxy,
                                      const SkBitmap& src, const SkMatrix& ctm,
                                      SkBitmap* result, SkIPoint* offset) const {
    // Ensure that GrContext calls under filterImage and filterImageGPU below will see an identity
    // matrix with no clip and that the matrix, clip, and render target set before this function was
    // called are restored before we return to the caller.
    GrContext* context = src.getTexture()->getContext();
    GrContext::AutoWideOpenIdentityDraw awoid(context, NULL);
    if (this->canFilterImageGPU()) {
        return this->filterImageGPU(proxy, src, ctm, result, offset);
    } else {
        if (this->filterImage(proxy, src, ctm, result, offset)) {
            if (!result->getTexture()) {
                SkImageInfo info;
                if (!result->asImageInfo(&info)) {
                    return false;
                }
                GrTexture* resultTex = GrLockAndRefCachedBitmapTexture(context, *result, NULL);
                result->setPixelRef(new SkGrPixelRef(info, resultTex))->unref();
                GrUnlockAndUnrefCachedBitmapTexture(resultTex);
            }
            return true;
        } else {
            return false;
        }
    }
}
#endif
