// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/ntp_snippets/breaking_news/breaking_news_gcm_app_handler.h"

#include <memory>
#include <string>

#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/test/histogram_tester.h"
#include "base/test/simple_test_clock.h"
#include "base/test/test_mock_time_task_runner.h"
#include "base/time/clock.h"
#include "base/time/tick_clock.h"
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "components/gcm_driver/gcm_driver.h"
#include "components/gcm_driver/instance_id/instance_id.h"
#include "components/gcm_driver/instance_id/instance_id_driver.h"
#include "components/ntp_snippets/breaking_news/breaking_news_metrics.h"
#include "components/ntp_snippets/breaking_news/subscription_manager.h"
#include "components/ntp_snippets/features.h"
#include "components/ntp_snippets/pref_names.h"
#include "components/ntp_snippets/remote/test_utils.h"
#include "components/ntp_snippets/time_serialization.h"
#include "components/prefs/testing_pref_service.h"
#include "components/variations/variations_params_manager.h"
#include "google_apis/gcm/engine/account_mapping.h"
#include "testing/gmock/include/gmock/gmock.h"

using base::TestMockTimeTaskRunner;
using base::TickClock;
using base::TimeTicks;
using gcm::InstanceIDHandler;
using instance_id::InstanceID;
using instance_id::InstanceIDDriver;
using testing::_;
using testing::AnyNumber;
using testing::AtMost;
using testing::ElementsAre;
using testing::IsEmpty;
using testing::NiceMock;
using testing::StrictMock;

namespace ntp_snippets {

namespace {

class MockSubscriptionManager : public SubscriptionManager {
 public:
  MockSubscriptionManager() = default;
  ~MockSubscriptionManager() override = default;

  MOCK_METHOD1(Subscribe, void(const std::string& token));
  MOCK_METHOD0(Unsubscribe, void());
  MOCK_METHOD0(IsSubscribed, bool());
  MOCK_METHOD1(Resubscribe, void(const std::string& new_token));
  MOCK_METHOD0(NeedsToResubscribe, bool());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockSubscriptionManager);
};

class MockInstanceIDDriver : public InstanceIDDriver {
 public:
  MockInstanceIDDriver() : InstanceIDDriver(/*gcm_driver=*/nullptr){};
  ~MockInstanceIDDriver() override = default;

  MOCK_METHOD1(GetInstanceID, InstanceID*(const std::string& app_id));
  MOCK_METHOD1(RemoveInstanceID, void(const std::string& app_id));
  MOCK_CONST_METHOD1(ExistsInstanceID, bool(const std::string& app_id));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockInstanceIDDriver);
};

class MockInstanceID : public InstanceID {
 public:
  MockInstanceID() : InstanceID("app_id", /*gcm_driver=*/nullptr) {}
  ~MockInstanceID() override = default;

  MOCK_METHOD1(GetID, void(const GetIDCallback& callback));
  MOCK_METHOD1(GetCreationTime, void(const GetCreationTimeCallback& callback));
  MOCK_METHOD4(GetToken,
               void(const std::string& authorized_entity,
                    const std::string& scope,
                    const std::map<std::string, std::string>& options,
                    const GetTokenCallback& callback));
  MOCK_METHOD4(ValidateToken,
               void(const std::string& authorized_entity,
                    const std::string& scope,
                    const std::string& token,
                    const ValidateTokenCallback& callback));

 protected:
  MOCK_METHOD3(DeleteTokenImpl,
               void(const std::string& authorized_entity,
                    const std::string& scope,
                    const DeleteTokenCallback& callback));
  MOCK_METHOD1(DeleteIDImpl, void(const DeleteIDCallback& callback));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockInstanceID);
};

class MockGCMDriver : public gcm::GCMDriver {
 public:
  MockGCMDriver()
      : GCMDriver(/*store_path=*/base::FilePath(),
                  /*blocking_task_runner=*/nullptr) {}
  ~MockGCMDriver() override = default;

  MOCK_METHOD4(ValidateRegistration,
               void(const std::string& app_id,
                    const std::vector<std::string>& sender_ids,
                    const std::string& registration_id,
                    const ValidateRegistrationCallback& callback));
  MOCK_METHOD0(OnSignedIn, void());
  MOCK_METHOD0(OnSignedOut, void());
  MOCK_METHOD1(AddConnectionObserver,
               void(gcm::GCMConnectionObserver* observer));
  MOCK_METHOD1(RemoveConnectionObserver,
               void(gcm::GCMConnectionObserver* observer));
  MOCK_METHOD0(Enable, void());
  MOCK_METHOD0(Disable, void());
  MOCK_CONST_METHOD0(GetGCMClientForTesting, gcm::GCMClient*());
  MOCK_CONST_METHOD0(IsStarted, bool());
  MOCK_CONST_METHOD0(IsConnected, bool());
  MOCK_METHOD2(GetGCMStatistics,
               void(const GetGCMStatisticsCallback& callback,
                    ClearActivityLogs clear_logs));
  MOCK_METHOD2(SetGCMRecording,
               void(const GetGCMStatisticsCallback& callback, bool recording));
  MOCK_METHOD1(SetAccountTokens,
               void(const std::vector<gcm::GCMClient::AccountTokenInfo>&
                        account_tokens));
  MOCK_METHOD1(UpdateAccountMapping,
               void(const gcm::AccountMapping& account_mapping));
  MOCK_METHOD1(RemoveAccountMapping, void(const std::string& account_id));
  MOCK_METHOD0(GetLastTokenFetchTime, base::Time());
  MOCK_METHOD1(SetLastTokenFetchTime, void(const base::Time& time));
  MOCK_METHOD1(WakeFromSuspendForHeartbeat, void(bool wake));
  MOCK_METHOD0(GetInstanceIDHandlerInternal, InstanceIDHandler*());
  MOCK_METHOD2(AddHeartbeatInterval,
               void(const std::string& scope, int interval_ms));
  MOCK_METHOD1(RemoveHeartbeatInterval, void(const std::string& scope));

