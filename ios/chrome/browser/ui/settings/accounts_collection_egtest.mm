// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <EarlGrey/EarlGrey.h>
#import <XCTest/XCTest.h>

#include "base/strings/sys_string_conversions.h"
#include "components/signin/core/browser/signin_manager.h"
#include "components/strings/grit/components_strings.h"
#import "ios/chrome/browser/signin/authentication_service.h"
#import "ios/chrome/browser/signin/authentication_service_factory.h"
#include "ios/chrome/browser/signin/signin_manager_factory.h"
#import "ios/chrome/browser/ui/authentication/account_control_item.h"
#import "ios/chrome/browser/ui/authentication/signin_earlgrey_utils.h"
#include "ios/chrome/browser/ui/tools_menu/tools_menu_constants.h"
#include "ios/chrome/grit/ios_strings.h"
#import "ios/chrome/test/app/chrome_test_util.h"
#import "ios/chrome/test/earl_grey/chrome_earl_grey_ui.h"
#import "ios/chrome/test/earl_grey/chrome_matchers.h"
#import "ios/chrome/test/earl_grey/chrome_test_case.h"
#import "ios/public/provider/chrome/browser/signin/fake_chrome_identity.h"
#import "ios/public/provider/chrome/browser/signin/fake_chrome_identity_service.h"
#include "ui/base/l10n/l10n_util.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

using chrome_test_util::AccountConsistencyConfirmationOkButton;
using chrome_test_util::AccountConsistencySetupSigninButton;
using chrome_test_util::AccountsSyncButton;
using chrome_test_util::ButtonWithAccessibilityLabel;
using chrome_test_util::NavigationBarDoneButton;
using chrome_test_util::SettingsAccountButton;
using chrome_test_util::SignOutAccountsButton;
using chrome_test_util::PrimarySignInButton;
using chrome_test_util::SecondarySignInButton;

namespace {

// Accepts account consistency popup prompts after signing in an account via the
// UI. Should be called right after signing in an account.
void AcceptAccountConsistencyPopup() {
  [[EarlGrey selectElementWithMatcher:AccountConsistencySetupSigninButton()]
      performAction:grey_tap()];

  // The "MORE" button shows up on screens where all content can't be shown, and
  // must be tapped to bring up the confirmation button.
  NSError* error = nil;
  [[EarlGrey selectElementWithMatcher:grey_accessibilityLabel(@"MORE")]
      assertWithMatcher:grey_notNil()
                  error:&error];
  if (error == nil) {
    [[EarlGrey selectElementWithMatcher:grey_accessibilityLabel(@"MORE")]
        performAction:grey_tap()];
  }

  [[EarlGrey selectElementWithMatcher:AccountConsistencyConfirmationOkButton()]
      performAction:grey_tap()];
}

// Returns a matcher for a button that matches the userEmail in the given
// |identity|.
id<GREYMatcher> ButtonWithIdentity(ChromeIdentity* identity) {
  return ButtonWithAccessibilityLabel(identity.userEmail);
}
}

// Integration tests using the Account Settings screen.
@interface AccountCollectionsTestCase : ChromeTestCase
@end

@implementation AccountCollectionsTestCase

