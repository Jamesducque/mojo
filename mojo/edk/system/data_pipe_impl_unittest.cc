// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains tests that are shared between different implementations of
// |DataPipeImpl|.

#include "mojo/edk/system/data_pipe_impl.h"

#include <stdint.h>

#include "base/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "base/test/test_io_thread.h"
#include "mojo/edk/embedder/platform_channel_pair.h"
#include "mojo/edk/embedder/simple_platform_support.h"
#include "mojo/edk/system/channel.h"
#include "mojo/edk/system/channel_endpoint.h"
#include "mojo/edk/system/data_pipe.h"
#include "mojo/edk/system/data_pipe_consumer_dispatcher.h"
#include "mojo/edk/system/data_pipe_producer_dispatcher.h"
#include "mojo/edk/system/memory.h"
#include "mojo/edk/system/message_pipe.h"
#include "mojo/edk/system/raw_channel.h"
#include "mojo/edk/system/waiter.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {
namespace system {
namespace {

const MojoHandleSignals kAllSignals = MOJO_HANDLE_SIGNAL_READABLE |
                                      MOJO_HANDLE_SIGNAL_WRITABLE |
                                      MOJO_HANDLE_SIGNAL_PEER_CLOSED;
const uint32_t kSizeOfOptions =
    static_cast<uint32_t>(sizeof(MojoCreateDataPipeOptions));

// DataPipeImplTestHelper ------------------------------------------------------

class DataPipeImplTestHelper {
 public:
  virtual ~DataPipeImplTestHelper() {}

  virtual void SetUp() = 0;
  virtual void TearDown() = 0;

  virtual void Create(const MojoCreateDataPipeOptions& validated_options) = 0;

  // Possibly transfers the producer/consumer.
  virtual void DoTransfer() = 0;

  // Returns the |DataPipe| object for the producer and consumer, respectively.
  virtual DataPipe* dpp() = 0;
  virtual DataPipe* dpc() = 0;

  virtual void ProducerClose() = 0;
  virtual void ConsumerClose() = 0;

 protected:
  DataPipeImplTestHelper() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(DataPipeImplTestHelper);
};

// DataPipeImplTest ------------------------------------------------------------

template <class Helper>
class DataPipeImplTest : public testing::Test {
 public:
  DataPipeImplTest() {}
  ~DataPipeImplTest() override {}

  void SetUp() override { helper_.SetUp(); }
  void TearDown() override { helper_.TearDown(); }

 protected:
  void Create(const MojoCreateDataPipeOptions& options) {
    MojoCreateDataPipeOptions validated_options = {};
    ASSERT_EQ(MOJO_RESULT_OK,
              DataPipe::ValidateCreateOptions(MakeUserPointer(&options),
                                              &validated_options));
    helper_.Create(validated_options);
  }

  void DoTransfer() { return helper_.DoTransfer(); }

  DataPipe* dpp() { return helper_.dpp(); }
  DataPipe* dpc() { return helper_.dpc(); }

  void ProducerClose() { helper_.ProducerClose(); }
  void ConsumerClose() { helper_.ConsumerClose(); }

 private:
  Helper helper_;

  DISALLOW_COPY_AND_ASSIGN(DataPipeImplTest);
};

// LocalDataPipeImplTestHelper -------------------------------------------------

class LocalDataPipeImplTestHelper : public DataPipeImplTestHelper {
 public:
  LocalDataPipeImplTestHelper() {}
  ~LocalDataPipeImplTestHelper() override {}

  void SetUp() override {}
  void TearDown() override {}

  void Create(const MojoCreateDataPipeOptions& validated_options) override {
    CHECK(!dp_);
    dp_ = DataPipe::CreateLocal(validated_options);
  }

  void DoTransfer() override {}

  // Returns the |DataPipe| object for the producer and consumer, respectively.
  DataPipe* dpp() override { return dp_.get(); }
  DataPipe* dpc() override { return dp_.get(); }

  void ProducerClose() override { dp_->ProducerClose(); }
  void ConsumerClose() override { dp_->ConsumerClose(); }

 private:
  scoped_refptr<DataPipe> dp_;

  DISALLOW_COPY_AND_ASSIGN(LocalDataPipeImplTestHelper);
};

// RemoteDataPipeImplTestHelper ------------------------------------------------

// Base class for |Remote{Producer,Consumer}DataPipeImplTestHelper|.
class RemoteDataPipeImplTestHelper : public DataPipeImplTestHelper {
 public:
  RemoteDataPipeImplTestHelper() : io_thread_(base::TestIOThread::kAutoStart) {}
  ~RemoteDataPipeImplTestHelper() override {}

  void SetUp() override {
    scoped_refptr<ChannelEndpoint> ep[2];
    message_pipes_[0] = MessagePipe::CreateLocalProxy(&ep[0]);
    message_pipes_[1] = MessagePipe::CreateLocalProxy(&ep[1]);

    io_thread_.PostTaskAndWait(
        FROM_HERE, base::Bind(&RemoteDataPipeImplTestHelper::SetUpOnIOThread,
                              base::Unretained(this), ep[0], ep[1]));
  }

  void TearDown() override {
    EnsureMessagePipeClosed(0);
    EnsureMessagePipeClosed(1);
    io_thread_.PostTaskAndWait(
        FROM_HERE, base::Bind(&RemoteDataPipeImplTestHelper::TearDownOnIOThread,
                              base::Unretained(this)));
  }

  void Create(const MojoCreateDataPipeOptions& validated_options) override {
    CHECK(!dp_);
    dp_ = DataPipe::CreateLocal(validated_options);
  }

 protected:
  scoped_refptr<MessagePipe> message_pipe(size_t i) {
    return message_pipes_[i];
  }

  void EnsureMessagePipeClosed(size_t i) {
    if (!message_pipes_[i])
      return;
    message_pipes_[i]->Close(0);
    message_pipes_[i] = nullptr;
  }

