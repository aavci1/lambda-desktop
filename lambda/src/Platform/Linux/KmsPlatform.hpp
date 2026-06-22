#pragma once

#include "UI/Platform/Application.hpp"
#include "UI/Platform/Window.hpp"
#include "Platform/Linux/GpuSurfaceProvider.hpp"

#include <Lambda/Platform/Linux/KmsOutput.hpp>
#include <Lambda/UI/Events.hpp>

#include <linux/vt.h>
#include <termios.h>
#include <xf86drmMode.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

struct libinput;
struct libinput_device;
struct libseat;
struct udev;
struct udev_monitor;

namespace lambda {

class KmsWindow;
struct WindowConfig;
namespace platform {
class KmsDevice;
}

struct KmsConnector {
  std::uint32_t connectorId = 0;
  std::uint32_t encoderId = 0;
  std::uint32_t crtcId = 0;
  drmModeModeInfo mode{};
  std::vector<drmModeModeInfo> modes;
  int widthMm = 0;
  int heightMm = 0;
  std::string name;
};

class KmsApplication final : public platform::Application, public platform::GpuSurfaceProvider {
public:
  KmsApplication();
  ~KmsApplication() override;

  void initialize() override;
  void setApplicationName(std::string name) override;
  std::string applicationName() const override;
  void setMenuBar(MenuBar const& menu, platform::MenuActionDispatcher dispatcher) override;
  void setTerminateHandler(std::function<void()> handler) override;
  void requestTerminate() override;
  std::unordered_set<platform::ShortcutKey, platform::ShortcutKeyHash> menuClaimedShortcuts() const override;
  void revalidateMenuItems(std::function<bool(std::string const&)> isEnabled) override;
  std::string userDataDir() const override;
  std::string cacheDir() const override;
  std::vector<std::string> availableOutputs() const override;
  platform::GpuSurfaceProvider* gpuSurfaceProvider() override { return this; }
  std::span<char const* const> requiredInstanceExtensions() const override;
  VkSurfaceKHR createSurface(VkInstance instance, void* nativeHandle) override;

  std::unique_ptr<platform::Window> createWindow(WindowConfig const& config);
  int drmFd() const noexcept { return drmFd_; }
  int inputFd() const noexcept;
  int wakeFd() const noexcept { return wakePipe_[0]; }
  void wakeEventLoop();
  platform::KmsPollResult pollInputAndWakeDetailed(int timeoutMs, std::span<int const> extraFds = {});
  bool pollInputAndWake(int timeoutMs, std::span<int const> extraFds = {});
  void dispatchPendingInput();
  bool isVtForeground() const noexcept { return vtForeground_; }
  void registerWindow(KmsWindow* window);
  void unregisterWindow(KmsWindow* window);
  KmsWindow* focusedWindow() const;
  void handleInputDeviceAdded(libinput_device* device);
  void setPointerPosition(KmsWindow* window, Point localPosition);
  void routePointer(Point position, InputEvent::Kind kind, MouseButton button = MouseButton::None,
                    Vec2 scrollDelta = {}, bool preciseScrollDelta = true);
  void routeKey(std::uint32_t evdevKey, bool pressed);
  void emitRawInput(platform::KmsInputEvent const& event);
  void discardPendingInputEvents(bool handleDeviceEvents);
  void releaseRawInputState(std::uint32_t timeMs);
  void suspendInputForVtSwitch();
  void resumeInputAfterVtSwitch();
  int openRestrictedDevice(char const* path, int flags);
  void closeRestrictedDevice(int fd);
  void handleSeatEnabled();
  void handleSeatDisabled();

private:
  friend class platform::KmsDevice;

  friend class KmsInput;

  bool openFirstDisplayCard();
  void enumerateConnectors();
  void initializeInput();
  void destroyInput();
  bool rebuildInputForSeatEnable();
  void initializeSeat();
  void closeSeat();
  void dispatchSeatEvents();
  void initializeDrmMonitor();
  void drainDrmMonitor();
  std::vector<KmsConnector> scanConnectors() const;
  void reEnumerateConnectors();
  void collectShortcuts(MenuItem const& item);
  void collectShortcuts(MenuBar const& menu);
  void drainWakePipe();
  void initializeConsole();
  void restoreConsole();
  void initializeActiveVtWatch();
  void closeActiveVtWatch();
  void drainActiveVtWatch();
  void handleActiveVt(int activeVt);
  void installSignalHandlers();
  void uninstallSignalHandlers();
  void handlePendingTerminateSignal();
  void handlePendingVtSignal();
  void pollActiveVt();
  void releaseDrmMasterForVt(bool acknowledge);
  void acquireDrmMasterForVt(bool acknowledge);
  void acknowledgePendingVtAcquire();
  bool switchSession(int session);
  bool switchAdjacentSession(int direction);
  KmsWindow* windowForConnector(std::uint32_t connectorId) const;
  Point windowOrigin(KmsWindow const* window) const;
  Point clampGlobalPointer(Point position) const;
  KmsWindow* windowAtGlobalPoint(Point position, Point& localPosition) const;
  void focusPointerWindow(KmsWindow* window);