// Tests that the Sync and Account Settings screen are correctly popped if the
// signed in account is removed.
- (void)testSignInPopUpAccountOnSyncSettings {
  ChromeIdentity* identity = [SigninEarlGreyUtils fakeIdentity1];
  ios::FakeChromeIdentityService::GetInstanceFromChromeProvider()->AddIdentity(
      identity);

  // Sign In |identity|, then open the Sync Settings.
  [ChromeEarlGreyUI openSettingsMenu];
  [ChromeEarlGreyUI tapSettingsMenuButton:SecondarySignInButton()];
  [[EarlGrey selectElementWithMatcher:ButtonWithIdentity(identity)]
      performAction:grey_tap()];
  AcceptAccountConsistencyPopup();
  [SigninEarlGreyUtils assertSignedInWithIdentity:identity];
  [ChromeEarlGreyUI tapSettingsMenuButton:SettingsAccountButton()];
  [ChromeEarlGreyUI tapAccountsMenuButton:AccountsSyncButton()];

  // Forget |identity|, screens should be popped back to the Main Settings.
  [[GREYUIThreadExecutor sharedInstance] drainUntilIdle];
  ios::FakeChromeIdentityService::GetInstanceFromChromeProvider()
      ->ForgetIdentity(identity, nil);

  [[EarlGrey selectElementWithMatcher:PrimarySignInButton()]
      assertWithMatcher:grey_sufficientlyVisible()];
  [SigninEarlGreyUtils assertSignedOut];

  [[EarlGrey selectElementWithMatcher:NavigationBarDoneButton()]
      performAction:grey_tap()];
}

// Tests that the Account Settings screen is correctly popped if the signed in
// account is removed while the "Disconnect Account" dialog is up.
- (void)testSignInPopUpAccountOnDisconnectAccount {
  ChromeIdentity* identity = [SigninEarlGreyUtils fakeIdentity1];
  ios::FakeChromeIdentityService::GetInstanceFromChromeProvider()->AddIdentity(
      identity);

  // Sign In |identity|, then open the Account Settings.
  [ChromeEarlGreyUI openSettingsMenu];
  [ChromeEarlGreyUI tapSettingsMenuButton:SecondarySignInButton()];
  [[EarlGrey selectElementWithMatcher:ButtonWithIdentity(identity)]
      performAction:grey_tap()];
  AcceptAccountConsistencyPopup();
  [SigninEarlGreyUtils assertSignedInWithIdentity:identity];
  [ChromeEarlGreyUI tapSettingsMenuButton:SettingsAccountButton()];
  [ChromeEarlGreyUI tapAccountsMenuButton:SignOutAccountsButton()];

  // Forget |identity|, screens should be popped back to the Main Settings.
  [[GREYUIThreadExecutor sharedInstance] drainUntilIdle];
  ios::FakeChromeIdentityService::GetInstanceFromChromeProvider()
      ->ForgetIdentity(identity, nil);

  [[EarlGrey selectElementWithMatcher:PrimarySignInButton()]
      assertWithMatcher:grey_sufficientlyVisible()];
  [SigninEarlGreyUtils assertSignedOut];

  [[EarlGrey selectElementWithMatcher:NavigationBarDoneButton()]
      performAction:grey_tap()];
}

// Tests that the Account Settings screen is correctly reloaded when one of
// the non-primary account is removed.
- (void)testSignInReloadOnRemoveAccount {
  ios::FakeChromeIdentityService* identity_service =
      ios::FakeChromeIdentityService::GetInstanceFromChromeProvider();
  ChromeIdentity* identity1 = [SigninEarlGreyUtils fakeIdentity1];
  ChromeIdentity* identity2 = [SigninEarlGreyUtils fakeIdentity2];
  identity_service->AddIdentity(identity1);
  identity_service->AddIdentity(identity2);

  // Sign In |identity|, then open the Account Settings.
  [ChromeEarlGreyUI openSettingsMenu];
  [ChromeEarlGreyUI tapSettingsMenuButton:SecondarySignInButton()];
  [[EarlGrey selectElementWithMatcher:ButtonWithIdentity(identity1)]
      performAction:grey_tap()];
  AcceptAccountConsistencyPopup();
  [SigninEarlGreyUtils assertSignedInWithIdentity:identity1];
  [ChromeEarlGreyUI tapSettingsMenuButton:SettingsAccountButton()];

  // Remove |identity2| from the device.
  [[EarlGrey selectElementWithMatcher:ButtonWithIdentity(identity2)]
      performAction:grey_tap()];
  [[EarlGrey
      selectElementWithMatcher:ButtonWithAccessibilityLabel(@"Remove account")]
      performAction:grey_tap()];

  // Check that |identity2| isn't available anymore on the Account Settings.
  [[EarlGrey
      selectElementWithMatcher:grey_allOf(
                                   grey_accessibilityLabel(identity2.userEmail),
                                   grey_sufficientlyVisible(), nil)]
      assertWithMatcher:grey_nil()];
  [SigninEarlGreyUtils assertSignedInWithIdentity:identity1];

  [[EarlGrey selectElementWithMatcher:NavigationBarDoneButton()]
      performAction:grey_tap()];
}

