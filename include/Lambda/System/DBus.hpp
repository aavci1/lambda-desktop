#pragma once

/// \file Lambda/System/DBus.hpp
///
/// Minimal D-Bus capability for Lambda desktop services.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#ifndef LAMBDA_HAS_DBUS
#define LAMBDA_HAS_DBUS 0
#endif

namespace lambda {

class Application;

namespace dbus {

namespace detail {
struct BackendAccess;
}

struct ArrayValue;
struct DictionaryValue;
struct StructValue;
struct VariantValue;
struct VariantDictionary;

enum class BusType {
  Session,
  System,
};

class Error : public std::runtime_error {
public:
  Error(int code, std::string operation, std::string detail, std::string name = {});

  [[nodiscard]] int code() const noexcept { return code_; }
  [[nodiscard]] std::string const& operation() const noexcept { return operation_; }
  [[nodiscard]] std::string const& name() const noexcept { return name_; }

private:
  int code_ = 0;
  std::string operation_;
  std::string name_;
};

struct ObjectPath {
  std::string value;
};

struct ObjectPathArray {
  std::vector<ObjectPath> values;
};

struct StringArray {
  std::vector<std::string> values;
};

struct ByteArray {
  std::vector<std::uint8_t> values;
};

struct ByteArrayArray {
  std::vector<ByteArray> values;
};

struct RgbColor {
  double red = 0.0;
  double green = 0.0;
  double blue = 0.0;
};

struct EmptyVariantDictionary {};

class UnixFd {
public:
  UnixFd() = default;
  ~UnixFd();

  UnixFd(UnixFd const& other);
  UnixFd& operator=(UnixFd const& other);
  UnixFd(UnixFd&& other) noexcept;
  UnixFd& operator=(UnixFd&& other) noexcept;

  [[nodiscard]] static UnixFd adopt(int fd) noexcept;
  [[nodiscard]] static UnixFd borrow(int fd);

  [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
  [[nodiscard]] int get() const noexcept { return fd_; }
  [[nodiscard]] int release() noexcept;
  void reset(int fd = -1) noexcept;

private:
  explicit UnixFd(int fd) noexcept;

  int fd_ = -1;
};

using BasicValue = std::variant<bool, std::uint8_t, std::int16_t, std::uint16_t,
                                std::int32_t, std::uint32_t,
                                std::int64_t, std::uint64_t, double, std::string,
                                ObjectPath, ObjectPathArray, StringArray, ByteArray,
                                ByteArrayArray, RgbColor, EmptyVariantDictionary,
                                std::shared_ptr<ArrayValue>,
                                std::shared_ptr<DictionaryValue>,
                                std::shared_ptr<StructValue>,
                                std::shared_ptr<VariantValue>,
                                std::shared_ptr<VariantDictionary>,
                                UnixFd>;

struct ArrayValue {
  std::string elementSignature;
  std::vector<BasicValue> values;
};

struct DictionaryEntry {
  BasicValue key;
  BasicValue value;
};

struct DictionaryValue {
  std::string keySignature;
  std::string valueSignature;
  std::vector<DictionaryEntry> entries;
};

struct StructValue {
  std::string signature;
  std::vector<BasicValue> fields;
};

struct VariantValue {
  BasicValue value;
};

struct VariantDictionary {
  std::map<std::string, BasicValue> values;
};

struct PropertiesChanged {
  std::string interface;
  VariantDictionary changed;
  StringArray invalidated;
};

struct NamespacedVariantDictionary {
  std::map<std::string, std::map<std::string, BasicValue>> values;
};

struct ManagedObjectDictionary {
  std::map<std::string, std::map<std::string, std::map<std::string, BasicValue>>> values;
};

using ReplyValue = std::variant<BasicValue,
                                VariantValue,
                                VariantDictionary,
                                NamespacedVariantDictionary,
                                ManagedObjectDictionary>;

class Message;

[[nodiscard]] std::string signatureFor(BasicValue const& value);
[[nodiscard]] PropertiesChanged readPropertiesChanged(Message& message);

struct MethodCall {
  std::string destination;
  std::string path;
  std::string interface;
  std::string member;
  std::vector<BasicValue> arguments;
  std::uint64_t timeoutUsec = 0;
};

struct PropertyAddress {
  std::string destination;
  std::string path;
  std::string interface;
  std::string name;
};

class Message {
public:
  Message();
  ~Message();

  Message(Message const&) = delete;
  Message& operator=(Message const&) = delete;
  Message(Message&& other) noexcept;
  Message& operator=(Message&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] std::string path() const;
  [[nodiscard]] std::string interface() const;
  [[nodiscard]] std::string member() const;
  [[nodiscard]] std::string sender() const;
  [[nodiscard]] std::string signature(bool complete = true) const;
  [[nodiscard]] bool hasSignature(std::string_view expected) const;

