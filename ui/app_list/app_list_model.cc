// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/app_list/app_list_model.h"

#include <string>
#include <utility>

#include "base/metrics/histogram_macros.h"
#include "ui/app_list/app_list_features.h"
#include "ui/app_list/app_list_folder_item.h"
#include "ui/app_list/app_list_item.h"
#include "ui/app_list/app_list_model_observer.h"
#include "ui/app_list/search_box_model.h"

namespace app_list {

AppListModel::AppListModel()
    : top_level_item_list_(new AppListItemList),
      search_box_(new SearchBoxModel),
      results_(new SearchResults),
      status_(STATUS_NORMAL),
      state_(INVALID_STATE),
      state_fullscreen_(AppListView::CLOSED),
      folders_enabled_(false),
      custom_launcher_page_enabled_(true),
      search_engine_is_google_(false),
      is_tablet_mode_(false) {
  top_level_item_list_->AddObserver(this);
}

AppListModel::~AppListModel() {
  top_level_item_list_->RemoveObserver(this);
}

void AppListModel::AddObserver(AppListModelObserver* observer) {
  observers_.AddObserver(observer);
}

void AppListModel::RemoveObserver(AppListModelObserver* observer) {
  observers_.RemoveObserver(observer);
}

void AppListModel::SetStatus(Status status) {
  if (status_ == status)
    return;

  status_ = status;
  for (auto& observer : observers_)
    observer.OnAppListModelStatusChanged();
}

void AppListModel::SetState(State state) {
  if (state_ == state)
    return;

  State old_state = state_;

  state_ = state;

  for (auto& observer : observers_)
    observer.OnAppListModelStateChanged(old_state, state_);
}

void AppListModel::SetStateFullscreen(AppListView::AppListState state) {
  state_fullscreen_ = state;
}

void AppListModel::SetTabletMode(bool started) {
  is_tablet_mode_ = started;
}

AppListItem* AppListModel::FindItem(const std::string& id) {
  AppListItem* item = top_level_item_list_->FindItem(id);
  if (item)
    return item;
  for (size_t i = 0; i < top_level_item_list_->item_count(); ++i) {
    AppListItem* child_item =
        top_level_item_list_->item_at(i)->FindChildItem(id);
    if (child_item)
      return child_item;
  }
  return NULL;
}

AppListFolderItem* AppListModel::FindFolderItem(const std::string& id) {
  AppListItem* item = top_level_item_list_->FindItem(id);
  if (item && item->GetItemType() == AppListFolderItem::kItemType)
    return static_cast<AppListFolderItem*>(item);
  DCHECK(!item);
  return NULL;
}

AppListItem* AppListModel::AddItem(std::unique_ptr<AppListItem> item) {
  DCHECK(!item->IsInFolder());
  DCHECK(!top_level_item_list()->FindItem(item->id()));
  return AddItemToItemListAndNotify(std::move(item));
}

AppListItem* AppListModel::AddItemToFolder(std::unique_ptr<AppListItem> item,
                                           const std::string& folder_id) {
  if (folder_id.empty())
    return AddItem(std::move(item));
  DVLOG(2) << "AddItemToFolder: " << item->id() << ": " << folder_id;
  CHECK_NE(folder_id, item->folder_id());
  DCHECK_NE(AppListFolderItem::kItemType, item->GetItemType());
  AppListFolderItem* dest_folder = FindOrCreateFolderItem(folder_id);
  if (!dest_folder)
    return NULL;
  DCHECK(!dest_folder->item_list()->FindItem(item->id()))
      << "Already in folder: " << dest_folder->id();
  return AddItemToFolderItemAndNotify(dest_folder, std::move(item));
}

const std::string AppListModel::MergeItems(const std::string& target_item_id,
                                           const std::string& source_item_id) {
  if (!folders_enabled()) {
    LOG(ERROR) << "MergeItems called with folders disabled.";
    return "";
  }
  DVLOG(2) << "MergeItems: " << source_item_id << " -> " << target_item_id;

  if (target_item_id == source_item_id) {
    LOG(WARNING) << "MergeItems tried to drop item onto itself ("
                 << source_item_id << " -> " << target_item_id << ").";
    return "";
  }

  // Find the target item.
  AppListItem* target_item = top_level_item_list_->FindItem(target_item_id);
  if (!target_item) {
    LOG(ERROR) << "MergeItems: Target no longer exists.";
    return "";
  }

  AppListItem* source_item = FindItem(source_item_id);
  if (!source_item) {
    LOG(ERROR) << "MergeItems: Source no longer exists.";
    return "";
  }

  // If the target item is a folder, just add the source item to it.
  if (target_item->GetItemType() == AppListFolderItem::kItemType) {
    AppListFolderItem* target_folder =
        static_cast<AppListFolderItem*>(target_item);
    if (target_folder->folder_type() == AppListFolderItem::FOLDER_TYPE_OEM) {
      LOG(WARNING) << "MergeItems called with OEM folder as target";
      return "";
    }
    std::unique_ptr<AppListItem> source_item_ptr = RemoveItem(source_item);
    source_item_ptr->set_position(
        target_folder->item_list()->CreatePositionBefore(
            syncer::StringOrdinal()));
    AddItemToFolderItemAndNotify(target_folder, std::move(source_item_ptr));
    return target_folder->id();
  }

  // Otherwise remove the source item and target item from their current
  // location, they will become owned by the new folder.
  std::unique_ptr<AppListItem> source_item_ptr = RemoveItem(source_item);
  CHECK(source_item_ptr);
  // Note: This would fail if |target_item_id == source_item_id|, except we
  // checked that they are distinct at the top of this method.
  std::unique_ptr<AppListItem> target_item_ptr =
      top_level_item_list_->RemoveItem(target_item_id);
  CHECK(target_item_ptr);

  // Create a new folder in the same location as the target item.
  std::string new_folder_id = AppListFolderItem::GenerateId();
  DVLOG(2) << "Creating folder for merge: " << new_folder_id;
  std::unique_ptr<AppListItem> new_folder_ptr(new AppListFolderItem(
      new_folder_id, AppListFolderItem::FOLDER_TYPE_NORMAL));
  new_folder_ptr->set_position(target_item_ptr->position());
  AppListFolderItem* new_folder = static_cast<AppListFolderItem*>(
      AddItemToItemListAndNotify(std::move(new_folder_ptr)));

  // Add the items to the new folder.
  target_item_ptr->set_position(
      new_folder->item_list()->CreatePositionBefore(syncer::StringOrdinal()));
  AddItemToFolderItemAndNotify(new_folder, std::move(target_item_ptr));
  source_item_ptr->set_position(
      new_folder->item_list()->CreatePositionBefore(syncer::StringOrdinal()));
  AddItemToFolderItemAndNotify(new_folder, std::move(source_item_ptr));

  return new_folder->id();
}

void AppListModel::MoveItemToFolder(AppListItem* item,
                                    const std::string& folder_id) {
  DVLOG(2) << "MoveItemToFolder: " << folder_id << " <- "
           << item->ToDebugString();
  if (item->folder_id() == folder_id)
    return;
  AppListFolderItem* dest_folder = FindOrCreateFolderItem(folder_id);
  std::unique_ptr<AppListItem> item_ptr = RemoveItem(item);
  if (dest_folder) {
    CHECK(!item->IsInFolder());
    AddItemToFolderItemAndNotify(dest_folder, std::move(item_ptr));
  } else {
    AddItemToItemListAndNotifyUpdate(std::move(item_ptr));
  }
}

bool AppListModel::MoveItemToFolderAt(AppListItem* item,
                                      const std::string& folder_id,
                                      syncer::StringOrdinal position) {
  DVLOG(2) << "MoveItemToFolderAt: " << folder_id << "["
           << position.ToDebugString() << "]"
           << " <- " << item->ToDebugString();
  if (item->folder_id() == folder_id)
    return false;
  AppListFolderItem* src_folder = FindOrCreateFolderItem(item->folder_id());
  if (src_folder &&
      src_folder->folder_type() == AppListFolderItem::FOLDER_TYPE_OEM) {
    LOG(WARNING) << "MoveItemToFolderAt called with OEM folder as source";
    return false;
  }
  AppListFolderItem* dest_folder = FindOrCreateFolderItem(folder_id);
  std::unique_ptr<AppListItem> item_ptr = RemoveItem(item);
  if (dest_folder) {
    item_ptr->set_position(
        dest_folder->item_list()->CreatePositionBefore(position));
    AddItemToFolderItemAndNotify(dest_folder, std::move(item_ptr));
  } else {
    item_ptr->set_position(
        top_level_item_list_->CreatePositionBefore(position));
    AddItemToItemListAndNotifyUpdate(std::move(item_ptr));
  }
  return true;
}

void AppListModel::SetItemPosition(AppListItem* item,
                                   const syncer::StringOrdinal& new_position) {
  if (!item->IsInFolder()) {
    top_level_item_list_->SetItemPosition(item, new_position);
    // Note: this will trigger OnListItemMoved which will signal observers.
    // (This is done this way because some View code still moves items within
    // the item list directly).
    return;
  }
  AppListFolderItem* folder = FindFolderItem(item->folder_id());
  DCHECK(folder);
  folder->item_list()->SetItemPosition(item, new_position);
  for (auto& observer : observers_)
    observer.OnAppListItemUpdated(item);
}

void AppListModel::SetItemName(AppListItem* item, const std::string& name) {
  item->SetName(name);
  DVLOG(2) << "AppListModel::SetItemName: " << item->ToDebugString();
  for (auto& observer : observers_)
    observer.OnAppListItemUpdated(item);
}

void AppListModel::SetItemNameAndShortName(AppListItem* item,
                                           const std::string& name,
                                           const std::string& short_name) {
  item->SetNameAndShortName(name, short_name);
  DVLOG(2) << "AppListModel::SetItemNameAndShortName: "
           << item->ToDebugString();
  for (auto& observer : observers_)
    observer.OnAppListItemUpdated(item);
}

void AppListModel::DeleteItem(const std::string& id) {
  AppListItem* item = FindItem(id);
  if (!item)
    return;
  if (!item->IsInFolder()) {
    DCHECK_EQ(0u, item->ChildItemCount())
        << "Invalid call to DeleteItem for item with children: " << id;
    for (auto& observer : observers_)
      observer.OnAppListItemWillBeDeleted(item);
    top_level_item_list_->DeleteItem(id);
    for (auto& observer : observers_)
      observer.OnAppListItemDeleted();
    return;
  }
  AppListFolderItem* folder = FindFolderItem(item->folder_id());
  DCHECK(folder) << "Folder not found for item: " << item->ToDebugString();
  std::unique_ptr<AppListItem> child_item = RemoveItemFromFolder(folder, item);
  DCHECK_EQ(item, child_item.get());
  for (auto& observer : observers_)
    observer.OnAppListItemWillBeDeleted(item);
  child_item.reset();  // Deletes item.
  for (auto& observer : observers_)
    observer.OnAppListItemDeleted();
}

void AppListModel::DeleteUninstalledItem(const std::string& id) {
  AppListItem* item = FindItem(id);
  if (!item)
    return;
  const std::string folder_id = item->folder_id();
  DeleteItem(id);

  // crbug.com/368111: Upon uninstall of 2nd-to-last folder item, reparent last
  // item to top; this will remove the folder.
  AppListFolderItem* folder = FindFolderItem(folder_id);
  if (folder && folder->ChildItemCount() == 1u) {
    AppListItem* last_item = folder->item_list()->item_at(0);
    MoveItemToFolderAt(last_item, "", folder->position());
  }
}

void AppListModel::SetFoldersEnabled(bool folders_enabled) {
  folders_enabled_ = folders_enabled;
  if (folders_enabled)
    return;
  // Remove child items from folders.
  std::vector<std::string> folder_ids;
  for (size_t i = 0; i < top_level_item_list_->item_count(); ++i) {
    AppListItem* item = top_level_item_list_->item_at(i);
    if (item->GetItemType() != AppListFolderItem::kItemType)
      continue;
    AppListFolderItem* folder = static_cast<AppListFolderItem*>(item);
    if (folder->folder_type() == AppListFolderItem::FOLDER_TYPE_OEM)
      continue;  // Do not remove OEM folders.
    while (folder->item_list()->item_count()) {
      std::unique_ptr<AppListItem> child = folder->item_list()->RemoveItemAt(0);
      child->set_folder_id("");
      AddItemToItemListAndNotifyUpdate(std::move(child));
    }
    folder_ids.push_back(folder->id());
  }
  // Delete folders.
  for (size_t i = 0; i < folder_ids.size(); ++i)
    DeleteItem(folder_ids[i]);
}

void AppListModel::RecordFolderMetrics() {
  int number_of_apps_in_folders = 0;
  int number_of_folders = 0;
  for (size_t i = 0; i < top_level_item_list_->item_count(); ++i) {
    AppListItem* item = top_level_item_list_->item_at(i);
    if (item->GetItemType() != AppListFolderItem::kItemType)
      continue;
    ++number_of_folders;
    AppListFolderItem* folder = static_cast<AppListFolderItem*>(item);
    if (folder->folder_type() == AppListFolderItem::FOLDER_TYPE_OEM)
      continue;  // Don't count items in OEM folders.
    number_of_apps_in_folders += folder->item_list()->item_count();
  }
  UMA_HISTOGRAM_COUNTS_100(kNumberOfFoldersHistogram, number_of_folders);
  UMA_HISTOGRAM_COUNTS_100(kNumberOfAppsInFoldersHistogram,
                           number_of_apps_in_folders);
}

void AppListModel::SetCustomLauncherPageEnabled(bool enabled) {
  custom_launcher_page_enabled_ = enabled;
  for (auto& observer : observers_)
    observer.OnCustomLauncherPageEnabledStateChanged(enabled);
}

std::vector<SearchResult*> AppListModel::FilterSearchResultsByDisplayType(
    SearchResults* results,
    SearchResult::DisplayType display_type,
    size_t max_results) {
  std::vector<SearchResult*> matches;
  for (size_t i = 0; i < results->item_count(); ++i) {
    SearchResult* item = results->GetItemAt(i);
    if (item->display_type() == display_type) {
      matches.push_back(item);
      if (matches.size() == max_results)
        break;
    }
  }
  return matches;
}

void AppListModel::PushCustomLauncherPageSubpage() {
  custom_launcher_page_subpage_depth_++;
}

bool AppListModel::PopCustomLauncherPageSubpage() {
  if (custom_launcher_page_subpage_depth_ == 0)
    return false;

  --custom_launcher_page_subpage_depth_;
  return true;
}

void AppListModel::ClearCustomLauncherPageSubpages() {
  custom_launcher_page_subpage_depth_ = 0;
}

void AppListModel::SetSearchEngineIsGoogle(bool is_google) {
  if (search_engine_is_google_ == is_google)
    return;

  search_engine_is_google_ = is_google;
  for (auto& observer : observers_)
    observer.OnSearchEngineIsGoogleChanged(is_google);
}

// Private methods

void AppListModel::OnListItemMoved(size_t from_index,
                                   size_t to_index,
                                   AppListItem* item) {
  for (auto& observer : observers_)
    observer.OnAppListItemUpdated(item);
}

AppListFolderItem* AppListModel::FindOrCreateFolderItem(
    const std::string& folder_id) {
  if (folder_id.empty())
    return NULL;

  AppListFolderItem* dest_folder = FindFolderItem(folder_id);
  if (dest_folder)
    return dest_folder;

  if (!folders_enabled()) {
    LOG(ERROR) << "Attempt to create folder item when disabled: " << folder_id;
    return NULL;
  }

  DVLOG(2) << "Creating new folder: " << folder_id;
  std::unique_ptr<AppListFolderItem> new_folder(
      new AppListFolderItem(folder_id, AppListFolderItem::FOLDER_TYPE_NORMAL));
  new_folder->set_position(
      top_level_item_list_->CreatePositionBefore(syncer::StringOrdinal()));
  AppListItem* new_folder_item =
      AddItemToItemListAndNotify(std::move(new_folder));
  return static_cast<AppListFolderItem*>(new_folder_item);
}

AppListItem* AppListModel::AddItemToItemListAndNotify(
    std::unique_ptr<AppListItem> item_ptr) {
  DCHECK(!item_ptr->IsInFolder());
  AppListItem* item = top_level_item_list_->AddItem(std::move(item_ptr));
  for (auto& observer : observers_)
    observer.OnAppListItemAdded(item);
  return item;
}

AppListItem* AppListModel::AddItemToItemListAndNotifyUpdate(
    std::unique_ptr<AppListItem> item_ptr) {
  DCHECK(!item_ptr->IsInFolder());
  AppListItem* item = top_level_item_list_->AddItem(std::move(item_ptr));
  for (auto& observer : observers_)
    observer.OnAppListItemUpdated(item);
  return item;
}

AppListItem* AppListModel::AddItemToFolderItemAndNotify(
    AppListFolderItem* folder,
    std::unique_ptr<AppListItem> item_ptr) {
  CHECK_NE(folder->id(), item_ptr->folder_id());
  AppListItem* item = folder->item_list()->AddItem(std::move(item_ptr));
  item->set_folder_id(folder->id());
  for (auto& observer : observers_)
    observer.OnAppListItemUpdated(item);
  return item;
}

std::unique_ptr<AppListItem> AppListModel::RemoveItem(AppListItem* item) {
  if (!item->IsInFolder())
    return top_level_item_list_->RemoveItem(item->id());

  AppListFolderItem* folder = FindFolderItem(item->folder_id());
  return RemoveItemFromFolder(folder, item);
}

std::unique_ptr<AppListItem> AppListModel::RemoveItemFromFolder(
    AppListFolderItem* folder,
    AppListItem* item) {
  std::string folder_id = folder->id();
  CHECK_EQ(item->folder_id(), folder_id);
  std::unique_ptr<AppListItem> result =
      folder->item_list()->RemoveItem(item->id());
  result->set_folder_id("");
  if (folder->item_list()->item_count() == 0) {
    DVLOG(2) << "Deleting empty folder: " << folder->ToDebugString();
    DeleteItem(folder_id);
  }
  return result;
}

}  // namespace app_list