  void SetUpOnIOThread(scoped_refptr<ChannelEndpoint> ep0,
                       scoped_refptr<ChannelEndpoint> ep1) {
    CHECK_EQ(base::MessageLoop::current(), io_thread_.message_loop());

    embedder::PlatformChannelPair channel_pair;
    channels_[0] = new Channel(&platform_support_);
    channels_[0]->Init(RawChannel::Create(channel_pair.PassServerHandle()));
    channels_[0]->SetBootstrapEndpoint(ep0);
    channels_[1] = new Channel(&platform_support_);
    channels_[1]->Init(RawChannel::Create(channel_pair.PassClientHandle()));
    channels_[1]->SetBootstrapEndpoint(ep1);
  }

  void TearDownOnIOThread() {
    CHECK_EQ(base::MessageLoop::current(), io_thread_.message_loop());

    if (channels_[0]) {
      channels_[0]->Shutdown();
      channels_[0] = nullptr;
    }
    if (channels_[1]) {
      channels_[1]->Shutdown();
      channels_[1] = nullptr;
    }
  }

  embedder::SimplePlatformSupport platform_support_;
  base::TestIOThread io_thread_;
  scoped_refptr<Channel> channels_[2];
  scoped_refptr<MessagePipe> message_pipes_[2];

  scoped_refptr<DataPipe> dp_;

  DISALLOW_COPY_AND_ASSIGN(RemoteDataPipeImplTestHelper);
};

// RemoteProducerDataPipeImplTestHelper ----------------------------------------

class RemoteProducerDataPipeImplTestHelper
    : public RemoteDataPipeImplTestHelper {
 public:
  RemoteProducerDataPipeImplTestHelper() {}
  ~RemoteProducerDataPipeImplTestHelper() override {}

  void DoTransfer() override {
    // Write the producer to MP 0 (port 0). Wait and receive on MP 1 (port 0).
    // (Add the waiter first, to avoid any handling the case where it's already
    // readable.)
    Waiter waiter;
    waiter.Init();
    ASSERT_EQ(MOJO_RESULT_OK,
              message_pipe(1)->AddAwakable(
                  0, &waiter, MOJO_HANDLE_SIGNAL_READABLE, 987, nullptr));
    {
      // This is the producer dispatcher we'll send.
      scoped_refptr<DataPipeProducerDispatcher> to_send =
          new DataPipeProducerDispatcher();
      to_send->Init(dp_);

      DispatcherTransport transport(
          test::DispatcherTryStartTransport(to_send.get()));
      ASSERT_TRUE(transport.is_valid());

      std::vector<DispatcherTransport> transports;
      transports.push_back(transport);
      ASSERT_EQ(MOJO_RESULT_OK, message_pipe(0)->WriteMessage(
                                    0, NullUserPointer(), 0, &transports,
                                    MOJO_WRITE_MESSAGE_FLAG_NONE));
      transport.End();

      // |to_send| should have been closed. This is |DCHECK()|ed when it is
      // destroyed.
      EXPECT_TRUE(to_send->HasOneRef());
    }
    uint32_t context = 0;
    ASSERT_EQ(MOJO_RESULT_OK, waiter.Wait(MOJO_DEADLINE_INDEFINITE, &context));
    EXPECT_EQ(987u, context);
    HandleSignalsState hss = HandleSignalsState();
    message_pipe(1)->RemoveAwakable(0, &waiter, &hss);
    EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_WRITABLE,
              hss.satisfied_signals);
    EXPECT_EQ(kAllSignals, hss.satisfiable_signals);
    char read_buffer[100] = {};
    uint32_t read_buffer_size = static_cast<uint32_t>(sizeof(read_buffer));
    DispatcherVector read_dispatchers;
    uint32_t read_num_dispatchers = 10;  // Maximum to get.
    ASSERT_EQ(MOJO_RESULT_OK,
              message_pipe(1)->ReadMessage(
                  0, UserPointer<void>(read_buffer),
                  MakeUserPointer(&read_buffer_size), &read_dispatchers,
                  &read_num_dispatchers, MOJO_READ_MESSAGE_FLAG_NONE));
    EXPECT_EQ(0u, static_cast<size_t>(read_buffer_size));
    ASSERT_EQ(1u, read_dispatchers.size());
    ASSERT_EQ(1u, read_num_dispatchers);
    ASSERT_TRUE(read_dispatchers[0]);
    EXPECT_TRUE(read_dispatchers[0]->HasOneRef());

    ASSERT_EQ(Dispatcher::kTypeDataPipeProducer,
              read_dispatchers[0]->GetType());
    producer_dispatcher_ =
        static_cast<DataPipeProducerDispatcher*>(read_dispatchers[0].get());
  }

  DataPipe* dpp() override {
    if (producer_dispatcher_)
      return producer_dispatcher_->GetDataPipeForTest();
    return dp_.get();
  }
  DataPipe* dpc() override { return dp_.get(); }

  void ProducerClose() override {
    if (producer_dispatcher_)
      ASSERT_EQ(MOJO_RESULT_OK, producer_dispatcher_->Close());
    else
      dp_->ProducerClose();
  }
  void ConsumerClose() override { dp_->ConsumerClose(); }

 private:
  scoped_refptr<DataPipeProducerDispatcher> producer_dispatcher_;

