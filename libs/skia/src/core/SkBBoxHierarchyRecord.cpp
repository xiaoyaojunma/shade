
/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkBBoxHierarchyRecord.h"
#include "SkPictureStateTree.h"

SkBBoxHierarchyRecord::SkBBoxHierarchyRecord(const SkISize& size,
                                             uint32_t recordFlags,
                                             SkBBoxHierarchy* h)
    : INHERITED(size, recordFlags) {
    fStateTree = SkNEW(SkPictureStateTree);
    fBoundingHierarchy = h;
    fBoundingHierarchy->ref();
    fBoundingHierarchy->setClient(this);
}

void SkBBoxHierarchyRecord::handleBBox(const SkRect& bounds) {
    SkIRect r;
    bounds.roundOut(&r);
    SkPictureStateTree::Draw* draw = fStateTree->appendDraw(this->writeStream().bytesWritten());
    fBoundingHierarchy->insert(draw, r, true);
}

int SkBBoxHierarchyRecord::save(SaveFlags flags) {
    fStateTree->appendSave();
    return INHERITED::save(flags);
}

int SkBBoxHierarchyRecord::saveLayer(const SkRect* bounds, const SkPaint* paint,
                                     SaveFlags flags) {
    fStateTree->appendSaveLayer(this->writeStream().bytesWritten());
    return INHERITED::saveLayer(bounds, paint, flags);
}

void SkBBoxHierarchyRecord::restore() {
    fStateTree->appendRestore();
    INHERITED::restore();
}

bool SkBBoxHierarchyRecord::translate(SkScalar dx, SkScalar dy) {
    bool result = INHERITED::translate(dx, dy);
    fStateTree->appendTransform(getTotalMatrix());
    return result;
}

bool SkBBoxHierarchyRecord::scale(SkScalar sx, SkScalar sy) {
    bool result = INHERITED::scale(sx, sy);
    fStateTree->appendTransform(getTotalMatrix());
    return result;
}

bool SkBBoxHierarchyRecord::rotate(SkScalar degrees) {
    bool result = INHERITED::rotate(degrees);
    fStateTree->appendTransform(getTotalMatrix());
    return result;
}

bool SkBBoxHierarchyRecord::skew(SkScalar sx, SkScalar sy) {
    bool result = INHERITED::skew(sx, sy);
    fStateTree->appendTransform(getTotalMatrix());
    return result;
}

bool SkBBoxHierarchyRecord::concat(const SkMatrix& matrix) {
    bool result = INHERITED::concat(matrix);
    fStateTree->appendTransform(getTotalMatrix());
    return result;
}

void SkBBoxHierarchyRecord::setMatrix(const SkMatrix& matrix) {
    INHERITED::setMatrix(matrix);
    fStateTree->appendTransform(getTotalMatrix());
}

void SkBBoxHierarchyRecord::onClipRect(const SkRect& rect,
                                       SkRegion::Op op,
                                       ClipEdgeStyle edgeStyle) {
    fStateTree->appendClip(this->writeStream().bytesWritten());
    this->INHERITED::onClipRect(rect, op, edgeStyle);
}

void SkBBoxHierarchyRecord::onClipRegion(const SkRegion& region,
                                         SkRegion::Op op) {
    fStateTree->appendClip(this->writeStream().bytesWritten());
    this->INHERITED::onClipRegion(region, op);
}

void SkBBoxHierarchyRecord::onClipPath(const SkPath& path,
                                       SkRegion::Op op,
                                       ClipEdgeStyle edgeStyle) {
    fStateTree->appendClip(this->writeStream().bytesWritten());
    this->INHERITED::onClipPath(path, op, edgeStyle);
}

void SkBBoxHierarchyRecord::onClipRRect(const SkRRect& rrect,
                                        SkRegion::Op op,
                                        ClipEdgeStyle edgeStyle) {
    fStateTree->appendClip(this->writeStream().bytesWritten());
    this->INHERITED::onClipRRect(rrect, op, edgeStyle);
}

bool SkBBoxHierarchyRecord::shouldRewind(void* data) {
    // SkBBoxHierarchy::rewindInserts is called by SkPicture after the
    // SkPicture has rewound its command stream.  To match that rewind in the
    // BBH, we rewind all draws that reference commands that were recorded
    // past the point to which the SkPicture has rewound, which is given by
    // writeStream().bytesWritten().
    SkPictureStateTree::Draw* draw = static_cast<SkPictureStateTree::Draw*>(data);
    return draw->fOffset >= writeStream().bytesWritten();
}
