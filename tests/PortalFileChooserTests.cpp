#include <Lambda/System/PortalFileChooser.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {

using lambda::testing::dbus::pollBus;
using lambda::testing::dbus::startPrivateBus;

template <typename Callback>
class ScopeExit {
public:
  explicit ScopeExit(Callback callback) : callback_(std::move(callback)) {}
  ~ScopeExit() { callback_(); }

private:
  Callback callback_;
};

lambda::dbus::ByteArray bytes(std::string value) {
  lambda::dbus::ByteArray array;
  array.values.assign(value.begin(), value.end());
  array.values.push_back(0);
  return array;
}

std::shared_ptr<lambda::dbus::StructValue>
structValue(std::string signature, std::vector<lambda::dbus::BasicValue> fields) {
  return std::make_shared<lambda::dbus::StructValue>(
      lambda::dbus::StructValue{.signature = std::move(signature), .fields = std::move(fields)});
}

std::shared_ptr<lambda::dbus::ArrayValue>
arrayValue(std::string elementSignature, std::vector<lambda::dbus::BasicValue> values) {
  return std::make_shared<lambda::dbus::ArrayValue>(
      lambda::dbus::ArrayValue{.elementSignature = std::move(elementSignature),
                               .values = std::move(values)});
}

std::shared_ptr<lambda::dbus::VariantDictionary> openOptions() {
  auto options = std::make_shared<lambda::dbus::VariantDictionary>();
  options->values["current_file"] = bytes("/tmp/Lambda Open.txt");
  auto filterRules = arrayValue("(us)",
                                {structValue("us",
                                             {std::uint32_t(0),
                                              std::string("*.txt")})});
  options->values["current_filter"] =
      structValue("sa(us)", {std::string("Text"), filterRules});
  auto choiceItems = arrayValue("(ss)",
                                {structValue("ss", {std::string("utf8"), std::string("UTF-8")}),
                                 structValue("ss", {std::string("plain"), std::string("Plain")})});
  options->values["choices"] =
      arrayValue("(ssa(ss)s)",
                 {structValue("ssa(ss)s",
                              {std::string("encoding"),
                               std::string("Encoding"),
                               choiceItems,
                               std::string("utf8")})});
  return options;
}

std::shared_ptr<lambda::dbus::VariantDictionary> saveOptions() {
  auto options = std::make_shared<lambda::dbus::VariantDictionary>();
  options->values["current_folder"] = bytes("/tmp");
  options->values["current_name"] = std::string("New Document.txt");
  return options;
}

std::shared_ptr<lambda::dbus::VariantDictionary> saveFilesOptions() {
  auto options = std::make_shared<lambda::dbus::VariantDictionary>();
  options->values["current_folder"] = bytes("/tmp/out");
  lambda::dbus::ByteArrayArray files;
  files.values.push_back(bytes("a.txt"));
  files.values.push_back(bytes("b b.txt"));
  options->values["files"] = std::move(files);
  return options;
}

lambda::dbus::MethodCall fileChooserCall(std::string member,
                                         std::string handle,
                                         std::shared_ptr<lambda::dbus::VariantDictionary> options) {
  return lambda::dbus::MethodCall{
      .destination = lambda::system::PortalFileChooserService::serviceName,
      .path = lambda::system::PortalFileChooserService::objectPath,
      .interface = lambda::system::PortalFileChooserService::interfaceName,
      .member = std::move(member),
      .arguments = {lambda::dbus::ObjectPath{std::move(handle)},
                    std::string("org.lambda.TestApp"),
                    std::string("wayland:window"),
                    std::string("Choose a file"),
                    std::move(options)},
  };
}

} // namespace

TEST_CASE("Portal FileChooser support is compile-time declared") {
  CHECK((LAMBDA_HAS_DBUS == 0 || LAMBDA_HAS_DBUS == 1));
}

#if LAMBDA_HAS_DBUS

