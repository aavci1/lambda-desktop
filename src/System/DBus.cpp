#include <Lambda/System/DBus.hpp>

#include <Lambda/UI/Application.hpp>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <type_traits>
#include <utility>
#include <unistd.h>

#if LAMBDA_HAS_DBUS
#include <systemd/sd-bus.h>
#endif

namespace lambda::dbus {

namespace {

std::string errorText(int code) {
  int const positive = code < 0 ? -code : code;
  if (positive == 0) {
    return "D-Bus operation failed";
  }
  return std::strerror(positive);
}

int duplicateUnixFd(int fd) {
  if (fd < 0) {
    return -1;
  }

  int duplicated = -1;
#if defined(F_DUPFD_CLOEXEC)
  errno = 0;
  duplicated = fcntl(fd, F_DUPFD_CLOEXEC, 3);
  if (duplicated < 0 && errno != EINVAL) {
    throw Error(-errno, "duplicate Unix fd", errorText(errno));
  }
#endif
  if (duplicated < 0) {
    duplicated = dup(fd);
    if (duplicated >= 0) {
      int flags = fcntl(duplicated, F_GETFD);
      if (flags >= 0) {
        (void)fcntl(duplicated, F_SETFD, flags | FD_CLOEXEC);
      }
    }
  }
  if (duplicated < 0) {
    throw Error(-errno, "duplicate Unix fd", errorText(errno));
  }
  return duplicated;
}

#if LAMBDA_HAS_DBUS

void throwIfFailed(int result, std::string const& operation, sd_bus_error const* busError = nullptr) {
  if (result >= 0) {
    return;
  }
  if (busError && sd_bus_error_is_set(busError)) {
    throw Error(result,
                operation,
                busError->message ? busError->message : errorText(result),
                busError->name ? busError->name : "");
  }
  throw Error(result, operation, errorText(result));
}

void appendValue(sd_bus_message* message, BasicValue const& value);

void appendStringArray(sd_bus_message* message, StringArray const& value) {
  throwIfFailed(sd_bus_message_open_container(message, SD_BUS_TYPE_ARRAY, "s"),
                "open D-Bus string array");
  for (auto const& item : value.values) {
    throwIfFailed(sd_bus_message_append_basic(message, 's', item.c_str()),
                  "append D-Bus string array item");
  }
  throwIfFailed(sd_bus_message_close_container(message), "close D-Bus string array");
}

void appendRgbColor(sd_bus_message* message, RgbColor const& value) {
  throwIfFailed(sd_bus_message_open_container(message, SD_BUS_TYPE_STRUCT, "ddd"),
                "open D-Bus RGB tuple");
  throwIfFailed(sd_bus_message_append_basic(message, 'd', &value.red), "append D-Bus RGB red");
  throwIfFailed(sd_bus_message_append_basic(message, 'd', &value.green), "append D-Bus RGB green");
  throwIfFailed(sd_bus_message_append_basic(message, 'd', &value.blue), "append D-Bus RGB blue");
  throwIfFailed(sd_bus_message_close_container(message), "close D-Bus RGB tuple");
}

void appendEmptyVariantDictionary(sd_bus_message* message) {
  throwIfFailed(sd_bus_message_open_container(message, SD_BUS_TYPE_ARRAY, "{sv}"),
                "open empty D-Bus variant dictionary");
  throwIfFailed(sd_bus_message_close_container(message), "close empty D-Bus variant dictionary");
}

void appendValue(sd_bus_message* message, BasicValue const& value) {
  int result = std::visit(
      [message](auto const& v) -> int {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>) {
          int const raw = v ? 1 : 0;
          return sd_bus_message_append_basic(message, 'b', &raw);
        } else if constexpr (std::is_same_v<T, std::int32_t>) {
          return sd_bus_message_append_basic(message, 'i', &v);
        } else if constexpr (std::is_same_v<T, std::uint32_t>) {
          return sd_bus_message_append_basic(message, 'u', &v);
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
          return sd_bus_message_append_basic(message, 'x', &v);
        } else if constexpr (std::is_same_v<T, std::uint64_t>) {
          return sd_bus_message_append_basic(message, 't', &v);
        } else if constexpr (std::is_same_v<T, double>) {
          return sd_bus_message_append_basic(message, 'd', &v);
        } else if constexpr (std::is_same_v<T, std::string>) {
          return sd_bus_message_append_basic(message, 's', v.c_str());
        } else if constexpr (std::is_same_v<T, ObjectPath>) {
          return sd_bus_message_append_basic(message, 'o', v.value.c_str());
        } else if constexpr (std::is_same_v<T, StringArray>) {
          appendStringArray(message, v);
          return 0;
        } else if constexpr (std::is_same_v<T, RgbColor>) {
          appendRgbColor(message, v);
          return 0;
        } else if constexpr (std::is_same_v<T, EmptyVariantDictionary>) {
          appendEmptyVariantDictionary(message);
          return 0;
        } else if constexpr (std::is_same_v<T, UnixFd>) {
          int const fd = v.get();
          return sd_bus_message_append_basic(message, 'h', &fd);
        }
      },
      value);
  throwIfFailed(result, "append D-Bus value");
}

void appendVariant(sd_bus_message* message, BasicValue const& value) {
  std::string const signature = signatureFor(value);
  throwIfFailed(sd_bus_message_open_container(message, SD_BUS_TYPE_VARIANT, signature.c_str()),
                "open D-Bus variant");
  appendValue(message, value);
  throwIfFailed(sd_bus_message_close_container(message), "close D-Bus variant");
}

void appendVariantDictionary(sd_bus_message* message, VariantDictionary const& value) {
  throwIfFailed(sd_bus_message_open_container(message, SD_BUS_TYPE_ARRAY, "{sv}"),
                "open D-Bus variant dictionary");
  for (auto const& [key, settingValue] : value.values) {
    throwIfFailed(sd_bus_message_open_container(message, SD_BUS_TYPE_DICT_ENTRY, "sv"),
                  "open D-Bus variant dictionary entry");
    throwIfFailed(sd_bus_message_append_basic(message, 's', key.c_str()),
                  "append D-Bus variant dictionary key");
    appendVariant(message, settingValue);
    throwIfFailed(sd_bus_message_close_container(message), "close D-Bus variant dictionary entry");
  }
  throwIfFailed(sd_bus_message_close_container(message), "close D-Bus variant dictionary");
}

void appendNamespacedVariantDictionary(sd_bus_message* message, NamespacedVariantDictionary const& value) {
  throwIfFailed(sd_bus_message_open_container(message, SD_BUS_TYPE_ARRAY, "{sa{sv}}"),
                "open D-Bus namespaced variant dictionary");
  for (auto const& [nameSpace, settings] : value.values) {
    throwIfFailed(sd_bus_message_open_container(message, SD_BUS_TYPE_DICT_ENTRY, "sa{sv}"),
                  "open D-Bus namespace dictionary entry");
    throwIfFailed(sd_bus_message_append_basic(message, 's', nameSpace.c_str()),
                  "append D-Bus namespace");
    throwIfFailed(sd_bus_message_open_container(message, SD_BUS_TYPE_ARRAY, "{sv}"),
                  "open D-Bus variant dictionary");
    for (auto const& [key, settingValue] : settings) {
      throwIfFailed(sd_bus_message_open_container(message, SD_BUS_TYPE_DICT_ENTRY, "sv"),
                    "open D-Bus setting dictionary entry");
      throwIfFailed(sd_bus_message_append_basic(message, 's', key.c_str()),
                    "append D-Bus setting key");
      appendVariant(message, settingValue);
      throwIfFailed(sd_bus_message_close_container(message), "close D-Bus setting dictionary entry");
    }
    throwIfFailed(sd_bus_message_close_container(message), "close D-Bus variant dictionary");
    throwIfFailed(sd_bus_message_close_container(message), "close D-Bus namespace dictionary entry");
  }
  throwIfFailed(sd_bus_message_close_container(message), "close D-Bus namespaced variant dictionary");
}

void appendReplyValue(sd_bus_message* message, ReplyValue const& value) {
  std::visit(
      [message](auto const& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, BasicValue>) {
          appendValue(message, v);
        } else if constexpr (std::is_same_v<T, VariantValue>) {
          appendVariant(message, v.value);
        } else if constexpr (std::is_same_v<T, VariantDictionary>) {
          appendVariantDictionary(message, v);
        } else if constexpr (std::is_same_v<T, NamespacedVariantDictionary>) {
          appendNamespacedVariantDictionary(message, v);
        }
      },
      value);
}