  DISALLOW_COPY_AND_ASSIGN(RemoteProducerDataPipeImplTestHelper);
};

// RemoteConsumerDataPipeImplTestHelper ----------------------------------------

class RemoteConsumerDataPipeImplTestHelper
    : public RemoteDataPipeImplTestHelper {
 public:
  RemoteConsumerDataPipeImplTestHelper() {}
  ~RemoteConsumerDataPipeImplTestHelper() override {}

  void DoTransfer() override {
    // Write the consumer to MP 0 (port 0). Wait and receive on MP 1 (port 0).
    // (Add the waiter first, to avoid any handling the case where it's already
    // readable.)
    Waiter waiter;
    waiter.Init();
    ASSERT_EQ(MOJO_RESULT_OK,
              message_pipe(1)->AddAwakable(
                  0, &waiter, MOJO_HANDLE_SIGNAL_READABLE, 987, nullptr));
    {
      // This is the consumer dispatcher we'll send.
      scoped_refptr<DataPipeConsumerDispatcher> to_send =
          new DataPipeConsumerDispatcher();
      to_send->Init(dp_);

      DispatcherTransport transport(
          test::DispatcherTryStartTransport(to_send.get()));
      ASSERT_TRUE(transport.is_valid());

      std::vector<DispatcherTransport> transports;
      transports.push_back(transport);
      ASSERT_EQ(MOJO_RESULT_OK, message_pipe(0)->WriteMessage(
                                    0, NullUserPointer(), 0, &transports,
                                    MOJO_WRITE_MESSAGE_FLAG_NONE));
      transport.End();

      // |to_send| should have been closed. This is |DCHECK()|ed when it is
      // destroyed.
      EXPECT_TRUE(to_send->HasOneRef());
    }
    uint32_t context = 0;
    ASSERT_EQ(MOJO_RESULT_OK, waiter.Wait(MOJO_DEADLINE_INDEFINITE, &context));
    EXPECT_EQ(987u, context);
    HandleSignalsState hss = HandleSignalsState();
    message_pipe(1)->RemoveAwakable(0, &waiter, &hss);
    EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_WRITABLE,
              hss.satisfied_signals);
    EXPECT_EQ(kAllSignals, hss.satisfiable_signals);
    char read_buffer[100] = {};
    uint32_t read_buffer_size = static_cast<uint32_t>(sizeof(read_buffer));
    DispatcherVector read_dispatchers;
    uint32_t read_num_dispatchers = 10;  // Maximum to get.
    ASSERT_EQ(MOJO_RESULT_OK,
              message_pipe(1)->ReadMessage(
                  0, UserPointer<void>(read_buffer),
                  MakeUserPointer(&read_buffer_size), &read_dispatchers,
                  &read_num_dispatchers, MOJO_READ_MESSAGE_FLAG_NONE));
    EXPECT_EQ(0u, static_cast<size_t>(read_buffer_size));
    ASSERT_EQ(1u, read_dispatchers.size());
    ASSERT_EQ(1u, read_num_dispatchers);
    ASSERT_TRUE(read_dispatchers[0]);
    EXPECT_TRUE(read_dispatchers[0]->HasOneRef());

    ASSERT_EQ(Dispatcher::kTypeDataPipeConsumer,
              read_dispatchers[0]->GetType());
    consumer_dispatcher_ =
        static_cast<DataPipeConsumerDispatcher*>(read_dispatchers[0].get());
  }

  DataPipe* dpp() override { return dp_.get(); }
  DataPipe* dpc() override {
    if (consumer_dispatcher_)
      return consumer_dispatcher_->GetDataPipeForTest();
    return dp_.get();
  }

  void ProducerClose() override { dp_->ProducerClose(); }
  void ConsumerClose() override {
    if (consumer_dispatcher_)
      ASSERT_EQ(MOJO_RESULT_OK, consumer_dispatcher_->Close());
    else
      dp_->ConsumerClose();
  }

 private:
  scoped_refptr<DataPipeConsumerDispatcher> consumer_dispatcher_;

  DISALLOW_COPY_AND_ASSIGN(RemoteConsumerDataPipeImplTestHelper);
};

// Test case instantiation -----------------------------------------------------

typedef testing::Types<LocalDataPipeImplTestHelper,
                       RemoteProducerDataPipeImplTestHelper,
                       RemoteConsumerDataPipeImplTestHelper> HelperTypes;

TYPED_TEST_CASE(DataPipeImplTest, HelperTypes);

// Tests -----------------------------------------------------------------------

