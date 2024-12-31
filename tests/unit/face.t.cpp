/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2013-2024 Regents of the University of California.
 *
 * This file is part of ndn-cxx library (NDN C++ library with eXperimental eXtensions).
 *
 * ndn-cxx library is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 *
 * ndn-cxx library is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 * You should have received copies of the GNU General Public License and GNU Lesser
 * General Public License along with ndn-cxx, e.g., in COPYING.md file.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * See AUTHORS.md for complete list of ndn-cxx authors and contributors.
 */

#include "ndn-cxx/face.hpp"
#include "ndn-cxx/lp/tags.hpp"
#include "ndn-cxx/transport/tcp-transport.hpp"
#include "ndn-cxx/transport/unix-transport.hpp"
#include "ndn-cxx/util/config-file.hpp"
#include "ndn-cxx/util/dummy-client-face.hpp"

#include "tests/test-common.hpp"
#include "tests/unit/io-key-chain-fixture.hpp"

#include <boost/logic/tribool.hpp>
#include <boost/mp11/list.hpp>

namespace ndn::tests {

struct WantPrefixRegReply;
struct NoPrefixRegReply;

template<typename PrefixRegReply = WantPrefixRegReply>
class FaceFixture : public IoKeyChainFixture
{
protected:
  FaceFixture()
    : face(m_io, m_keyChain, {true, !std::is_same_v<PrefixRegReply, NoPrefixRegReply>})
  {
    static_assert(std::is_same_v<PrefixRegReply, WantPrefixRegReply> ||
                  std::is_same_v<PrefixRegReply, NoPrefixRegReply>);
  }

  /** \brief Execute a prefix registration, and optionally check the name in callback.
   *  \return whether the prefix registration succeeded.
   */
  bool
  runPrefixReg(std::function<void(const RegisterPrefixSuccessCallback&,
                                  const RegisterPrefixFailureCallback&)> f)
  {
    boost::logic::tribool result = boost::logic::indeterminate;
    f([&] (auto) { result = true; },
      [&] (auto, auto) { result = false; });

    advanceClocks(1_ms);
    BOOST_REQUIRE(!boost::logic::indeterminate(result));
    return static_cast<bool>(result);
  }

  /** \brief Execute a prefix unregistration, and optionally check the name in callback.
   *  \return whether the prefix unregistration succeeded.
   */
  bool
  runPrefixUnreg(std::function<void(const UnregisterPrefixSuccessCallback&,
                                    const UnregisterPrefixFailureCallback&)> f)
  {
    boost::logic::tribool result = boost::logic::indeterminate;
    f([&] { result = true; },
      [&] (auto) { result = false; });

    advanceClocks(1_ms);
    BOOST_REQUIRE(!boost::logic::indeterminate(result));
    return static_cast<bool>(result);
  }