StringArray readStringArrayFrom(sd_bus_message* message) {
  StringArray array;
  throwIfFailed(sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, "s"),
                "enter D-Bus string array");
  while (sd_bus_message_at_end(message, 0) == 0) {
    char const* item = nullptr;
    throwIfFailed(sd_bus_message_read_basic(message, 's', &item), "read D-Bus string array item");
    array.values.emplace_back(item ? item : "");
  }
  throwIfFailed(sd_bus_message_exit_container(message), "exit D-Bus string array");
  return array;
}

RgbColor readRgbColorFrom(sd_bus_message* message) {
  RgbColor color;
  throwIfFailed(sd_bus_message_enter_container(message, SD_BUS_TYPE_STRUCT, "ddd"),
                "enter D-Bus RGB tuple");
  throwIfFailed(sd_bus_message_read_basic(message, 'd', &color.red), "read D-Bus RGB red");
  throwIfFailed(sd_bus_message_read_basic(message, 'd', &color.green), "read D-Bus RGB green");
  throwIfFailed(sd_bus_message_read_basic(message, 'd', &color.blue), "read D-Bus RGB blue");
  throwIfFailed(sd_bus_message_exit_container(message), "exit D-Bus RGB tuple");
  return color;
}

BasicValue readValueFromSignature(sd_bus_message* message, std::string_view signature) {
  if (signature == "as") {
    return readStringArrayFrom(message);
  }
  if (signature == "(ddd)") {
    return readRgbColorFrom(message);
  }
  if (signature.size() != 1) {
    throw Error(-EINVAL, "read D-Bus value", "unsupported D-Bus signature");
  }
  switch (signature.front()) {
  case 'b': {
    int raw = 0;
    throwIfFailed(sd_bus_message_read_basic(message, 'b', &raw), "read D-Bus bool");
    return raw != 0;
  }
  case 'i': {
    std::int32_t value = 0;
    throwIfFailed(sd_bus_message_read_basic(message, 'i', &value), "read D-Bus int32");
    return value;
  }
  case 'u': {
    std::uint32_t value = 0;
    throwIfFailed(sd_bus_message_read_basic(message, 'u', &value), "read D-Bus uint32");
    return value;
  }
  case 'x': {
    std::int64_t value = 0;
    throwIfFailed(sd_bus_message_read_basic(message, 'x', &value), "read D-Bus int64");
    return value;
  }
  case 't': {
    std::uint64_t value = 0;
    throwIfFailed(sd_bus_message_read_basic(message, 't', &value), "read D-Bus uint64");
    return value;
  }
  case 'd': {
    double value = 0.0;
    throwIfFailed(sd_bus_message_read_basic(message, 'd', &value), "read D-Bus double");
    return value;
  }
  case 's': {
    char const* value = nullptr;
    throwIfFailed(sd_bus_message_read_basic(message, 's', &value), "read D-Bus string");
    return std::string(value ? value : "");
  }
  case 'o': {
    char const* value = nullptr;
    throwIfFailed(sd_bus_message_read_basic(message, 'o', &value), "read D-Bus object path");
    return ObjectPath{value ? value : ""};
  }
  case 'h': {
    int fd = -1;
    throwIfFailed(sd_bus_message_read_basic(message, 'h', &fd), "read D-Bus Unix fd");
    return UnixFd::adopt(duplicateUnixFd(fd));
  }
  default:
    throw Error(-EINVAL, "read D-Bus value", "unsupported D-Bus signature");
  }
}