 protected:
  MOCK_METHOD1(EnsureStarted,
               gcm::GCMClient::Result(gcm::GCMClient::StartMode start_mode));
  MOCK_METHOD2(RegisterImpl,
               void(const std::string& app_id,
                    const std::vector<std::string>& sender_ids));
  MOCK_METHOD1(UnregisterImpl, void(const std::string& app_id));
  MOCK_METHOD3(SendImpl,
               void(const std::string& app_id,
                    const std::string& receiver_id,
                    const gcm::OutgoingMessage& message));
  MOCK_METHOD2(RecordDecryptionFailure,
               void(const std::string& app_id,
                    gcm::GCMDecryptionResult result));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockGCMDriver);
};

ACTION_TEMPLATE(InvokeCallbackArgument,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_1_VALUE_PARAMS(p0)) {
  ::std::tr1::get<k>(args).Run(p0);
}

ACTION_TEMPLATE(InvokeCallbackArgument,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_2_VALUE_PARAMS(p0, p1)) {
  ::std::tr1::get<k>(args).Run(p0, p1);
}

base::Time GetDummyNow() {
  base::Time out_time;
  EXPECT_TRUE(base::Time::FromUTCString("2017-01-02T00:00:01Z", &out_time));
  return out_time;
}

base::TimeDelta GetTokenValidationPeriod() {
  return base::TimeDelta::FromHours(24);
}

base::TimeDelta GetForcedSubscriptionPeriod() {
  return base::TimeDelta::FromDays(7);
}

const char kBreakingNewsGCMAppID[] = "com.google.breakingnews.gcm";

std::string BoolToString(bool value) {
  return value ? "true" : "false";
}

}  // namespace

class BreakingNewsGCMAppHandlerTest : public testing::Test {
 public:
  void SetUp() override {
    // Our app handler obtains InstanceID through InstanceIDDriver. We mock
    // InstanceIDDriver and return MockInstanceID through it.
    mock_instance_id_driver_ =
        base::MakeUnique<StrictMock<MockInstanceIDDriver>>();
    mock_instance_id_ = base::MakeUnique<StrictMock<MockInstanceID>>();
    mock_gcm_driver_ = base::MakeUnique<StrictMock<MockGCMDriver>>();
    BreakingNewsGCMAppHandler::RegisterProfilePrefs(
        utils_.pref_service()->registry());

    // This is called in BreakingNewsGCMAppHandler.
    EXPECT_CALL(*mock_instance_id_driver_, GetInstanceID(kBreakingNewsGCMAppID))
        .WillRepeatedly(Return(mock_instance_id_.get()));
  }

  std::unique_ptr<BreakingNewsGCMAppHandler> MakeHandler(
      scoped_refptr<TestMockTimeTaskRunner> timer_mock_task_runner) {
    tick_clock_ = timer_mock_task_runner->GetMockTickClock();
    message_loop_.SetTaskRunner(timer_mock_task_runner);

    // TODO(vitaliii): Initialize MockSubscriptionManager in the constructor, so
    // that one could set up expectations before creating the handler.
    auto wrapped_mock_subscription_manager =
        base::MakeUnique<NiceMock<MockSubscriptionManager>>();
    mock_subscription_manager_ = wrapped_mock_subscription_manager.get();

    auto token_validation_timer =
        base::MakeUnique<base::OneShotTimer>(tick_clock_.get());
    token_validation_timer->SetTaskRunner(timer_mock_task_runner);

    auto forced_subscription_timer =
        base::MakeUnique<base::OneShotTimer>(tick_clock_.get());
    forced_subscription_timer->SetTaskRunner(timer_mock_task_runner);

    // TODO(vitaliii): Either parse JSON for real or expose another method to
    // avoid it completely.
    return base::MakeUnique<BreakingNewsGCMAppHandler>(
        mock_gcm_driver_.get(), mock_instance_id_driver_.get(), pref_service(),
        std::move(wrapped_mock_subscription_manager),
        /*parse_json_callback=*/
        base::Bind(
            [](const std::string& raw_json_string,
               const BreakingNewsGCMAppHandler::SuccessCallback&
                   success_callback,
               const BreakingNewsGCMAppHandler::ErrorCallback& error_callback) {
              error_callback.Run(/*error=*/"Not implemented");
            }),
        timer_mock_task_runner->GetMockClock(),
        std::move(token_validation_timer),
        std::move(forced_subscription_timer));
  }

  void SetFeatureParams(bool enable_token_validation,
                        bool enable_forced_subscription) {
    // VariationParamsManager supports only one
    // |SetVariationParamsWithFeatureAssociations| at a time, so we clear
    // previous settings first to make this explicit.
    params_manager_.ClearAllVariationParams();
    params_manager_.SetVariationParamsWithFeatureAssociations(
        kBreakingNewsPushFeature.name,
        {
            {"enable_token_validation", BoolToString(enable_token_validation)},
            {"enable_forced_subscription",
             BoolToString(enable_forced_subscription)},
        },
        {kBreakingNewsPushFeature.name});
  }

  PrefService* pref_service() { return utils_.pref_service(); }
  NiceMock<MockSubscriptionManager>* mock_subscription_manager() {
    return mock_subscription_manager_;
  }
  StrictMock<MockInstanceID>* mock_instance_id() {
    return mock_instance_id_.get();
  }