  [[nodiscard]] bool readBool();
  [[nodiscard]] std::int16_t readInt16();
  [[nodiscard]] std::uint16_t readUint16();
  [[nodiscard]] std::int32_t readInt32();
  [[nodiscard]] std::uint32_t readUint32();
  [[nodiscard]] std::int64_t readInt64();
  [[nodiscard]] std::uint64_t readUint64();
  [[nodiscard]] double readDouble();
  [[nodiscard]] std::uint8_t readByte();
  [[nodiscard]] std::string readString();
  [[nodiscard]] ObjectPath readObjectPath();
  [[nodiscard]] ObjectPathArray readObjectPathArray();
  [[nodiscard]] StringArray readStringArray();
  [[nodiscard]] ByteArray readByteArray();
  [[nodiscard]] ByteArrayArray readByteArrayArray();
  [[nodiscard]] RgbColor readRgbColor();
  [[nodiscard]] UnixFd readUnixFd();
  [[nodiscard]] BasicValue readBasic(std::string_view signature);
  [[nodiscard]] BasicValue readVariant(std::string_view signature);
  [[nodiscard]] VariantDictionary readVariantDictionary();
  [[nodiscard]] NamespacedVariantDictionary readNamespacedVariantDictionary();
  [[nodiscard]] ManagedObjectDictionary readManagedObjectDictionary();
  void skip(std::string_view signature);

private:
  friend class Bus;
  friend struct detail::BackendAccess;
  explicit Message(void* native, bool owning);
  [[nodiscard]] void* native() const noexcept { return native_; }

  void reset() noexcept;

  void* native_ = nullptr;
  bool owning_ = true;
};

struct AsyncMethodReply {
  Message message;
  std::optional<std::string> errorName;
  std::optional<std::string> errorMessage;
  int errorCode = 0;

  [[nodiscard]] bool ok() const noexcept { return message.valid() && !errorName; }
};

struct SignalMatch {
  std::optional<std::string> sender;
  std::optional<std::string> path;
  std::optional<std::string> interface;
  std::optional<std::string> member;
};

struct MethodReply {
  std::vector<ReplyValue> values;
  std::optional<std::string> errorName;
  std::optional<std::string> errorMessage;

  [[nodiscard]] static MethodReply error(std::string name, std::string message);
};

struct ExportedMethod {
  std::string interface;
  std::string member;
  std::function<MethodReply(Message&)> handler;
};

struct ExportedProperty {
  std::string interface;
  std::string name;
  BasicValue value;
  bool writable = false;
  std::function<BasicValue()> getter;
  std::function<void(BasicValue const&)> setter;
};

struct ObjectDefinition {
  std::vector<ExportedMethod> methods;
  std::vector<ExportedProperty> properties;
};

class Slot {
public:
  Slot();
  ~Slot();

  Slot(Slot const&) = delete;
  Slot& operator=(Slot const&) = delete;
  Slot(Slot&& other) noexcept;
  Slot& operator=(Slot&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept { return native_ != nullptr; }

private:
  friend class Bus;
  explicit Slot(void* native);

  void reset() noexcept;

  void* native_ = nullptr;
};

class PendingCall {
public:
  PendingCall();
  ~PendingCall();

  PendingCall(PendingCall const&) = delete;
  PendingCall& operator=(PendingCall const&) = delete;
  PendingCall(PendingCall&& other) noexcept;
  PendingCall& operator=(PendingCall&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept { return native_ != nullptr; }
  void cancel() noexcept;

private:
  friend class Bus;
  explicit PendingCall(void* native);

  void reset() noexcept;

  void* native_ = nullptr;
};

class Bus {
public:
  Bus();
  ~Bus();

  Bus(Bus const&) = delete;
  Bus& operator=(Bus const&) = delete;
  Bus(Bus&&) noexcept;
  Bus& operator=(Bus&&) noexcept;

  [[nodiscard]] static Bus open(BusType type);
  [[nodiscard]] static Bus openAddress(std::string const& address);

  [[nodiscard]] std::string uniqueName() const;
  void requestName(std::string const& name, std::uint64_t flags = 0);
  [[nodiscard]] Message call(MethodCall const& call);
  [[nodiscard]] PendingCall callAsync(MethodCall const& call, std::function<void(AsyncMethodReply)> handler);
  [[nodiscard]] BasicValue getProperty(PropertyAddress const& property, std::string_view signature);
  void setProperty(PropertyAddress const& property, BasicValue const& value);
  Slot matchSignal(SignalMatch const& match, std::function<void(Message&)> handler);
  Slot exportObject(std::string const& path, ObjectDefinition definition);
  void emitSignal(std::string const& path, std::string const& interface,
                  std::string const& member, std::vector<ReplyValue> const& arguments = {});
  void emitPropertiesChanged(std::string const& path,
                             std::string const& interface,
                             VariantDictionary changed,
                             StringArray invalidated = {});

  [[nodiscard]] int eventFileDescriptor() const;
  [[nodiscard]] int eventMask() const;
  [[nodiscard]] int processPending();
  [[nodiscard]] int waitAndProcess(int timeoutMs);
  void flush();

private:
  struct Impl;
  explicit Bus(std::unique_ptr<Impl> impl);

  [[nodiscard]] void* native() const noexcept;
  Impl& impl();
  Impl const& impl() const;

  std::unique_ptr<Impl> d;
};

class BusEventPump {
public:
  BusEventPump(Application& application, Bus& bus);
  ~BusEventPump();

  BusEventPump(BusEventPump const&) = delete;
  BusEventPump& operator=(BusEventPump const&) = delete;
  BusEventPump(BusEventPump&& other) noexcept;
  BusEventPump& operator=(BusEventPump&& other) noexcept;

private:
  Application* application_ = nullptr;
  std::uint64_t pollSourceId_ = 0;
};

namespace detail {

struct BackendAccess {
  static Message wrapMessage(void* native, bool owning) { return Message(native, owning); }
  static void* nativeMessage(Message const& message) noexcept { return message.native(); }
};

} // namespace detail

} // namespace dbus
} // namespace lambda
