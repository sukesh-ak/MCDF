// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include <doctest/doctest.h>

#include <mcdf/serialize/canonical_json.hpp>

TEST_CASE("canonical json sorts object keys and strips whitespace") {
  auto out = mcdf::canonicalize_json(R"({ "b": 1, "a": 2 })");
  REQUIRE(out.has_value());
  CHECK(*out == R"({"a":2,"b":1})");
}

TEST_CASE("canonical json sorts nested keys") {
  auto out = mcdf::canonicalize_json(R"({"z":{"y":1,"x":2},"a":3})");
  REQUIRE(out.has_value());
  CHECK(*out == R"({"a":3,"z":{"x":2,"y":1}})");
}

TEST_CASE("canonical json escapes control characters minimally") {
  // A real tab and newline inside the string value.
  auto out = mcdf::canonicalize_json("{\"k\":\"a\\tb\\n\"}");
  REQUIRE(out.has_value());
  CHECK(*out == "{\"k\":\"a\\tb\\n\"}");
}

TEST_CASE("canonical json rejects malformed input") {
  auto out = mcdf::canonicalize_json("{not json");
  REQUIRE_FALSE(out.has_value());
  CHECK(out.error().code == mcdf::ErrorCode::kParse);
}
