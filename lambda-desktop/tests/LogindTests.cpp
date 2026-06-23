#include <Lambda/System/Logind.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

using lambdaui::testing::dbus::pollBus;
using lambdaui::testing::dbus::pumpUntil;
using lambdaui::testing::dbus::startPrivateBus;

constexpr char kLogin1Service[] = "org.freedesktop.login1";
constexpr char kManagerPath[] = "/org/freedesktop/login1";
constexpr char kManagerInterface[] = "org.freedesktop.login1.Manager";
constexpr char kSessionPath[] = "/org/freedesktop/login1/session/self";
constexpr char kSessionInterface[] = "org.freedesktop.login1.Session";

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

struct PowerCall {
  std::string member;
  bool interactive = false;
};

using InhibitRequest = std::tuple<std::string, std::string, std::string, std::string>;

template <typename Callback>
class ScopeExit {
public:
  explicit ScopeExit(Callback callback) : callback_(std::move(callback)) {}
  ~ScopeExit() { callback_(); }

private:
  Callback callback_;
};

} // namespace

TEST_CASE("Logind support is compile-time declared") {
  CHECK((LAMBDA_HAS_DBUS == 0 || LAMBDA_HAS_DBUS == 1));
}

#if LAMBDA_HAS_DBUS

TEST_CASE("LogindClient sends power calls takes inhibitors and watches session signals") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping logind integration test because a private bus could not be started in this environment");
    return;
  }

  auto service = lambdaui::dbus::Bus::openAddress(privateBus->address);
  lambdaui::system::LogindClient client(lambdaui::dbus::Bus::openAddress(privateBus->address));
  service.requestName(kLogin1Service);

  TestPipe inhibitorPipe;
  REQUIRE(inhibitorPipe.open());

  std::mutex recordsMutex;
  std::vector<PowerCall> powerCalls;
  std::vector<std::string> sessionCalls;
  std::vector<InhibitRequest> inhibitRequests;
  std::vector<std::uint32_t> sessionPathRequests;

  auto recordPowerCall = [&](std::string member) {
    return [&, member = std::move(member)](lambdaui::dbus::Message& message) {
      bool const interactive = message.readBool();
      {
        std::lock_guard guard(recordsMutex);
        powerCalls.push_back(PowerCall{.member = member, .interactive = interactive});
      }
      return lambdaui::dbus::MethodReply{};
    };
  };

  auto objectSlot = service.exportObject(
      kManagerPath,
      lambdaui::dbus::ObjectDefinition{
          .methods = {
              lambdaui::dbus::ExportedMethod{
                  .interface = kManagerInterface,
                  .member = "Suspend",
                  .handler = recordPowerCall("Suspend"),
              },
              lambdaui::dbus::ExportedMethod{
                  .interface = kManagerInterface,
                  .member = "Hibernate",
                  .handler = recordPowerCall("Hibernate"),
              },
              lambdaui::dbus::ExportedMethod{
                  .interface = kManagerInterface,
                  .member = "PowerOff",
                  .handler = recordPowerCall("PowerOff"),
              },
              lambdaui::dbus::ExportedMethod{
                  .interface = kManagerInterface,
                  .member = "Reboot",
                  .handler = recordPowerCall("Reboot"),
              },
              lambdaui::dbus::ExportedMethod{
                  .interface = kManagerInterface,
                  .member = "Inhibit",
                  .handler = [&](lambdaui::dbus::Message& message) {
                    auto what = message.readString();
                    auto who = message.readString();
                    auto why = message.readString();
                    auto mode = message.readString();
                    {
                      std::lock_guard guard(recordsMutex);
                      inhibitRequests.emplace_back(what, who, why, mode);
                    }
                    return lambdaui::dbus::MethodReply{
                        .values = {lambdaui::dbus::UnixFd::borrow(inhibitorPipe.writeFd)},
                    };
                  },
              },
              lambdaui::dbus::ExportedMethod{
                  .interface = kManagerInterface,
                  .member = "GetSessionByPID",
                  .handler = [&](lambdaui::dbus::Message& message) {
                    std::uint32_t const pid = message.readUint32();
                    {
                      std::lock_guard guard(recordsMutex);
                      sessionPathRequests.push_back(pid);
                    }
                    return lambdaui::dbus::MethodReply{
                        .values = {lambdaui::dbus::ObjectPath{kSessionPath}},
                    };
                  },
              },
          },
      });
  auto sessionObjectSlot = service.exportObject(
      kSessionPath,
      lambdaui::dbus::ObjectDefinition{
          .methods = {
              lambdaui::dbus::ExportedMethod{
                  .interface = kSessionInterface,
                  .member = "Lock",
                  .handler = [&](lambdaui::dbus::Message&) {
                    std::lock_guard guard(recordsMutex);
                    sessionCalls.push_back("Lock");
                    return lambdaui::dbus::MethodReply{};
                  },
              },
              lambdaui::dbus::ExportedMethod{
                  .interface = kSessionInterface,
                  .member = "Terminate",
                  .handler = [&](lambdaui::dbus::Message&) {
                    std::lock_guard guard(recordsMutex);
                    sessionCalls.push_back("Terminate");
                    return lambdaui::dbus::MethodReply{};
                  },
              },
          },
      });

  std::atomic<bool> running = true;
  std::thread serviceThread([&] {
    while (running.load()) {
      pollBus(service, 25);
    }
  });
  ScopeExit stopServiceThread([&] {
    running = false;
    if (serviceThread.joinable()) {
      serviceThread.join();
    }
  });

  client.suspend(false);
  client.hibernate(true);
  client.powerOff(false);
  client.reboot(true);

  {
    std::lock_guard guard(recordsMutex);
    CHECK(powerCalls.size() == 4);
    if (powerCalls.size() == 4) {
      CHECK(powerCalls[0].member == "Suspend");
      CHECK(powerCalls[0].interactive == false);
      CHECK(powerCalls[1].member == "Hibernate");
      CHECK(powerCalls[1].interactive == true);
      CHECK(powerCalls[2].member == "PowerOff");
      CHECK(powerCalls[2].interactive == false);
      CHECK(powerCalls[3].member == "Reboot");
      CHECK(powerCalls[3].interactive == true);
    }
  }

  auto inhibitor = client.inhibit("handle-power-key", "lambda", "test inhibitor", "delay");
  CHECK(inhibitor.valid());
  if (inhibitor.valid()) {
    std::string const payload = "held";
    CHECK(write(inhibitor.fd(), payload.data(), payload.size()) == static_cast<ssize_t>(payload.size()));
    char buffer[8] = {};
    CHECK(read(inhibitorPipe.readFd, buffer, payload.size()) == static_cast<ssize_t>(payload.size()));
    CHECK(std::string(buffer, payload.size()) == payload);
  }
  {
    std::lock_guard guard(recordsMutex);
    CHECK(inhibitRequests.size() == 1);
    if (inhibitRequests.size() == 1) {
      CHECK(std::get<0>(inhibitRequests[0]) == "handle-power-key");
      CHECK(std::get<1>(inhibitRequests[0]) == "lambda");
      CHECK(std::get<2>(inhibitRequests[0]) == "test inhibitor");
      CHECK(std::get<3>(inhibitRequests[0]) == "delay");
    }
  }

  CHECK(client.sessionPathForPid(4242) == kSessionPath);
  CHECK(client.currentSessionPath() == kSessionPath);
  client.lockCurrentSession();
  client.terminateCurrentSession();
  {
    std::lock_guard guard(recordsMutex);
    REQUIRE(sessionPathRequests.size() == 4);
    CHECK(sessionPathRequests[0] == 4242);
    CHECK(sessionPathRequests[1] == static_cast<std::uint32_t>(getpid()));
    CHECK(sessionPathRequests[2] == static_cast<std::uint32_t>(getpid()));
    CHECK(sessionPathRequests[3] == static_cast<std::uint32_t>(getpid()));
    REQUIRE(sessionCalls.size() == 2);
    CHECK(sessionCalls[0] == "Lock");
    CHECK(sessionCalls[1] == "Terminate");
  }

  bool preparingForSleep = false;
  int lockSignals = 0;
  int unlockSignals = 0;
  int currentLockSignals = 0;
  int currentUnlockSignals = 0;
  auto sleepSlot = client.watchPrepareForSleep([&](bool preparing) {
    preparingForSleep = preparing;
  });
  auto lockSlot = client.watchSessionLock(kSessionPath, [&] {
    ++lockSignals;
  });
  auto unlockSlot = client.watchSessionUnlock(kSessionPath, [&] {
    ++unlockSignals;
  });
  auto currentLockSlot = client.watchCurrentSessionLock([&] {
    ++currentLockSignals;
  });
  auto currentUnlockSlot = client.watchCurrentSessionUnlock([&] {
    ++currentUnlockSignals;
  });

  service.emitSignal(kManagerPath, kManagerInterface, "PrepareForSleep", {true});
  service.emitSignal(kSessionPath, kSessionInterface, "Lock");
  service.emitSignal(kSessionPath, kSessionInterface, "Unlock");
  service.flush();

  CHECK(pumpUntil(client.bus(),
                  [&] {
                    return preparingForSleep && lockSignals == 1 && unlockSignals == 1 &&
                           currentLockSignals == 1 && currentUnlockSignals == 1;
                  },
                  std::chrono::milliseconds(500)));
}

#endif