BasicValue readVariantFrom(sd_bus_message* message, std::string_view expectedSignature) {
  std::string signature(expectedSignature);
  if (signature.empty()) {
    char type = 0;
    char const* contents = nullptr;
    throwIfFailed(sd_bus_message_peek_type(message, &type, &contents), "peek D-Bus variant");
    if (type != SD_BUS_TYPE_VARIANT || !contents) {
      throw Error(-EINVAL, "read D-Bus variant", "message item is not a variant");
    }
    signature = contents;
  }
  throwIfFailed(sd_bus_message_enter_container(message, SD_BUS_TYPE_VARIANT, signature.c_str()),
                "enter D-Bus variant");
  BasicValue value = readValueFromSignature(message, signature);
  throwIfFailed(sd_bus_message_exit_container(message), "exit D-Bus variant");
  return value;
}

NamespacedVariantDictionary readNamespacedVariantDictionaryFrom(sd_bus_message* message) {
  NamespacedVariantDictionary dictionary;
  throwIfFailed(sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, "{sa{sv}}"),
                "enter D-Bus namespaced variant dictionary");
  while (sd_bus_message_at_end(message, 0) == 0) {
    throwIfFailed(sd_bus_message_enter_container(message, SD_BUS_TYPE_DICT_ENTRY, "sa{sv}"),
                  "enter D-Bus namespace dictionary entry");
    char const* nameSpace = nullptr;
    throwIfFailed(sd_bus_message_read_basic(message, 's', &nameSpace), "read D-Bus namespace");
    auto& settings = dictionary.values[nameSpace ? nameSpace : ""];
    throwIfFailed(sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, "{sv}"),
                  "enter D-Bus variant dictionary");
    while (sd_bus_message_at_end(message, 0) == 0) {
      throwIfFailed(sd_bus_message_enter_container(message, SD_BUS_TYPE_DICT_ENTRY, "sv"),
                    "enter D-Bus setting dictionary entry");
      char const* key = nullptr;
      throwIfFailed(sd_bus_message_read_basic(message, 's', &key), "read D-Bus setting key");
      settings[key ? key : ""] = readVariantFrom(message, {});
      throwIfFailed(sd_bus_message_exit_container(message), "exit D-Bus setting dictionary entry");
    }
    throwIfFailed(sd_bus_message_exit_container(message), "exit D-Bus variant dictionary");
    throwIfFailed(sd_bus_message_exit_container(message), "exit D-Bus namespace dictionary entry");
  }
  throwIfFailed(sd_bus_message_exit_container(message), "exit D-Bus namespaced variant dictionary");
  return dictionary;
}

BasicValue propertyValue(ExportedProperty const& property) {
  return property.getter ? property.getter() : property.value;
}

void updateProperty(ExportedProperty& property, BasicValue const& value) {
  if (property.setter) {
    property.setter(value);
  } else {
    property.value = value;
  }
}

