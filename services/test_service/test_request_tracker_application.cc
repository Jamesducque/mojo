// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/test_service/test_request_tracker_application.h"

#include <assert.h>

#include "mojo/public/c/system/main.h"
#include "mojo/public/cpp/application/application_connection.h"
#include "mojo/public/cpp/application/application_runner.h"
#include "services/test_service/test_time_service_impl.h"

namespace mojo {
namespace test {

TestRequestTrackerApplication::TestRequestTrackerApplication()
    : test_tracked_request_factory_(&context_) {
}

TestRequestTrackerApplication::~TestRequestTrackerApplication() {
}

bool TestRequestTrackerApplication::ConfigureIncomingConnection(
    ApplicationConnection* connection) {
  // Every instance of the service and recorder shares the context.
  // Note, this app is single-threaded, so this is thread safe.
  connection->AddService(&test_tracked_request_factory_);
  connection->AddService<TestTimeService>(this);
  connection->AddService<TestRequestTracker>(this);
  return true;
}

void TestRequestTrackerApplication::Create(
    ApplicationConnection* connection,
    InterfaceRequest<TestTimeService> request) {
  new TestTimeServiceImpl(connection, request.Pass());
}

void TestRequestTrackerApplication::Create(
    ApplicationConnection* connection,
    InterfaceRequest<TestRequestTracker> request) {
  TestRequestTrackerImpl* impl =
      BindToRequest(new TestRequestTrackerImpl(&context_), &request);
  impl->OnConnectionEstablished();
}

}  // namespace test
}  // namespace mojo

MojoResult MojoMain(MojoHandle shell_handle) {
  mojo::ApplicationRunner runner(
      new mojo::test::TestRequestTrackerApplication);
  return runner.Run(shell_handle);
}