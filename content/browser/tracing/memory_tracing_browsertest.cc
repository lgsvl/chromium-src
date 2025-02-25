// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include "base/bind.h"
#include "base/callback_forward.h"
#include "base/command_line.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/trace_event/memory_dump_manager.h"
#include "base/trace_event/memory_dump_provider.h"
#include "base/trace_event/memory_dump_request_args.h"
#include "base/trace_event/trace_config_memory_test_util.h"
#include "base/trace_event/trace_log.h"
#include "build/build_config.h"
#include "content/browser/tracing/tracing_controller_impl.h"
#include "content/public/browser/tracing_controller.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "services/resource_coordinator/public/cpp/memory_instrumentation/memory_instrumentation.h"
#include "testing/gmock/include/gmock/gmock.h"

using base::trace_event::MemoryDumpArgs;
using base::trace_event::MemoryDumpLevelOfDetail;
using base::trace_event::MemoryDumpManager;
using base::trace_event::MemoryDumpType;
using base::trace_event::ProcessMemoryDump;
using testing::_;
using testing::Return;

namespace content {

// A mock dump provider, used to check that dump requests actually end up
// creating memory dumps.
class MockDumpProvider : public base::trace_event::MemoryDumpProvider {
 public:
  MOCK_METHOD2(OnMemoryDump, bool(const MemoryDumpArgs& args,
                                  ProcessMemoryDump* pmd));
};

class MemoryTracingTest : public ContentBrowserTest {
 public:
  // Used as callback argument for MemoryDumpManager::RequestGlobalDump():
  void OnGlobalMemoryDumpDone(
      scoped_refptr<base::SingleThreadTaskRunner> task_runner,
      base::Closure closure,
      uint32_t request_index,
      bool success,
      uint64_t dump_guid) {
    // Make sure we run the RunLoop closure on the same thread that originated
    // the run loop (which is the IN_PROC_BROWSER_TEST_F main thread).
    if (!task_runner->RunsTasksInCurrentSequence()) {
      task_runner->PostTask(
          FROM_HERE,
          base::BindOnce(&MemoryTracingTest::OnGlobalMemoryDumpDone,
                         base::Unretained(this), task_runner, closure,
                         request_index, success, dump_guid));
      return;
    }
    if (success)
      EXPECT_NE(0u, dump_guid);
    OnMemoryDumpDone(request_index, success);
    if (!closure.is_null())
      closure.Run();
  }

  void RequestGlobalDumpWithClosure(
      bool from_renderer_thread,
      const MemoryDumpType& dump_type,
      const MemoryDumpLevelOfDetail& level_of_detail,
      const base::Closure& closure) {
    uint32_t request_index = next_request_index_++;
    memory_instrumentation::MemoryInstrumentation::
        RequestGlobalDumpAndAppendToTraceCallback callback = base::Bind(
            &MemoryTracingTest::OnGlobalMemoryDumpDone, base::Unretained(this),
            base::ThreadTaskRunnerHandle::Get(), closure, request_index);
    if (from_renderer_thread) {
      PostTaskToInProcessRendererAndWait(base::Bind(
          &memory_instrumentation::MemoryInstrumentation::
              RequestGlobalDumpAndAppendToTrace,
          base::Unretained(
              memory_instrumentation::MemoryInstrumentation::GetInstance()),
          dump_type, level_of_detail, callback));
    } else {
      memory_instrumentation::MemoryInstrumentation::GetInstance()
          ->RequestGlobalDumpAndAppendToTrace(dump_type, level_of_detail,
                                              callback);
    }
  }

 protected:
  void SetUp() override {
    next_request_index_ = 0;

    mock_dump_provider_.reset(new MockDumpProvider());
    MemoryDumpManager::GetInstance()->RegisterDumpProvider(
        mock_dump_provider_.get(), "MockDumpProvider", nullptr);
    MemoryDumpManager::GetInstance()
        ->set_dumper_registrations_ignored_for_testing(false);
    ContentBrowserTest::SetUp();
  }

  void TearDown() override {
    MemoryDumpManager::GetInstance()->UnregisterAndDeleteDumpProviderSoon(
        std::move(mock_dump_provider_));
    mock_dump_provider_.reset();
    ContentBrowserTest::TearDown();
  }

  void EnableMemoryTracing() {
    // Re-enabling tracing could crash these tests https://crbug.com/657628 .
    if (base::trace_event::TraceLog::GetInstance()->IsEnabled()) {
      FAIL() << "Tracing seems to be already enabled. "
                "Very likely this is because the startup tracing file "
                "has been leaked from a previous test.";
    }
    // Enable tracing without periodic dumps.
    base::trace_event::TraceConfig trace_config(
        base::trace_event::TraceConfigMemoryTestUtil::
            GetTraceConfig_EmptyTriggers());

    base::RunLoop run_loop;
    bool success = TracingController::GetInstance()->StartTracing(
      trace_config, run_loop.QuitClosure());
    EXPECT_TRUE(success);
    run_loop.Run();
  }