struct SignalState {
  std::function<void(Message&)> handler;
};

int signalThunk(sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto* state = static_cast<SignalState*>(userdata);
  if (!state || !state->handler) {
    return 0;
  }
  Message wrapped = detail::BackendAccess::wrapMessage(message, false);
  state->handler(wrapped);
  return 0;
}

void destroySignalState(void* userdata) {
  delete static_cast<SignalState*>(userdata);
}

struct ExportState {
  ObjectDefinition definition;
};

ExportedProperty* findProperty(ExportState& state, std::string_view interface, std::string_view name) {
  for (auto& property : state.definition.properties) {
    if (property.interface == interface && property.name == name) {
      return &property;
    }
  }
  return nullptr;
}

int replyWithValues(sd_bus_message* call, std::vector<ReplyValue> const& values) {
  sd_bus_message* rawReply = nullptr;
  int result = sd_bus_message_new_method_return(call, &rawReply);
  if (result < 0) {
    return result;
  }
  Message reply = detail::BackendAccess::wrapMessage(rawReply, true);
  for (auto const& value : values) {
    appendReplyValue(static_cast<sd_bus_message*>(detail::BackendAccess::nativeMessage(reply)), value);
  }
  return sd_bus_send(sd_bus_message_get_bus(call),
                     static_cast<sd_bus_message*>(detail::BackendAccess::nativeMessage(reply)),
                     nullptr);
}

int handlePropertiesGet(sd_bus_message* message, ExportState& state) {
  char const* interface = nullptr;
  char const* name = nullptr;
  int result = sd_bus_message_read(message, "ss", &interface, &name);
  if (result < 0) {
    return result;
  }
  ExportedProperty* property = findProperty(state, interface ? interface : "", name ? name : "");
  if (!property) {
    return sd_bus_reply_method_errorf(message,
                                      "org.freedesktop.DBus.Error.UnknownProperty",
                                      "Unknown property %s.%s",
                                      interface ? interface : "",
                                      name ? name : "");
  }

  BasicValue const value = propertyValue(*property);
  sd_bus_message* rawReply = nullptr;
  result = sd_bus_message_new_method_return(message, &rawReply);
  if (result < 0) {
    return result;
  }
  Message reply = detail::BackendAccess::wrapMessage(rawReply, true);
  appendVariant(static_cast<sd_bus_message*>(detail::BackendAccess::nativeMessage(reply)), value);
  return sd_bus_send(sd_bus_message_get_bus(message),
                     static_cast<sd_bus_message*>(detail::BackendAccess::nativeMessage(reply)),
                     nullptr);
}

int handlePropertiesSet(sd_bus_message* message, ExportState& state) {
  char const* interface = nullptr;
  char const* name = nullptr;
  int result = sd_bus_message_read(message, "ss", &interface, &name);
  if (result < 0) {
    return result;
  }
  ExportedProperty* property = findProperty(state, interface ? interface : "", name ? name : "");
  if (!property) {
    return sd_bus_reply_method_errorf(message,
                                      "org.freedesktop.DBus.Error.UnknownProperty",
                                      "Unknown property %s.%s",
                                      interface ? interface : "",
                                      name ? name : "");
  }
  if (!property->writable) {
    return sd_bus_reply_method_errorf(message,
                                      "org.freedesktop.DBus.Error.PropertyReadOnly",
                                      "Property %s.%s is read-only",
                                      interface ? interface : "",
                                      name ? name : "");
  }

  BasicValue const currentValue = propertyValue(*property);
  std::string const signature = signatureFor(currentValue);
  result = sd_bus_message_enter_container(message, SD_BUS_TYPE_VARIANT, signature.c_str());
  if (result < 0) {
    return result;
  }
  BasicValue const value = readValueFromSignature(message, signature);
  result = sd_bus_message_exit_container(message);
  if (result < 0) {
    return result;
  }
  updateProperty(*property, value);
  return sd_bus_reply_method_return(message, "");
}

int exportThunk(sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto* state = static_cast<ExportState*>(userdata);
  if (!state) {
    return 0;
  }

  char const* interface = sd_bus_message_get_interface(message);
  char const* member = sd_bus_message_get_member(message);
  if (!interface || !member) {
    return 0;
  }

  try {
    if (std::string_view(interface) == "org.freedesktop.DBus.Properties") {
      if (std::string_view(member) == "Get") {
        return handlePropertiesGet(message, *state);
      }
      if (std::string_view(member) == "Set") {
        return handlePropertiesSet(message, *state);
      }
    }

    for (auto const& method : state->definition.methods) {
      if (method.interface == interface && method.member == member) {
        if (!method.handler) {
          return sd_bus_reply_method_errorf(message,
                                           "org.freedesktop.DBus.Error.Failed",
                                           "Lambda D-Bus method has no handler");
        }
        Message wrapped = detail::BackendAccess::wrapMessage(message, false);
        MethodReply reply = method.handler(wrapped);
        if (reply.errorName) {
          return sd_bus_reply_method_errorf(message,
                                           reply.errorName->c_str(),
                                           "%s",
                                           reply.errorMessage ? reply.errorMessage->c_str() : "");
        }
        return replyWithValues(message, reply.values);
      }
    }
  } catch (Error const& error) {
    return sd_bus_reply_method_errorf(message, "org.lambda.Error.DBus", "%s", error.what());
  } catch (std::exception const& error) {
    return sd_bus_reply_method_errorf(message, "org.lambda.Error.Failed", "%s", error.what());
  }

  return 0;
}