 private:
  variations::testing::VariationParamsManager params_manager_;
  base::MessageLoop message_loop_;
  test::RemoteSuggestionsTestUtils utils_;
  NiceMock<MockSubscriptionManager>* mock_subscription_manager_;
  std::unique_ptr<StrictMock<MockGCMDriver>> mock_gcm_driver_;
  std::unique_ptr<StrictMock<MockInstanceIDDriver>> mock_instance_id_driver_;
  std::unique_ptr<StrictMock<MockInstanceID>> mock_instance_id_;
  std::unique_ptr<TickClock> tick_clock_;
};

TEST_F(BreakingNewsGCMAppHandlerTest,
       ShouldValidateTokenImmediatelyIfValidationIsDue) {
  SetFeatureParams(/*enable_token_validation=*/true,
                   /*enable_forced_subscription=*/false);

  // Last validation was long time ago.
  const base::Time last_validation =
      GetDummyNow() - 10 * GetTokenValidationPeriod();
  pref_service()->SetInt64(prefs::kBreakingNewsGCMLastTokenValidationTime,
                           SerializeTime(last_validation));
  // Omit receiving the token by putting it there directly.
  pref_service()->SetString(prefs::kBreakingNewsGCMSubscriptionTokenCache,
                            "token");
  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));
  auto handler = MakeHandler(task_runner);

  // Check that the handler validates the token through GetToken. ValidateToken
  // always returns true on Android, so it's not useful. Instead, the handler
  // must check that the result from GetToken is unchanged.
  EXPECT_CALL(*mock_instance_id(), GetToken(_, _, _, _))
      .WillOnce(
          InvokeCallbackArgument<3>("token", InstanceID::Result::SUCCESS));
  EXPECT_CALL(*mock_instance_id(), ValidateToken(_, _, _, _)).Times(0);
  handler->StartListening(
      base::Bind([](std::unique_ptr<RemoteSuggestion> remote_suggestion) {}));
  task_runner->RunUntilIdle();
}

TEST_F(BreakingNewsGCMAppHandlerTest,
       ShouldScheduleTokenValidationIfNotYetDue) {
  SetFeatureParams(/*enable_token_validation=*/true,
                   /*enable_forced_subscription=*/false);

  // The next validation will be soon.
  const base::TimeDelta time_to_validation = base::TimeDelta::FromHours(1);
  const base::Time last_validation =
      GetDummyNow() - (GetTokenValidationPeriod() - time_to_validation);
  pref_service()->SetInt64(prefs::kBreakingNewsGCMLastTokenValidationTime,
                           SerializeTime(last_validation));
  // Omit receiving the token by putting it there directly.
  pref_service()->SetString(prefs::kBreakingNewsGCMSubscriptionTokenCache,
                            "token");

  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));
  auto handler = MakeHandler(task_runner);

  // Check that handler does not validate the token yet.
  EXPECT_CALL(*mock_instance_id(), GetToken(_, _, _, _)).Times(0);
  EXPECT_CALL(*mock_instance_id(), ValidateToken(_, _, _, _)).Times(0);
  handler->StartListening(
      base::Bind([](std::unique_ptr<RemoteSuggestion> remote_suggestion) {}));
  task_runner->FastForwardBy(time_to_validation -
                             base::TimeDelta::FromSeconds(1));

  // But when it is time, validation happens.
  EXPECT_CALL(*mock_instance_id(), GetToken(_, _, _, _))
      .WillOnce(
          InvokeCallbackArgument<3>("token", InstanceID::Result::SUCCESS));
  task_runner->FastForwardBy(base::TimeDelta::FromSeconds(1));
}

TEST_F(BreakingNewsGCMAppHandlerTest, ShouldNotValidateTokenBeforeListening) {
  SetFeatureParams(/*enable_token_validation=*/true,
                   /*enable_forced_subscription=*/false);

  // Last validation was long time ago.
  const base::Time last_validation =
      GetDummyNow() - 10 * GetTokenValidationPeriod();
  pref_service()->SetInt64(prefs::kBreakingNewsGCMLastTokenValidationTime,
                           SerializeTime(last_validation));

  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));

  // Check that handler does not validate the token before StartListening even
  // though a validation is due.
  EXPECT_CALL(*mock_instance_id(), GetToken(_, _, _, _)).Times(0);
  EXPECT_CALL(*mock_instance_id(), ValidateToken(_, _, _, _)).Times(0);
  auto handler = MakeHandler(task_runner);
  task_runner->FastForwardBy(10 * GetTokenValidationPeriod());
}

TEST_F(BreakingNewsGCMAppHandlerTest,
       ShouldNotValidateTokenAfterStopListening) {
  SetFeatureParams(/*enable_token_validation=*/true,
                   /*enable_forced_subscription=*/false);

  // The next validation will be soon.
  const base::TimeDelta time_to_validation = base::TimeDelta::FromHours(1);
  const base::Time last_validation =
      GetDummyNow() - (GetTokenValidationPeriod() - time_to_validation);
  pref_service()->SetInt64(prefs::kBreakingNewsGCMLastTokenValidationTime,
                           SerializeTime(last_validation));
  // Omit receiving the token by putting it there directly.
  pref_service()->SetString(prefs::kBreakingNewsGCMSubscriptionTokenCache,
                            "token");

  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));

  // Check that handler does not validate the token after StopListening even
  // though a validation is due.
  EXPECT_CALL(*mock_instance_id(), GetToken(_, _, _, _)).Times(0);
  EXPECT_CALL(*mock_instance_id(), ValidateToken(_, _, _, _)).Times(0);
  auto handler = MakeHandler(task_runner);
  handler->StartListening(
      base::Bind([](std::unique_ptr<RemoteSuggestion> remote_suggestion) {}));
  handler->StopListening();
  task_runner->FastForwardBy(10 * GetTokenValidationPeriod());
}