TYPED_TEST(DataPipeImplTest, SimpleReadWrite) {
  const MojoCreateDataPipeOptions options = {
      kSizeOfOptions,                           // |struct_size|.
      MOJO_CREATE_DATA_PIPE_OPTIONS_FLAG_NONE,  // |flags|.
      static_cast<uint32_t>(sizeof(int32_t)),   // |element_num_bytes|.
      1000 * sizeof(int32_t)                    // |capacity_num_bytes|.
  };
  this->Create(options);
  this->DoTransfer();

  Waiter waiter;
  HandleSignalsState hss;
  uint32_t context;

  int32_t elements[10] = {};
  uint32_t num_bytes = 0;

  // Try reading; nothing there yet.
  num_bytes = static_cast<uint32_t>(arraysize(elements) * sizeof(elements[0]));
  EXPECT_EQ(
      MOJO_RESULT_SHOULD_WAIT,
      this->dpc()->ConsumerReadData(UserPointer<void>(elements),
                                    MakeUserPointer(&num_bytes), false, false));

  // Query; nothing there yet.
  num_bytes = 0;
  EXPECT_EQ(MOJO_RESULT_OK,
            this->dpc()->ConsumerQueryData(MakeUserPointer(&num_bytes)));
  EXPECT_EQ(0u, num_bytes);

  // Discard; nothing there yet.
  num_bytes = static_cast<uint32_t>(5u * sizeof(elements[0]));
  EXPECT_EQ(MOJO_RESULT_SHOULD_WAIT, this->dpc()->ConsumerDiscardData(
                                         MakeUserPointer(&num_bytes), false));

  // Read with invalid |num_bytes|.
  num_bytes = sizeof(elements[0]) + 1;
  EXPECT_EQ(
      MOJO_RESULT_INVALID_ARGUMENT,
      this->dpc()->ConsumerReadData(UserPointer<void>(elements),
                                    MakeUserPointer(&num_bytes), false, false));

  // For remote data pipes, we'll have to wait; add the waiter before writing.
  waiter.Init();
  ASSERT_EQ(MOJO_RESULT_OK,
            this->dpc()->ConsumerAddAwakable(
                &waiter, MOJO_HANDLE_SIGNAL_READABLE, 123, nullptr));

  // Write two elements.
  elements[0] = 123;
  elements[1] = 456;
  num_bytes = static_cast<uint32_t>(2u * sizeof(elements[0]));
  EXPECT_EQ(MOJO_RESULT_OK,
            this->dpp()->ProducerWriteData(UserPointer<const void>(elements),
                                           MakeUserPointer(&num_bytes), false));
  // It should have written everything (even without "all or none").
  EXPECT_EQ(2u * sizeof(elements[0]), num_bytes);

  // Wait.
  context = 0;
  EXPECT_EQ(MOJO_RESULT_OK, waiter.Wait(MOJO_DEADLINE_INDEFINITE, &context));
  EXPECT_EQ(123u, context);
  hss = HandleSignalsState();
  this->dpc()->ConsumerRemoveAwakable(&waiter, &hss);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE, hss.satisfied_signals);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfiable_signals);

  // Query.
  // TODO(vtl): It's theoretically possible (though not with the current
  // implementation/configured limits) that not all the data has arrived yet.
  // (The theoretically-correct assertion here is that |num_bytes| is |1 * ...|
  // or |2 * ...|.)
  num_bytes = 0;
  EXPECT_EQ(MOJO_RESULT_OK,
            this->dpc()->ConsumerQueryData(MakeUserPointer(&num_bytes)));
  EXPECT_EQ(2 * sizeof(elements[0]), num_bytes);

  // Read one element.
  elements[0] = -1;
  elements[1] = -1;
  num_bytes = static_cast<uint32_t>(1u * sizeof(elements[0]));
  EXPECT_EQ(MOJO_RESULT_OK, this->dpc()->ConsumerReadData(
                                UserPointer<void>(elements),
                                MakeUserPointer(&num_bytes), false, false));
  EXPECT_EQ(1u * sizeof(elements[0]), num_bytes);
  EXPECT_EQ(123, elements[0]);
  EXPECT_EQ(-1, elements[1]);

  // Query.
  // TODO(vtl): See previous TODO. (If we got 2 elements there, however, we
  // should get 1 here.)
  num_bytes = 0;
  EXPECT_EQ(MOJO_RESULT_OK,
            this->dpc()->ConsumerQueryData(MakeUserPointer(&num_bytes)));
  EXPECT_EQ(1 * sizeof(elements[0]), num_bytes);

  // Peek one element.
  elements[0] = -1;
  elements[1] = -1;
  num_bytes = static_cast<uint32_t>(1u * sizeof(elements[0]));
  EXPECT_EQ(MOJO_RESULT_OK, this->dpc()->ConsumerReadData(
                                UserPointer<void>(elements),
                                MakeUserPointer(&num_bytes), false, true));
  EXPECT_EQ(1u * sizeof(elements[0]), num_bytes);
  EXPECT_EQ(456, elements[0]);
  EXPECT_EQ(-1, elements[1]);

  // Query. Still has 1 element remaining.
  num_bytes = 0;
  EXPECT_EQ(MOJO_RESULT_OK,
            this->dpc()->ConsumerQueryData(MakeUserPointer(&num_bytes)));
  EXPECT_EQ(1 * sizeof(elements[0]), num_bytes);

  // Try to read two elements, with "all or none".
  elements[0] = -1;
  elements[1] = -1;
  num_bytes = static_cast<uint32_t>(2u * sizeof(elements[0]));
  EXPECT_EQ(
      MOJO_RESULT_OUT_OF_RANGE,
      this->dpc()->ConsumerReadData(UserPointer<void>(elements),
                                    MakeUserPointer(&num_bytes), true, false));
  EXPECT_EQ(-1, elements[0]);
  EXPECT_EQ(-1, elements[1]);

  // Try to read two elements, without "all or none".
  elements[0] = -1;
  elements[1] = -1;
  num_bytes = static_cast<uint32_t>(2u * sizeof(elements[0]));
  EXPECT_EQ(MOJO_RESULT_OK, this->dpc()->ConsumerReadData(
                                UserPointer<void>(elements),
                                MakeUserPointer(&num_bytes), false, false));
  EXPECT_EQ(1u * sizeof(elements[0]), num_bytes);
  EXPECT_EQ(456, elements[0]);
  EXPECT_EQ(-1, elements[1]);

  // Query.
  num_bytes = 0;
  EXPECT_EQ(MOJO_RESULT_OK,
            this->dpc()->ConsumerQueryData(MakeUserPointer(&num_bytes)));
  EXPECT_EQ(0u, num_bytes);

  this->ProducerClose();
  this->ConsumerClose();
}

