/**
 * Copyright (C) 2013 Regents of the University of California.
 * @author: Yingdi Yu <yingdi0@cs.ucla.edu>
 * See COPYING for copyright and distribution information.
 */

#include "security/key-chain.hpp"
#include "security/validator.hpp"

#include "security/cryptopp.hpp"

#include "boost-test.hpp"

using namespace std;
namespace ndn {

BOOST_AUTO_TEST_SUITE(TestSignatureSha256)

string SHA256_RESULT("a883dafc480d466ee04e0d6da986bd78eb1fdd2178d04693723da3a8f95d42f4");

BOOST_AUTO_TEST_CASE (Sha256)
{
  using namespace CryptoPP;

  char content[6] = "1234\n";
  ConstBufferPtr buf = crypto::sha256(reinterpret_cast<uint8_t*>(content), 5);
  string result;
  StringSource(buf->buf(), buf->size(), true, new HexEncoder(new StringSink(result), false));

  BOOST_REQUIRE_EQUAL(SHA256_RESULT, result);
}

BOOST_AUTO_TEST_CASE (Signature)
{
  using namespace CryptoPP;

  Name name("/TestSignatureSha/Basic");
  Data testData(name);
  char content[5] = "1234";
  testData.setContent(reinterpret_cast<uint8_t*>(content), 5);

  KeyChainImpl<SecPublicInfoSqlite3, SecTpmFile> keychain;
  keychain.signWithSha256(testData);

  testData.wireEncode();

  SignatureSha256 sig(testData.getSignature());

  BOOST_REQUIRE(Validator::verifySignature(testData, sig));
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace ndn