TEST_F(BreakingNewsGCMAppHandlerTest,
       ShouldRescheduleTokenValidationWhenRetrievingToken) {
  SetFeatureParams(/*enable_token_validation=*/true,
                   /*enable_forced_subscription=*/false);

  // The next validation will be soon.
  const base::TimeDelta time_to_validation = base::TimeDelta::FromHours(1);
  const base::Time last_validation =
      GetDummyNow() - (GetTokenValidationPeriod() - time_to_validation);
  pref_service()->SetInt64(prefs::kBreakingNewsGCMLastTokenValidationTime,
                           SerializeTime(last_validation));

  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));
  auto handler = MakeHandler(task_runner);

  // There is no token yet, thus, handler retrieves it on StartListening.
  EXPECT_CALL(*mock_instance_id(), GetToken(_, _, _, _))
      .WillOnce(
          InvokeCallbackArgument<3>("token", InstanceID::Result::SUCCESS));
  handler->StartListening(
      base::Bind([](std::unique_ptr<RemoteSuggestion> remote_suggestion) {}));

  // Check that the validation schedule has changed. "Old validation" should not
  // happen because the token was retrieved recently.
  EXPECT_CALL(*mock_instance_id(), GetToken(_, _, _, _)).Times(0);
  task_runner->FastForwardBy(GetTokenValidationPeriod() -
                             base::TimeDelta::FromSeconds(1));

  // The new validation should happen.
  EXPECT_CALL(*mock_instance_id(), GetToken(_, _, _, _))
      .WillOnce(
          InvokeCallbackArgument<3>("token", InstanceID::Result::SUCCESS));
  task_runner->FastForwardBy(base::TimeDelta::FromSeconds(1));
}

TEST_F(BreakingNewsGCMAppHandlerTest,
       ShouldScheduleNewTokenValidationAfterValidation) {
  SetFeatureParams(/*enable_token_validation=*/true,
                   /*enable_forced_subscription=*/false);

  // The next validation will be soon.
  const base::TimeDelta time_to_validation = base::TimeDelta::FromHours(1);
  const base::Time last_validation =
      GetDummyNow() - (GetTokenValidationPeriod() - time_to_validation);
  pref_service()->SetInt64(prefs::kBreakingNewsGCMLastTokenValidationTime,
                           SerializeTime(last_validation));
  // Omit receiving the token by putting it there directly.
  pref_service()->SetString(prefs::kBreakingNewsGCMSubscriptionTokenCache,
                            "token");

  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));
  auto handler = MakeHandler(task_runner);
  handler->StartListening(
      base::Bind([](std::unique_ptr<RemoteSuggestion> remote_suggestion) {}));

  // Handler validates the token.
  EXPECT_CALL(*mock_instance_id(), GetToken(_, _, _, _))
      .WillOnce(
          InvokeCallbackArgument<3>("token", InstanceID::Result::SUCCESS));
  EXPECT_CALL(*mock_instance_id(), ValidateToken(_, _, _, _)).Times(0);
  task_runner->FastForwardBy(time_to_validation);

  // Check that the next validation is scheduled in time.
  EXPECT_CALL(*mock_instance_id(), GetToken(_, _, _, _)).Times(0);
  task_runner->FastForwardBy(GetTokenValidationPeriod() -
                             base::TimeDelta::FromSeconds(1));

  EXPECT_CALL(*mock_instance_id(), GetToken(_, _, _, _))
      .WillOnce(
          InvokeCallbackArgument<3>("token", InstanceID::Result::SUCCESS));
  task_runner->FastForwardBy(base::TimeDelta::FromSeconds(1));
}

TEST_F(BreakingNewsGCMAppHandlerTest,
       ShouldResubscribeWithNewTokenIfOldIsInvalidAfterValidation) {
  SetFeatureParams(/*enable_token_validation=*/true,
                   /*enable_forced_subscription=*/false);

  // Last validation was long time ago.
  const base::Time last_validation =
      GetDummyNow() - 10 * GetTokenValidationPeriod();
  pref_service()->SetInt64(prefs::kBreakingNewsGCMLastTokenValidationTime,
                           SerializeTime(last_validation));
  // Omit receiving the token by putting it there directly.
  pref_service()->SetString(prefs::kBreakingNewsGCMSubscriptionTokenCache,
                            "old_token");

  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));
  auto handler = MakeHandler(task_runner);
  handler->StartListening(
      base::Bind([](std::unique_ptr<RemoteSuggestion> remote_suggestion) {}));

  // Check that handler resubscribes with the new token after a validation, if
  // old is invalid.
  EXPECT_CALL(*mock_instance_id(), GetToken(_, _, _, _))
      .WillOnce(
          InvokeCallbackArgument<3>("new_token", InstanceID::Result::SUCCESS));
  EXPECT_CALL(*mock_instance_id(), ValidateToken(_, _, _, _)).Times(0);
  EXPECT_CALL(*mock_subscription_manager(), Resubscribe("new_token"));
  EXPECT_CALL(*mock_subscription_manager(), Subscribe(_)).Times(0);
  task_runner->RunUntilIdle();
}