  int drmFd_ = -1;
  libseat* seat_ = nullptr;
  int seatFd_ = -1;
  std::string seatName_;
  std::unordered_map<int, int> seatDeviceIdsByFd_;
  int ttyFd_ = -1;
  int activeVtNotifyFd_ = -1;
  int activeVtWatch_ = -1;
  int wakePipe_[2]{-1, -1};
  udev* udev_ = nullptr;
  udev_monitor* udevMonitor_ = nullptr;
  int udevMonitorFd_ = -1;
  libinput* input_ = nullptr;
  std::vector<libinput_device*> pathInputDevices_;
  int inputDeviceCount_ = 0;
  std::vector<KmsConnector> connectors_;
  std::vector<KmsWindow*> windows_;
  KmsWindow* pointerFocus_ = nullptr;
  platform::MenuActionDispatcher dispatcher_;
  std::function<void(platform::KmsInputEvent const&)> rawInputHandler_;
  std::function<void()> terminateHandler_;
  std::unordered_set<platform::ShortcutKey, platform::ShortcutKeyHash> claimedShortcuts_;
  std::string appName_ = "lambda";
  Point pointerPos_{};
  std::uint8_t pressedButtons_ = 0;
  std::unordered_set<std::uint32_t> rawPressedButtons_;
  std::unordered_set<std::uint32_t> rawPressedKeys_;
  std::atomic<bool> terminateRequested_{false};
  bool signalHandlersInstalled_ = false;
  bool drmMaster_ = false;
  bool seatEnabled_ = false;
  bool seatDisablePending_ = false;
  bool inputInitialized_ = false;
  bool consoleInitialized_ = false;
  bool terminalConfigured_ = false;
  bool vtProcessMode_ = false;
  bool vtForeground_ = true;
  bool vtAcquireAckPending_ = false;
  bool inputSuspendedForVt_ = false;
  int previousConsoleMode_ = 0;
  termios previousTermios_{};
  vt_mode previousVtMode_{};
  int ourVt_ = 0;
  std::int64_t nextActiveVtPollMs_ = 0;
};

KmsApplication& kmsApplication();

class KmsWindow final : public platform::Window {
public:
  KmsWindow(KmsApplication& app, KmsConnector connector, WindowConfig const& config);
  ~KmsWindow() override;

  void setLambdaWindow(::lambda::Window* window) override;
  void show() override;
  std::unique_ptr<Canvas> createCanvas(::lambda::Window& owner) override;
  void resize(Size const& newSize) override;
  void setFullscreen(bool fullscreen) override;
  void setTitle(std::string const& title) override;
  Size currentSize() const override;
  bool isFullscreen() const override;
  unsigned int handle() const override;
  void* nativeGraphicsSurface() const override;
  void processEvents() override;
  void waitForEvents(int timeoutMs) override;
  int eventFd() const override;
  int wakeFd() const override;
  void wakeEventLoop() override;
  void requestAnimationFrame() override;
  void acknowledgeAnimationFrameTick() override;
  void completeAnimationFrame(bool needsAnotherFrame) override;
  void setCursor(Cursor kind) override;
  [[nodiscard]] PlatformWindowCapabilities capabilities() const override;

  void suspendForVtSwitch();
  void resumeFromVtSwitch();
  std::uint32_t connectorId() const noexcept { return connector_.connectorId; }
  std::string const& outputName() const noexcept { return connector_.name; }
  void updateConnector(KmsConnector connector);
  void postFrameTick();
  Point clampPointer(Point p) const;
  void moveCursor(Point p);
  void hideCursor();
  int frameTimerFd() const noexcept { return frameTimerFd_; }

private:
  struct CursorBuffer {
    std::uint32_t handle = 0;
    std::uint64_t size = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
  };

  void armFrameTimer();
  void drainFrameTimer();
  void applyCursor();
  void destroyCursorBuffer();
  bool ensureCursorBuffer();

  KmsApplication& app_;
  KmsConnector connector_;
  ::lambda::Window* lambdaWindow_ = nullptr;
  Canvas* canvas_ = nullptr;
  unsigned int handle_ = 0;
  Size size_{};
  std::string title_;
  int requestedModeWidth_ = 0;
  int requestedModeHeight_ = 0;
  int requestedModeRefreshHz_ = 0;
  int frameTimerFd_ = -1;
  bool framePending_ = false;
  Cursor cursor_ = Cursor::Arrow;
  CursorBuffer cursorBuffer_{};
  Point cursorPos_{};
  bool cursorVisible_ = false;
};

} // namespace lambda
