// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/download_request_limiter.h"

#include "base/bind.h"
#include "base/stl_util.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/download/download_permission_request.h"
#include "chrome/browser/permissions/permission_request_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "chrome/browser/vr/vr_tab_helper.h"
#include "components/content_settings/core/browser/content_settings_details.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/resource_dispatcher_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "url/gurl.h"

using content::BrowserThread;
using content::NavigationController;
using content::NavigationEntry;

namespace {

ContentSetting GetSettingFromStatus(
    DownloadRequestLimiter::DownloadStatus status) {
  switch (status) {
    case DownloadRequestLimiter::ALLOW_ONE_DOWNLOAD:
    case DownloadRequestLimiter::PROMPT_BEFORE_DOWNLOAD:
      return CONTENT_SETTING_ASK;
    case DownloadRequestLimiter::ALLOW_ALL_DOWNLOADS:
      return CONTENT_SETTING_ALLOW;
    case DownloadRequestLimiter::DOWNLOADS_NOT_ALLOWED:
      return CONTENT_SETTING_BLOCK;
  }
  NOTREACHED();
  return CONTENT_SETTING_DEFAULT;
}

DownloadRequestLimiter::DownloadStatus GetStatusFromSetting(
    ContentSetting setting) {
  switch (setting) {
    case CONTENT_SETTING_ALLOW:
      return DownloadRequestLimiter::ALLOW_ALL_DOWNLOADS;
    case CONTENT_SETTING_BLOCK:
      return DownloadRequestLimiter::DOWNLOADS_NOT_ALLOWED;
    case CONTENT_SETTING_DEFAULT:
    case CONTENT_SETTING_ASK:
      return DownloadRequestLimiter::PROMPT_BEFORE_DOWNLOAD;
    case CONTENT_SETTING_SESSION_ONLY:
    case CONTENT_SETTING_NUM_SETTINGS:
    case CONTENT_SETTING_DETECT_IMPORTANT_CONTENT:
      NOTREACHED();
      return DownloadRequestLimiter::PROMPT_BEFORE_DOWNLOAD;
  }
  NOTREACHED();
  return DownloadRequestLimiter::PROMPT_BEFORE_DOWNLOAD;
}

}  // namespace

// TabDownloadState ------------------------------------------------------------

DownloadRequestLimiter::TabDownloadState::TabDownloadState(
    DownloadRequestLimiter* host,
    content::WebContents* contents,
    content::WebContents* originating_web_contents)
    : content::WebContentsObserver(contents),
      web_contents_(contents),
      host_(host),
      status_(DownloadRequestLimiter::ALLOW_ONE_DOWNLOAD),
      download_count_(0),
      observer_(this),
      factory_(this) {
  observer_.Add(GetContentSettings(contents));
  NavigationEntry* last_entry =
      originating_web_contents
          ? originating_web_contents->GetController().GetLastCommittedEntry()
          : contents->GetController().GetLastCommittedEntry();
  if (last_entry)
    initial_page_host_ = last_entry->GetURL().host();
}

DownloadRequestLimiter::TabDownloadState::~TabDownloadState() {
  // We should only be destroyed after the callbacks have been notified.
  DCHECK(callbacks_.empty());

  // And we should have invalidated the back pointer.
  DCHECK(!factory_.HasWeakPtrs());
}

void DownloadRequestLimiter::TabDownloadState::SetDownloadStatusAndNotify(
    DownloadStatus status) {
  SetDownloadStatusAndNotifyImpl(status, GetSettingFromStatus(status));
}

void DownloadRequestLimiter::TabDownloadState::DidStartNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->IsInMainFrame())
    return;

  // If the navigation is renderer-initiated (but not user-initiated), ensure
  // that a prompting or blocking limiter state is not reset, so
  // window.location.href or meta refresh can't be abused to avoid the limiter.
  // User-initiated navigations will trigger DidGetUserInteraction, which resets
  // the limiter before the navigation starts.
  if (navigation_handle->IsRendererInitiated() &&
      (status_ == PROMPT_BEFORE_DOWNLOAD || status_ == DOWNLOADS_NOT_ALLOWED)) {
    return;
  }

  if (status_ == DownloadRequestLimiter::ALLOW_ALL_DOWNLOADS ||
      status_ == DownloadRequestLimiter::DOWNLOADS_NOT_ALLOWED) {
    // User has either allowed all downloads or blocked all downloads. Only
    // reset the download state if the user is navigating to a different host
    // (or host is empty).
    if (!initial_page_host_.empty() &&
        navigation_handle->GetURL().host_piece() == initial_page_host_) {
      return;
    }
  }

  NotifyCallbacks(false);
  host_->Remove(this, web_contents());
}