void destroyExportState(void* userdata) {
  delete static_cast<ExportState*>(userdata);
}

#endif

} // namespace

Error::Error(int code, std::string operation, std::string detail, std::string name)
    : std::runtime_error(operation + ": " + detail),
      code_(code),
      operation_(std::move(operation)),
      name_(std::move(name)) {}

std::string signatureFor(BasicValue const& value) {
#if LAMBDA_HAS_DBUS
  return std::visit(
      [](auto const& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>) {
          return "b";
        } else if constexpr (std::is_same_v<T, std::int32_t>) {
          return "i";
        } else if constexpr (std::is_same_v<T, std::uint32_t>) {
          return "u";
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
          return "x";
        } else if constexpr (std::is_same_v<T, std::uint64_t>) {
          return "t";
        } else if constexpr (std::is_same_v<T, double>) {
          return "d";
        } else if constexpr (std::is_same_v<T, std::string>) {
          return "s";
        } else if constexpr (std::is_same_v<T, ObjectPath>) {
          return "o";
        } else if constexpr (std::is_same_v<T, StringArray>) {
          return "as";
        } else if constexpr (std::is_same_v<T, RgbColor>) {
          return "(ddd)";
        } else if constexpr (std::is_same_v<T, EmptyVariantDictionary>) {
          return "a{sv}";
        } else if constexpr (std::is_same_v<T, UnixFd>) {
          return "h";
        }
      },
      value);
#else
  (void)value;
  throw Error(-ENOTSUP, "resolve D-Bus signature", "D-Bus support is not available in this build");
#endif
}

MethodReply MethodReply::error(std::string name, std::string message) {
  MethodReply reply;
  reply.errorName = std::move(name);
  reply.errorMessage = std::move(message);
  return reply;
}

UnixFd::UnixFd(int fd) noexcept : fd_(fd) {}

UnixFd::~UnixFd() {
  reset();
}

UnixFd::UnixFd(UnixFd const& other) {
  *this = other;
}

UnixFd& UnixFd::operator=(UnixFd const& other) {
  if (this == &other) {
    return *this;
  }
  reset(other.valid() ? borrow(other.fd_).release() : -1);
  return *this;
}

