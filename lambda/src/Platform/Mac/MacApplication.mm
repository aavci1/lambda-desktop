#include "UI/Platform/Application.hpp"

#include <Lambda/UI/KeyCodes.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#import <Cocoa/Cocoa.h>

namespace lambdaui {
class MacApplication;
}

@interface LambdaAppDelegate : NSObject <NSApplicationDelegate, NSMenuDelegate>
@property(nonatomic, assign) lambdaui::MacApplication* owner;
- (void)lambdaMenuAction:(id)sender;
@end

namespace lambdaui {

namespace {

NSString* ns(std::string const& text) {
  NSString* out = [NSString stringWithUTF8String:text.c_str()];
  return out ? out : @"";
}

std::string appNameFromBundle() {
  NSString* name = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleName"];
  if (!name || name.length == 0) {
    name = [[NSProcessInfo processInfo] processName];
  }
  return name ? std::string([name UTF8String]) : std::string("Lambda");
}

std::string roleActionName(MenuRole role) {
  switch (role) {
  case MenuRole::AppAbout:
    return "app.about";
  case MenuRole::AppPreferences:
    return "settings";
  case MenuRole::AppHide:
    return "app.hide";
  case MenuRole::AppHideOthers:
    return "app.hide-others";
  case MenuRole::AppShowAll:
    return "app.show-all";
  case MenuRole::AppQuit:
    return "app.quit";
  case MenuRole::EditUndo:
    return "undo";
  case MenuRole::EditRedo:
    return "redo";
  case MenuRole::EditCut:
    return "cut";
  case MenuRole::EditCopy:
    return "copy";
  case MenuRole::EditPaste:
    return "paste";
  case MenuRole::EditDelete:
    return "delete";
  case MenuRole::EditSelectAll:
    return "select-all";
  case MenuRole::WindowMinimize:
    return "window.minimize";
  case MenuRole::WindowZoom:
    return "window.zoom";
  case MenuRole::WindowFullscreen:
    return "window.fullscreen";
  case MenuRole::WindowBringAllToFront:
    return "window.bring-all-to-front";
  default:
    return {};
  }
}

bool systemHandledAction(std::string_view action) {
  return action == "app.about" || action == "app.hide" || action == "app.hide-others" ||
         action == "app.show-all" || action == "app.quit" || action == "window.minimize" ||
         action == "window.zoom" || action == "window.fullscreen" ||
         action == "window.bring-all-to-front";
}

std::string roleLabel(MenuRole role, std::string const& appName) {
  switch (role) {
  case MenuRole::AppAbout:
    return "About " + appName;
  case MenuRole::AppPreferences:
    return "Settings...";
  case MenuRole::AppHide:
    return "Hide " + appName;
  case MenuRole::AppHideOthers:
    return "Hide Others";
  case MenuRole::AppShowAll:
    return "Show All";
  case MenuRole::AppQuit:
    return "Quit " + appName;
  case MenuRole::EditUndo:
    return "Undo";
  case MenuRole::EditRedo:
    return "Redo";
  case MenuRole::EditCut:
    return "Cut";
  case MenuRole::EditCopy:
    return "Copy";
  case MenuRole::EditPaste:
    return "Paste";
  case MenuRole::EditDelete:
    return "Delete";
  case MenuRole::EditSelectAll:
    return "Select All";
  case MenuRole::WindowMinimize:
    return "Minimize";
  case MenuRole::WindowZoom:
    return "Zoom";
  case MenuRole::WindowFullscreen:
    return "Enter Full Screen";
  case MenuRole::WindowBringAllToFront:
    return "Bring All to Front";
  case MenuRole::HelpSearch:
    return "Search";
  default:
    return {};
  }
}

Shortcut roleShortcut(MenuRole role) {
  switch (role) {
  case MenuRole::AppPreferences:
    return Shortcut{keys::Comma, Modifiers::Meta};
  case MenuRole::AppHide:
    return Shortcut{keys::H, Modifiers::Meta};
  case MenuRole::AppHideOthers:
    return Shortcut{keys::H, Modifiers::Meta | Modifiers::Alt};
  case MenuRole::AppQuit:
    return shortcuts::Quit;
  case MenuRole::EditUndo:
    return shortcuts::Undo;
  case MenuRole::EditRedo:
    return shortcuts::Redo;
  case MenuRole::EditCut:
    return shortcuts::Cut;
  case MenuRole::EditCopy:
    return shortcuts::Copy;
  case MenuRole::EditPaste:
    return shortcuts::Paste;
  case MenuRole::EditSelectAll:
    return shortcuts::SelectAll;
  case MenuRole::WindowMinimize:
    return Shortcut{keys::M, Modifiers::Meta};
  case MenuRole::WindowFullscreen:
    return Shortcut{keys::F, Modifiers::Meta | Modifiers::Ctrl};
  default:
    return {};
  }
}

NSString* keyEquivalent(KeyCode key) {
  switch (key) {
  case keys::A: return @"a";
  case keys::B: return @"b";
  case keys::C: return @"c";
  case keys::D: return @"d";
  case keys::E: return @"e";
  case keys::F: return @"f";
  case keys::G: return @"g";
  case keys::H: return @"h";
  case keys::I: return @"i";
  case keys::J: return @"j";
  case keys::K: return @"k";
  case keys::L: return @"l";
  case keys::M: return @"m";
  case keys::N: return @"n";
  case keys::O: return @"o";
  case keys::P: return @"p";
  case keys::Q: return @"q";
  case keys::R: return @"r";
  case keys::S: return @"s";
  case keys::T: return @"t";
  case keys::U: return @"u";
  case keys::V: return @"v";
  case keys::W: return @"w";
  case keys::X: return @"x";
  case keys::Y: return @"y";
  case keys::Z: return @"z";
  case keys::Comma: return @",";
  case keys::Period: return @".";
  case keys::Slash: return @"/";
  case keys::Return: return @"\r";
  default:
    return @"";
  }
}

NSEventModifierFlags modifierMask(Modifiers modifiers) {
  NSEventModifierFlags out = 0;
  if (any(modifiers & Modifiers::Meta)) {
    out |= NSEventModifierFlagCommand;
  }
  if (any(modifiers & Modifiers::Shift)) {
    out |= NSEventModifierFlagShift;
  }
  if (any(modifiers & Modifiers::Ctrl)) {
    out |= NSEventModifierFlagControl;
  }
  if (any(modifiers & Modifiers::Alt)) {
    out |= NSEventModifierFlagOption;
  }
  return out;
}

NSURL* directoryURL(NSSearchPathDirectory directory, std::string const& explicitAppName) {
  NSArray<NSURL*>* urls = [[NSFileManager defaultManager] URLsForDirectory:directory
                                                                  inDomains:NSUserDomainMask];
  NSURL* base = urls.firstObject;
  if (!base) {
    return nil;
  }
  NSString* appName = explicitAppName.empty() ? nil : ns(explicitAppName);
  if (!appName || appName.length == 0) {
    appName = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleName"];
    if (!appName || appName.length == 0) {
      appName = [[NSProcessInfo processInfo] processName];
    }
  }
  NSURL* appDir = [base URLByAppendingPathComponent:(appName ? appName : @"Lambda")];
  [[NSFileManager defaultManager] createDirectoryAtURL:appDir
                           withIntermediateDirectories:YES
                                            attributes:nil
                                                 error:nil];
  return appDir;
}

} // namespace

class MacApplication final : public platform::Application {
public:
  void initialize() override {
    [NSApplication sharedApplication];
#if defined(LAMBDAUI_TESTING)
    NSString* processName = [[NSProcessInfo processInfo] processName];
    bool const headlessTestProcess = processName && [processName isEqualToString:@"lambda-tests"];
    [NSApp setActivationPolicy:headlessTestProcess ? NSApplicationActivationPolicyProhibited
                                                   : NSApplicationActivationPolicyRegular];
#else
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
#endif
    delegate_ = [[LambdaAppDelegate alloc] init];
    delegate_.owner = this;
    [NSApp setDelegate:delegate_];
  }