// Tests that the Sync Settings screen is correctly reloaded when one of the
// secondary accounts disappears.
- (void)testSignInReloadSyncOnForgetIdentity {
  ios::FakeChromeIdentityService* identity_service =
      ios::FakeChromeIdentityService::GetInstanceFromChromeProvider();
  ChromeIdentity* identity1 = [SigninEarlGreyUtils fakeIdentity1];
  ChromeIdentity* identity2 = [SigninEarlGreyUtils fakeIdentity2];
  identity_service->AddIdentity(identity1);
  identity_service->AddIdentity(identity2);

  // Sign In |identity|, then open the Sync Settings.
  [ChromeEarlGreyUI openSettingsMenu];
  [ChromeEarlGreyUI tapSettingsMenuButton:SecondarySignInButton()];
  [[EarlGrey selectElementWithMatcher:ButtonWithIdentity(identity1)]
      performAction:grey_tap()];
  AcceptAccountConsistencyPopup();
  [SigninEarlGreyUtils assertSignedInWithIdentity:identity1];
  [ChromeEarlGreyUI tapSettingsMenuButton:SettingsAccountButton()];
  [ChromeEarlGreyUI tapAccountsMenuButton:AccountsSyncButton()];

  // Forget |identity2|, allowing the UI to synchronize before and after
  // forgetting the identity.
  [[GREYUIThreadExecutor sharedInstance] drainUntilIdle];
  identity_service->ForgetIdentity(identity2, nil);
  [[GREYUIThreadExecutor sharedInstance] drainUntilIdle];

  // Check that both |identity1| and |identity2| aren't shown in the Sync
  // Settings.
  [[EarlGrey
      selectElementWithMatcher:grey_allOf(
                                   grey_accessibilityLabel(identity1.userEmail),
                                   grey_sufficientlyVisible(), nil)]
      assertWithMatcher:grey_nil()];
  [[EarlGrey
      selectElementWithMatcher:grey_allOf(
                                   grey_accessibilityLabel(identity2.userEmail),
                                   grey_sufficientlyVisible(), nil)]
      assertWithMatcher:grey_nil()];
  [SigninEarlGreyUtils assertSignedInWithIdentity:identity1];

  [[EarlGrey selectElementWithMatcher:NavigationBarDoneButton()]
      performAction:grey_tap()];
}

// Tests that the Account Settings screen is popped and the user signed out
// when the account is removed.
- (void)testSignOutOnRemoveAccount {
  ChromeIdentity* identity = [SigninEarlGreyUtils fakeIdentity1];
  ios::FakeChromeIdentityService::GetInstanceFromChromeProvider()->AddIdentity(
      identity);

  // Sign In |identity|, then open the Account Settings.
  [ChromeEarlGreyUI openSettingsMenu];
  [ChromeEarlGreyUI tapSettingsMenuButton:SecondarySignInButton()];
  [[EarlGrey selectElementWithMatcher:ButtonWithIdentity(identity)]
      performAction:grey_tap()];
  AcceptAccountConsistencyPopup();
  [SigninEarlGreyUtils assertSignedInWithIdentity:identity];
  [ChromeEarlGreyUI tapSettingsMenuButton:SettingsAccountButton()];

  // Remove |identity| from the device.
  [[EarlGrey selectElementWithMatcher:ButtonWithIdentity(identity)]
      performAction:grey_tap()];
  [[EarlGrey
      selectElementWithMatcher:ButtonWithAccessibilityLabel(@"Remove account")]
      performAction:grey_tap()];

  // Check that the user is signed out and the Main Settings screen is shown.
  [[EarlGrey selectElementWithMatcher:PrimarySignInButton()]
      assertWithMatcher:grey_sufficientlyVisible()];
  [SigninEarlGreyUtils assertSignedOut];

  [[EarlGrey selectElementWithMatcher:NavigationBarDoneButton()]
      performAction:grey_tap()];
}

