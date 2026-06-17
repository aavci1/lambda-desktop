#include <doctest/doctest.h>

#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/EventQueue.hpp>
#include <Lambda/UI/Events.hpp>

#include <atomic>
#include <chrono>
#include <poll.h>
#include <thread>
#include <unistd.h>

namespace {

struct EventQueuePayload {
  int value = 0;
};

struct TestPipe {
  int readFd = -1;
  int writeFd = -1;

  ~TestPipe() {
    if (readFd >= 0) {
      close(readFd);
    }
    if (writeFd >= 0) {
      close(writeFd);
    }
  }

  [[nodiscard]] bool open() {
    int fds[2] = {-1, -1};
    if (pipe(fds) != 0) {
      return false;
    }
    readFd = fds[0];
    writeFd = fds[1];
    return true;
  }
};

} // namespace

TEST_CASE("EventQueue snapshots first-class handlers registered during dispatch") {
  lambda::Application app;
  lambda::EventQueue& queue = app.eventQueue();

  int primaryCalls = 0;
  int addedCalls = 0;
  queue.on<lambda::InputEvent>([&](lambda::InputEvent const&) {
    ++primaryCalls;
    for (int i = 0; i < 64; ++i) {
      queue.on<lambda::InputEvent>([&](lambda::InputEvent const&) {
        ++addedCalls;
      });
    }
  });

  queue.post(lambda::InputEvent{.kind = lambda::InputEvent::Kind::KeyDown});
  queue.dispatch();
  CHECK(primaryCalls == 1);
  CHECK(addedCalls == 0);

  queue.post(lambda::InputEvent{.kind = lambda::InputEvent::Kind::KeyDown});
  queue.dispatch();
  CHECK(primaryCalls == 2);
  CHECK(addedCalls == 64);
}

TEST_CASE("Application poll sources honor requested event masks") {
  lambda::Application app;
  TestPipe pipe;
  REQUIRE(pipe.open());

  std::atomic<bool> callbackRan = false;
  std::atomic<bool> sawWritable = false;
  std::uint64_t pollSourceId = 0;
  pollSourceId = app.registerEventPollSource(
      pipe.writeFd,
      [] {
        return POLLOUT;
      },
      [&](int revents) {
        sawWritable.store((revents & POLLOUT) != 0);
        callbackRan.store(true);
        app.unregisterEventPollSource(pollSourceId);
        app.quit();
      });
  REQUIRE(pollSourceId != 0);

  std::thread watchdog([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    if (!callbackRan.load()) {
      app.quit();
    }
  });
  int const exitCode = app.exec();
  watchdog.join();

  CHECK(exitCode == 0);
  CHECK(callbackRan.load());
  CHECK(sawWritable.load());
}

TEST_CASE("EventQueue snapshots custom handlers registered during dispatch") {
  lambda::Application app;
  lambda::EventQueue& queue = app.eventQueue();

  int primaryCalls = 0;
  int addedCalls = 0;
  queue.on<EventQueuePayload>([&](EventQueuePayload const&) {
    ++primaryCalls;
    for (int i = 0; i < 64; ++i) {
      queue.on<EventQueuePayload>([&](EventQueuePayload const&) {
        ++addedCalls;
      });
    }
  });

  queue.post(EventQueuePayload{.value = 1});
  queue.dispatch();
  CHECK(primaryCalls == 1);
  CHECK(addedCalls == 0);

  queue.post(EventQueuePayload{.value = 2});
  queue.dispatch();
  CHECK(primaryCalls == 2);
  CHECK(addedCalls == 64);
}