TEST_F(BreakingNewsGCMAppHandlerTest,
       ShouldDoNothingIfOldTokenIsValidAfterValidation) {
  SetFeatureParams(/*enable_token_validation=*/true,
                   /*enable_forced_subscription=*/false);

  // Last validation was long time ago.
  const base::Time last_validation =
      GetDummyNow() - 10 * GetTokenValidationPeriod();
  pref_service()->SetInt64(prefs::kBreakingNewsGCMLastTokenValidationTime,
                           SerializeTime(last_validation));
  // Omit receiving the token by putting it there directly.
  pref_service()->SetString(prefs::kBreakingNewsGCMSubscriptionTokenCache,
                            "token");

  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));
  auto handler = MakeHandler(task_runner);
  handler->StartListening(
      base::Bind([](std::unique_ptr<RemoteSuggestion> remote_suggestion) {}));

  // Check that provider does not resubscribe if the old token is still valid
  // after validation.
  EXPECT_CALL(*mock_instance_id(), GetToken(_, _, _, _))
      .WillOnce(
          InvokeCallbackArgument<3>("token", InstanceID::Result::SUCCESS));
  EXPECT_CALL(*mock_instance_id(), ValidateToken(_, _, _, _)).Times(0);
  EXPECT_CALL(*mock_subscription_manager(), Subscribe(_)).Times(0);
  EXPECT_CALL(*mock_subscription_manager(), Resubscribe(_)).Times(0);
  task_runner->RunUntilIdle();
}

TEST_F(BreakingNewsGCMAppHandlerTest,
       IsListeningShouldReturnFalseBeforeListening) {
  SetFeatureParams(/*enable_token_validation=*/false,
                   /*enable_forced_subscription=*/false);

  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));
  auto handler = MakeHandler(task_runner);

  EXPECT_FALSE(handler->IsListening());
}

TEST_F(BreakingNewsGCMAppHandlerTest,
       IsListeningShouldReturnTrueAfterStartListening) {
  SetFeatureParams(/*enable_token_validation=*/false,
                   /*enable_forced_subscription=*/false);

  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));
  auto handler = MakeHandler(task_runner);
  ASSERT_FALSE(handler->IsListening());

  EXPECT_CALL(*mock_instance_id(), GetToken(_, _, _, _))
      .WillRepeatedly(
          InvokeCallbackArgument<3>("token", InstanceID::Result::SUCCESS));
  handler->StartListening(
      base::Bind([](std::unique_ptr<RemoteSuggestion> remote_suggestion) {}));

  EXPECT_TRUE(handler->IsListening());
}

TEST_F(BreakingNewsGCMAppHandlerTest,
       IsListeningShouldReturnFalseAfterStopListening) {
  SetFeatureParams(/*enable_token_validation=*/false,
                   /*enable_forced_subscription=*/false);

  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));
  auto handler = MakeHandler(task_runner);
  ASSERT_FALSE(handler->IsListening());

  EXPECT_CALL(*mock_instance_id(), GetToken(_, _, _, _))
      .WillRepeatedly(
          InvokeCallbackArgument<3>("token", InstanceID::Result::SUCCESS));
  handler->StartListening(
      base::Bind([](std::unique_ptr<RemoteSuggestion> remote_suggestion) {}));
  ASSERT_TRUE(handler->IsListening());

  handler->StopListening();

  EXPECT_FALSE(handler->IsListening());
}

TEST_F(BreakingNewsGCMAppHandlerTest,
       IsListeningShouldReturnTrueAfterSecondStartListening) {
  SetFeatureParams(/*enable_token_validation=*/false,
                   /*enable_forced_subscription=*/false);

  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));
  auto handler = MakeHandler(task_runner);
  ASSERT_FALSE(handler->IsListening());

  EXPECT_CALL(*mock_instance_id(), GetToken(_, _, _, _))
      .WillRepeatedly(
          InvokeCallbackArgument<3>("token", InstanceID::Result::SUCCESS));
  handler->StartListening(
      base::Bind([](std::unique_ptr<RemoteSuggestion> remote_suggestion) {}));
  ASSERT_TRUE(handler->IsListening());

  handler->StopListening();
  ASSERT_FALSE(handler->IsListening());

  handler->StartListening(
      base::Bind([](std::unique_ptr<RemoteSuggestion> remote_suggestion) {}));

  EXPECT_TRUE(handler->IsListening());
}

TEST_F(BreakingNewsGCMAppHandlerTest, ShouldForceSubscribeImmediatelyIfDue) {
  SetFeatureParams(/*enable_token_validation=*/false,
                   /*enable_forced_subscription=*/true);

  // Last subscription was long time ago.
  const base::Time last_subscription =
      GetDummyNow() - 10 * GetForcedSubscriptionPeriod();
  pref_service()->SetInt64(prefs::kBreakingNewsGCMLastForcedSubscriptionTime,
                           SerializeTime(last_subscription));
  // Omit receiving the token by putting it there directly.
  pref_service()->SetString(prefs::kBreakingNewsGCMSubscriptionTokenCache,
                            "token");
  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));
  auto handler = MakeHandler(task_runner);

  handler->StartListening(
      base::Bind([](std::unique_ptr<RemoteSuggestion> remote_suggestion) {}));
  EXPECT_CALL(*mock_subscription_manager(), Subscribe("token"));
  task_runner->RunUntilIdle();
}