// Tests that the user isn't signed out and the UI is correct when the
// disconnect is cancelled in the Account Settings screen.
- (void)testSignInDisconnectCancelled {
// TODO(crbug.com/669613): Re-enable this test on devices.
#if !TARGET_IPHONE_SIMULATOR
  EARL_GREY_TEST_DISABLED(@"Test disabled on device.");
#endif
  ChromeIdentity* identity = [SigninEarlGreyUtils fakeIdentity1];
  ios::FakeChromeIdentityService::GetInstanceFromChromeProvider()->AddIdentity(
      identity);

  // Sign In |identity|, then open the Account Settings.
  [ChromeEarlGreyUI openSettingsMenu];
  [ChromeEarlGreyUI tapSettingsMenuButton:SecondarySignInButton()];
  [[EarlGrey selectElementWithMatcher:ButtonWithIdentity(identity)]
      performAction:grey_tap()];
  AcceptAccountConsistencyPopup();
  [SigninEarlGreyUtils assertSignedInWithIdentity:identity];
  [ChromeEarlGreyUI tapSettingsMenuButton:SettingsAccountButton()];

  // Open the "Disconnect Account" dialog, then tap "Cancel".
  [ChromeEarlGreyUI tapAccountsMenuButton:SignOutAccountsButton()];
  [[EarlGrey selectElementWithMatcher:chrome_test_util::CancelButton()]
      performAction:grey_tap()];

  // Check that Account Settings screen is open and |identity| is signed in.
  [[EarlGrey selectElementWithMatcher:chrome_test_util::
                                          SettingsAccountsCollectionView()]
      assertWithMatcher:grey_sufficientlyVisible()];
  [SigninEarlGreyUtils assertSignedInWithIdentity:identity];

  [[EarlGrey selectElementWithMatcher:NavigationBarDoneButton()]
      performAction:grey_tap()];
}

- (void)testMDMError {
  ChromeIdentity* identity = [SigninEarlGreyUtils fakeIdentity1];
  ios::FakeChromeIdentityService* fakeChromeIdentityService =
      ios::FakeChromeIdentityService::GetInstanceFromChromeProvider();
  fakeChromeIdentityService->AddIdentity(identity);
  fakeChromeIdentityService->SetFakeMDMError(true);

  [ChromeEarlGreyUI openSettingsMenu];
  [ChromeEarlGreyUI tapSettingsMenuButton:SecondarySignInButton()];
  [[EarlGrey selectElementWithMatcher:ButtonWithIdentity(identity)]
      performAction:grey_tap()];
  AcceptAccountConsistencyPopup();
  [SigninEarlGreyUtils assertSignedInWithIdentity:identity];
  [ChromeEarlGreyUI tapSettingsMenuButton:SettingsAccountButton()];

  // Check that account sync button display the sync error.
  GREYPerformBlock block = ^(id element, NSError* __strong* errorOrNil) {
    GREYAssertTrue([element isKindOfClass:[AccountControlCell class]],
                   @"Should be AccountControlCell type");
    AccountControlCell* cell = static_cast<AccountControlCell*>(element);
    NSString* expectedString =
        l10n_util::GetNSString(IDS_IOS_OPTIONS_ACCOUNTS_SYNC_ERROR);
    return [cell.detailTextLabel.text isEqualToString:expectedString];
  };
  [[EarlGrey selectElementWithMatcher:AccountsSyncButton()]
      performAction:[GREYActionBlock
                        actionWithName:@"Invoke clearStateForTest selector"
                          performBlock:block]];
}

@end