// Note: The "basic" waiting tests test that the "wait states" are correct in
// various situations; they don't test that waiters are properly awoken on state
// changes. (For that, we need to use multiple threads.)
TYPED_TEST(DataPipeImplTest, BasicProducerWaiting) {
  // Note: We take advantage of the fact that current for current
  // implementations capacities are strict maximums. This is not guaranteed by
  // the API.

  const MojoCreateDataPipeOptions options = {
      kSizeOfOptions,                           // |struct_size|.
      MOJO_CREATE_DATA_PIPE_OPTIONS_FLAG_NONE,  // |flags|.
      static_cast<uint32_t>(sizeof(int32_t)),   // |element_num_bytes|.
      2 * sizeof(int32_t)                       // |capacity_num_bytes|.
  };
  this->Create(options);
  this->DoTransfer();

  Waiter pwaiter;  // For producer.
  Waiter cwaiter;  // For consumer.
  HandleSignalsState hss;
  uint32_t context;

  // Never readable.
  pwaiter.Init();
  hss = HandleSignalsState();
  EXPECT_EQ(MOJO_RESULT_FAILED_PRECONDITION,
            this->dpp()->ProducerAddAwakable(
                &pwaiter, MOJO_HANDLE_SIGNAL_READABLE, 12, &hss));
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_WRITABLE, hss.satisfied_signals);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_WRITABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfiable_signals);

  // Already writable.
  pwaiter.Init();
  hss = HandleSignalsState();
  EXPECT_EQ(MOJO_RESULT_ALREADY_EXISTS,
            this->dpp()->ProducerAddAwakable(
                &pwaiter, MOJO_HANDLE_SIGNAL_WRITABLE, 34, &hss));

  // We'll need to wait for readability for the remote cases.
  cwaiter.Init();
  ASSERT_EQ(MOJO_RESULT_OK,
            this->dpc()->ConsumerAddAwakable(
                &cwaiter, MOJO_HANDLE_SIGNAL_READABLE, 1234, nullptr));

  // Write two elements.
  int32_t elements[2] = {123, 456};
  uint32_t num_bytes = static_cast<uint32_t>(2u * sizeof(elements[0]));
  EXPECT_EQ(MOJO_RESULT_OK,
            this->dpp()->ProducerWriteData(UserPointer<const void>(elements),
                                           MakeUserPointer(&num_bytes), true));
  EXPECT_EQ(static_cast<uint32_t>(2u * sizeof(elements[0])), num_bytes);

  // Adding a waiter should now succeed.
  pwaiter.Init();
  ASSERT_EQ(MOJO_RESULT_OK,
            this->dpp()->ProducerAddAwakable(
                &pwaiter, MOJO_HANDLE_SIGNAL_WRITABLE, 56, nullptr));
  // And it shouldn't be writable yet.
  EXPECT_EQ(MOJO_RESULT_DEADLINE_EXCEEDED, pwaiter.Wait(0, nullptr));
  hss = HandleSignalsState();
  this->dpp()->ProducerRemoveAwakable(&pwaiter, &hss);
  EXPECT_EQ(0u, hss.satisfied_signals);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_WRITABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfiable_signals);

  // Wait for data to become available to the consumer.
  context = 0;
  EXPECT_EQ(MOJO_RESULT_OK, cwaiter.Wait(1000, &context));
  EXPECT_EQ(1234u, context);
  hss = HandleSignalsState();
  this->dpc()->ConsumerRemoveAwakable(&cwaiter, &hss);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE, hss.satisfied_signals);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfiable_signals);

  // Peek one element.
  elements[0] = -1;
  elements[1] = -1;
  num_bytes = static_cast<uint32_t>(1u * sizeof(elements[0]));
  EXPECT_EQ(MOJO_RESULT_OK, this->dpc()->ConsumerReadData(
                                UserPointer<void>(elements),
                                MakeUserPointer(&num_bytes), true, true));
  EXPECT_EQ(static_cast<uint32_t>(1u * sizeof(elements[0])), num_bytes);
  EXPECT_EQ(123, elements[0]);
  EXPECT_EQ(-1, elements[1]);

  // Add a waiter.
  pwaiter.Init();
  ASSERT_EQ(MOJO_RESULT_OK,
            this->dpp()->ProducerAddAwakable(
                &pwaiter, MOJO_HANDLE_SIGNAL_WRITABLE, 56, nullptr));
  // And it still shouldn't be writable yet.
  EXPECT_EQ(MOJO_RESULT_DEADLINE_EXCEEDED, pwaiter.Wait(0, nullptr));
  hss = HandleSignalsState();
  this->dpp()->ProducerRemoveAwakable(&pwaiter, &hss);
  EXPECT_EQ(0u, hss.satisfied_signals);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_WRITABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfiable_signals);

  // Do it again.
  pwaiter.Init();
  ASSERT_EQ(MOJO_RESULT_OK,
            this->dpp()->ProducerAddAwakable(
                &pwaiter, MOJO_HANDLE_SIGNAL_WRITABLE, 78, nullptr));

  // Read one element.
  elements[0] = -1;
  elements[1] = -1;
  num_bytes = static_cast<uint32_t>(1u * sizeof(elements[0]));
  EXPECT_EQ(MOJO_RESULT_OK, this->dpc()->ConsumerReadData(
                                UserPointer<void>(elements),
                                MakeUserPointer(&num_bytes), true, false));
  EXPECT_EQ(static_cast<uint32_t>(1u * sizeof(elements[0])), num_bytes);
  EXPECT_EQ(123, elements[0]);
  EXPECT_EQ(-1, elements[1]);

  // Waiting should now succeed.
  context = 0;
  EXPECT_EQ(MOJO_RESULT_OK, pwaiter.Wait(1000, &context));
  EXPECT_EQ(78u, context);
  hss = HandleSignalsState();
  this->dpp()->ProducerRemoveAwakable(&pwaiter, &hss);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_WRITABLE, hss.satisfied_signals);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_WRITABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfiable_signals);

  // Try writing, using a two-phase write.
  void* buffer = nullptr;
  num_bytes = static_cast<uint32_t>(3u * sizeof(elements[0]));
  EXPECT_EQ(MOJO_RESULT_OK,
            this->dpp()->ProducerBeginWriteData(
                MakeUserPointer(&buffer), MakeUserPointer(&num_bytes), false));
  EXPECT_TRUE(buffer);
  EXPECT_EQ(static_cast<uint32_t>(1u * sizeof(elements[0])), num_bytes);

  static_cast<int32_t*>(buffer)[0] = 789;
  EXPECT_EQ(MOJO_RESULT_OK,
            this->dpp()->ProducerEndWriteData(
                static_cast<uint32_t>(1u * sizeof(elements[0]))));

  // Add a waiter.
  pwaiter.Init();
  ASSERT_EQ(MOJO_RESULT_OK,
            this->dpp()->ProducerAddAwakable(
                &pwaiter, MOJO_HANDLE_SIGNAL_WRITABLE, 90, nullptr));

  // Read one element, using a two-phase read.
  const void* read_buffer = nullptr;
  num_bytes = 0u;
  EXPECT_EQ(MOJO_RESULT_OK, this->dpc()->ConsumerBeginReadData(
                                MakeUserPointer(&read_buffer),
                                MakeUserPointer(&num_bytes), false));
  EXPECT_TRUE(read_buffer);
  // Since we only read one element (after having written three in all), the
  // two-phase read should only allow us to read one. This checks an
  // implementation detail!
  EXPECT_EQ(static_cast<uint32_t>(1u * sizeof(elements[0])), num_bytes);
  EXPECT_EQ(456, static_cast<const int32_t*>(read_buffer)[0]);
  EXPECT_EQ(MOJO_RESULT_OK,
            this->dpc()->ConsumerEndReadData(
                static_cast<uint32_t>(1u * sizeof(elements[0]))));

  // Waiting should succeed.
  context = 0;
  EXPECT_EQ(MOJO_RESULT_OK, pwaiter.Wait(1000, &context));
  EXPECT_EQ(90u, context);
  hss = HandleSignalsState();
  this->dpp()->ProducerRemoveAwakable(&pwaiter, &hss);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_WRITABLE, hss.satisfied_signals);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_WRITABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfiable_signals);

  // Write one element.
  elements[0] = 123;
  num_bytes = static_cast<uint32_t>(1u * sizeof(elements[0]));
  EXPECT_EQ(MOJO_RESULT_OK,
            this->dpp()->ProducerWriteData(UserPointer<const void>(elements),
                                           MakeUserPointer(&num_bytes), false));
  EXPECT_EQ(static_cast<uint32_t>(1u * sizeof(elements[0])), num_bytes);

  // Add a waiter.
  pwaiter.Init();
  ASSERT_EQ(MOJO_RESULT_OK,
            this->dpp()->ProducerAddAwakable(
                &pwaiter, MOJO_HANDLE_SIGNAL_WRITABLE, 12, nullptr));

  // Close the consumer.
  this->ConsumerClose();

  // It should now be never-writable.
  context = 0;
  EXPECT_EQ(MOJO_RESULT_FAILED_PRECONDITION, pwaiter.Wait(1000, &context));
  EXPECT_EQ(12u, context);
  hss = HandleSignalsState();
  this->dpp()->ProducerRemoveAwakable(&pwaiter, &hss);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, hss.satisfied_signals);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, hss.satisfiable_signals);

  this->ProducerClose();
}