TEST_CASE("PortalFileChooserService returns basic file chooser results on a private bus") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping portal file chooser integration test because a private bus could not be started");
    return;
  }

  auto serviceBus = lambda::dbus::Bus::openAddress(privateBus->address);
  auto client = lambda::dbus::Bus::openAddress(privateBus->address);
  serviceBus.requestName(lambda::system::PortalFileChooserService::serviceName);

  lambda::system::PortalFileChooserService fileChooser(serviceBus);
  auto slot = fileChooser.exportObject();

  std::atomic<bool> serviceRunning = true;
  std::thread serviceThread([&] {
    while (serviceRunning.load()) {
      pollBus(serviceBus, 25);
    }
  });
  ScopeExit stopService([&] {
    serviceRunning = false;
    if (serviceThread.joinable()) {
      serviceThread.join();
    }
  });

  std::string const openHandle = "/org/freedesktop/portal/desktop/request/1_1/open";
  auto openReply = client.call(fileChooserCall("OpenFile", openHandle, openOptions()));
  CHECK(openReply.readUint32() == 0);
  auto openResults = openReply.readVariantDictionary();
  auto openUris = std::get<lambda::dbus::StringArray>(openResults.values["uris"]);
  REQUIRE(openUris.values.size() == 1);
  CHECK(openUris.values.front() == "file:///tmp/Lambda%20Open.txt");
  auto choices = std::get<std::shared_ptr<lambda::dbus::ArrayValue>>(openResults.values["choices"]);
  REQUIRE(choices);
  CHECK(choices->elementSignature == "(ss)");
  REQUIRE(choices->values.size() == 1);
  auto choice = std::get<std::shared_ptr<lambda::dbus::StructValue>>(choices->values.front());
  REQUIRE(choice);
  REQUIRE(choice->fields.size() == 2);
  CHECK(std::get<std::string>(choice->fields[0]) == "encoding");
  CHECK(std::get<std::string>(choice->fields[1]) == "utf8");
  auto currentFilter =
      std::get<std::shared_ptr<lambda::dbus::StructValue>>(openResults.values["current_filter"]);
  REQUIRE(currentFilter);
  CHECK(lambda::dbus::signatureFor(lambda::dbus::BasicValue(currentFilter)) == "(sa(us))");
  auto request = fileChooser.request(openHandle);
  REQUIRE(request);
  CHECK(request->appId == "org.lambda.TestApp");
  CHECK(request->parentWindow == "wayland:window");
  CHECK(request->title == "Choose a file");
  CHECK(request->kind == lambda::system::PortalFileChooserKind::OpenFile);
  CHECK(request->uris == openUris.values);
  CHECK(!request->closed);

  auto closeReply = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::PortalFileChooserService::serviceName,
      .path = openHandle,
      .interface = lambda::system::PortalFileChooserService::requestInterfaceName,
      .member = "Close",
  });
  CHECK(closeReply.valid());
  request = fileChooser.request(openHandle);
  REQUIRE(request);
  CHECK(request->closed);

  auto saveReply = client.call(fileChooserCall(
      "SaveFile",
      "/org/freedesktop/portal/desktop/request/1_1/save",
      saveOptions()));
  CHECK(saveReply.readUint32() == 0);
  auto saveResults = saveReply.readVariantDictionary();
  auto saveUris = std::get<lambda::dbus::StringArray>(saveResults.values["uris"]);
  REQUIRE(saveUris.values.size() == 1);
  CHECK(saveUris.values.front() == "file:///tmp/New%20Document.txt");

  auto saveFilesReply = client.call(fileChooserCall(
      "SaveFiles",
      "/org/freedesktop/portal/desktop/request/1_1/save_files",
      saveFilesOptions()));
  CHECK(saveFilesReply.readUint32() == 0);
  auto saveFilesResults = saveFilesReply.readVariantDictionary();
  auto saveFilesUris = std::get<lambda::dbus::StringArray>(saveFilesResults.values["uris"]);
  CHECK(saveFilesUris.values == std::vector<std::string>{"file:///tmp/out/a.txt",
                                                          "file:///tmp/out/b%20b.txt"});

  auto emptyOpenReply = client.call(fileChooserCall(
      "OpenFile",
      "/org/freedesktop/portal/desktop/request/1_1/empty",
      std::make_shared<lambda::dbus::VariantDictionary>()));
  CHECK(emptyOpenReply.readUint32() == 2);
  CHECK(emptyOpenReply.readVariantDictionary().values.empty());
}

#endif