  void setApplicationName(std::string name) override {
    appName_ = std::move(name);
  }

  std::string applicationName() const override {
    if (!appName_.empty()) return appName_;
    return appNameFromBundle();
  }

  void setMenuBar(MenuBar const& menu, platform::MenuActionDispatcher dispatcher) override {
    dispatcher_ = std::move(dispatcher);
    claimedShortcuts_.clear();

    std::string const appName = applicationName();
    std::vector<MenuItem> menus = menu.menus;
    if (menus.empty() || menus.front().role != MenuRole::Submenu) {
      menus.insert(menus.begin(), MenuItem::submenu(appName, {
                                     MenuItem::standard(MenuRole::AppAbout),
                                     MenuItem::separator(),
                                     MenuItem::standard(MenuRole::AppHide),
                                     MenuItem::standard(MenuRole::AppHideOthers),
                                     MenuItem::standard(MenuRole::AppShowAll),
                                     MenuItem::separator(),
                                     MenuItem::standard(MenuRole::AppQuit),
                                 }));
    }

    NSMenu* main = [[NSMenu alloc] initWithTitle:@""];
    for (MenuItem const& top : menus) {
      if (top.role != MenuRole::Submenu) {
        continue;
      }
      NSMenuItem* topItem = [[NSMenuItem alloc] initWithTitle:ns(top.label)
                                                       action:nil
                                                keyEquivalent:@""];
      NSMenu* submenu = [[NSMenu alloc] initWithTitle:ns(top.label)];
      submenu.delegate = delegate_;
      for (MenuItem const& child : top.children) {
        addMenuItem(submenu, child, appName);
      }
      topItem.submenu = submenu;
      [main addItem:topItem];
    }
    [NSApp setMainMenu:main];
  }