TYPED_TEST(DataPipeImplTest, PeerClosedProducerWaiting) {
  const MojoCreateDataPipeOptions options = {
      kSizeOfOptions,                           // |struct_size|.
      MOJO_CREATE_DATA_PIPE_OPTIONS_FLAG_NONE,  // |flags|.
      static_cast<uint32_t>(sizeof(int32_t)),   // |element_num_bytes|.
      2 * sizeof(int32_t)                       // |capacity_num_bytes|.
  };
  this->Create(options);
  this->DoTransfer();

  Waiter waiter;
  HandleSignalsState hss;
  uint32_t context;

  // Add a waiter.
  waiter.Init();
  ASSERT_EQ(MOJO_RESULT_OK,
            this->dpp()->ProducerAddAwakable(
                &waiter, MOJO_HANDLE_SIGNAL_PEER_CLOSED, 12, nullptr));

  // Close the consumer.
  this->ConsumerClose();

  // It should be signaled.
  context = 0;
  EXPECT_EQ(MOJO_RESULT_OK, waiter.Wait(1000, &context));
  EXPECT_EQ(12u, context);
  hss = HandleSignalsState();
  this->dpp()->ProducerRemoveAwakable(&waiter, &hss);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, hss.satisfied_signals);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, hss.satisfiable_signals);

  this->ProducerClose();
}

TYPED_TEST(DataPipeImplTest, PeerClosedConsumerWaiting) {
  const MojoCreateDataPipeOptions options = {
      kSizeOfOptions,                           // |struct_size|.
      MOJO_CREATE_DATA_PIPE_OPTIONS_FLAG_NONE,  // |flags|.
      static_cast<uint32_t>(sizeof(int32_t)),   // |element_num_bytes|.
      2 * sizeof(int32_t)                       // |capacity_num_bytes|.
  };
  this->Create(options);
  this->DoTransfer();

  Waiter waiter;
  HandleSignalsState hss;
  uint32_t context;

  // Add a waiter.
  waiter.Init();
  ASSERT_EQ(MOJO_RESULT_OK,
            this->dpc()->ConsumerAddAwakable(
                &waiter, MOJO_HANDLE_SIGNAL_PEER_CLOSED, 12, nullptr));

  // Close the producer.
  this->ProducerClose();

  // It should be signaled.
  context = 0;
  EXPECT_EQ(MOJO_RESULT_OK, waiter.Wait(1000, &context));
  EXPECT_EQ(12u, context);
  hss = HandleSignalsState();
  this->dpc()->ConsumerRemoveAwakable(&waiter, &hss);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, hss.satisfied_signals);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, hss.satisfiable_signals);

  this->ConsumerClose();
}

