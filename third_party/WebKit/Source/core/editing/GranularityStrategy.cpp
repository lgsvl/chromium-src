// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/editing/GranularityStrategy.h"

#include "core/editing/EditingUtilities.h"
#include "core/editing/FrameSelection.h"
#include "core/editing/SelectionTemplate.h"
#include "core/editing/VisiblePosition.h"
#include "core/editing/VisibleSelection.h"
#include "core/editing/VisibleUnits.h"

namespace blink {

enum class BoundAdjust { kCurrentPosIfOnBound, kNextBoundIfOnBound };
enum class SearchDirection { kSearchBackwards, kSearchForward };

// We use the bottom-left corner of the selection rect to represent the
// location of a VisiblePosition. This way locations corresponding to
// VisiblePositions on the same line will all have the same y coordinate
// unless the text is transformed.
static IntPoint PositionLocation(const VisiblePosition& vp) {
  return AbsoluteSelectionBoundsOf(vp).MinXMaxYCorner();
}

// Order is specified using the same contract as comparePositions.
static bool ArePositionsInSpecifiedOrder(const VisiblePosition& vp1,
                                         const VisiblePosition& vp2,
                                         int specified_order) {
  int position_order = ComparePositions(vp1, vp2);
  if (specified_order == 0)
    return position_order == 0;
  return specified_order > 0 ? position_order > 0 : position_order < 0;
}

// Returns the next word boundary starting from |pos|. |direction| specifies
// the direction in which to search for the next bound. nextIfOnBound
// controls whether |pos| or the next boundary is returned when |pos| is
// located exactly on word boundary.
static VisiblePosition NextWordBound(const VisiblePosition& pos,
                                     SearchDirection direction,
                                     BoundAdjust word_bound_adjust) {
  bool next_bound_if_on_bound =
      word_bound_adjust == BoundAdjust::kNextBoundIfOnBound;
  if (direction == SearchDirection::kSearchForward) {
    EWordSide word_side = next_bound_if_on_bound ? kNextWordIfOnBoundary
                                                 : kPreviousWordIfOnBoundary;
    return EndOfWord(pos, word_side);
  }
  EWordSide word_side = next_bound_if_on_bound ? kPreviousWordIfOnBoundary
                                               : kNextWordIfOnBoundary;
  return StartOfWord(pos, word_side);
}

GranularityStrategy::GranularityStrategy() {}

GranularityStrategy::~GranularityStrategy() {}

CharacterGranularityStrategy::CharacterGranularityStrategy() {}

CharacterGranularityStrategy::~CharacterGranularityStrategy() {}

SelectionStrategy CharacterGranularityStrategy::GetType() const {
  return SelectionStrategy::kCharacter;
}

void CharacterGranularityStrategy::Clear() {}

SelectionInDOMTree CharacterGranularityStrategy::UpdateExtent(
    const IntPoint& extent_point,
    LocalFrame* frame) {
  const VisiblePosition& extent_position =
      VisiblePositionForContentsPoint(extent_point, frame);
  const VisibleSelection& selection =
      frame->Selection().ComputeVisibleSelectionInDOMTree();
  if (extent_position.IsNull() || selection.VisibleBase().DeepEquivalent() ==
                                      extent_position.DeepEquivalent())
    return selection.AsSelection();
  return SelectionInDOMTree::Builder()
      .Collapse(selection.Base())
      .Extend(extent_position.DeepEquivalent())
      .SetAffinity(selection.Affinity())
      .Build();
}

DirectionGranularityStrategy::DirectionGranularityStrategy()
    : state_(StrategyState::kCleared),
      granularity_(TextGranularity::kCharacter),
      offset_(0) {}

DirectionGranularityStrategy::~DirectionGranularityStrategy() {}

SelectionStrategy DirectionGranularityStrategy::GetType() const {
  return SelectionStrategy::kDirection;
}

void DirectionGranularityStrategy::Clear() {
  state_ = StrategyState::kCleared;
  granularity_ = TextGranularity::kCharacter;
  offset_ = 0;
  diff_extent_point_from_extent_position_ = IntSize();
}

SelectionInDOMTree DirectionGranularityStrategy::UpdateExtent(
    const IntPoint& extent_point,
    LocalFrame* frame) {
  const VisibleSelection& selection =
      frame->Selection().ComputeVisibleSelectionInDOMTree();

  if (state_ == StrategyState::kCleared)
    state_ = StrategyState::kExpanding;

  VisiblePosition old_offset_extent_position = selection.VisibleExtent();
  IntPoint old_extent_location = PositionLocation(old_offset_extent_position);

  IntPoint old_offset_extent_point =
      old_extent_location + diff_extent_point_from_extent_position_;
  IntPoint old_extent_point = IntPoint(old_offset_extent_point.X() - offset_,
                                       old_offset_extent_point.Y());

  // Apply the offset.
  IntPoint new_offset_extent_point = extent_point;
  int dx = extent_point.X() - old_extent_point.X();
  if (offset_ != 0) {
    if (offset_ > 0 && dx > 0)
      offset_ = std::max(0, offset_ - dx);
    else if (offset_ < 0 && dx < 0)
      offset_ = std::min(0, offset_ - dx);
    new_offset_extent_point.Move(offset_, 0);
  }

  VisiblePosition new_offset_extent_position =
      VisiblePositionForContentsPoint(new_offset_extent_point, frame);
  if (new_offset_extent_position.IsNull())
    return selection.AsSelection();
  IntPoint new_offset_location = PositionLocation(new_offset_extent_position);

  // Reset the offset in case of a vertical change in the location (could be
  // due to a line change or due to an unusual layout, e.g. rotated text).
  bool vertical_change = new_offset_location.Y() != old_extent_location.Y();
  if (vertical_change) {
    offset_ = 0;
    granularity_ = TextGranularity::kCharacter;
    new_offset_extent_point = extent_point;
    new_offset_extent_position =
        VisiblePositionForContentsPoint(extent_point, frame);
  }

  const VisiblePosition base = selection.VisibleBase();

  // Do not allow empty selection.
  if (new_offset_extent_position.DeepEquivalent() == base.DeepEquivalent())
    return selection.AsSelection();

  // The direction granularity strategy, particularly the "offset" feature
  // doesn't work with non-horizontal text (e.g. when the text is rotated).
  // So revert to the behavior equivalent to the character granularity
  // strategy if we detect that the text's baseline coordinate changed
  // without a line change.
  if (vertical_change &&
      InSameLine(new_offset_extent_position, old_offset_extent_position)) {
    return SelectionInDOMTree::Builder()
        .Collapse(selection.Base())
        .Extend(new_offset_extent_position.DeepEquivalent())
        .SetAffinity(selection.Affinity())
        .Build();
  }

  int old_extent_base_order = selection.IsBaseFirst() ? 1 : -1;

  int new_extent_base_order;
  bool this_move_shrunk_selection;
  if (new_offset_extent_position.DeepEquivalent() ==
      old_offset_extent_position.DeepEquivalent()) {
    if (granularity_ == TextGranularity::kCharacter)
      return selection.AsSelection();

    // If we are in Word granularity, we cannot exit here, since we may pass
    // the middle of the word without changing the position (in which case
    // the selection needs to expand).
    this_move_shrunk_selection = false;
    new_extent_base_order = old_extent_base_order;
  } else {
    bool selection_expanded = ArePositionsInSpecifiedOrder(
        new_offset_extent_position, old_offset_extent_position,
        old_extent_base_order);
    bool extent_base_order_switched =
        selection_expanded
            ? false
            : !ArePositionsInSpecifiedOrder(new_offset_extent_position, base,
                                            old_extent_base_order);
    new_extent_base_order = extent_base_order_switched ? -old_extent_base_order
                                                       : old_extent_base_order;

    // Determine the word boundary, i.e. the boundary extending beyond which
    // should change the granularity to WordGranularity.
    VisiblePosition word_boundary;
    if (extent_base_order_switched) {
      // Special case.
      // If the extent-base order was switched, then the selection is now
      // expanding in a different direction than before. Therefore we
      // calculate the word boundary in this new direction and based on
      // the |base| position.
      word_boundary = NextWordBound(base,
                                    new_extent_base_order > 0
                                        ? SearchDirection::kSearchForward
                                        : SearchDirection::kSearchBackwards,
                                    BoundAdjust::kNextBoundIfOnBound);
      granularity_ = TextGranularity::kCharacter;
    } else {
      // Calculate the word boundary based on |oldExtentWithGranularity|.
      // If selection was shrunk in the last update and the extent is now
      // exactly on the word boundary - we need to take the next bound as
      // the bound of the current word.
      word_boundary = NextWordBound(old_offset_extent_position,
                                    old_extent_base_order > 0
                                        ? SearchDirection::kSearchForward
                                        : SearchDirection::kSearchBackwards,
                                    state_ == StrategyState::kShrinking
                                        ? BoundAdjust::kNextBoundIfOnBound
                                        : BoundAdjust::kCurrentPosIfOnBound);
    }

    bool expanded_beyond_word_boundary;
    if (selection_expanded)
      expanded_beyond_word_boundary = ArePositionsInSpecifiedOrder(
          new_offset_extent_position, word_boundary, new_extent_base_order);
    else if (extent_base_order_switched)
      expanded_beyond_word_boundary = ArePositionsInSpecifiedOrder(
          new_offset_extent_position, word_boundary, new_extent_base_order);
    else
      expanded_beyond_word_boundary = false;

    // The selection is shrunk if the extent changes position to be closer to
    // the base, and the extent/base order wasn't switched.
    this_move_shrunk_selection =
        !extent_base_order_switched && !selection_expanded;

    if (expanded_beyond_word_boundary)
      granularity_ = TextGranularity::kWord;
    else if (this_move_shrunk_selection)
      granularity_ = TextGranularity::kCharacter;
  }

  VisiblePosition new_selection_extent = new_offset_extent_position;
  if (granularity_ == TextGranularity::kWord) {
    // Determine the bounds of the word where the extent is located.
    // Set the selection extent to one of the two bounds depending on
    // whether the extent is passed the middle of the word.
    VisiblePosition bound_before_extent = NextWordBound(
        new_offset_extent_position, SearchDirection::kSearchBackwards,
        BoundAdjust::kCurrentPosIfOnBound);
    VisiblePosition bound_after_extent = NextWordBound(
        new_offset_extent_position, SearchDirection::kSearchForward,
        BoundAdjust::kCurrentPosIfOnBound);
    int x_middle_between_bounds = (PositionLocation(bound_after_extent).X() +
                                   PositionLocation(bound_before_extent).X()) /
                                  2;
    bool offset_extent_before_middle =
        new_offset_extent_point.X() < x_middle_between_bounds;
    new_selection_extent =
        offset_extent_before_middle ? bound_before_extent : bound_after_extent;
    // Update the offset if selection expanded in word granularity.
    if (new_selection_extent.DeepEquivalent() !=
            selection.VisibleExtent().DeepEquivalent() &&
        ((new_extent_base_order > 0 && !offset_extent_before_middle) ||
         (new_extent_base_order < 0 && offset_extent_before_middle))) {
      offset_ = PositionLocation(new_selection_extent).X() - extent_point.X();
    }
  }

  // Only update the state if the selection actually changed as a result of
  // this move.
  if (new_selection_extent.DeepEquivalent() !=
      selection.VisibleExtent().DeepEquivalent())
    state_ = this_move_shrunk_selection ? StrategyState::kShrinking
                                        : StrategyState::kExpanding;

  diff_extent_point_from_extent_position_ =
      extent_point + IntSize(offset_, 0) -
      PositionLocation(new_selection_extent);
  return SelectionInDOMTree::Builder(selection.AsSelection())
      .Collapse(selection.Base())
      .Extend(new_selection_extent.DeepEquivalent())
      .Build();
}

}  // namespace blink