  /** \brief Execute a prefix announcement, and optionally check the name in callback.
   *  \return whether the prefix announcement succeeded.
   */
  bool
  runPrefixAnnouncement(std::function<void(const RegisterPrefixSuccessCallback&,
                                           const RegisterPrefixFailureCallback&)> f)
  {
    boost::logic::tribool result = boost::logic::indeterminate;
    f([&] (auto) { result = true; },
      [&] (auto, auto) { result = false; });

    advanceClocks(1_ms);
    BOOST_REQUIRE(!boost::logic::indeterminate(result));
    return static_cast<bool>(result);
  }

protected:
  DummyClientFace face;
};

BOOST_FIXTURE_TEST_SUITE(TestFace, FaceFixture<>)

BOOST_AUTO_TEST_SUITE(ExpressInterest)

BOOST_AUTO_TEST_CASE(ReplyData)
{
  size_t nData = 0;
  face.expressInterest(*makeInterest("/Hello/World", true, 50_ms),
                       [&] (const Interest& i, const Data& d) {
                         BOOST_CHECK(i.getName().isPrefixOf(d.getName()));
                         BOOST_CHECK_EQUAL(i.getName(), "/Hello/World");
                         ++nData;
                       },
                       [] (auto&&...) { BOOST_FAIL("Unexpected Nack"); },
                       [] (auto&&...) { BOOST_FAIL("Unexpected timeout"); });

  advanceClocks(40_ms);

  face.receive(*makeData("/Bye/World/a"));
  face.receive(*makeData("/Hello/World/a"));

  advanceClocks(50_ms, 2);

  BOOST_CHECK_EQUAL(nData, 1);
  BOOST_CHECK_EQUAL(face.sentInterests.size(), 1);
  BOOST_CHECK_EQUAL(face.sentData.size(), 0);

  size_t nTimeouts = 0;
  face.expressInterest(*makeInterest("/Hello/World/a/2", false, 50_ms),
                       [] (auto&&...) {},
                       [] (auto&&...) {},
                       [&] (auto&&...) { ++nTimeouts; });
  advanceClocks(200_ms, 5);
  BOOST_CHECK_EQUAL(nTimeouts, 1);
}

BOOST_AUTO_TEST_CASE(MultipleData)
{
  size_t nData = 0;

  face.expressInterest(*makeInterest("/Hello/World", true, 50_ms),
                       [&] (auto&&...) { ++nData; },
                       [] (auto&&...) { BOOST_FAIL("Unexpected Nack"); },
                       [] (auto&&...) { BOOST_FAIL("Unexpected timeout"); });

  face.expressInterest(*makeInterest("/Hello/World/a", true, 50_ms),
                       [&] (auto&&...) { ++nData; },
                       [] (auto&&...) { BOOST_FAIL("Unexpected Nack"); },
                       [] (auto&&...) { BOOST_FAIL("Unexpected timeout"); });

  advanceClocks(40_ms);

  face.receive(*makeData("/Hello/World/a/b"));

  advanceClocks(50_ms, 2);

  BOOST_CHECK_EQUAL(nData, 2);
  BOOST_CHECK_EQUAL(face.sentInterests.size(), 2);
  BOOST_CHECK_EQUAL(face.sentData.size(), 0);
}

BOOST_AUTO_TEST_CASE(EmptyDataCallback)
{
  face.expressInterest(*makeInterest("/Hello/World", true),
                       nullptr,
                       [] (auto&&...) { BOOST_FAIL("Unexpected Nack"); },
                       [] (auto&&...) { BOOST_FAIL("Unexpected timeout"); });
  advanceClocks(1_ms);

  BOOST_CHECK_NO_THROW(do {
    face.receive(*makeData("/Hello/World/a"));
    advanceClocks(1_ms);
  } while (false));
}

BOOST_AUTO_TEST_CASE(Timeout)
{
  size_t nTimeouts = 0;
  face.expressInterest(*makeInterest("/Hello/World", false, 50_ms),
                       [] (auto&&...) { BOOST_FAIL("Unexpected Data"); },
                       [] (auto&&...) { BOOST_FAIL("Unexpected Nack"); },
                       [&nTimeouts] (const Interest& i) {
                         BOOST_CHECK_EQUAL(i.getName(), "/Hello/World");
                         ++nTimeouts;
                       });
  advanceClocks(200_ms, 5);

  BOOST_CHECK_EQUAL(nTimeouts, 1);
  BOOST_CHECK_EQUAL(face.sentInterests.size(), 1);
  BOOST_CHECK_EQUAL(face.sentData.size(), 0);
  BOOST_CHECK_EQUAL(face.sentNacks.size(), 0);
}

BOOST_AUTO_TEST_CASE(EmptyTimeoutCallback)
{
  face.expressInterest(*makeInterest("/Hello/World", false, 50_ms),
                       [] (auto&&...) { BOOST_FAIL("Unexpected Data"); },
                       [] (auto&&...) { BOOST_FAIL("Unexpected Nack"); },
                       nullptr);
  advanceClocks(40_ms);

  BOOST_CHECK_NO_THROW(do {
    advanceClocks(6_ms, 2);
  } while (false));
}

BOOST_AUTO_TEST_CASE(ReplyNack)
{
  size_t nNacks = 0;

  auto interest = makeInterest("/Hello/World", false, 50_ms);
  face.expressInterest(*interest,
                       [] (auto&&...) { BOOST_FAIL("Unexpected Data"); },
                       [&] (const Interest& i, const lp::Nack& n) {
                         BOOST_CHECK(i.getName().isPrefixOf(n.getInterest().getName()));
                         BOOST_CHECK_EQUAL(i.getName(), "/Hello/World");
                         BOOST_CHECK_EQUAL(n.getReason(), lp::NackReason::DUPLICATE);
                         ++nNacks;
                       },
                       [] (auto&&...) { BOOST_FAIL("Unexpected timeout"); });

  advanceClocks(40_ms);

  face.receive(makeNack(face.sentInterests.at(0), lp::NackReason::DUPLICATE));

  advanceClocks(50_ms, 2);

  BOOST_CHECK_EQUAL(nNacks, 1);
  BOOST_CHECK_EQUAL(face.sentInterests.size(), 1);
}

BOOST_AUTO_TEST_CASE(MultipleNacks)
{
  size_t nNacks = 0;

  auto interest = makeInterest("/Hello/World", false, 50_ms, 1);
  face.expressInterest(*interest,
                       [] (auto&&...) { BOOST_FAIL("Unexpected Data"); },
                       [&] (const auto&, const auto&) { ++nNacks; },
                       [] (auto&&...) { BOOST_FAIL("Unexpected timeout"); });

  interest->setNonce(2);
  face.expressInterest(*interest,
                       [] (auto&&...) { BOOST_FAIL("Unexpected Data"); },
                       [&] (const auto&, const auto&) { ++nNacks; },
                       [] (auto&&...) { BOOST_FAIL("Unexpected timeout"); });

  advanceClocks(40_ms);

  face.receive(makeNack(face.sentInterests.at(1), lp::NackReason::DUPLICATE));

  advanceClocks(50_ms, 2);

  BOOST_CHECK_EQUAL(nNacks, 2);
  BOOST_CHECK_EQUAL(face.sentInterests.size(), 2);
}

BOOST_AUTO_TEST_CASE(EmptyNackCallback)
{
  face.expressInterest(*makeInterest("/Hello/World"),
                       [] (auto&&...) { BOOST_FAIL("Unexpected Data"); },
                       nullptr,
                       [] (auto&&...) { BOOST_FAIL("Unexpected timeout"); });
  advanceClocks(1_ms);

  BOOST_CHECK_NO_THROW(do {
    face.receive(makeNack(face.sentInterests.at(0), lp::NackReason::DUPLICATE));
    advanceClocks(1_ms);
  } while (false));
}

BOOST_AUTO_TEST_CASE(PutDataFromDataCallback,
  * ut::description("test for bug #4596"))
{
  face.expressInterest(*makeInterest("/localhost/notification/1"),
                       [&] (auto&&...) {
                         face.put(*makeData("/chronosync/sampleDigest/1"));
                       }, nullptr, nullptr);
  advanceClocks(10_ms);
  BOOST_CHECK_EQUAL(face.sentInterests.back().getName(), "/localhost/notification/1");

  face.receive(*makeInterest("/chronosync/sampleDigest", true));
  advanceClocks(10_ms);

  face.put(*makeData("/localhost/notification/1"));
  advanceClocks(10_ms);
  BOOST_CHECK_EQUAL(face.sentData.back().getName(), "/chronosync/sampleDigest/1");
}

BOOST_AUTO_TEST_CASE(DestroyWithPendingInterest,
  * ut::description("test for bug #2518"))
{
  auto face2 = make_unique<DummyClientFace>(m_io, m_keyChain);
  face2->expressInterest(*makeInterest("/Hello/World", false, 50_ms),
                         nullptr, nullptr, nullptr);
  advanceClocks(50_ms, 2);
  face2.reset();

  advanceClocks(50_ms, 2); // should not crash

  // avoid "test case [...] did not check any assertions" message from Boost.Test
  BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(Handle)
{
  auto hdl = face.expressInterest(*makeInterest("/Hello/World", true, 50_ms),
                                  [] (auto&&...) { BOOST_FAIL("Unexpected data"); },
                                  [] (auto&&...) { BOOST_FAIL("Unexpected nack"); },
                                  [] (auto&&...) { BOOST_FAIL("Unexpected timeout"); });
  advanceClocks(1_ms);
  hdl.cancel();
  advanceClocks(1_ms);
  face.receive(*makeData("/Hello/World/%21"));
  advanceClocks(200_ms, 5);

  // cancel after destructing face
  auto face2 = make_unique<DummyClientFace>(m_io, m_keyChain);
  auto hdl2 = face2->expressInterest(*makeInterest("/Hello/World", true, 50_ms),
                                     [] (auto&&...) { BOOST_FAIL("Unexpected data"); },
                                     [] (auto&&...) { BOOST_FAIL("Unexpected nack"); },
                                     [] (auto&&...) { BOOST_FAIL("Unexpected timeout"); });
  advanceClocks(1_ms);
  face2.reset();
  advanceClocks(1_ms);
  hdl2.cancel(); // should not crash
  advanceClocks(1_ms);

  // avoid "test case [...] did not check any assertions" message from Boost.Test
  BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END() // ExpressInterest

BOOST_AUTO_TEST_CASE(RemoveAllPendingInterests)
{
  face.expressInterest(*makeInterest("/Hello/World/0", false, 50_ms),
                       [] (auto&&...) { BOOST_FAIL("Unexpected data"); },
                       [] (auto&&...) { BOOST_FAIL("Unexpected nack"); },
                       [] (auto&&...) { BOOST_FAIL("Unexpected timeout"); });

  face.expressInterest(*makeInterest("/Hello/World/1", false, 50_ms),
                       [] (auto&&...) { BOOST_FAIL("Unexpected data"); },
                       [] (auto&&...) { BOOST_FAIL("Unexpected nack"); },
                       [] (auto&&...) { BOOST_FAIL("Unexpected timeout"); });

  advanceClocks(10_ms);

  face.removeAllPendingInterests();
  advanceClocks(10_ms);

  BOOST_CHECK_EQUAL(face.getNPendingInterests(), 0);

  face.receive(*makeData("/Hello/World/0"));
  face.receive(*makeData("/Hello/World/1"));
  advanceClocks(200_ms, 5);
}

BOOST_AUTO_TEST_SUITE(Producer)

BOOST_AUTO_TEST_CASE(PutData)
{
  BOOST_CHECK_EQUAL(face.sentData.size(), 0);

  Data data("/4g7xxcuEow/KFvK5Kf2m");
  signData(data);
  face.put(data);

  lp::CachePolicy cachePolicy;
  cachePolicy.setPolicy(lp::CachePolicyType::NO_CACHE);
  data.setTag(make_shared<lp::CachePolicyTag>(cachePolicy));
  data.setTag(make_shared<lp::CongestionMarkTag>(1));
  face.put(data);

  advanceClocks(10_ms);
  BOOST_REQUIRE_EQUAL(face.sentData.size(), 2);
  BOOST_CHECK(face.sentData[0].getTag<lp::CachePolicyTag>() == nullptr);
  BOOST_CHECK(face.sentData[0].getTag<lp::CongestionMarkTag>() == nullptr);
  BOOST_CHECK(face.sentData[1].getTag<lp::CachePolicyTag>() != nullptr);
  BOOST_CHECK(face.sentData[1].getTag<lp::CongestionMarkTag>() != nullptr);
}

BOOST_AUTO_TEST_CASE(PutDataLoopback)
{
  bool hasInterest1 = false, hasData = false;

  // first InterestFilter allows loopback and should receive Interest
  face.setInterestFilter("/", [&] (auto&&...) {
    hasInterest1 = true;
    // do not respond with Data right away, so Face must send Interest to forwarder
  });

  // second InterestFilter disallows loopback and should not receive Interest
  face.setInterestFilter(InterestFilter("/").allowLoopback(false),
                         [] (auto&&...) { BOOST_ERROR("Unexpected Interest on second InterestFilter"); });

  face.expressInterest(*makeInterest("/A", true),
                       [&] (auto&&...) { hasData = true; },
                       [] (auto&&...) { BOOST_FAIL("Unexpected nack"); },
                       [] (auto&&...) { BOOST_FAIL("Unexpected timeout"); });
  advanceClocks(1_ms);
  BOOST_CHECK_EQUAL(hasInterest1, true); // Interest looped back
  BOOST_CHECK_EQUAL(face.sentInterests.size(), 1); // Interest sent to forwarder
  BOOST_CHECK_EQUAL(hasData, false); // waiting for Data

  face.put(*makeData("/A/B")); // first InterestFilter responds with Data
  advanceClocks(1_ms);
  BOOST_CHECK_EQUAL(hasData, true);
  BOOST_CHECK_EQUAL(face.sentData.size(), 0); // do not spill Data to forwarder
}

BOOST_AUTO_TEST_CASE(PutMultipleData)
{
  bool hasInterest1 = false;
  // register two Interest destinations
  face.setInterestFilter("/", [&] (auto&&...) {
    hasInterest1 = true;
    // sending Data right away from the first destination, don't care whether Interest goes to second destination
    face.put(*makeData("/A/B"));
  });
  face.setInterestFilter("/", [] (auto&&...) {});
  advanceClocks(10_ms);

  face.receive(*makeInterest("/A", true));
  advanceClocks(10_ms);
  BOOST_CHECK(hasInterest1);
  BOOST_CHECK_EQUAL(face.sentData.size(), 1);
  BOOST_CHECK_EQUAL(face.sentData.at(0).getName(), "/A/B");

  face.put(*makeData("/A/C"));
  BOOST_CHECK_EQUAL(face.sentData.size(), 1); // additional Data are ignored
}

BOOST_AUTO_TEST_CASE(PutNack)
{
  // register one Interest destination so that face can accept Nacks
  face.setInterestFilter("/", [] (auto&&...) {});
  advanceClocks(10_ms);

  BOOST_CHECK_EQUAL(face.sentNacks.size(), 0);

  face.put(makeNack(*makeInterest("/unsolicited", false, std::nullopt, 18645250),
                    lp::NackReason::NO_ROUTE));
  advanceClocks(10_ms);
  BOOST_CHECK_EQUAL(face.sentNacks.size(), 0); // unsolicited Nack would not be sent

  auto interest1 = makeInterest("/Hello/World", false, std::nullopt, 14247162);
  face.receive(*interest1);
  auto interest2 = makeInterest("/another/prefix", false, std::nullopt, 92203002);
  face.receive(*interest2);
  advanceClocks(10_ms);

  face.put(makeNack(*interest1, lp::NackReason::DUPLICATE));
  advanceClocks(10_ms);
  BOOST_REQUIRE_EQUAL(face.sentNacks.size(), 1);
  BOOST_CHECK_EQUAL(face.sentNacks[0].getReason(), lp::NackReason::DUPLICATE);
  BOOST_CHECK(face.sentNacks[0].getTag<lp::CongestionMarkTag>() == nullptr);

  auto nack = makeNack(*interest2, lp::NackReason::NO_ROUTE);
  nack.setTag(make_shared<lp::CongestionMarkTag>(1));
  face.put(nack);
  advanceClocks(10_ms);
  BOOST_REQUIRE_EQUAL(face.sentNacks.size(), 2);
  BOOST_CHECK_EQUAL(face.sentNacks[1].getReason(), lp::NackReason::NO_ROUTE);
  BOOST_CHECK(face.sentNacks[1].getTag<lp::CongestionMarkTag>() != nullptr);
}

BOOST_AUTO_TEST_CASE(PutMultipleNack)
{
  bool hasInterest1 = false, hasInterest2 = false;
  // register two Interest destinations
  face.setInterestFilter("/", [&] (const InterestFilter&, const Interest& interest) {
    hasInterest1 = true;
    // sending Nack right away from the first destination, Interest should still go to second destination
    face.put(makeNack(interest, lp::NackReason::CONGESTION));
  });
  face.setInterestFilter("/", [&] (auto&&...) { hasInterest2 = true; });
  advanceClocks(10_ms);

  auto interest = makeInterest("/A", false, std::nullopt, 14333271);
  face.receive(*interest);
  advanceClocks(10_ms);
  BOOST_CHECK(hasInterest1);
  BOOST_CHECK(hasInterest2);

  // Nack from first destination is received, should wait for a response from the other destination
  BOOST_CHECK_EQUAL(face.sentNacks.size(), 0);

  face.put(makeNack(*interest, lp::NackReason::NO_ROUTE)); // Nack from second destination
  advanceClocks(10_ms);
  BOOST_CHECK_EQUAL(face.sentNacks.size(), 1); // sending Nack after both destinations Nacked
  BOOST_CHECK_EQUAL(face.sentNacks.at(0).getReason(), lp::NackReason::CONGESTION); // least severe reason

  face.put(makeNack(*interest, lp::NackReason::DUPLICATE));
  BOOST_CHECK_EQUAL(face.sentNacks.size(), 1); // additional Nacks are ignored
}

BOOST_AUTO_TEST_CASE(PutMultipleNackLoopback)
{
  bool hasInterest1 = false, hasNack = false;

  // first InterestFilter allows loopback and should receive Interest
  face.setInterestFilter("/", [&] (const InterestFilter&, const Interest& interest) {
    hasInterest1 = true;
    face.put(makeNack(interest, lp::NackReason::CONGESTION));
  });

  // second InterestFilter disallows loopback and should not receive Interest
  face.setInterestFilter(InterestFilter("/").allowLoopback(false),
                         [] (auto&&...) { BOOST_ERROR("Unexpected Interest on second InterestFilter"); });

  auto interest = makeInterest("/A", false, std::nullopt, 28395852);
  face.expressInterest(*interest,
                       [] (auto&&...) { BOOST_FAIL("Unexpected data"); },
                       [&] (const Interest&, const lp::Nack& nack) {
                         hasNack = true;
                         BOOST_CHECK_EQUAL(nack.getReason(), lp::NackReason::CONGESTION);
                       },
                       [] (auto&&...) { BOOST_FAIL("Unexpected timeout"); });
  advanceClocks(1_ms);
  BOOST_CHECK_EQUAL(hasInterest1, true); // Interest looped back
  BOOST_CHECK_EQUAL(face.sentInterests.size(), 1); // Interest sent to forwarder
  BOOST_CHECK_EQUAL(hasNack, false); // waiting for Nack from forwarder

  face.receive(makeNack(*interest, lp::NackReason::NO_ROUTE));
  advanceClocks(1_ms);
  BOOST_CHECK_EQUAL(hasNack, true);
}

BOOST_AUTO_TEST_SUITE_END() // Producer

BOOST_AUTO_TEST_SUITE(RegisterPrefix)

BOOST_FIXTURE_TEST_CASE(Failure, FaceFixture<NoPrefixRegReply>)
{
  BOOST_CHECK(!runPrefixReg([&] (const auto& success, const auto& failure) {
    face.registerPrefix("/Hello/World", success, failure);
    this->advanceClocks(5_s, 20); // wait for command timeout
  }));
}

BOOST_AUTO_TEST_CASE(Handle)
{
  RegisteredPrefixHandle hdl;
  auto doReg = [&] {
    return runPrefixReg([&] (const auto& success, const auto& failure) {
      hdl = face.registerPrefix("/Hello/World", success, failure);
    });
  };
  auto doUnreg = [&] {
    return runPrefixUnreg([&] (const auto& success, const auto& failure) {
      hdl.unregister(success, failure);
    });
  };

  // despite the "undefined behavior" warning, we try not to crash, but no API guarantee for this
  BOOST_CHECK(!doUnreg());

  // cancel after unregister
  BOOST_CHECK(doReg());
  BOOST_CHECK(doUnreg());
  hdl.cancel();
  advanceClocks(1_ms);

  // unregister after cancel
  BOOST_CHECK(doReg());
  hdl.cancel();
  advanceClocks(1_ms);
  BOOST_CHECK(!doUnreg());

  // cancel after destructing face
  auto face2 = make_unique<DummyClientFace>(m_io, m_keyChain);
  hdl = face2->registerPrefix("/Hello/World/2", nullptr,
                              [] (auto&&...) { BOOST_FAIL("Unexpected failure"); });
  advanceClocks(1_ms);
  face2.reset();
  advanceClocks(1_ms);
  hdl.cancel(); // should not crash
  advanceClocks(1_ms);

  // unregister after destructing face
  auto face3 = make_unique<DummyClientFace>(m_io, m_keyChain);
  hdl = face3->registerPrefix("/Hello/World/3", nullptr,
                              [] (auto&&...) { BOOST_FAIL("Unexpected failure"); });
  advanceClocks(1_ms);
  face3.reset();
  advanceClocks(1_ms);
  BOOST_CHECK(!doUnreg());
}

BOOST_AUTO_TEST_SUITE_END() // RegisterPrefix

BOOST_AUTO_TEST_SUITE(AnnouncePrefix)

BOOST_FIXTURE_TEST_CASE(Failure, FaceFixture<NoPrefixRegReply>)
{
  BOOST_CHECK(!runPrefixAnnouncement([&] (const auto& success, const auto& failure) {
    face.announcePrefix("/Hello/World", 10000_ms, std::nullopt, success, failure);
    this->advanceClocks(5_s, 20); // wait for command timeout
  }));
}

BOOST_AUTO_TEST_CASE(Handle)
{
  RegisteredPrefixHandle hdl;
  PrefixAnnouncement prefixAnnouncement;
  prefixAnnouncement.setAnnouncedName("/Hello/World").setExpiration(1000_ms);
  prefixAnnouncement.toData(m_keyChain);

  auto doAnnounce = [&] {
    return runPrefixAnnouncement([&] (const auto& success, const auto& failure) {
      hdl = face.announcePrefix(prefixAnnouncement, success, failure);
    });
  };

  auto doUnreg = [&] {
    return runPrefixUnreg([&] (const auto& success, const auto& failure) {
      hdl.unregister(success, failure);
    });
  };

  // despite the "undefined behavior" warning, we try not to crash, but no API guarantee for this
  BOOST_CHECK(!doUnreg());

  // cancel after unregister
  BOOST_CHECK(doAnnounce());
  BOOST_CHECK(doUnreg());
  hdl.cancel();
  advanceClocks(1_ms);

  // unregister after cancel
  BOOST_CHECK(doAnnounce());
  hdl.cancel();
  advanceClocks(1_ms);
  BOOST_CHECK(!doUnreg());

  // check overload
  auto doAnnounceWithoutObejct = [&] {
    return runPrefixAnnouncement([&] (const auto& success, const auto& failure) {
      hdl = face.announcePrefix("/Hello/World", 1000_ms, std::nullopt, success, failure);
    });
  };

  BOOST_CHECK(doAnnounceWithoutObejct());
  BOOST_CHECK(doUnreg());
  hdl.cancel();
  advanceClocks(1_ms);

  // cancel after destructing face
  auto face2 = make_unique<DummyClientFace>(m_io, m_keyChain);
  hdl = face2->announcePrefix("/Hello/World/2", 1000_ms, std::nullopt, nullptr,
  [] (auto&&...) { BOOST_FAIL("Unexpected failure"); });
  advanceClocks(1_ms);
  face2.reset();
  advanceClocks(1_ms);
  hdl.cancel(); // should not crash
  advanceClocks(1_ms);

  // unregister after destructing face
  auto face3 = make_unique<DummyClientFace>(m_io, m_keyChain);
  hdl = face3->announcePrefix("/Hello/World/3", 1000_ms, std::nullopt, nullptr,
  [] (auto&&...) { BOOST_FAIL("Unexpected failure"); });
  advanceClocks(1_ms);
  face3.reset();
  advanceClocks(1_ms);
  BOOST_CHECK(!doUnreg());
}

BOOST_AUTO_TEST_SUITE_END() // AnnouncePrefix

BOOST_AUTO_TEST_SUITE(SetInterestFilter)

BOOST_AUTO_TEST_CASE(SetAndCancel)
{
  size_t nInterests = 0;
  size_t nRegs = 0;
  auto hdl = face.setInterestFilter("/Hello/World",
                                    [&] (auto&&...) { ++nInterests; },
                                    [&] (auto&&...) { ++nRegs; },
                                    [] (auto&&...) { BOOST_FAIL("Unexpected failure"); });
  advanceClocks(25_ms, 4);
  BOOST_CHECK_EQUAL(nRegs, 1);
  BOOST_CHECK_EQUAL(nInterests, 0);

  face.receive(*makeInterest("/Hello/World/%21"));
  advanceClocks(25_ms, 4);

  BOOST_CHECK_EQUAL(nRegs, 1);
  BOOST_CHECK_EQUAL(nInterests, 1);

  face.receive(*makeInterest("/Bye/World/%21"));
  advanceClocks(10000_ms, 10);
  BOOST_CHECK_EQUAL(nInterests, 1);

  face.receive(*makeInterest("/Hello/World/%21/2"));
  advanceClocks(25_ms, 4);
  BOOST_CHECK_EQUAL(nInterests, 2);

  // removing filter
  hdl.cancel();
  advanceClocks(25_ms, 4);

  face.receive(*makeInterest("/Hello/World/%21/3"));
  BOOST_CHECK_EQUAL(nInterests, 2);
}

BOOST_AUTO_TEST_CASE(EmptyInterestCallback)
{
  face.setInterestFilter("/A", nullptr);
  advanceClocks(1_ms);

  BOOST_CHECK_NO_THROW(do {
    face.receive(*makeInterest("/A/1"));
    advanceClocks(1_ms);
  } while (false));
}

BOOST_AUTO_TEST_CASE(WithoutSuccessCallback)
{
  size_t nInterests = 0;
  auto hdl = face.setInterestFilter("/Hello/World",
                                    [&] (auto&&...) { ++nInterests; },
                                    [] (auto&&...) { BOOST_FAIL("Unexpected failure"); });
  advanceClocks(25_ms, 4);
  BOOST_CHECK_EQUAL(nInterests, 0);

  face.receive(*makeInterest("/Hello/World/%21"));
  advanceClocks(25_ms, 4);

  BOOST_CHECK_EQUAL(nInterests, 1);

  face.receive(*makeInterest("/Bye/World/%21"));
  advanceClocks(10000_ms, 10);
  BOOST_CHECK_EQUAL(nInterests, 1);

  face.receive(*makeInterest("/Hello/World/%21/2"));
  advanceClocks(25_ms, 4);
  BOOST_CHECK_EQUAL(nInterests, 2);

  // removing filter
  hdl.cancel();
  advanceClocks(25_ms, 4);

  face.receive(*makeInterest("/Hello/World/%21/3"));
  BOOST_CHECK_EQUAL(nInterests, 2);
}

BOOST_FIXTURE_TEST_CASE(Failure, FaceFixture<NoPrefixRegReply>)
{
  // don't enable registration reply
  size_t nRegFailed = 0;
  face.setInterestFilter("/Hello/World",
                         [] (auto&&...) { BOOST_FAIL("Unexpected Interest"); },
                         [] (auto&&...) { BOOST_FAIL("Unexpected success"); },
                         [&] (auto&&...) { ++nRegFailed; });

  advanceClocks(25_ms, 4);
  BOOST_CHECK_EQUAL(nRegFailed, 0);

  advanceClocks(2000_ms, 5);
  BOOST_CHECK_EQUAL(nRegFailed, 1);
}

BOOST_FIXTURE_TEST_CASE(FailureWithoutSuccessCallback, FaceFixture<NoPrefixRegReply>)
{
  // don't enable registration reply
  size_t nRegFailed = 0;
  face.setInterestFilter("/Hello/World",
                         [] (auto&&...) { BOOST_FAIL("Unexpected Interest"); },
                         [&] (auto&&...) { ++nRegFailed; });

  advanceClocks(25_ms, 4);
  BOOST_CHECK_EQUAL(nRegFailed, 0);

  advanceClocks(2000_ms, 5);
  BOOST_CHECK_EQUAL(nRegFailed, 1);
}

BOOST_AUTO_TEST_CASE(SimilarFilters)
{
  size_t nInInterests1 = 0;
  face.setInterestFilter("/Hello/World",
                         [&nInInterests1] (auto&&...) { ++nInInterests1; },
                         nullptr,
                         [] (auto&&...) { BOOST_FAIL("Unexpected failure"); });

  size_t nInInterests2 = 0;
  face.setInterestFilter("/Hello",
                         [&nInInterests2] (auto&&...) { ++nInInterests2; },
                         nullptr,
                         [] (auto&&...) { BOOST_FAIL("Unexpected failure"); });

  size_t nInInterests3 = 0;
  face.setInterestFilter("/Los/Angeles/Lakers",
                         [&nInInterests3] (auto&&...) { ++nInInterests3; },
                         nullptr,
                         [] (auto&&...) { BOOST_FAIL("Unexpected failure"); });

  advanceClocks(25_ms, 4);

  face.receive(*makeInterest("/Hello/World/%21"));
  advanceClocks(25_ms, 4);

  BOOST_CHECK_EQUAL(nInInterests1, 1);
  BOOST_CHECK_EQUAL(nInInterests2, 1);
  BOOST_CHECK_EQUAL(nInInterests3, 0);
}

BOOST_AUTO_TEST_CASE(RegexFilter)
{
  size_t nInInterests = 0;
  face.setInterestFilter(InterestFilter("/Hello/World", "<><b><c>?"),
                         [&nInInterests] (auto&&...) { ++nInInterests; },
                         nullptr,
                         [] (auto&&...) { BOOST_FAIL("Unexpected failure"); });

  advanceClocks(25_ms, 4);

  face.receive(*makeInterest("/Hello/World/a"));     // shouldn't match
  BOOST_CHECK_EQUAL(nInInterests, 0);

  face.receive(*makeInterest("/Hello/World/a/b"));   // should match
  BOOST_CHECK_EQUAL(nInInterests, 1);

  face.receive(*makeInterest("/Hello/World/a/b/c")); // should match
  BOOST_CHECK_EQUAL(nInInterests, 2);

  face.receive(*makeInterest("/Hello/World/a/b/d")); // should not match
  BOOST_CHECK_EQUAL(nInInterests, 2);
}

BOOST_AUTO_TEST_CASE(RegexFilterError)
{
  face.setInterestFilter(InterestFilter("/Hello/World", "<><b><c>?"),
                         // Do NOT use 'auto' for this lambda. This is testing the (failure of)
                         // implicit conversion from InterestFilter to Name, therefore the type
                         // of the first parameter must be explicit.
                         [] (const Name&, const Interest&) {
                           BOOST_FAIL("InterestFilter::Error should have been raised");
                         },
                         nullptr,
                         [] (auto&&...) { BOOST_FAIL("Unexpected failure"); });

  advanceClocks(25_ms, 4);

  BOOST_CHECK_THROW(face.receive(*makeInterest("/Hello/World/XXX/b/c")), InterestFilter::Error);
}

BOOST_AUTO_TEST_CASE(RegexFilterAndRegisterPrefix)
{
  size_t nInInterests = 0;
  face.setInterestFilter(InterestFilter("/Hello/World", "<><b><c>?"),
                         [&] (auto&&...) { ++nInInterests; });

  size_t nRegSuccesses = 0;
  face.registerPrefix("/Hello/World",
                      [&] (auto&&...) { ++nRegSuccesses; },
                      [] (auto&&...) { BOOST_FAIL("Unexpected failure"); });

  advanceClocks(25_ms, 4);
  BOOST_CHECK_EQUAL(nRegSuccesses, 1);

  face.receive(*makeInterest("/Hello/World/a")); // shouldn't match
  BOOST_CHECK_EQUAL(nInInterests, 0);

  face.receive(*makeInterest("/Hello/World/a/b")); // should match
  BOOST_CHECK_EQUAL(nInInterests, 1);

  face.receive(*makeInterest("/Hello/World/a/b/c")); // should match
  BOOST_CHECK_EQUAL(nInInterests, 2);

  face.receive(*makeInterest("/Hello/World/a/b/d")); // should not match
  BOOST_CHECK_EQUAL(nInInterests, 2);
}

BOOST_FIXTURE_TEST_CASE(WithoutRegisterPrefix, FaceFixture<NoPrefixRegReply>,
  * ut::description("test for bug #2318"))
{
  // This behavior is specific to DummyClientFace.
  // Regular Face won't accept incoming packets until something is sent.

  int hit = 0;
  face.setInterestFilter(Name("/"), [&hit] (auto&&...) { ++hit; });
  face.processEvents(-1_ms);

  face.receive(*makeInterest("/A"));
  face.processEvents(-1_ms);

  BOOST_CHECK_EQUAL(hit, 1);
}

BOOST_AUTO_TEST_CASE(Handle)
{
  int hit = 0;
  InterestFilterHandle hdl = face.setInterestFilter(Name("/"), [&hit] (auto&&...) { ++hit; });
  face.processEvents(-1_ms);

  face.receive(*makeInterest("/A"));
  face.processEvents(-1_ms);
  BOOST_CHECK_EQUAL(hit, 1);

  hdl.cancel();
  face.processEvents(-1_ms);

  face.receive(*makeInterest("/B"));
  face.processEvents(-1_ms);
  BOOST_CHECK_EQUAL(hit, 1);

  // cancel after destructing face
  auto face2 = make_unique<DummyClientFace>(m_io, m_keyChain);
  InterestFilterHandle hdl2 = face2->setInterestFilter("/Hello/World/2", nullptr);
  advanceClocks(1_ms);
  face2.reset();
  advanceClocks(1_ms);
  hdl2.cancel(); // should not crash
  advanceClocks(1_ms);
}

BOOST_AUTO_TEST_SUITE_END() // SetInterestFilter

BOOST_AUTO_TEST_CASE(ProcessEvents)
{
  face.processEvents(-1_ms); // io_context::restart()/poll() inside

  int nRegSuccesses = 0;
  face.registerPrefix("/Hello/World",
                      [&] (auto&&...) { ++nRegSuccesses; },
                      [] (auto&&...) { BOOST_FAIL("Unexpected failure"); });

  // io_context::poll() without reset
  face.getIoContext().poll();
  BOOST_CHECK_EQUAL(nRegSuccesses, 0);

  face.processEvents(-1_ms); // io_context::restart()/poll() inside
  BOOST_CHECK_EQUAL(nRegSuccesses, 1);
}

BOOST_AUTO_TEST_CASE(DestroyWithoutProcessEvents,
  * ut::description("test for bug #3248"))
{
  auto face2 = make_unique<Face>(m_io);
  face2.reset();

  m_io.poll(); // should not crash

  // avoid "test case [...] did not check any assertions" message from Boost.Test
  BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE(Transport)

using ndn::Transport;

BOOST_FIXTURE_TEST_CASE(FaceTransport, IoKeyChainFixture)
{
  BOOST_CHECK_NO_THROW(Face(shared_ptr<Transport>()));
  BOOST_CHECK_NO_THROW(Face(shared_ptr<Transport>(), m_io));
  BOOST_CHECK_NO_THROW(Face(shared_ptr<Transport>(), m_io, m_keyChain));

  auto transport = make_shared<TcpTransport>("localhost", "6363"); // no real io operations will be scheduled
  BOOST_CHECK(&Face(transport).getTransport() == transport.get());
  BOOST_CHECK(&Face(transport, m_io).getTransport() == transport.get());
  BOOST_CHECK(&Face(transport, m_io, m_keyChain).getTransport() == transport.get());
}

class WithEnv
{
public:
  WithEnv()
  {
    if (getenv("NDN_CLIENT_TRANSPORT") != nullptr) {
      m_oldTransport = getenv("NDN_CLIENT_TRANSPORT");
      unsetenv("NDN_CLIENT_TRANSPORT");
    }
  }

  void
  configure(const std::string& faceUri)
  {
    setenv("NDN_CLIENT_TRANSPORT", faceUri.data(), true);
  }

  ~WithEnv()
  {
    if (!m_oldTransport.empty()) {
      setenv("NDN_CLIENT_TRANSPORT", m_oldTransport.data(), true);
    }
    else {
      unsetenv("NDN_CLIENT_TRANSPORT");
    }
  }

private:
  std::string m_oldTransport;
};

class WithConfig : private TestHomeFixture<DefaultPibDir>
{
public:
  void
  configure(const std::string& faceUri)
  {
    createClientConf({"transport=" + faceUri});
  }
};

class WithEnvAndConfig : public WithEnv, public WithConfig
{
};

using ConfigOptions = boost::mp11::mp_list<WithEnv, WithConfig>;

BOOST_FIXTURE_TEST_CASE(NoConfig, WithEnvAndConfig) // fixture configures test HOME and PIB/TPM path
{
  shared_ptr<Face> face;
  BOOST_CHECK_NO_THROW(face = make_shared<Face>());
  BOOST_CHECK(dynamic_cast<UnixTransport*>(&face->getTransport()) != nullptr);
}

BOOST_FIXTURE_TEST_CASE_TEMPLATE(Unix, T, ConfigOptions, T)
{
  this->configure("unix://some/path");

  shared_ptr<Face> face;
  BOOST_CHECK_NO_THROW(face = make_shared<Face>());
  BOOST_CHECK(dynamic_cast<UnixTransport*>(&face->getTransport()) != nullptr);
}

BOOST_FIXTURE_TEST_CASE_TEMPLATE(Tcp, T, ConfigOptions, T)
{
  this->configure("tcp://127.0.0.1:6000");

  shared_ptr<Face> face;
  BOOST_CHECK_NO_THROW(face = make_shared<Face>());
  BOOST_CHECK(dynamic_cast<TcpTransport*>(&face->getTransport()) != nullptr);
}

BOOST_FIXTURE_TEST_CASE_TEMPLATE(WrongTransport, T, ConfigOptions, T)
{
  this->configure("wrong-transport:");

  BOOST_CHECK_THROW(make_shared<Face>(), ConfigFile::Error);
}

BOOST_FIXTURE_TEST_CASE_TEMPLATE(WrongUri, T, ConfigOptions, T)
{
  this->configure("wrong-uri");

  BOOST_CHECK_THROW(make_shared<Face>(), ConfigFile::Error);
}

BOOST_FIXTURE_TEST_CASE(EnvOverride, WithEnvAndConfig)
{
  this->WithEnv::configure("tcp://127.0.0.1:6000");
  this->WithConfig::configure("unix://some/path");

  shared_ptr<Face> face;
  BOOST_CHECK_NO_THROW(face = make_shared<Face>());
  BOOST_CHECK(dynamic_cast<TcpTransport*>(&face->getTransport()) != nullptr);
}

BOOST_FIXTURE_TEST_CASE(ExplicitTransport, WithEnvAndConfig)
{
  this->WithEnv::configure("wrong-uri");
  this->WithConfig::configure("wrong-transport:");

  auto transport = make_shared<UnixTransport>("unix://some/path");
  shared_ptr<Face> face;
  BOOST_CHECK_NO_THROW(face = make_shared<Face>(transport));
  BOOST_CHECK(dynamic_cast<UnixTransport*>(&face->getTransport()) != nullptr);
}

BOOST_AUTO_TEST_SUITE_END() // Transport

BOOST_AUTO_TEST_SUITE_END() // TestFace

} // namespace ndn::tests