TYPED_TEST(DataPipeImplTest, BasicConsumerWaiting) {
  const MojoCreateDataPipeOptions options = {
      kSizeOfOptions,                           // |struct_size|.
      MOJO_CREATE_DATA_PIPE_OPTIONS_FLAG_NONE,  // |flags|.
      static_cast<uint32_t>(sizeof(int32_t)),   // |element_num_bytes|.
      1000 * sizeof(int32_t)                    // |capacity_num_bytes|.
  };
  this->Create(options);
  this->DoTransfer();

  Waiter waiter;
  Waiter waiter2;
  HandleSignalsState hss;
  uint32_t context;

  // Never writable.
  waiter.Init();
  hss = HandleSignalsState();
  EXPECT_EQ(MOJO_RESULT_FAILED_PRECONDITION,
            this->dpc()->ConsumerAddAwakable(
                &waiter, MOJO_HANDLE_SIGNAL_WRITABLE, 12, &hss));
  EXPECT_EQ(0u, hss.satisfied_signals);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfiable_signals);

  // Add waiter: not yet readable.
  waiter.Init();
  ASSERT_EQ(MOJO_RESULT_OK,
            this->dpc()->ConsumerAddAwakable(
                &waiter, MOJO_HANDLE_SIGNAL_READABLE, 34, nullptr));

  // Write two elements.
  int32_t elements[2] = {123, 456};
  uint32_t num_bytes = static_cast<uint32_t>(2u * sizeof(elements[0]));
  EXPECT_EQ(MOJO_RESULT_OK,
            this->dpp()->ProducerWriteData(UserPointer<const void>(elements),
                                           MakeUserPointer(&num_bytes), true));

  // Wait for readability (needed for remote cases).
  context = 0;
  EXPECT_EQ(MOJO_RESULT_OK, waiter.Wait(1000, &context));
  EXPECT_EQ(34u, context);
  hss = HandleSignalsState();
  this->dpc()->ConsumerRemoveAwakable(&waiter, &hss);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE, hss.satisfied_signals);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfiable_signals);

  // Discard one element.
  num_bytes = static_cast<uint32_t>(1u * sizeof(elements[0]));
  EXPECT_EQ(MOJO_RESULT_OK, this->dpc()->ConsumerDiscardData(
                                MakeUserPointer(&num_bytes), true));
  EXPECT_EQ(static_cast<uint32_t>(1u * sizeof(elements[0])), num_bytes);

  // Should still be readable.
  waiter.Init();
  hss = HandleSignalsState();
  EXPECT_EQ(MOJO_RESULT_ALREADY_EXISTS,
            this->dpc()->ConsumerAddAwakable(
                &waiter, MOJO_HANDLE_SIGNAL_READABLE, 78, &hss));
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE, hss.satisfied_signals);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfiable_signals);

  // Peek one element.
  elements[0] = -1;
  elements[1] = -1;
  num_bytes = static_cast<uint32_t>(1u * sizeof(elements[0]));
  EXPECT_EQ(MOJO_RESULT_OK, this->dpc()->ConsumerReadData(
                                UserPointer<void>(elements),
                                MakeUserPointer(&num_bytes), true, true));
  EXPECT_EQ(static_cast<uint32_t>(1u * sizeof(elements[0])), num_bytes);
  EXPECT_EQ(456, elements[0]);
  EXPECT_EQ(-1, elements[1]);

  // Should still be readable.
  waiter.Init();
  hss = HandleSignalsState();
  EXPECT_EQ(MOJO_RESULT_ALREADY_EXISTS,
            this->dpc()->ConsumerAddAwakable(
                &waiter, MOJO_HANDLE_SIGNAL_READABLE, 78, &hss));
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE, hss.satisfied_signals);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfiable_signals);

  // Read one element.
  elements[0] = -1;
  elements[1] = -1;
  num_bytes = static_cast<uint32_t>(1u * sizeof(elements[0]));
  EXPECT_EQ(MOJO_RESULT_OK, this->dpc()->ConsumerReadData(
                                UserPointer<void>(elements),
                                MakeUserPointer(&num_bytes), true, false));
  EXPECT_EQ(static_cast<uint32_t>(1u * sizeof(elements[0])), num_bytes);
  EXPECT_EQ(456, elements[0]);
  EXPECT_EQ(-1, elements[1]);

  // Adding a waiter should now succeed.
  waiter.Init();
  ASSERT_EQ(MOJO_RESULT_OK,
            this->dpc()->ConsumerAddAwakable(
                &waiter, MOJO_HANDLE_SIGNAL_READABLE, 90, nullptr));

  // Write one element.
  elements[0] = 789;
  elements[1] = -1;
  num_bytes = static_cast<uint32_t>(1u * sizeof(elements[0]));
  EXPECT_EQ(MOJO_RESULT_OK,
            this->dpp()->ProducerWriteData(UserPointer<const void>(elements),
                                           MakeUserPointer(&num_bytes), true));

  // Waiting should now succeed.
  context = 0;
  EXPECT_EQ(MOJO_RESULT_OK, waiter.Wait(1000, &context));
  EXPECT_EQ(90u, context);
  hss = HandleSignalsState();
  this->dpc()->ConsumerRemoveAwakable(&waiter, &hss);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE, hss.satisfied_signals);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfiable_signals);

  // We'll want to wait for the peer closed signal to propagate.
  waiter.Init();
  EXPECT_EQ(MOJO_RESULT_OK,
            this->dpc()->ConsumerAddAwakable(
                &waiter, MOJO_HANDLE_SIGNAL_PEER_CLOSED, 12, nullptr));

  // Close the producer.
  this->ProducerClose();

  // Should still be readable, even if the peer closed signal hasn't propagated
  // yet.
  waiter2.Init();
  hss = HandleSignalsState();
  EXPECT_EQ(MOJO_RESULT_ALREADY_EXISTS,
            this->dpc()->ConsumerAddAwakable(
                &waiter2, MOJO_HANDLE_SIGNAL_READABLE, 34, &hss));
  // We don't know if the peer closed signal has propagated yet (for the remote
  // cases).
  EXPECT_TRUE((hss.satisfied_signals & MOJO_HANDLE_SIGNAL_READABLE));
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfiable_signals);

  // Wait for the peer closed signal.
  context = 0;
  EXPECT_EQ(MOJO_RESULT_OK, waiter.Wait(1000, &context));
  EXPECT_EQ(12u, context);
  hss = HandleSignalsState();
  this->dpc()->ConsumerRemoveAwakable(&waiter, &hss);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfied_signals);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfiable_signals);

  // Read one element.
  elements[0] = -1;
  elements[1] = -1;
  num_bytes = static_cast<uint32_t>(1u * sizeof(elements[0]));
  EXPECT_EQ(MOJO_RESULT_OK, this->dpc()->ConsumerReadData(
                                UserPointer<void>(elements),
                                MakeUserPointer(&num_bytes), true, false));
  EXPECT_EQ(static_cast<uint32_t>(1u * sizeof(elements[0])), num_bytes);
  EXPECT_EQ(789, elements[0]);
  EXPECT_EQ(-1, elements[1]);

  // Should be never-readable.
  waiter.Init();
  hss = HandleSignalsState();
  EXPECT_EQ(MOJO_RESULT_FAILED_PRECONDITION,
            this->dpc()->ConsumerAddAwakable(
                &waiter, MOJO_HANDLE_SIGNAL_READABLE, 56, &hss));
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, hss.satisfied_signals);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, hss.satisfiable_signals);

  this->ConsumerClose();
}