  void DisableTracing() {
    base::RunLoop run_loop;
    bool success = TracingController::GetInstance()->StopTracing(
        TracingControllerImpl::CreateCallbackEndpoint(base::BindRepeating(
            [](base::Closure quit_closure,
               std::unique_ptr<const base::DictionaryValue> metadata,
               base::RefCountedString* trace_str) { quit_closure.Run(); },
            run_loop.QuitClosure())));
    EXPECT_TRUE(success);
    run_loop.Run();
  }

  void RequestGlobalDumpAndWait(
      bool from_renderer_thread,
      const MemoryDumpType& dump_type,
      const MemoryDumpLevelOfDetail& level_of_detail) {
    base::RunLoop run_loop;
    RequestGlobalDumpWithClosure(from_renderer_thread, dump_type,
                                 level_of_detail, run_loop.QuitClosure());
    run_loop.Run();
  }

  void RequestGlobalDump(bool from_renderer_thread,
                         const MemoryDumpType& dump_type,
                         const MemoryDumpLevelOfDetail& level_of_detail) {
    RequestGlobalDumpWithClosure(from_renderer_thread, dump_type,
                                 level_of_detail, base::Closure());
  }

  void Navigate(Shell* shell) {
    NavigateToURL(shell, GetTestUrl("", "title1.html"));
  }

  MOCK_METHOD2(OnMemoryDumpDone, void(uint32_t request_index, bool successful));

  base::Closure on_memory_dump_complete_closure_;
  std::unique_ptr<MockDumpProvider> mock_dump_provider_;
  uint32_t next_request_index_;
  bool last_callback_success_;
};

// Run SingleProcessMemoryTracingTests only on Android, since these tests are
// intended to give coverage to Android WebView.
#if defined(OS_ANDROID)

class SingleProcessMemoryTracingTest : public MemoryTracingTest {
 public:
  SingleProcessMemoryTracingTest() {}

  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(switches::kSingleProcess);
  }
};

// Checks that a memory dump initiated from a the main browser thread ends up in
// a single dump even in single process mode.
IN_PROC_BROWSER_TEST_F(SingleProcessMemoryTracingTest,
                       BrowserInitiatedSingleDump) {
  Navigate(shell());

  EXPECT_CALL(*mock_dump_provider_, OnMemoryDump(_,_)).WillOnce(Return(true));
  EXPECT_CALL(*this, OnMemoryDumpDone(_, true /* success */));

  EnableMemoryTracing();
  RequestGlobalDumpAndWait(false /* from_renderer_thread */,
                           MemoryDumpType::EXPLICITLY_TRIGGERED,
                           MemoryDumpLevelOfDetail::DETAILED);
  DisableTracing();
}

// Checks that a memory dump initiated from a renderer thread ends up in a
// single dump even in single process mode.
IN_PROC_BROWSER_TEST_F(SingleProcessMemoryTracingTest,
                       RendererInitiatedSingleDump) {
  Navigate(shell());

  EXPECT_CALL(*mock_dump_provider_, OnMemoryDump(_,_)).WillOnce(Return(true));
  EXPECT_CALL(*this, OnMemoryDumpDone(_, true /* success */));

  EnableMemoryTracing();
  RequestGlobalDumpAndWait(true /* from_renderer_thread */,
                           MemoryDumpType::EXPLICITLY_TRIGGERED,
                           MemoryDumpLevelOfDetail::DETAILED);
  DisableTracing();
}

IN_PROC_BROWSER_TEST_F(SingleProcessMemoryTracingTest, ManyInterleavedDumps) {
  Navigate(shell());

  EXPECT_CALL(*mock_dump_provider_, OnMemoryDump(_,_))
      .Times(4)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*this, OnMemoryDumpDone(_, true /* success */)).Times(4);

  EnableMemoryTracing();
  RequestGlobalDumpAndWait(true /* from_renderer_thread */,
                           MemoryDumpType::EXPLICITLY_TRIGGERED,
                           MemoryDumpLevelOfDetail::DETAILED);
  RequestGlobalDumpAndWait(false /* from_renderer_thread */,
                           MemoryDumpType::EXPLICITLY_TRIGGERED,
                           MemoryDumpLevelOfDetail::DETAILED);
  RequestGlobalDumpAndWait(false /* from_renderer_thread */,
                           MemoryDumpType::EXPLICITLY_TRIGGERED,
                           MemoryDumpLevelOfDetail::DETAILED);
  RequestGlobalDumpAndWait(true /* from_renderer_thread */,
                           MemoryDumpType::EXPLICITLY_TRIGGERED,
                           MemoryDumpLevelOfDetail::DETAILED);
  DisableTracing();
}