UnixFd::UnixFd(UnixFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

UnixFd& UnixFd::operator=(UnixFd&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  reset(std::exchange(other.fd_, -1));
  return *this;
}

UnixFd UnixFd::adopt(int fd) noexcept {
  return UnixFd(fd);
}

UnixFd UnixFd::borrow(int fd) {
  return UnixFd(duplicateUnixFd(fd));
}

int UnixFd::release() noexcept {
  return std::exchange(fd_, -1);
}

void UnixFd::reset(int fd) noexcept {
  if (fd_ >= 0) {
    close(fd_);
  }
  fd_ = fd;
}

Message::Message() = default;

Message::Message(void* native, bool owning) : native_(native), owning_(owning) {}

Message::~Message() {
  reset();
}

Message::Message(Message&& other) noexcept
    : native_(std::exchange(other.native_, nullptr)),
      owning_(std::exchange(other.owning_, true)) {}

Message& Message::operator=(Message&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  reset();
  native_ = std::exchange(other.native_, nullptr);
  owning_ = std::exchange(other.owning_, true);
  return *this;
}

void Message::reset() noexcept {
#if LAMBDA_HAS_DBUS
  if (native_ && owning_) {
    sd_bus_message_unref(static_cast<sd_bus_message*>(native_));
  }
#endif
  native_ = nullptr;
  owning_ = true;
}

bool Message::valid() const noexcept {
  return native_ != nullptr;
}

std::string Message::path() const {
#if LAMBDA_HAS_DBUS
  char const* value = native_ ? sd_bus_message_get_path(static_cast<sd_bus_message*>(native_)) : nullptr;
  return value ? value : "";
#else
  return "";
#endif
}

std::string Message::interface() const {
#if LAMBDA_HAS_DBUS
  char const* value = native_ ? sd_bus_message_get_interface(static_cast<sd_bus_message*>(native_)) : nullptr;
  return value ? value : "";
#else
  return "";
#endif
}

std::string Message::member() const {
#if LAMBDA_HAS_DBUS
  char const* value = native_ ? sd_bus_message_get_member(static_cast<sd_bus_message*>(native_)) : nullptr;
  return value ? value : "";
#else
  return "";
#endif
}

std::string Message::sender() const {
#if LAMBDA_HAS_DBUS
  char const* value = native_ ? sd_bus_message_get_sender(static_cast<sd_bus_message*>(native_)) : nullptr;
  return value ? value : "";
#else
  return "";
#endif
}

std::string Message::signature(bool complete) const {
#if LAMBDA_HAS_DBUS
  char const* value = native_ ? sd_bus_message_get_signature(static_cast<sd_bus_message*>(native_), complete ? 1 : 0)
                              : nullptr;
  return value ? value : "";
#else
  return "";
#endif
}

bool Message::hasSignature(std::string_view expected) const {
#if LAMBDA_HAS_DBUS
  std::string const sig(expected);
  return native_ && sd_bus_message_has_signature(static_cast<sd_bus_message*>(native_), sig.c_str()) > 0;
#else
  return expected.empty();
#endif
}

bool Message::readBool() {
  return std::get<bool>(readBasic("b"));
}

std::int32_t Message::readInt32() {
  return std::get<std::int32_t>(readBasic("i"));
}

std::uint32_t Message::readUint32() {
  return std::get<std::uint32_t>(readBasic("u"));
}

std::int64_t Message::readInt64() {
  return std::get<std::int64_t>(readBasic("x"));
}

std::uint64_t Message::readUint64() {
  return std::get<std::uint64_t>(readBasic("t"));
}

double Message::readDouble() {
  return std::get<double>(readBasic("d"));
}

std::string Message::readString() {
  return std::get<std::string>(readBasic("s"));
}

ObjectPath Message::readObjectPath() {
  return std::get<ObjectPath>(readBasic("o"));
}

StringArray Message::readStringArray() {
  return std::get<StringArray>(readBasic("as"));
}

RgbColor Message::readRgbColor() {
  return std::get<RgbColor>(readBasic("(ddd)"));
}

UnixFd Message::readUnixFd() {
  BasicValue value = readBasic("h");
  return std::get<UnixFd>(std::move(value));
}

BasicValue Message::readBasic(std::string_view expectedSignature) {
#if LAMBDA_HAS_DBUS
  if (!native_) {
    throw Error(-EINVAL, "read D-Bus value", "message is empty");
  }
  return readValueFromSignature(static_cast<sd_bus_message*>(native_), expectedSignature);
#else
  (void)expectedSignature;
  throw Error(-ENOTSUP, "read D-Bus value", "D-Bus support is not available in this build");
#endif
}

BasicValue Message::readVariant(std::string_view expectedSignature) {
#if LAMBDA_HAS_DBUS
  if (!native_) {
    throw Error(-EINVAL, "read D-Bus variant", "message is empty");
  }
  return readVariantFrom(static_cast<sd_bus_message*>(native_), expectedSignature);
#else
  (void)expectedSignature;
  throw Error(-ENOTSUP, "read D-Bus variant", "D-Bus support is not available in this build");
#endif
}

NamespacedVariantDictionary Message::readNamespacedVariantDictionary() {
#if LAMBDA_HAS_DBUS
  if (!native_) {
    throw Error(-EINVAL, "read D-Bus namespaced variant dictionary", "message is empty");
  }
  return readNamespacedVariantDictionaryFrom(static_cast<sd_bus_message*>(native_));
#else
  throw Error(-ENOTSUP,
              "read D-Bus namespaced variant dictionary",
              "D-Bus support is not available in this build");
#endif
}

void Message::skip(std::string_view signature) {
#if LAMBDA_HAS_DBUS
  if (!native_) {
    throw Error(-EINVAL, "skip D-Bus value", "message is empty");
  }
  std::string const sig(signature);
  throwIfFailed(sd_bus_message_skip(static_cast<sd_bus_message*>(native_), sig.c_str()),
                "skip D-Bus value");
#else
  (void)signature;
  throw Error(-ENOTSUP, "skip D-Bus value", "D-Bus support is not available in this build");
#endif
}

Slot::Slot() = default;

Slot::Slot(void* native) : native_(native) {}

Slot::~Slot() {
  reset();
}

Slot::Slot(Slot&& other) noexcept : native_(std::exchange(other.native_, nullptr)) {}

Slot& Slot::operator=(Slot&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  reset();
  native_ = std::exchange(other.native_, nullptr);
  return *this;
}

void Slot::reset() noexcept {
#if LAMBDA_HAS_DBUS
  if (native_) {
    sd_bus_slot_unref(static_cast<sd_bus_slot*>(native_));
  }
#endif
  native_ = nullptr;
}

struct Bus::Impl {
#if LAMBDA_HAS_DBUS
  explicit Impl(sd_bus* busIn) : bus(busIn) {}
  ~Impl() {
    if (bus) {
      sd_bus_flush_close_unref(bus);
    }
  }

  sd_bus* bus = nullptr;
#endif
};

Bus::Bus() = default;

Bus::Bus(std::unique_ptr<Impl> impl) : d(std::move(impl)) {}

Bus::~Bus() = default;

Bus::Bus(Bus&&) noexcept = default;
Bus& Bus::operator=(Bus&&) noexcept = default;

Bus Bus::open(BusType type) {
#if LAMBDA_HAS_DBUS
  sd_bus* bus = nullptr;
  int const result = type == BusType::Session ? sd_bus_open_user(&bus) : sd_bus_open_system(&bus);
  throwIfFailed(result, type == BusType::Session ? "open session D-Bus" : "open system D-Bus");
  return Bus(std::make_unique<Impl>(bus));
#else
  (void)type;
  throw Error(-ENOTSUP, "open D-Bus", "D-Bus support is not available in this build");
#endif
}

Bus Bus::openAddress(std::string const& address) {
#if LAMBDA_HAS_DBUS
  sd_bus* bus = nullptr;
  throwIfFailed(sd_bus_new(&bus), "create D-Bus connection");
  try {
    throwIfFailed(sd_bus_set_address(bus, address.c_str()), "set D-Bus address");
    throwIfFailed(sd_bus_set_bus_client(bus, 1), "configure D-Bus client");
    throwIfFailed(sd_bus_negotiate_fds(bus, 1), "negotiate D-Bus file descriptors");
    throwIfFailed(sd_bus_start(bus), "start D-Bus connection");
  } catch (...) {
    sd_bus_unref(bus);
    throw;
  }
  return Bus(std::make_unique<Impl>(bus));
#else
  (void)address;
  throw Error(-ENOTSUP, "open D-Bus address", "D-Bus support is not available in this build");
#endif
}

void* Bus::native() const noexcept {
#if LAMBDA_HAS_DBUS
  return d ? d->bus : nullptr;
#else
  return nullptr;
#endif
}

Bus::Impl& Bus::impl() {
  if (!d) {
    throw Error(-EINVAL, "use D-Bus connection", "connection is empty");
  }
  return *d;
}

Bus::Impl const& Bus::impl() const {
  if (!d) {
    throw Error(-EINVAL, "use D-Bus connection", "connection is empty");
  }
  return *d;
}

void Bus::requestName(std::string const& name, std::uint64_t flags) {
#if LAMBDA_HAS_DBUS
  throwIfFailed(sd_bus_request_name(impl().bus, name.c_str(), flags), "request D-Bus name");
#else
  (void)name;
  (void)flags;
  throw Error(-ENOTSUP, "request D-Bus name", "D-Bus support is not available in this build");
#endif
}

Message Bus::call(MethodCall const& call) {
#if LAMBDA_HAS_DBUS
  sd_bus_message* rawCall = nullptr;
  throwIfFailed(sd_bus_message_new_method_call(impl().bus,
                                               &rawCall,
                                               call.destination.c_str(),
                                               call.path.c_str(),
                                               call.interface.c_str(),
                                               call.member.c_str()),
                "create D-Bus method call");
  Message callMessage(rawCall, true);
  for (auto const& argument : call.arguments) {
    appendValue(rawCall, argument);
  }

  sd_bus_error error = SD_BUS_ERROR_NULL;
  sd_bus_message* rawReply = nullptr;
  int const result = sd_bus_call(impl().bus, rawCall, call.timeoutUsec, &error, &rawReply);
  try {
    throwIfFailed(result, "call D-Bus method " + call.interface + "." + call.member, &error);
  } catch (...) {
    sd_bus_error_free(&error);
    throw;
  }
  sd_bus_error_free(&error);
  return Message(rawReply, true);
#else
  (void)call;
  throw Error(-ENOTSUP, "call D-Bus method", "D-Bus support is not available in this build");
#endif
}

BasicValue Bus::getProperty(PropertyAddress const& property, std::string_view signature) {
  MethodCall call{
      .destination = property.destination,
      .path = property.path,
      .interface = "org.freedesktop.DBus.Properties",
      .member = "Get",
      .arguments = {property.interface, property.name},
  };
  Message reply = this->call(call);
  return reply.readVariant(signature);
}

void Bus::setProperty(PropertyAddress const& property, BasicValue const& value) {
#if LAMBDA_HAS_DBUS
  sd_bus_message* rawCall = nullptr;
  throwIfFailed(sd_bus_message_new_method_call(impl().bus,
                                               &rawCall,
                                               property.destination.c_str(),
                                               property.path.c_str(),
                                               "org.freedesktop.DBus.Properties",
                                               "Set"),
                "create D-Bus property set call");
  Message callMessage(rawCall, true);
  throwIfFailed(sd_bus_message_append_basic(rawCall, 's', property.interface.c_str()),
                "append D-Bus property interface");
  throwIfFailed(sd_bus_message_append_basic(rawCall, 's', property.name.c_str()),
                "append D-Bus property name");
  appendVariant(rawCall, value);

  sd_bus_error error = SD_BUS_ERROR_NULL;
  sd_bus_message* rawReply = nullptr;
  int const result = sd_bus_call(impl().bus, rawCall, 0, &error, &rawReply);
  if (rawReply) {
    sd_bus_message_unref(rawReply);
  }
  try {
    throwIfFailed(result, "set D-Bus property " + property.interface + "." + property.name, &error);
  } catch (...) {
    sd_bus_error_free(&error);
    throw;
  }
  sd_bus_error_free(&error);
#else
  (void)property;
  (void)value;
  throw Error(-ENOTSUP, "set D-Bus property", "D-Bus support is not available in this build");
#endif
}

Slot Bus::matchSignal(SignalMatch const& match, std::function<void(Message&)> handler) {
#if LAMBDA_HAS_DBUS
  auto* state = new SignalState{.handler = std::move(handler)};
  sd_bus_slot* slot = nullptr;
  int const result = sd_bus_match_signal(impl().bus,
                                         &slot,
                                         match.sender ? match.sender->c_str() : nullptr,
                                         match.path ? match.path->c_str() : nullptr,
                                         match.interface ? match.interface->c_str() : nullptr,
                                         match.member ? match.member->c_str() : nullptr,
                                         signalThunk,
                                         state);
  if (result < 0) {
    delete state;
    throwIfFailed(result, "match D-Bus signal");
  }
  sd_bus_slot_set_destroy_callback(slot, destroySignalState);
  return Slot(slot);
#else
  (void)match;
  (void)handler;
  throw Error(-ENOTSUP, "match D-Bus signal", "D-Bus support is not available in this build");
#endif
}

Slot Bus::exportObject(std::string const& path, ObjectDefinition definition) {
#if LAMBDA_HAS_DBUS
  auto* state = new ExportState{.definition = std::move(definition)};
  sd_bus_slot* slot = nullptr;
  int const result = sd_bus_add_object(impl().bus, &slot, path.c_str(), exportThunk, state);
  if (result < 0) {
    delete state;
    throwIfFailed(result, "export D-Bus object");
  }
  sd_bus_slot_set_destroy_callback(slot, destroyExportState);
  return Slot(slot);
#else
  (void)path;
  (void)definition;
  throw Error(-ENOTSUP, "export D-Bus object", "D-Bus support is not available in this build");
#endif
}

void Bus::emitSignal(std::string const& path, std::string const& interface,
                     std::string const& member, std::vector<ReplyValue> const& arguments) {
#if LAMBDA_HAS_DBUS
  sd_bus_message* rawSignal = nullptr;
  throwIfFailed(sd_bus_message_new_signal(impl().bus,
                                          &rawSignal,
                                          path.c_str(),
                                          interface.c_str(),
                                          member.c_str()),
                "create D-Bus signal");
  Message signal(rawSignal, true);
  for (auto const& argument : arguments) {
    appendReplyValue(rawSignal, argument);
  }
  throwIfFailed(sd_bus_send(impl().bus, rawSignal, nullptr), "emit D-Bus signal");
#else
  (void)path;
  (void)interface;
  (void)member;
  (void)arguments;
  throw Error(-ENOTSUP, "emit D-Bus signal", "D-Bus support is not available in this build");
#endif
}

int Bus::eventFileDescriptor() const {
#if LAMBDA_HAS_DBUS
  return sd_bus_get_fd(const_cast<sd_bus*>(static_cast<sd_bus const*>(native())));
#else
  return -1;
#endif
}

int Bus::eventMask() const {
#if LAMBDA_HAS_DBUS
  return sd_bus_get_events(const_cast<sd_bus*>(static_cast<sd_bus const*>(native())));
#else
  return 0;
#endif
}

int Bus::processPending() {
#if LAMBDA_HAS_DBUS
  int processed = 0;
  while (true) {
    int const result = sd_bus_process(impl().bus, nullptr);
    throwIfFailed(result, "process D-Bus events");
    if (result == 0) {
      return processed;
    }
    ++processed;
  }
#else
  throw Error(-ENOTSUP, "process D-Bus events", "D-Bus support is not available in this build");
#endif
}

void Bus::flush() {
#if LAMBDA_HAS_DBUS
  throwIfFailed(sd_bus_flush(impl().bus), "flush D-Bus connection");
#else
  throw Error(-ENOTSUP, "flush D-Bus connection", "D-Bus support is not available in this build");
#endif
}

BusEventPump::BusEventPump(Application& application, Bus& bus) : application_(&application) {
  pollSourceId_ = application.registerEventPollSource(bus.eventFileDescriptor(), [&bus] {
    (void)bus.processPending();
  });
}

BusEventPump::~BusEventPump() {
  if (application_ && pollSourceId_ != 0) {
    application_->unregisterEventPollSource(pollSourceId_);
  }
}

BusEventPump::BusEventPump(BusEventPump&& other) noexcept
    : application_(std::exchange(other.application_, nullptr)),
      pollSourceId_(std::exchange(other.pollSourceId_, 0)) {}

BusEventPump& BusEventPump::operator=(BusEventPump&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  if (application_ && pollSourceId_ != 0) {
    application_->unregisterEventPollSource(pollSourceId_);
  }
  application_ = std::exchange(other.application_, nullptr);
  pollSourceId_ = std::exchange(other.pollSourceId_, 0);
  return *this;
}

} // namespace lambda::dbus
