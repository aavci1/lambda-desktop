# Event Queue

The event queue is the central dispatch path for application-visible work: window lifecycle, window chrome events, input, timer ticks, and user-defined payloads. There is one queue per `Application`, owned by it and accessed through `Application::instance().eventQueue()` or `app.eventQueue()`.

## Design Goals

- Unified framework and custom event delivery.
- Defined bucket order for each dispatch pass.
- Batched draining until pending buckets are empty.
- Main-thread contract for `post`, `dispatch`, and `on`.

## Event Types

`Event` is a `std::variant` of:

```cpp
WindowLifecycleEvent
WindowEvent
InputEvent
TimerEvent
CustomEvent
```

`CustomEvent` stores a type id plus `std::any` payload. `EventQueue::post<T>()` and `EventQueue::on<T>()` map non-framework payload types to custom events.

## Queue API

```cpp
void post(Event event);

template<typename T>
void post(T&& value);

template<typename T>
void on(std::function<void(T const&)> handler);

void dispatch();
```

Only `Application` constructs the queue.

## Dispatch Order

Pending events drain in this bucket order:

| Priority | Event type |
|----------|------------|
| 1 | `WindowLifecycleEvent` |
| 2 | `WindowEvent` |
| 3 | `InputEvent` |
| 4 | `TimerEvent` |
| 5 | `CustomEvent` |

Within each bucket, order is FIFO.

## Threading

Lambda expects `post`, `dispatch`, and `on` to be called on the application main thread. Background systems should marshal results to the UI thread before posting custom events.

## Re-entrancy

`dispatch()` has a guard: nested dispatch calls return immediately. Events posted by a handler remain queued and are processed by the outer dispatch pass.

## macOS Integration

On macOS, `Application::exec()` alternates platform event waits, due-timer processing, reactive and next-frame work, and `EventQueue::dispatch()`. The wait timeout comes from the next timer deadline so timer-driven work can wake without busy-waiting.

Cocoa callbacks may either post for the next dispatch pass or post and dispatch immediately when synchronous setup or teardown requires it.

## Custom Payload Example

```cpp
struct DataReadyEvent {
  User user;
};

app.eventQueue().on<DataReadyEvent>([&](DataReadyEvent const& event) {
  updateUser(event.user);
});

app.eventQueue().post(DataReadyEvent{user});
```
