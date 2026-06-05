#include <doctest/doctest.h>

#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/EventQueue.hpp>
#include <Lambda/UI/Events.hpp>

namespace {

struct EventQueuePayload {
  int value = 0;
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