void DownloadRequestLimiter::TabDownloadState::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->IsInMainFrame())
    return;

  // When the status is ALLOW_ALL_DOWNLOADS or DOWNLOADS_NOT_ALLOWED, don't drop
  // this information. The user has explicitly said that they do/don't want
  // downloads from this host. If they accidentally Accepted or Canceled, they
  // can adjust the limiter state by adjusting the automatic downloads content
  // settings. Alternatively, they can copy the URL into a new tab, which will
  // make a new DownloadRequestLimiter. See also the initial_page_host_ logic in
  // DidStartNavigation.
  if (status_ == ALLOW_ONE_DOWNLOAD ||
      (status_ == PROMPT_BEFORE_DOWNLOAD &&
       !navigation_handle->IsRendererInitiated())) {
    // When the user reloads the page without responding to the infobar,
    // they are expecting DownloadRequestLimiter to behave as if they had
    // just initially navigated to this page. See http://crbug.com/171372.
    // However, explicitly leave the limiter in place if the navigation was
    // renderer-initiated and we are in a prompt state.
    NotifyCallbacks(false);
    host_->Remove(this, web_contents());
    // WARNING: We've been deleted.
  }
}

void DownloadRequestLimiter::TabDownloadState::DidGetUserInteraction(
    const blink::WebInputEvent::Type type) {
  if (is_showing_prompt() ||
      type == blink::WebInputEvent::kGestureScrollBegin) {
    // Don't change state if a prompt is showing or if the user has scrolled.
    return;
  }

  bool promptable =
      PermissionRequestManager::FromWebContents(web_contents()) != nullptr;

  // See PromptUserForDownload(): if there's no PermissionRequestManager, then
  // DOWNLOADS_NOT_ALLOWED is functionally equivalent to PROMPT_BEFORE_DOWNLOAD.
  if ((status_ != DownloadRequestLimiter::ALLOW_ALL_DOWNLOADS) &&
      (!promptable ||
       (status_ != DownloadRequestLimiter::DOWNLOADS_NOT_ALLOWED))) {
    // Revert to default status.
    host_->Remove(this, web_contents());
    // WARNING: We've been deleted.
  }
}

void DownloadRequestLimiter::TabDownloadState::WebContentsDestroyed() {
  // Tab closed, no need to handle closing the dialog as it's owned by the
  // WebContents.

  NotifyCallbacks(false);
  host_->Remove(this, web_contents());
  // WARNING: We've been deleted.
}

void DownloadRequestLimiter::TabDownloadState::PromptUserForDownload(
    const DownloadRequestLimiter::Callback& callback) {
  callbacks_.push_back(callback);
  DCHECK(web_contents_);
  if (is_showing_prompt())
    return;

  if (vr::VrTabHelper::IsInVr(web_contents_)) {
    vr::VrTabHelper::UISuppressed(vr::UiSuppressedElement::kDownloadPermission);

    // Permission request UI cannot currently be rendered binocularly in VR
    // mode, so we suppress the UI and return cancelled to inform the caller
    // that the request will not progress. crbug.com/736568
    Cancel();
    return;
  }

  PermissionRequestManager* permission_request_manager =
      PermissionRequestManager::FromWebContents(web_contents_);
  if (permission_request_manager) {
    permission_request_manager->AddRequest(
        new DownloadPermissionRequest(factory_.GetWeakPtr()));
  } else {
    Cancel();
  }
}

void DownloadRequestLimiter::TabDownloadState::SetContentSetting(
    ContentSetting setting) {
  if (!web_contents_)
    return;
  HostContentSettingsMap* settings =
      DownloadRequestLimiter::GetContentSettings(web_contents_);
  if (!settings)
    return;
  settings->SetContentSettingDefaultScope(
      web_contents_->GetURL(), GURL(),
      CONTENT_SETTINGS_TYPE_AUTOMATIC_DOWNLOADS, std::string(), setting);
}

void DownloadRequestLimiter::TabDownloadState::Cancel() {
  SetContentSetting(CONTENT_SETTING_BLOCK);
  bool throttled = NotifyCallbacks(false);
  SetDownloadStatusAndNotify(throttled ? PROMPT_BEFORE_DOWNLOAD
                                       : DOWNLOADS_NOT_ALLOWED);
}

void DownloadRequestLimiter::TabDownloadState::CancelOnce() {
  bool throttled = NotifyCallbacks(false);
  SetDownloadStatusAndNotify(throttled ? PROMPT_BEFORE_DOWNLOAD
                                       : DOWNLOADS_NOT_ALLOWED);
}

void DownloadRequestLimiter::TabDownloadState::Accept() {
  SetContentSetting(CONTENT_SETTING_ALLOW);
  bool throttled = NotifyCallbacks(true);
  SetDownloadStatusAndNotify(throttled ? PROMPT_BEFORE_DOWNLOAD
                                       : ALLOW_ALL_DOWNLOADS);
}

