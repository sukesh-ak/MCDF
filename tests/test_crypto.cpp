// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include <doctest/doctest.h>

#include <mcdf/mcdf.hpp>

#include <string>

TEST_CASE("base64url round-trips arbitrary bytes") {
  const std::string data("\x00\x01\x02hello\xff\xfe", 10);
  auto dec = mcdf::base64url_decode(mcdf::base64url_encode(data));
  REQUIRE(dec.has_value());
  CHECK(*dec == data);
}

TEST_CASE("base64url has no padding") {
  CHECK(mcdf::base64url_encode("a").find('=') == std::string::npos);
}

TEST_CASE("base58btc round-trips and handles leading zeros") {
  const std::string data("\x00\x00\x12\x34hello", 9);
  auto dec = mcdf::base58btc_decode(mcdf::base58btc_encode(data));
  REQUIRE(dec.has_value());
  CHECK(*dec == data);
}

TEST_CASE("ed25519 did:key round-trips through public key") {
  auto key = mcdf::PrivateKey::generate_ed25519();
  REQUIRE(key.has_value());
  auto did = key->did_key();
  REQUIRE(did.has_value());
  // All ed25519 did:keys share the z6Mk multibase/multicodec prefix.
  CHECK(did->starts_with("did:key:z6Mk"));

  auto pub = mcdf::PublicKey::from_did_key(*did);
  REQUIRE(pub.has_value());
  auto pub_did = pub->did_key();
  REQUIRE(pub_did.has_value());
  CHECK(*pub_did == *did);
}

TEST_CASE("private key survives a PEM round-trip") {
  auto key = mcdf::PrivateKey::generate_ed25519();
  REQUIRE(key.has_value());
  auto pem = key->to_pem();
  REQUIRE(pem.has_value());
  auto loaded = mcdf::PrivateKey::from_pem(*pem);
  REQUIRE(loaded.has_value());
  auto a = key->did_key();
  auto b = loaded->did_key();
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());
  CHECK(*a == *b);
}

TEST_CASE("ecdsa p-256 did:key round-trips and reports ES256") {
  auto key = mcdf::PrivateKey::generate_ecdsa_p256();
  REQUIRE(key.has_value());
  CHECK(key->jws_algorithm() == "ES256");

  auto did = key->did_key();
  REQUIRE(did.has_value());
  // P-256 did:keys share the zDn multibase/multicodec prefix.
  CHECK(did->starts_with("did:key:zDn"));

  auto pub = mcdf::PublicKey::from_did_key(*did);
  REQUIRE(pub.has_value());
  CHECK(pub->jws_algorithm() == "ES256");
  auto pub_did = pub->did_key();
  REQUIRE(pub_did.has_value());
  CHECK(*pub_did == *did);  // compressed point survives the round-trip
}

TEST_CASE("ES256 signatures are raw R||S and verify") {
  auto key = mcdf::PrivateKey::generate_ecdsa_p256();
  REQUIRE(key.has_value());
  auto sig = key->sign("message");
  REQUIRE(sig.has_value());
  CHECK(sig->size() == 64);  // JOSE raw form, not DER

  auto did = key->did_key();
  REQUIRE(did.has_value());
  auto pub = mcdf::PublicKey::from_did_key(*did);
  REQUIRE(pub.has_value());
  auto valid = pub->verify("message", *sig);
  REQUIRE(valid.has_value());
  CHECK(*valid);
  auto invalid = pub->verify("tampered", *sig);
  REQUIRE(invalid.has_value());
  CHECK_FALSE(*invalid);
}

TEST_CASE("detached JWS works with an ES256 key") {
  auto key = mcdf::PrivateKey::generate_ecdsa_p256();
  REQUIRE(key.has_value());
  auto kid = key->did_key();
  REQUIRE(kid.has_value());

  const std::string payload = R"({"files":{}})";
  auto jws = mcdf::jws_sign_detached(*key, payload, *kid);
  REQUIRE(jws.has_value());

  auto good = mcdf::jws_verify_detached(*jws, payload);
  REQUIRE(good.has_value());
  CHECK(good->valid);
  CHECK(good->alg == "ES256");
  CHECK(good->kid == *kid);

  auto bad = mcdf::jws_verify_detached(*jws, payload + " ");
  REQUIRE(bad.has_value());
  CHECK_FALSE(bad->valid);
}

TEST_CASE("detached JWS verifies and detects tampering") {
  auto key = mcdf::PrivateKey::generate_ed25519();
  REQUIRE(key.has_value());
  auto kid = key->did_key();
  REQUIRE(kid.has_value());

  const std::string payload = R"({"files":{},"hash_algorithm":"sha256"})";
  auto jws = mcdf::jws_sign_detached(*key, payload, *kid);
  REQUIRE(jws.has_value());
  // Detached form: empty payload segment.
  CHECK(jws->find("..") != std::string::npos);

  auto good = mcdf::jws_verify_detached(*jws, payload);
  REQUIRE(good.has_value());
  CHECK(good->valid);
  CHECK(good->alg == "EdDSA");
  CHECK(good->kid == *kid);

  auto bad = mcdf::jws_verify_detached(*jws, payload + " ");
  REQUIRE(bad.has_value());
  CHECK_FALSE(bad->valid);
}