// Test with two-phase APIs and also closing the producer with an active
// consumer waiter.
TYPED_TEST(DataPipeImplTest, ConsumerWaitingTwoPhase) {
  const MojoCreateDataPipeOptions options = {
      kSizeOfOptions,                           // |struct_size|.
      MOJO_CREATE_DATA_PIPE_OPTIONS_FLAG_NONE,  // |flags|.
      static_cast<uint32_t>(sizeof(int32_t)),   // |element_num_bytes|.
      1000 * sizeof(int32_t)                    // |capacity_num_bytes|.
  };
  this->Create(options);
  this->DoTransfer();

  Waiter waiter;
  HandleSignalsState hss;
  uint32_t context;

  // Add waiter: not yet readable.
  waiter.Init();
  ASSERT_EQ(MOJO_RESULT_OK,
            this->dpc()->ConsumerAddAwakable(
                &waiter, MOJO_HANDLE_SIGNAL_READABLE, 12, nullptr));

  // Write two elements.
  int32_t* elements = nullptr;
  void* buffer = nullptr;
  // Request room for three (but we'll only write two).
  uint32_t num_bytes = static_cast<uint32_t>(3u * sizeof(elements[0]));
  EXPECT_EQ(MOJO_RESULT_OK,
            this->dpp()->ProducerBeginWriteData(
                MakeUserPointer(&buffer), MakeUserPointer(&num_bytes), true));
  EXPECT_TRUE(buffer);
  EXPECT_GE(num_bytes, static_cast<uint32_t>(3u * sizeof(elements[0])));
  elements = static_cast<int32_t*>(buffer);
  elements[0] = 123;
  elements[1] = 456;
  EXPECT_EQ(MOJO_RESULT_OK,
            this->dpp()->ProducerEndWriteData(
                static_cast<uint32_t>(2u * sizeof(elements[0]))));

  // Wait for readability (needed for remote cases).
  context = 0;
  EXPECT_EQ(MOJO_RESULT_OK, waiter.Wait(1000, &context));
  EXPECT_EQ(12u, context);
  hss = HandleSignalsState();
  this->dpc()->ConsumerRemoveAwakable(&waiter, &hss);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE, hss.satisfied_signals);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfiable_signals);

  // Read one element.
  // Request two in all-or-none mode, but only read one.
  const void* read_buffer = nullptr;
  num_bytes = static_cast<uint32_t>(2u * sizeof(elements[0]));
  EXPECT_EQ(MOJO_RESULT_OK, this->dpc()->ConsumerBeginReadData(
                                MakeUserPointer(&read_buffer),
                                MakeUserPointer(&num_bytes), true));
  EXPECT_TRUE(read_buffer);
  EXPECT_EQ(static_cast<uint32_t>(2u * sizeof(elements[0])), num_bytes);
  const int32_t* read_elements = static_cast<const int32_t*>(read_buffer);
  EXPECT_EQ(123, read_elements[0]);
  EXPECT_EQ(MOJO_RESULT_OK,
            this->dpc()->ConsumerEndReadData(
                static_cast<uint32_t>(1u * sizeof(elements[0]))));

  // Should still be readable.
  waiter.Init();
  hss = HandleSignalsState();
  EXPECT_EQ(MOJO_RESULT_ALREADY_EXISTS,
            this->dpc()->ConsumerAddAwakable(
                &waiter, MOJO_HANDLE_SIGNAL_READABLE, 34, &hss));
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE, hss.satisfied_signals);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfiable_signals);

  // Read one element.
  // Request three, but not in all-or-none mode.
  read_buffer = nullptr;
  num_bytes = static_cast<uint32_t>(3u * sizeof(elements[0]));
  EXPECT_EQ(MOJO_RESULT_OK, this->dpc()->ConsumerBeginReadData(
                                MakeUserPointer(&read_buffer),
                                MakeUserPointer(&num_bytes), false));
  EXPECT_TRUE(read_buffer);
  EXPECT_EQ(static_cast<uint32_t>(1u * sizeof(elements[0])), num_bytes);
  read_elements = static_cast<const int32_t*>(read_buffer);
  EXPECT_EQ(456, read_elements[0]);
  EXPECT_EQ(MOJO_RESULT_OK,
            this->dpc()->ConsumerEndReadData(
                static_cast<uint32_t>(1u * sizeof(elements[0]))));

  // Adding a waiter should now succeed.
  waiter.Init();
  ASSERT_EQ(MOJO_RESULT_OK,
            this->dpc()->ConsumerAddAwakable(
                &waiter, MOJO_HANDLE_SIGNAL_READABLE, 56, nullptr));

  // Close the producer.
  this->ProducerClose();

  // Should be never-readable.
  context = 0;
  EXPECT_EQ(MOJO_RESULT_FAILED_PRECONDITION, waiter.Wait(1000, &context));
  EXPECT_EQ(56u, context);
  hss = HandleSignalsState();
  this->dpc()->ConsumerRemoveAwakable(&waiter, &hss);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, hss.satisfied_signals);
  EXPECT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, hss.satisfiable_signals);

  this->ConsumerClose();
}

}  // namespace
}  // namespace system
}  // namespace mojo
