// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains unit tests for the sid class.

#include "sandbox/win/src/sid.h"

#define _ATL_NO_EXCEPTIONS
#include <atlbase.h>
#include <atlsecurity.h>
#include <sddl.h>

#include "base/win/windows_version.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace sandbox {

namespace {

bool EqualSid(const Sid& sid, const ATL::CSid& compare_sid) {
  if (!sid.IsValid())
    return false;
  return !!::EqualSid(sid.GetPSID(), const_cast<SID*>(compare_sid.GetPSID()));
}

bool EqualSid(const Sid& sid, const wchar_t* sddl_sid) {
  PSID compare_sid;
  if (!sid.IsValid())
    return false;
  if (!::ConvertStringSidToSid(sddl_sid, &compare_sid))
    return false;
  bool equal = !!::EqualSid(sid.GetPSID(), compare_sid);
  ::LocalFree(compare_sid);
  return equal;
}

struct CapabilityTestEntry {
  WellKnownCapabilities capability_;
  const wchar_t* capability_name_;
  const wchar_t* sddl_sid_;
};

}  // namespace

// Tests the creation of a Sid.
TEST(SidTest, Constructors) {
  ATL::CSid sid_world = ATL::Sids::World();
  PSID sid_world_pointer = const_cast<SID*>(sid_world.GetPSID());

  // Check the SID* constructor.
  Sid sid_sid_star(sid_world_pointer);
  ASSERT_TRUE(EqualSid(sid_sid_star, sid_world));

  // Check the copy constructor.
  Sid sid_copy(sid_sid_star);
  ASSERT_TRUE(EqualSid(sid_copy, sid_world));

  Sid sid_sddl = Sid::FromSddlString(L"S-1-1-0");
  ASSERT_TRUE(sid_sddl.IsValid());
  ASSERT_TRUE(EqualSid(sid_sddl, sid_world));

  Sid sid_sddl_invalid = Sid::FromSddlString(L"X-1-1-0");
  ASSERT_FALSE(sid_sddl_invalid.IsValid());

  Sid sid_sddl_empty = Sid::FromSddlString(L"");
  ASSERT_FALSE(sid_sddl_empty.IsValid());

  // Note that the WELL_KNOWN_SID_TYPE constructor is tested in the GetPSID
  // test. AppContainer related constructors are tested in AppContainer.
}

// Tests the method GetPSID
TEST(SidTest, GetPSID) {
  // Check for non-null result;
  ASSERT_NE(nullptr, Sid(::WinLocalSid).GetPSID());
  ASSERT_NE(nullptr, Sid(::WinCreatorOwnerSid).GetPSID());
  ASSERT_NE(nullptr, Sid(::WinBatchSid).GetPSID());

  ASSERT_TRUE(EqualSid(Sid(::WinNullSid), ATL::Sids::Null()));

  ASSERT_TRUE(EqualSid(Sid(::WinWorldSid), ATL::Sids::World()));

  ASSERT_TRUE(EqualSid(Sid(::WinDialupSid), ATL::Sids::Dialup()));

  ASSERT_TRUE(EqualSid(Sid(::WinNetworkSid), ATL::Sids::Network()));

  ASSERT_TRUE(
      EqualSid(Sid(::WinBuiltinAdministratorsSid), ATL::Sids::Admins()));

  ASSERT_TRUE(EqualSid(Sid(::WinBuiltinUsersSid), ATL::Sids::Users()));

  ASSERT_TRUE(EqualSid(Sid(::WinBuiltinGuestsSid), ATL::Sids::Guests()));

  ASSERT_TRUE(EqualSid(Sid(::WinProxySid), ATL::Sids::Proxy()));
}

TEST(SidTest, AppContainer) {
  const CapabilityTestEntry capabilities[] = {
      {kInternetClient, L"internetClient", L"S-1-15-3-1"},
      {kInternetClientServer, L"internetClientServer", L"S-1-15-3-2"},
      {kRegistryRead, L"registryRead",
       L"S-1-15-3-1024-1065365936-1281604716-3511738428-"
       "1654721687-432734479-3232135806-4053264122-3456934681"},
      {kLpacCryptoServices, L"lpacCryptoServices",
       L"S-1-15-3-1024-3203351429-2120443784-2872670797-"
       "1918958302-2829055647-4275794519-765664414-2751773334"},
      {kEnterpriseAuthentication, L"enterpriseAuthentication", L"S-1-15-3-8"},
      {kPrivateNetworkClientServer, L"privateNetworkClientServer",
       L"S-1-15-3-3"}};

  // No support for AppContainer less than Win10 RS2.
  if (base::win::GetVersion() < base::win::VERSION_WIN10_RS2)
    return;

  Sid sid_nullptr = Sid::FromNamedCapability(nullptr);
  EXPECT_FALSE(sid_nullptr.IsValid());

  Sid sid_empty = Sid::FromNamedCapability(L"");
  EXPECT_FALSE(sid_empty.IsValid());

  WellKnownCapabilities invalid_well_known =
      static_cast<WellKnownCapabilities>(kMaxWellKnownCapability + 1);
  Sid sid_invalid_well_known = Sid::FromKnownCapability(invalid_well_known);
  EXPECT_FALSE(sid_invalid_well_known.IsValid());

  for (auto capability : capabilities) {
    EXPECT_TRUE(EqualSid(Sid::FromNamedCapability(capability.capability_name_),
                         capability.sddl_sid_))
        << "Named Capability: " << capability.sddl_sid_;
    EXPECT_TRUE(EqualSid(Sid::FromKnownCapability(capability.capability_),
                         capability.sddl_sid_))
        << "Known Capability: " << capability.sddl_sid_;
  }
}

TEST(SidTest, Sddl) {
  Sid sid_sddl = Sid::FromSddlString(L"S-1-1-0");
  ASSERT_TRUE(sid_sddl.IsValid());
  base::string16 sddl_str;
  ASSERT_TRUE(sid_sddl.ToSddlString(&sddl_str));
  ASSERT_EQ(L"S-1-1-0", sddl_str);
}

TEST(SidTest, SubAuthorities) {
  DWORD world_subauthorities[] = {0};
  SID_IDENTIFIER_AUTHORITY world_authority = {SECURITY_WORLD_SID_AUTHORITY};
  Sid sid_world =
      Sid::FromSubAuthorities(&world_authority, 1, world_subauthorities);
  ASSERT_TRUE(EqualSid(sid_world, ATL::Sids::World()));
  ASSERT_TRUE(Sid::FromSubAuthorities(&world_authority, 0, nullptr).IsValid());

  DWORD admin_subauthorities[] = {32, 544};
  SID_IDENTIFIER_AUTHORITY nt_authority = {SECURITY_NT_AUTHORITY};
  Sid sid_admin =
      Sid::FromSubAuthorities(&nt_authority, 2, admin_subauthorities);
  ASSERT_TRUE(EqualSid(sid_admin, ATL::Sids::Admins()));
}

}  // namespace sandbox