// Checks that, if there already is a memory dump in progress, subsequent memory
// dump requests are queued and carried out after it's finished. Also checks
// that periodic dump requests fail in case there is already a request in the
// queue with the same level of detail.
// Flaky failures on all platforms. https://crbug.com/752613
IN_PROC_BROWSER_TEST_F(SingleProcessMemoryTracingTest, DISABLED_QueuedDumps) {
  Navigate(shell());

  EnableMemoryTracing();

  // Issue the following 6 global memory dump requests:
  //
  //   0 (ED)  req-------------------------------------->ok
  //   1 (PD)      req->fail(0)
  //   2 (PL)                   req------------------------>ok
  //   3 (PL)                       req->fail(2)
  //   4 (EL)                                    req---------->ok
  //   5 (ED)                                        req--------->ok
  //   6 (PL)                                                        req->ok
  //
  // where P=PERIODIC_INTERVAL, E=EXPLICITLY_TRIGGERED, D=DETAILED and L=LIGHT.

  EXPECT_CALL(*mock_dump_provider_, OnMemoryDump(_, _))
      .Times(5)
      .WillRepeatedly(Return(true));

  EXPECT_CALL(*this, OnMemoryDumpDone(0, true /* success */));
  RequestGlobalDump(true /* from_renderer_thread */,
                    MemoryDumpType::EXPLICITLY_TRIGGERED,
                    MemoryDumpLevelOfDetail::DETAILED);

  // This dump should fail immediately because there's already a detailed dump
  // request in the queue.
  EXPECT_CALL(*this, OnMemoryDumpDone(1, false /* success */));
  RequestGlobalDump(true /* from_renderer_thread */,
                    MemoryDumpType::PERIODIC_INTERVAL,
                    MemoryDumpLevelOfDetail::DETAILED);

  EXPECT_CALL(*this, OnMemoryDumpDone(2, true /* success */));
  RequestGlobalDump(true /* from_renderer_thread */,
                    MemoryDumpType::PERIODIC_INTERVAL,
                    MemoryDumpLevelOfDetail::LIGHT);

  // This dump should fail immediately because there's already a light dump
  // request in the queue.
  EXPECT_CALL(*this, OnMemoryDumpDone(3, false /* success */));
  RequestGlobalDump(true /* from_renderer_thread */,
                    MemoryDumpType::PERIODIC_INTERVAL,
                    MemoryDumpLevelOfDetail::LIGHT);

  EXPECT_CALL(*this, OnMemoryDumpDone(4, true /* success */));
  RequestGlobalDump(true /* from_renderer_thread */,
                    MemoryDumpType::EXPLICITLY_TRIGGERED,
                    MemoryDumpLevelOfDetail::LIGHT);

  EXPECT_CALL(*this, OnMemoryDumpDone(5, true /* success */));
  RequestGlobalDumpAndWait(true /* from_renderer_thread */,
                           MemoryDumpType::EXPLICITLY_TRIGGERED,
                           MemoryDumpLevelOfDetail::DETAILED);

  EXPECT_CALL(*this, OnMemoryDumpDone(6, true /* success */));
  RequestGlobalDumpAndWait(true /* from_renderer_thread */,
                           MemoryDumpType::PERIODIC_INTERVAL,
                           MemoryDumpLevelOfDetail::LIGHT);

  DisableTracing();
}

#endif  // defined(OS_ANDROID)

// Non-deterministic races under TSan. crbug.com/529678
#if defined(THREAD_SANITIZER)
#define MAYBE_BrowserInitiatedDump DISABLED_BrowserInitiatedDump
#else
#define MAYBE_BrowserInitiatedDump BrowserInitiatedDump
#endif
// Checks that a memory dump initiated from a the main browser thread ends up in
// a successful dump.
IN_PROC_BROWSER_TEST_F(MemoryTracingTest, MAYBE_BrowserInitiatedDump) {
  Navigate(shell());

  EXPECT_CALL(*mock_dump_provider_, OnMemoryDump(_,_)).WillOnce(Return(true));
#if defined(OS_LINUX)
  // TODO(ssid): Test for dump success once the on start tracing done callback
  // is fixed to be called after enable tracing is acked by all processes,
  // crbug.com/709524. The test still tests if dumping does not crash.
  EXPECT_CALL(*this, OnMemoryDumpDone(_, _));
#else
  EXPECT_CALL(*this, OnMemoryDumpDone(_, true /* success */));
#endif

  EnableMemoryTracing();
  RequestGlobalDumpAndWait(false /* from_renderer_thread */,
                           MemoryDumpType::EXPLICITLY_TRIGGERED,
                           MemoryDumpLevelOfDetail::DETAILED);
  DisableTracing();
}

}  // namespace content