TEST_F(BreakingNewsGCMAppHandlerTest,
       ShouldScheduleForcedSubscribtionIfNotYetDue) {
  SetFeatureParams(/*enable_token_validation=*/false,
                   /*enable_forced_subscription=*/true);

  // The next forced subscription will be soon.
  const base::TimeDelta time_to_subscription = base::TimeDelta::FromHours(1);
  const base::Time last_subscription =
      GetDummyNow() - (GetForcedSubscriptionPeriod() - time_to_subscription);
  pref_service()->SetInt64(prefs::kBreakingNewsGCMLastForcedSubscriptionTime,
                           SerializeTime(last_subscription));
  // Omit receiving the token by putting it there directly.
  pref_service()->SetString(prefs::kBreakingNewsGCMSubscriptionTokenCache,
                            "token");
  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));
  auto handler = MakeHandler(task_runner);

  // Check that handler does not force subscribe yet.
  handler->StartListening(
      base::Bind([](std::unique_ptr<RemoteSuggestion> remote_suggestion) {}));
  // TODO(vitaliii): Consider making FakeSubscriptionManager, because
  // IsSubscribed() affects forced subscriptions. Currently we have to carefully
  // avoid the initial subscription.
  EXPECT_CALL(*mock_subscription_manager(), Subscribe("token")).Times(0);
  task_runner->FastForwardBy(time_to_subscription -
                             base::TimeDelta::FromSeconds(1));

  // But when it is time, forced subscription happens.
  testing::Mock::VerifyAndClearExpectations(mock_subscription_manager());
  EXPECT_CALL(*mock_subscription_manager(), Subscribe("token"));
  task_runner->FastForwardBy(base::TimeDelta::FromSeconds(1));
}

TEST_F(BreakingNewsGCMAppHandlerTest, ShouldNotForceSubscribeBeforeListening) {
  SetFeatureParams(/*enable_token_validation=*/false,
                   /*enable_forced_subscription=*/true);

  // Last subscription was long time ago.
  const base::Time last_subscription =
      GetDummyNow() - 10 * GetForcedSubscriptionPeriod();
  pref_service()->SetInt64(prefs::kBreakingNewsGCMLastForcedSubscriptionTime,
                           SerializeTime(last_subscription));
  // Omit receiving the token by putting it there directly.
  pref_service()->SetString(prefs::kBreakingNewsGCMSubscriptionTokenCache,
                            "token");
  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));

  // Check that handler does not force subscribe before StartListening even
  // though a forced subscription is due.
  auto handler = MakeHandler(task_runner);
  EXPECT_CALL(*mock_subscription_manager(), Subscribe("token")).Times(0);
  task_runner->FastForwardBy(10 * GetForcedSubscriptionPeriod());
}

TEST_F(BreakingNewsGCMAppHandlerTest,
       ShouldNotForceSubscribeAfterStopListening) {
  SetFeatureParams(/*enable_token_validation=*/false,
                   /*enable_forced_subscription=*/true);

  // The next forced subscription will be soon.
  const base::TimeDelta time_to_subscription = base::TimeDelta::FromHours(1);
  const base::Time last_subscription =
      GetDummyNow() - (GetForcedSubscriptionPeriod() - time_to_subscription);
  pref_service()->SetInt64(prefs::kBreakingNewsGCMLastForcedSubscriptionTime,
                           SerializeTime(last_subscription));
  // Omit receiving the token by putting it there directly.
  pref_service()->SetString(prefs::kBreakingNewsGCMSubscriptionTokenCache,
                            "token");
  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));

  // Check that handler does not force subscribe after StopListening even
  // though a forced subscription is due.
  auto handler = MakeHandler(task_runner);
  handler->StartListening(
      base::Bind([](std::unique_ptr<RemoteSuggestion> remote_suggestion) {}));
  handler->StopListening();
  EXPECT_CALL(*mock_subscription_manager(), Subscribe("token")).Times(0);
  task_runner->FastForwardBy(10 * GetForcedSubscriptionPeriod());
}

TEST_F(BreakingNewsGCMAppHandlerTest,
       ShouldScheduleNewForcedSubscriptionAfterForcedSubscription) {
  SetFeatureParams(/*enable_token_validation=*/false,
                   /*enable_forced_subscription=*/true);

  // The next forced subscription will be soon.
  const base::TimeDelta time_to_subscription = base::TimeDelta::FromHours(1);
  const base::Time last_subscription =
      GetDummyNow() - (GetForcedSubscriptionPeriod() - time_to_subscription);
  pref_service()->SetInt64(prefs::kBreakingNewsGCMLastForcedSubscriptionTime,
                           SerializeTime(last_subscription));
  // Omit receiving the token by putting it there directly.
  pref_service()->SetString(prefs::kBreakingNewsGCMSubscriptionTokenCache,
                            "token");

  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));
  auto handler = MakeHandler(task_runner);
  handler->StartListening(
      base::Bind([](std::unique_ptr<RemoteSuggestion> remote_suggestion) {}));

  // Handler force subscribes.
  EXPECT_CALL(*mock_subscription_manager(), Subscribe("token"));
  task_runner->FastForwardBy(time_to_subscription);

  // Check that the next forced subscription is scheduled in time.
  EXPECT_CALL(*mock_subscription_manager(), Subscribe("token")).Times(0);
  task_runner->FastForwardBy(GetForcedSubscriptionPeriod() -
                             base::TimeDelta::FromSeconds(1));

  EXPECT_CALL(*mock_subscription_manager(), Subscribe("token"));
  task_runner->FastForwardBy(base::TimeDelta::FromSeconds(1));
}

