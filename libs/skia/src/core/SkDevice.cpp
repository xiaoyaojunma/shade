/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkDevice.h"
#include "SkMetaData.h"

#if SK_PMCOLOR_BYTE_ORDER(B,G,R,A)
    const SkCanvas::Config8888 SkBaseDevice::kPMColorAlias = SkCanvas::kBGRA_Premul_Config8888;
#elif SK_PMCOLOR_BYTE_ORDER(R,G,B,A)
    const SkCanvas::Config8888 SkBaseDevice::kPMColorAlias = SkCanvas::kRGBA_Premul_Config8888;
#else
    const SkCanvas::Config8888 SkBaseDevice::kPMColorAlias = (SkCanvas::Config8888) -1;
#endif

///////////////////////////////////////////////////////////////////////////////

SkBaseDevice::SkBaseDevice()
    : fLeakyProperties(SkDeviceProperties::MakeDefault())
#ifdef SK_DEBUG
    , fAttachedToCanvas(false)
#endif
{
    fOrigin.setZero();
    fMetaData = NULL;
}

SkBaseDevice::SkBaseDevice(const SkDeviceProperties& deviceProperties)
    : fLeakyProperties(deviceProperties)
#ifdef SK_DEBUG
    , fAttachedToCanvas(false)
#endif
{
    fOrigin.setZero();
    fMetaData = NULL;
}

SkBaseDevice::~SkBaseDevice() {
    delete fMetaData;
}

SkBaseDevice* SkBaseDevice::createCompatibleDevice(const SkImageInfo& info) {
#ifdef SK_SUPPORT_LEGACY_COMPATIBLEDEVICE_CONFIG
    // We call the old method to support older subclasses.
    // If they have, we return their device, else we use the new impl.
    SkBitmap::Config config = SkColorTypeToBitmapConfig(info.colorType());
    SkBaseDevice* dev = this->onCreateCompatibleDevice(config,
                                                       info.width(),
                                                       info.height(),
                                                       info.isOpaque(),
                                                       kGeneral_Usage);
    if (dev) {
        return dev;
    }
    // fall through to new impl
#endif
    return this->onCreateDevice(info, kGeneral_Usage);
}

SkBaseDevice* SkBaseDevice::createCompatibleDeviceForSaveLayer(const SkImageInfo& info) {
#ifdef SK_SUPPORT_LEGACY_COMPATIBLEDEVICE_CONFIG
    // We call the old method to support older subclasses.
    // If they have, we return their device, else we use the new impl.
    SkBitmap::Config config = SkColorTypeToBitmapConfig(info.colorType());
    SkBaseDevice* dev = this->onCreateCompatibleDevice(config,
                                                       info.width(),
                                                       info.height(),
                                                       info.isOpaque(),
                                                       kSaveLayer_Usage);
    if (dev) {
        return dev;
    }
    // fall through to new impl
#endif
    return this->onCreateDevice(info, kSaveLayer_Usage);
}

#ifdef SK_SUPPORT_LEGACY_COMPATIBLEDEVICE_CONFIG
SkBaseDevice* SkBaseDevice::createCompatibleDevice(SkBitmap::Config config,
                                                   int width, int height,
                                                   bool isOpaque) {
    SkImageInfo info = SkImageInfo::Make(width, height,
                                         SkBitmapConfigToColorType(config),
                                         isOpaque ? kOpaque_SkAlphaType
                                                  : kPremul_SkAlphaType);
    return this->createCompatibleDevice(info);
}
#endif

SkMetaData& SkBaseDevice::getMetaData() {
    // metadata users are rare, so we lazily allocate it. If that changes we
    // can decide to just make it a field in the device (rather than a ptr)
    if (NULL == fMetaData) {
        fMetaData = new SkMetaData;
    }
    return *fMetaData;
}

// TODO: should make this guy pure-virtual.
SkImageInfo SkBaseDevice::imageInfo() const {
    return SkImageInfo::MakeUnknown(this->width(), this->height());
}

const SkBitmap& SkBaseDevice::accessBitmap(bool changePixels) {
    const SkBitmap& bitmap = this->onAccessBitmap();
    if (changePixels) {
        bitmap.notifyPixelsChanged();
    }
    return bitmap;
}

bool SkBaseDevice::readPixels(SkBitmap* bitmap, int x, int y,
                              SkCanvas::Config8888 config8888) {
    if (SkBitmap::kARGB_8888_Config != bitmap->config() ||
        NULL != bitmap->getTexture()) {
        return false;
    }

    const SkBitmap& src = this->accessBitmap(false);

    SkIRect srcRect = SkIRect::MakeXYWH(x, y, bitmap->width(),
                                              bitmap->height());
    SkIRect devbounds = SkIRect::MakeWH(src.width(), src.height());
    if (!srcRect.intersect(devbounds)) {
        return false;
    }

    SkBitmap tmp;
    SkBitmap* bmp;
    if (bitmap->isNull()) {
        if (!tmp.allocPixels(SkImageInfo::MakeN32Premul(bitmap->width(),
                                                        bitmap->height()))) {
            return false;
        }
        bmp = &tmp;
    } else {
        bmp = bitmap;
    }

    SkIRect subrect = srcRect;
    subrect.offset(-x, -y);
    SkBitmap bmpSubset;
    bmp->extractSubset(&bmpSubset, subrect);

    bool result = this->onReadPixels(bmpSubset,
                                     srcRect.fLeft,
                                     srcRect.fTop,
                                     config8888);
    if (result && bmp == &tmp) {
        tmp.swap(*bitmap);
    }
    return result;
}

SkSurface* SkBaseDevice::newSurface(const SkImageInfo&) { return NULL; }

const void* SkBaseDevice::peekPixels(SkImageInfo*, size_t*) { return NULL; }

void SkBaseDevice::drawDRRect(const SkDraw& draw, const SkRRect& outer,
                              const SkRRect& inner, const SkPaint& paint) {
    SkPath path;
    path.addRRect(outer);
    path.addRRect(inner);
    path.setFillType(SkPath::kEvenOdd_FillType);

    const SkMatrix* preMatrix = NULL;
    const bool pathIsMutable = true;
    this->drawPath(draw, path, paint, preMatrix, pathIsMutable);
}