DownloadRequestLimiter::TabDownloadState::TabDownloadState()
    : web_contents_(NULL),
      host_(NULL),
      status_(DownloadRequestLimiter::ALLOW_ONE_DOWNLOAD),
      download_count_(0),
      observer_(this),
      factory_(this) {}

bool DownloadRequestLimiter::TabDownloadState::is_showing_prompt() const {
  return factory_.HasWeakPtrs();
}

void DownloadRequestLimiter::TabDownloadState::OnContentSettingChanged(
    const ContentSettingsPattern& primary_pattern,
    const ContentSettingsPattern& secondary_pattern,
    ContentSettingsType content_type,
    std::string resource_identifier) {
  if (content_type != CONTENT_SETTINGS_TYPE_AUTOMATIC_DOWNLOADS)
    return;

  // Analogous to TabSpecificContentSettings::OnContentSettingChanged:
  const ContentSettingsDetails details(primary_pattern, secondary_pattern,
                                       content_type, resource_identifier);
  const NavigationController& controller = web_contents()->GetController();

  // The visible NavigationEntry is the URL in the URL field of a tab.
  // Currently this should be matched by the |primary_pattern|.
  NavigationEntry* entry = controller.GetVisibleEntry();
  GURL entry_url;
  if (entry)
    entry_url = entry->GetURL();
  if (!details.update_all() && !details.primary_pattern().Matches(entry_url))
    return;

  // Content settings have been updated for our web contents, e.g. via the OIB
  // or the settings page. Check to see if the automatic downloads setting is
  // different to our internal state, and update the internal state to match if
  // necessary. If there is no content setting persisted, then retain the
  // current state and do nothing.
  //
  // NotifyCallbacks is not called as this notification should be triggered when
  // a download is not pending.
  //
  // Fetch the content settings map for this web contents, and extract the
  // automatic downloads permission value.
  HostContentSettingsMap* content_settings = GetContentSettings(web_contents());
  if (!content_settings)
    return;

  ContentSetting setting = content_settings->GetContentSetting(
      web_contents()->GetURL(), web_contents()->GetURL(),
      CONTENT_SETTINGS_TYPE_AUTOMATIC_DOWNLOADS, std::string());

  // Update the internal state to match if necessary.
  SetDownloadStatusAndNotifyImpl(GetStatusFromSetting(setting), setting);
}

bool DownloadRequestLimiter::TabDownloadState::NotifyCallbacks(bool allow) {
  std::vector<DownloadRequestLimiter::Callback> callbacks;
  bool throttled = false;

  // Selectively send first few notifications only if number of downloads exceed
  // kMaxDownloadsAtOnce. In that case, we also retain the infobar instance and
  // don't close it. If allow is false, we send all the notifications to cancel
  // all remaining downloads and close the infobar.
  if (!allow || (callbacks_.size() < kMaxDownloadsAtOnce)) {
    // Null the generated weak pointer so we don't get notified again.
    factory_.InvalidateWeakPtrs();
    callbacks.swap(callbacks_);
  } else {
    std::vector<DownloadRequestLimiter::Callback>::iterator start, end;
    start = callbacks_.begin();
    end = callbacks_.begin() + kMaxDownloadsAtOnce;
    callbacks.assign(start, end);
    callbacks_.erase(start, end);
    throttled = true;
  }

  for (const auto& callback : callbacks) {
    // When callback runs, it can cause the WebContents to be destroyed.
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                            base::BindOnce(callback, allow));
  }

  return throttled;
}

void DownloadRequestLimiter::TabDownloadState::SetDownloadStatusAndNotifyImpl(
    DownloadStatus status,
    ContentSetting setting) {
  DCHECK((GetSettingFromStatus(status) == setting) ||
         (GetStatusFromSetting(setting) == status))
      << "status " << status << " and setting " << setting
      << " do not correspond to each other";

  ContentSetting last_setting = GetSettingFromStatus(status_);
  status_ = status;
  if (!web_contents())
    return;
  if (last_setting == setting)
    return;
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_WEB_CONTENT_SETTINGS_CHANGED,
      content::Source<content::WebContents>(web_contents()),
      content::NotificationService::NoDetails());
}

// DownloadRequestLimiter ------------------------------------------------------

DownloadRequestLimiter::DownloadRequestLimiter() : factory_(this) {}

DownloadRequestLimiter::~DownloadRequestLimiter() {
  // All the tabs should have closed before us, which sends notification and
  // removes from state_map_. As such, there should be no pending callbacks.
  DCHECK(state_map_.empty());
}

DownloadRequestLimiter::DownloadStatus
DownloadRequestLimiter::GetDownloadStatus(content::WebContents* web_contents) {
  TabDownloadState* state = GetDownloadState(web_contents, NULL, false);
  return state ? state->download_status() : ALLOW_ONE_DOWNLOAD;
}