  void setTerminateHandler(std::function<void()> handler) override {
    terminateHandler_ = std::move(handler);
  }

  void requestTerminate() override {
    if (terminateHandler_) {
      terminateHandler_();
    }
  }

  std::unordered_set<platform::ShortcutKey, platform::ShortcutKeyHash> menuClaimedShortcuts() const override {
    return claimedShortcuts_;
  }

  void revalidateMenuItems(std::function<bool(std::string const&)> isEnabled) override {
    isEnabled_ = std::move(isEnabled);
  }

  std::string userDataDir() const override {
    NSURL* url = directoryURL(NSApplicationSupportDirectory, appName_);
    return url ? std::string(url.path.UTF8String) : std::string{};
  }

  std::string cacheDir() const override {
    NSURL* url = directoryURL(NSCachesDirectory, appName_);
    return url ? std::string(url.path.UTF8String) : std::string{};
  }

  std::vector<std::string> availableOutputs() const override {
    std::vector<std::string> outputs;
    for (NSScreen* screen in [NSScreen screens]) {
      NSString* name = screen.localizedName;
      if (name && name.length > 0) {
        outputs.emplace_back(name.UTF8String);
      }
    }
    return outputs;
  }

  bool dispatchMenuItem(NSMenuItem* item) {
    NSString* actionObject = [item representedObject];
    if (!actionObject) {
      return false;
    }
    std::string const action = actionObject.UTF8String;
    if (action == "app.about") {
      [NSApp orderFrontStandardAboutPanel:nil];
      return true;
    }
    if (action == "app.hide") {
      [NSApp hide:nil];
      return true;
    }
    if (action == "app.hide-others") {
      [NSApp hideOtherApplications:nil];
      return true;
    }
    if (action == "app.show-all") {
      [NSApp unhideAllApplications:nil];
      return true;
    }
    if (action == "app.quit") {
      [NSApp terminate:nil];
      return true;
    }
    if (action == "window.minimize") {
      [[NSApp keyWindow] performMiniaturize:nil];
      return true;
    }
    if (action == "window.zoom") {
      [[NSApp keyWindow] performZoom:nil];
      return true;
    }
    if (action == "window.fullscreen") {
      [[NSApp keyWindow] toggleFullScreen:nil];
      return true;
    }
    if (action == "window.bring-all-to-front") {
      [NSApp arrangeInFront:nil];
      return true;
    }
    return dispatcher_ && dispatcher_(action);
  }