TEST_F(BreakingNewsGCMAppHandlerTest,
       TokenValidationAndForcedSubscriptionShouldNotAffectEachOther) {
  SetFeatureParams(/*enable_token_validation=*/true,
                   /*enable_forced_subscription=*/true);

  // The next forced subscription will be soon.
  const base::TimeDelta time_to_subscription = base::TimeDelta::FromHours(1);
  const base::Time last_subscription =
      GetDummyNow() - (GetForcedSubscriptionPeriod() - time_to_subscription);
  pref_service()->SetInt64(prefs::kBreakingNewsGCMLastForcedSubscriptionTime,
                           SerializeTime(last_subscription));

  // The next validation will be sooner.
  const base::TimeDelta time_to_validation = base::TimeDelta::FromMinutes(30);
  const base::Time last_validation =
      GetDummyNow() - (GetTokenValidationPeriod() - time_to_validation);
  pref_service()->SetInt64(prefs::kBreakingNewsGCMLastTokenValidationTime,
                           SerializeTime(last_validation));

  // Omit receiving the token by putting it there directly.
  pref_service()->SetString(prefs::kBreakingNewsGCMSubscriptionTokenCache,
                            "token");

  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));
  auto handler = MakeHandler(task_runner);
  handler->StartListening(
      base::Bind([](std::unique_ptr<RemoteSuggestion> remote_suggestion) {}));

  // Check that the next validation is scheduled in time.
  EXPECT_CALL(*mock_instance_id(), GetToken(_, _, _, _)).Times(0);
  task_runner->FastForwardBy(time_to_validation -
                             base::TimeDelta::FromSeconds(1));

  EXPECT_CALL(*mock_instance_id(), GetToken(_, _, _, _))
      .WillOnce(
          InvokeCallbackArgument<3>("token", InstanceID::Result::SUCCESS));
  task_runner->FastForwardBy(base::TimeDelta::FromSeconds(1));

  // Check that the next forced subscription is scheduled in time.
  EXPECT_CALL(*mock_subscription_manager(), Subscribe("token")).Times(0);
  task_runner->FastForwardBy((time_to_subscription - time_to_validation) -
                             base::TimeDelta::FromSeconds(1));

  EXPECT_CALL(*mock_subscription_manager(), Subscribe("token"));
  task_runner->FastForwardBy(base::TimeDelta::FromSeconds(1));
}

TEST_F(BreakingNewsGCMAppHandlerTest, ShouldReportReceivedMessageWithoutNews) {
  base::HistogramTester histogram_tester;
  SetFeatureParams(/*enable_token_validation=*/false,
                   /*enable_forced_subscription=*/false);

  // Omit receiving the token by putting it there directly.
  pref_service()->SetString(prefs::kBreakingNewsGCMSubscriptionTokenCache,
                            "token");

  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));
  auto handler = MakeHandler(task_runner);
  handler->StartListening(
      base::Bind([](std::unique_ptr<RemoteSuggestion> remote_suggestion) {}));

  handler->OnMessage("com.google.breakingnews.gcm", gcm::IncomingMessage());

  EXPECT_THAT(histogram_tester.GetAllSamples(
                  "NewTabPage.ContentSuggestions.BreakingNews.MessageReceived"),
              ElementsAre(base::Bucket(
                  /*min=*/metrics::ReceivedMessageStatus::
                      WITHOUT_PUSHED_NEWS_AND_HANDLER_WAS_LISTENING,
                  /*count=*/1)));
}

TEST_F(BreakingNewsGCMAppHandlerTest,
       WhenNotListeningShouldIgnoreAndReportReceivedMessageWithoutNews) {
  base::HistogramTester histogram_tester;
  SetFeatureParams(/*enable_token_validation=*/false,
                   /*enable_forced_subscription=*/false);

  // Omit receiving the token by putting it there directly.
  pref_service()->SetString(prefs::kBreakingNewsGCMSubscriptionTokenCache,
                            "token");

  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));
  auto handler = MakeHandler(task_runner);

  // We do not verify that the message is not propagated futher, because there
  // is nowhere to propagate it. The handler just should not crash.
  handler->OnMessage("com.google.breakingnews.gcm", gcm::IncomingMessage());

  EXPECT_THAT(histogram_tester.GetAllSamples(
                  "NewTabPage.ContentSuggestions.BreakingNews.MessageReceived"),
              ElementsAre(base::Bucket(
                  /*min=*/metrics::ReceivedMessageStatus::
                      WITHOUT_PUSHED_NEWS_AND_HANDLER_WAS_NOT_LISTENING,
                  /*count=*/1)));
}

TEST_F(BreakingNewsGCMAppHandlerTest, ShouldReportReceivedMessageWithNews) {
  base::HistogramTester histogram_tester;
  SetFeatureParams(/*enable_token_validation=*/false,
                   /*enable_forced_subscription=*/false);

  // Omit receiving the token by putting it there directly.
  pref_service()->SetString(prefs::kBreakingNewsGCMSubscriptionTokenCache,
                            "token");

  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));
  auto handler = MakeHandler(task_runner);
  handler->StartListening(
      base::Bind([](std::unique_ptr<RemoteSuggestion> remote_suggestion) {}));
  gcm::IncomingMessage message;
  message.data["payload"] = "news";

  handler->OnMessage("com.google.breakingnews.gcm", message);

  EXPECT_THAT(
      histogram_tester.GetAllSamples(
          "NewTabPage.ContentSuggestions.BreakingNews.MessageReceived"),
      ElementsAre(base::Bucket(/*min=*/metrics::ReceivedMessageStatus::
                                   WITH_PUSHED_NEWS_AND_HANDLER_WAS_LISTENING,
                               /*count=*/1)));
}

TEST_F(BreakingNewsGCMAppHandlerTest,
       WhenNotListeningShouldIgnoreAndReportReceivedMessageWithNews) {
  base::HistogramTester histogram_tester;
  SetFeatureParams(/*enable_token_validation=*/false,
                   /*enable_forced_subscription=*/false);

  // Omit receiving the token by putting it there directly.
  pref_service()->SetString(prefs::kBreakingNewsGCMSubscriptionTokenCache,
                            "token");

  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));
  auto handler = MakeHandler(task_runner);

  gcm::IncomingMessage message;
  message.data["payload"] = "news";

  // We do not verify that the message is not propagated futher, because there
  // is nowhere to propagate it. The handler just should not crash.
  handler->OnMessage("com.google.breakingnews.gcm", message);

  EXPECT_THAT(histogram_tester.GetAllSamples(
                  "NewTabPage.ContentSuggestions.BreakingNews.MessageReceived"),
              ElementsAre(base::Bucket(
                  /*min=*/metrics::ReceivedMessageStatus::
                      WITH_PUSHED_NEWS_AND_HANDLER_WAS_NOT_LISTENING,
                  /*count=*/1)));
}