DownloadRequestLimiter::TabDownloadState*
DownloadRequestLimiter::GetDownloadState(
    content::WebContents* web_contents,
    content::WebContents* originating_web_contents,
    bool create) {
  DCHECK(web_contents);
  StateMap::iterator i = state_map_.find(web_contents);
  if (i != state_map_.end())
    return i->second;

  if (!create)
    return NULL;

  TabDownloadState* state =
      new TabDownloadState(this, web_contents, originating_web_contents);
  state_map_[web_contents] = state;
  return state;
}

void DownloadRequestLimiter::CanDownload(
    const content::ResourceRequestInfo::WebContentsGetter& web_contents_getter,
    const GURL& url,
    const std::string& request_method,
    const Callback& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  content::WebContents* originating_contents = web_contents_getter.Run();
  if (!originating_contents) {
    // The WebContents was closed, don't allow the download.
    callback.Run(false);
    return;
  }

  if (!originating_contents->GetDelegate()) {
    callback.Run(false);
    return;
  }

  // Note that because |originating_contents| might go away before
  // OnCanDownloadDecided is invoked, we look it up by |render_process_host_id|
  // and |render_view_id|.
  base::Callback<void(bool)> can_download_callback = base::Bind(
      &DownloadRequestLimiter::OnCanDownloadDecided, factory_.GetWeakPtr(),
      web_contents_getter, request_method, callback);

  originating_contents->GetDelegate()->CanDownload(url, request_method,
                                                   can_download_callback);
}

void DownloadRequestLimiter::OnCanDownloadDecided(
    const content::ResourceRequestInfo::WebContentsGetter& web_contents_getter,
    const std::string& request_method,
    const Callback& orig_callback,
    bool allow) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  content::WebContents* originating_contents = web_contents_getter.Run();
  if (!originating_contents || !allow) {
    orig_callback.Run(false);
    return;
  }

  CanDownloadImpl(originating_contents, request_method, orig_callback);
}

HostContentSettingsMap* DownloadRequestLimiter::GetContentSettings(
    content::WebContents* contents) {
  return HostContentSettingsMapFactory::GetForProfile(
      Profile::FromBrowserContext(contents->GetBrowserContext()));
}

void DownloadRequestLimiter::CanDownloadImpl(
    content::WebContents* originating_contents,
    const std::string& request_method,
    const Callback& callback) {
  DCHECK(originating_contents);

  TabDownloadState* state =
      GetDownloadState(originating_contents, originating_contents, true);
  switch (state->download_status()) {
    case ALLOW_ALL_DOWNLOADS:
      if (state->download_count() &&
          !(state->download_count() %
            DownloadRequestLimiter::kMaxDownloadsAtOnce))
        state->SetDownloadStatusAndNotify(PROMPT_BEFORE_DOWNLOAD);
      callback.Run(true);
      state->increment_download_count();
      break;

    case ALLOW_ONE_DOWNLOAD:
      state->SetDownloadStatusAndNotify(PROMPT_BEFORE_DOWNLOAD);
      callback.Run(true);
      state->increment_download_count();
      break;

    case DOWNLOADS_NOT_ALLOWED:
      callback.Run(false);
      break;

    case PROMPT_BEFORE_DOWNLOAD: {
      HostContentSettingsMap* content_settings =
          GetContentSettings(originating_contents);
      ContentSetting setting = CONTENT_SETTING_ASK;
      if (content_settings)
        setting = content_settings->GetContentSetting(
            originating_contents->GetURL(), originating_contents->GetURL(),
            CONTENT_SETTINGS_TYPE_AUTOMATIC_DOWNLOADS, std::string());
      switch (setting) {
        case CONTENT_SETTING_ALLOW: {
          state->SetDownloadStatusAndNotify(ALLOW_ALL_DOWNLOADS);
          callback.Run(true);
          state->increment_download_count();
          return;
        }
        case CONTENT_SETTING_BLOCK: {
          state->SetDownloadStatusAndNotify(DOWNLOADS_NOT_ALLOWED);
          callback.Run(false);
          return;
        }
        case CONTENT_SETTING_DEFAULT:
        case CONTENT_SETTING_ASK:
          state->PromptUserForDownload(callback);
          state->increment_download_count();
          break;
        case CONTENT_SETTING_SESSION_ONLY:
        case CONTENT_SETTING_NUM_SETTINGS:
        default:
          NOTREACHED();
          return;
      }
      break;
    }

    default:
      NOTREACHED();
  }
}

void DownloadRequestLimiter::Remove(TabDownloadState* state,
                                    content::WebContents* contents) {
  DCHECK(base::ContainsKey(state_map_, contents));
  state_map_.erase(contents);
  delete state;
}