  bool validateMenuItem(NSMenuItem* item) const {
    NSString* actionObject = [item representedObject];
    if (!actionObject) {
      return true;
    }
    std::string const action = actionObject.UTF8String;
    if (systemHandledAction(action)) {
      return true;
    }
    return isEnabled_ && isEnabled_(action);
  }

  void handleShouldTerminate() {
    if (terminateHandler_) {
      terminateHandler_();
    }
  }

private:
  void addMenuItem(NSMenu* menu, MenuItem const& item, std::string const& appName) {
    if (item.role == MenuRole::Separator) {
      [menu addItem:[NSMenuItem separatorItem]];
      return;
    }

    if (item.role == MenuRole::Submenu) {
      NSMenuItem* submenuItem = [[NSMenuItem alloc] initWithTitle:ns(item.label)
                                                          action:nil
                                                   keyEquivalent:@""];
      NSMenu* submenu = [[NSMenu alloc] initWithTitle:ns(item.label)];
      submenu.delegate = delegate_;
      for (MenuItem const& child : item.children) {
        addMenuItem(submenu, child, appName);
      }
      submenuItem.submenu = submenu;
      [menu addItem:submenuItem];
      return;
    }

    std::string actionName = item.actionName;
    if (actionName.empty()) {
      actionName = roleActionName(item.role);
    }
    std::string title = item.label.empty() ? roleLabel(item.role, appName) : item.label;
    Shortcut shortcut = item.shortcut;
    if (shortcut.key == 0 && shortcut.modifiers == Modifiers::None) {
      shortcut = roleShortcut(item.role);
    }
    NSString* key = keyEquivalent(shortcut.key);
    NSMenuItem* nsItem = [[NSMenuItem alloc] initWithTitle:ns(title)
                                                    action:@selector(lambdaMenuAction:)
                                             keyEquivalent:(key ? key : @"")];
    nsItem.target = delegate_;
    if (!actionName.empty()) {
      nsItem.representedObject = ns(actionName);
    }
    nsItem.state = item.checked ? NSControlStateValueOn : NSControlStateValueOff;
    if (shortcut.matches(shortcut.key, shortcut.modifiers)) {
      nsItem.keyEquivalentModifierMask = modifierMask(shortcut.modifiers);
      claimedShortcuts_.insert(platform::ShortcutKey{.key = shortcut.key, .modifiers = shortcut.modifiers});
    }
    [menu addItem:nsItem];
  }

  __strong LambdaAppDelegate* delegate_{nil};
  platform::MenuActionDispatcher dispatcher_;
  std::function<void()> terminateHandler_;
  std::function<bool(std::string const&)> isEnabled_;
  std::unordered_set<platform::ShortcutKey, platform::ShortcutKeyHash> claimedShortcuts_;
  std::string appName_;
};

namespace platform {
std::unique_ptr<Application> createApplication() {
  return std::make_unique<MacApplication>();
}
} // namespace platform

} // namespace lambdaui

@implementation LambdaAppDelegate

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
  (void)sender;
  return YES;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
  (void)sender;
  if (self.owner) {
    self.owner->handleShouldTerminate();
  }
  return NSTerminateNow;
}

- (void)lambdaMenuAction:(id)sender {
  NSMenuItem* item = [sender isKindOfClass:[NSMenuItem class]] ? sender : nil;
  if (item && self.owner) {
    self.owner->dispatchMenuItem(item);
  }
}

- (BOOL)validateMenuItem:(NSMenuItem*)menuItem {
  return self.owner ? self.owner->validateMenuItem(menuItem) : YES;
}

@end