TEST_F(BreakingNewsGCMAppHandlerTest, ShouldReportTokenRetrievalResult) {
  base::HistogramTester histogram_tester;
  SetFeatureParams(/*enable_token_validation=*/false,
                   /*enable_forced_subscription=*/false);

  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));
  auto handler = MakeHandler(task_runner);

  EXPECT_CALL(*mock_instance_id(), GetToken(_, _, _, _))
      .WillOnce(InvokeCallbackArgument<3>(/*token=*/"",
                                          InstanceID::Result::NETWORK_ERROR));
  handler->StartListening(
      base::Bind([](std::unique_ptr<RemoteSuggestion> remote_suggestion) {}));

  EXPECT_THAT(
      histogram_tester.GetAllSamples(
          "NewTabPage.ContentSuggestions.BreakingNews.TokenRetrievalResult"),
      ElementsAre(base::Bucket(
          /*min=*/static_cast<int>(InstanceID::Result::NETWORK_ERROR),
          /*count=*/1)));
}

TEST_F(BreakingNewsGCMAppHandlerTest,
       ShouldReportTimeSinceLastTokenValidation) {
  base::HistogramTester histogram_tester;
  SetFeatureParams(/*enable_token_validation=*/true,
                   /*enable_forced_subscription=*/false);

  // The next validation will be soon.
  const base::TimeDelta time_to_validation = base::TimeDelta::FromHours(1);
  const base::Time last_validation =
      GetDummyNow() - (GetTokenValidationPeriod() - time_to_validation);
  pref_service()->SetInt64(prefs::kBreakingNewsGCMLastTokenValidationTime,
                           SerializeTime(last_validation));
  // Omit receiving the token by putting it there directly.
  pref_service()->SetString(prefs::kBreakingNewsGCMSubscriptionTokenCache,
                            "token");

  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));
  auto handler = MakeHandler(task_runner);
  handler->StartListening(
      base::Bind([](std::unique_ptr<RemoteSuggestion> remote_suggestion) {}));

  // Check that handler does not report the metric before the validation.
  task_runner->FastForwardBy(time_to_validation -
                             base::TimeDelta::FromSeconds(1));
  EXPECT_THAT(
      histogram_tester.GetAllSamples(
          "NewTabPage.ContentSuggestions.BreakingNews.TokenRetrievalResult"),
      IsEmpty());

  // But when the validation happens, the time is reported.
  EXPECT_CALL(*mock_instance_id(), GetToken(_, _, _, _))
      .WillOnce(
          InvokeCallbackArgument<3>("token", InstanceID::Result::SUCCESS));
  task_runner->FastForwardBy(base::TimeDelta::FromSeconds(1));

  histogram_tester.ExpectTimeBucketCount(
      "NewTabPage.ContentSuggestions.BreakingNews.TimeSinceLastTokenValidation",
      GetTokenValidationPeriod(), /*count=*/1);
  // |ExpectTimeBucketCount| allows other buckets. Let's ensure that there are
  // none.
  histogram_tester.ExpectTotalCount(
      "NewTabPage.ContentSuggestions.BreakingNews.TimeSinceLastTokenValidation",
      /*count=*/1);
}

TEST_F(BreakingNewsGCMAppHandlerTest,
       ShouldReportWhetherTokenWasValidBeforeValidation) {
  base::HistogramTester histogram_tester;
  SetFeatureParams(/*enable_token_validation=*/true,
                   /*enable_forced_subscription=*/false);

  // The next validation will be soon.
  const base::TimeDelta time_to_validation = base::TimeDelta::FromHours(1);
  const base::Time last_validation =
      GetDummyNow() - (GetTokenValidationPeriod() - time_to_validation);
  pref_service()->SetInt64(prefs::kBreakingNewsGCMLastTokenValidationTime,
                           SerializeTime(last_validation));
  // Omit receiving the token by putting it there directly.
  pref_service()->SetString(prefs::kBreakingNewsGCMSubscriptionTokenCache,
                            "token");

  scoped_refptr<TestMockTimeTaskRunner> task_runner(
      new TestMockTimeTaskRunner(GetDummyNow(), TimeTicks::Now()));
  auto handler = MakeHandler(task_runner);
  handler->StartListening(
      base::Bind([](std::unique_ptr<RemoteSuggestion> remote_suggestion) {}));

  // Check that handler does not report the metric before the validation.
  task_runner->FastForwardBy(time_to_validation -
                             base::TimeDelta::FromSeconds(1));
  EXPECT_THAT(histogram_tester.GetAllSamples("NewTabPage.ContentSuggestions."
                                             "BreakingNews."
                                             "WasTokenValidBeforeValidation"),
              IsEmpty());

  // But when the validation happens, the old token validness is reported.
  EXPECT_CALL(*mock_instance_id(), GetToken(_, _, _, _))
      .WillOnce(
          InvokeCallbackArgument<3>("token", InstanceID::Result::SUCCESS));
  task_runner->FastForwardBy(base::TimeDelta::FromSeconds(1));

  EXPECT_THAT(histogram_tester.GetAllSamples("NewTabPage.ContentSuggestions."
                                             "BreakingNews."
                                             "WasTokenValidBeforeValidation"),
              ElementsAre(base::Bucket(
                  /*min=*/1,
                  /*count=*/1)));
}

}  // namespace ntp_snippets
