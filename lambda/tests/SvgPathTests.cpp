#include <Lambda/Graphics/SvgPath.hpp>

#include <doctest/doctest.h>

using namespace lambda;

namespace {

void checkPoint(Path::CommandView command, float x, float y) {
  REQUIRE(command.dataCount >= 2);
  CHECK(command.data[0] == doctest::Approx(x));
  CHECK(command.data[1] == doctest::Approx(y));
}

} // namespace

TEST_CASE("parseSvgPath parses absolute and relative line commands") {
  Path path = parseSvgPath("M 10 20 l 5,-4 H 30 v 8 z");

  REQUIRE(path.commandCount() == 5);
  CHECK(path.command(0).type == Path::CommandType::MoveTo);
  checkPoint(path.command(0), 10.f, 20.f);
  CHECK(path.command(1).type == Path::CommandType::LineTo);
  checkPoint(path.command(1), 15.f, 16.f);
  CHECK(path.command(2).type == Path::CommandType::LineTo);
  checkPoint(path.command(2), 30.f, 16.f);
  CHECK(path.command(3).type == Path::CommandType::LineTo);
  checkPoint(path.command(3), 30.f, 24.f);
  CHECK(path.command(4).type == Path::CommandType::Close);
}

TEST_CASE("parseSvgPath treats repeated move coordinates as line commands") {
  Path path = parseSvgPath("M0 0 10 10 20 0");

  REQUIRE(path.commandCount() == 3);
  CHECK(path.command(0).type == Path::CommandType::MoveTo);
  CHECK(path.command(1).type == Path::CommandType::LineTo);
  checkPoint(path.command(1), 10.f, 10.f);
  CHECK(path.command(2).type == Path::CommandType::LineTo);
  checkPoint(path.command(2), 20.f, 0.f);
}

TEST_CASE("parseSvgPath accepts compact numeric forms") {
  Path path = parseSvgPath("M1.5.5 L1e2,-1.2e-3");

  REQUIRE(path.commandCount() == 2);
  checkPoint(path.command(0), 1.5f, 0.5f);
  checkPoint(path.command(1), 100.f, -0.0012f);
}

TEST_CASE("parseSvgPath reflects smooth curve control points") {
  Path path = parseSvgPath("M0 0 C 10 0 20 0 30 0 S 50 0 60 0 Q 70 10 80 0 T 100 0");

  REQUIRE(path.commandCount() == 5);
  CHECK(path.command(2).type == Path::CommandType::BezierTo);
  CHECK(path.command(2).data[0] == doctest::Approx(40.f));
  CHECK(path.command(2).data[1] == doctest::Approx(0.f));
  CHECK(path.command(4).type == Path::CommandType::QuadTo);
  CHECK(path.command(4).data[0] == doctest::Approx(90.f));
  CHECK(path.command(4).data[1] == doctest::Approx(-10.f));
}

TEST_CASE("parseSvgPath converts arcs to cubic beziers") {
  Path path = parseSvgPath("M0 0 A 10 10 0 0 1 10 10");

  REQUIRE(path.commandCount() == 2);
  CHECK(path.command(1).type == Path::CommandType::BezierTo);
  REQUIRE(path.command(1).dataCount == 6);
  CHECK(path.command(1).data[4] == doctest::Approx(10.f));
  CHECK(path.command(1).data[5] == doctest::Approx(10.f));
}

TEST_CASE("parseSvgPath returns partial path and error on malformed input") {
  SvgPathParseError error;
  Path path = parseSvgPath("M 0 0 L 10", &error);

  REQUIRE(path.commandCount() == 1);
  CHECK(path.command(0).type == Path::CommandType::MoveTo);
  CHECK(error.position > 0);
  CHECK_FALSE(error.message.empty());
}
