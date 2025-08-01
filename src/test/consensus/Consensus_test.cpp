//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <test/csf.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpld/consensus/Consensus.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/json/to_string.h>

namespace ripple {
namespace test {

class Consensus_test : public beast::unit_test::suite
{
    SuiteJournal journal_;

public:
    Consensus_test() : journal_("Consensus_test", *this)
    {
    }

    void
    testShouldCloseLedger()
    {
        using namespace std::chrono_literals;
        testcase("should close ledger");

        // Use default parameters
        ConsensusParms const p{};

        // Bizarre times forcibly close
        BEAST_EXPECT(shouldCloseLedger(
            true, 10, 10, 10, -10s, 10s, 1s, 1s, p, journal_));
        BEAST_EXPECT(shouldCloseLedger(
            true, 10, 10, 10, 100h, 10s, 1s, 1s, p, journal_));
        BEAST_EXPECT(shouldCloseLedger(
            true, 10, 10, 10, 10s, 100h, 1s, 1s, p, journal_));

        // Rest of network has closed
        BEAST_EXPECT(
            shouldCloseLedger(true, 10, 3, 5, 10s, 10s, 10s, 10s, p, journal_));

        // No transactions means wait until end of internval
        BEAST_EXPECT(
            !shouldCloseLedger(false, 10, 0, 0, 1s, 1s, 1s, 10s, p, journal_));
        BEAST_EXPECT(
            shouldCloseLedger(false, 10, 0, 0, 1s, 10s, 1s, 10s, p, journal_));

        // Enforce minimum ledger open time
        BEAST_EXPECT(
            !shouldCloseLedger(true, 10, 0, 0, 10s, 10s, 1s, 10s, p, journal_));

        // Don't go too much faster than last time
        BEAST_EXPECT(
            !shouldCloseLedger(true, 10, 0, 0, 10s, 10s, 3s, 10s, p, journal_));

        BEAST_EXPECT(
            shouldCloseLedger(true, 10, 0, 0, 10s, 10s, 10s, 10s, p, journal_));
    }

    void
    testCheckConsensus()
    {
        using namespace std::chrono_literals;
        testcase("check consensus");

        // Use default parameterss
        ConsensusParms const p{};

        ///////////////
        // Disputes still in doubt
        //
        // Not enough time has elapsed
        BEAST_EXPECT(
            ConsensusState::No ==
            checkConsensus(10, 2, 2, 0, 3s, 2s, false, p, true, journal_));

        // If not enough peers have propsed, ensure
        // more time for proposals
        BEAST_EXPECT(
            ConsensusState::No ==
            checkConsensus(10, 2, 2, 0, 3s, 4s, false, p, true, journal_));

        // Enough time has elapsed and we all agree
        BEAST_EXPECT(
            ConsensusState::Yes ==
            checkConsensus(10, 2, 2, 0, 3s, 10s, false, p, true, journal_));

        // Enough time has elapsed and we don't yet agree
        BEAST_EXPECT(
            ConsensusState::No ==
            checkConsensus(10, 2, 1, 0, 3s, 10s, false, p, true, journal_));

        // Our peers have moved on
        // Enough time has elapsed and we all agree
        BEAST_EXPECT(
            ConsensusState::MovedOn ==
            checkConsensus(10, 2, 1, 8, 3s, 10s, false, p, true, journal_));

        // If no peers, don't agree until time has passed.
        BEAST_EXPECT(
            ConsensusState::No ==
            checkConsensus(0, 0, 0, 0, 3s, 10s, false, p, true, journal_));

        // Agree if no peers and enough time has passed.
        BEAST_EXPECT(
            ConsensusState::Yes ==
            checkConsensus(0, 0, 0, 0, 3s, 16s, false, p, true, journal_));

        // Expire if too much time has passed without agreement
        BEAST_EXPECT(
            ConsensusState::Expired ==
            checkConsensus(10, 8, 1, 0, 1s, 19s, false, p, true, journal_));

        ///////////////
        // Stalled
        //
        // Not enough time has elapsed
        BEAST_EXPECT(
            ConsensusState::No ==
            checkConsensus(10, 2, 2, 0, 3s, 2s, true, p, true, journal_));

        // If not enough peers have propsed, ensure
        // more time for proposals
        BEAST_EXPECT(
            ConsensusState::No ==
            checkConsensus(10, 2, 2, 0, 3s, 4s, true, p, true, journal_));

        // Enough time has elapsed and we all agree
        BEAST_EXPECT(
            ConsensusState::Yes ==
            checkConsensus(10, 2, 2, 0, 3s, 10s, true, p, true, journal_));

        // Enough time has elapsed and we don't yet agree, but there's nothing
        // left to dispute
        BEAST_EXPECT(
            ConsensusState::Yes ==
            checkConsensus(10, 2, 1, 0, 3s, 10s, true, p, true, journal_));

        // Our peers have moved on
        // Enough time has elapsed and we all agree, nothing left to dispute
        BEAST_EXPECT(
            ConsensusState::Yes ==
            checkConsensus(10, 2, 1, 8, 3s, 10s, true, p, true, journal_));

        // If no peers, don't agree until time has passed.
        BEAST_EXPECT(
            ConsensusState::No ==
            checkConsensus(0, 0, 0, 0, 3s, 10s, true, p, true, journal_));

        // Agree if no peers and enough time has passed.
        BEAST_EXPECT(
            ConsensusState::Yes ==
            checkConsensus(0, 0, 0, 0, 3s, 16s, true, p, true, journal_));

        // We are done if there's nothing left to dispute, no matter how much
        // time has passed
        BEAST_EXPECT(
            ConsensusState::Yes ==
            checkConsensus(10, 8, 1, 0, 1s, 19s, true, p, true, journal_));
    }

    void
    testStandalone()
    {
        using namespace std::chrono_literals;
        using namespace csf;
        testcase("standalone");

        Sim s;
        PeerGroup peers = s.createGroup(1);
        Peer* peer = peers[0];
        peer->targetLedgers = 1;
        peer->start();
        peer->submit(Tx{1});

        s.scheduler.step();

        // Inspect that the proper ledger was created
        auto const& lcl = peer->lastClosedLedger;
        BEAST_EXPECT(peer->prevLedgerID() == lcl.id());
        BEAST_EXPECT(lcl.seq() == Ledger::Seq{1});
        BEAST_EXPECT(lcl.txs().size() == 1);
        BEAST_EXPECT(lcl.txs().find(Tx{1}) != lcl.txs().end());
        BEAST_EXPECT(peer->prevProposers == 0);
    }

    void
    testPeersAgree()
    {
        using namespace csf;
        using namespace std::chrono;
        testcase("peers agree");

        ConsensusParms const parms{};
        Sim sim;
        PeerGroup peers = sim.createGroup(5);

        // Connected trust and network graphs with single fixed delay
        peers.trustAndConnect(
            peers, round<milliseconds>(0.2 * parms.ledgerGRANULARITY));

        // everyone submits their own ID as a TX
        for (Peer* p : peers)
            p->submit(Tx(static_cast<std::uint32_t>(p->id)));

        sim.run(1);

        // All peers are in sync
        if (BEAST_EXPECT(sim.synchronized()))
        {
            for (Peer const* peer : peers)
            {
                auto const& lcl = peer->lastClosedLedger;
                BEAST_EXPECT(lcl.id() == peer->prevLedgerID());
                BEAST_EXPECT(lcl.seq() == Ledger::Seq{1});
                // All peers proposed
                BEAST_EXPECT(peer->prevProposers == peers.size() - 1);
                // All transactions were accepted
                for (std::uint32_t i = 0; i < peers.size(); ++i)
                    BEAST_EXPECT(lcl.txs().find(Tx{i}) != lcl.txs().end());
            }
        }
    }

    void
    testSlowPeers()
    {
        using namespace csf;
        using namespace std::chrono;
        testcase("slow peers");

        // Several tests of a complete trust graph with a subset of peers
        // that have significantly longer network delays to the rest of the
        // network

        // Test when a slow peer doesn't delay a consensus quorum (4/5 agree)
        {
            ConsensusParms const parms{};
            Sim sim;
            PeerGroup slow = sim.createGroup(1);
            PeerGroup fast = sim.createGroup(4);
            PeerGroup network = fast + slow;

            // Fully connected trust graph
            network.trust(network);

            // Fast and slow network connections
            fast.connect(
                fast, round<milliseconds>(0.2 * parms.ledgerGRANULARITY));

            slow.connect(
                network, round<milliseconds>(1.1 * parms.ledgerGRANULARITY));

            // All peers submit their own ID as a transaction
            for (Peer* peer : network)
                peer->submit(Tx{static_cast<std::uint32_t>(peer->id)});

            sim.run(1);

            // Verify all peers have same LCL but are missing transaction 0
            // All peers are in sync even with a slower peer 0
            if (BEAST_EXPECT(sim.synchronized()))
            {
                for (Peer* peer : network)
                {
                    auto const& lcl = peer->lastClosedLedger;
                    BEAST_EXPECT(lcl.id() == peer->prevLedgerID());
                    BEAST_EXPECT(lcl.seq() == Ledger::Seq{1});

                    BEAST_EXPECT(peer->prevProposers == network.size() - 1);
                    BEAST_EXPECT(
                        peer->prevRoundTime == network[0]->prevRoundTime);

                    BEAST_EXPECT(lcl.txs().find(Tx{0}) == lcl.txs().end());
                    for (std::uint32_t i = 2; i < network.size(); ++i)
                        BEAST_EXPECT(lcl.txs().find(Tx{i}) != lcl.txs().end());

                    // Tx 0 didn't make it
                    BEAST_EXPECT(
                        peer->openTxs.find(Tx{0}) != peer->openTxs.end());
                }
            }
        }

        // Test when the slow peers delay a consensus quorum (4/6 agree)
        {
            // Run two tests
            //  1. The slow peers are participating in consensus
            //  2. The slow peers are just observing

            for (auto isParticipant : {true, false})
            {
                ConsensusParms const parms{};

                Sim sim;
                PeerGroup slow = sim.createGroup(2);
                PeerGroup fast = sim.createGroup(4);
                PeerGroup network = fast + slow;

                // Connected trust graph
                network.trust(network);

                // Fast and slow network connections
                fast.connect(
                    fast, round<milliseconds>(0.2 * parms.ledgerGRANULARITY));

                slow.connect(
                    network,
                    round<milliseconds>(1.1 * parms.ledgerGRANULARITY));

                for (Peer* peer : slow)
                    peer->runAsValidator = isParticipant;

                // All peers submit their own ID as a transaction and relay it
                // to peers
                for (Peer* peer : network)
                    peer->submit(Tx{static_cast<std::uint32_t>(peer->id)});

                sim.run(1);

                if (BEAST_EXPECT(sim.synchronized()))
                {
                    // Verify all peers have same LCL but are missing
                    // transaction 0,1 which was not received by all peers
                    // before the ledger closed
                    for (Peer* peer : network)
                    {
                        // Closed ledger has all but transaction 0,1
                        auto const& lcl = peer->lastClosedLedger;
                        BEAST_EXPECT(lcl.seq() == Ledger::Seq{1});
                        BEAST_EXPECT(lcl.txs().find(Tx{0}) == lcl.txs().end());
                        BEAST_EXPECT(lcl.txs().find(Tx{1}) == lcl.txs().end());
                        for (std::uint32_t i = slow.size(); i < network.size();
                             ++i)
                            BEAST_EXPECT(
                                lcl.txs().find(Tx{i}) != lcl.txs().end());

                        // Tx 0-1 didn't make it
                        BEAST_EXPECT(
                            peer->openTxs.find(Tx{0}) != peer->openTxs.end());
                        BEAST_EXPECT(
                            peer->openTxs.find(Tx{1}) != peer->openTxs.end());
                    }

                    Peer const* slowPeer = slow[0];
                    if (isParticipant)
                        BEAST_EXPECT(
                            slowPeer->prevProposers == network.size() - 1);
                    else
                        BEAST_EXPECT(slowPeer->prevProposers == fast.size());

                    for (Peer* peer : fast)
                    {
                        // Due to the network link delay settings
                        //    Peer 0 initially proposes {0}
                        //    Peer 1 initially proposes {1}
                        //    Peers 2-5 initially propose {2,3,4,5}
                        // Since peers 2-5 agree, 4/6 > the initial 50% needed
                        // to include a disputed transaction, so Peer 0/1 switch
                        // to agree with those peers. Peer 0/1 then closes with
                        // an 80% quorum of agreeing positions (5/6) match.
                        //
                        // Peers 2-5 do not change position, since tx 0 or tx 1
                        // have less than the 50% initial threshold. They also
                        // cannot declare consensus, since 4/6 agreeing
                        // positions are < 80% threshold. They therefore need an
                        // additional timerEntry call to see the updated
                        // positions from Peer 0 & 1.

                        if (isParticipant)
                        {
                            BEAST_EXPECT(
                                peer->prevProposers == network.size() - 1);
                            BEAST_EXPECT(
                                peer->prevRoundTime > slowPeer->prevRoundTime);
                        }
                        else
                        {
                            BEAST_EXPECT(
                                peer->prevProposers == fast.size() - 1);
                            // so all peers should have closed together
                            BEAST_EXPECT(
                                peer->prevRoundTime == slowPeer->prevRoundTime);
                        }
                    }
                }
            }
        }
    }

    void
    testCloseTimeDisagree()
    {
        using namespace csf;
        using namespace std::chrono;
        testcase("close time disagree");

        // This is a very specialized test to get ledgers to disagree on
        // the close time. It unfortunately assumes knowledge about current
        // timing constants. This is a necessary evil to get coverage up
        // pending more extensive refactorings of timing constants.

        // In order to agree-to-disagree on the close time, there must be no
        // clear majority of nodes agreeing on a close time. This test
        // sets a relative offset to the peers internal clocks so that they
        // send proposals with differing times.

        // However, agreement is on the effective close time, not the
        // exact close time. The minimum closeTimeResolution is given by
        // ledgerPossibleTimeResolutions[0], which is currently 10s. This means
        // the skews need to be at least 10 seconds to have different effective
        // close times.

        // Complicating this matter is that nodes will ignore proposals
        // with times more than proposeFRESHNESS =20s in the past. So at
        // the minimum granularity, we have at most 3 types of skews
        // (0s,10s,20s).

        // This test therefore has 6 nodes, with 2 nodes having each type of
        // skew. Then no majority (1/3 < 1/2) of nodes will agree on an
        // actual close time.

        ConsensusParms const parms{};
        Sim sim;

        PeerGroup groupA = sim.createGroup(2);
        PeerGroup groupB = sim.createGroup(2);
        PeerGroup groupC = sim.createGroup(2);
        PeerGroup network = groupA + groupB + groupC;

        network.trust(network);
        network.connect(
            network, round<milliseconds>(0.2 * parms.ledgerGRANULARITY));

        // Run consensus without skew until we have a short close time
        // resolution
        Peer* firstPeer = *groupA.begin();
        while (firstPeer->lastClosedLedger.closeTimeResolution() >=
               parms.proposeFRESHNESS)
            sim.run(1);

        // Introduce a shift on the time of 2/3 of peers
        for (Peer* peer : groupA)
            peer->clockSkew = parms.proposeFRESHNESS / 2;
        for (Peer* peer : groupB)
            peer->clockSkew = parms.proposeFRESHNESS;

        sim.run(1);

        // All nodes agreed to disagree on the close time
        if (BEAST_EXPECT(sim.synchronized()))
        {
            for (Peer* peer : network)
                BEAST_EXPECT(!peer->lastClosedLedger.closeAgree());
        }
    }

    void
    testWrongLCL()
    {
        using namespace csf;
        using namespace std::chrono;
        testcase("wrong LCL");

        // Specialized test to exercise a temporary fork in which some peers
        // are working on an incorrect prior ledger.

        ConsensusParms const parms{};

        // Vary the time it takes to process validations to exercise detecting
        // the wrong LCL at different phases of consensus
        for (auto validationDelay : {0ms, parms.ledgerMIN_CLOSE})
        {
            // Consider 10 peers:
            // 0 1         2 3 4       5 6 7 8 9
            // minority   majorityA   majorityB
            //
            // Nodes 0-1 trust nodes 0-4
            // Nodes 2-9 trust nodes 2-9
            //
            // By submitting tx 0 to nodes 0-4 and tx 1 to nodes 5-9,
            // nodes 0-1 will generate the wrong LCL (with tx 0). The remaining
            // nodes will instead accept the ledger with tx 1.

            // Nodes 0-1 will detect this mismatch during a subsequent round
            // since nodes 2-4 will validate a different ledger.

            // Nodes 0-1 will acquire the proper ledger from the network and
            // resume consensus and eventually generate the dominant network
            // ledger.

            // This topology can potentially fork with the above trust relations
            // but that is intended for this test.

            Sim sim;

            PeerGroup minority = sim.createGroup(2);
            PeerGroup majorityA = sim.createGroup(3);
            PeerGroup majorityB = sim.createGroup(5);

            PeerGroup majority = majorityA + majorityB;
            PeerGroup network = minority + majority;

            SimDuration delay =
                round<milliseconds>(0.2 * parms.ledgerGRANULARITY);
            minority.trustAndConnect(minority + majorityA, delay);
            majority.trustAndConnect(majority, delay);

            CollectByNode<JumpCollector> jumps;
            sim.collectors.add(jumps);

            BEAST_EXPECT(sim.trustGraph.canFork(parms.minCONSENSUS_PCT / 100.));

            // initial round to set prior state
            sim.run(1);

            // Nodes in smaller UNL have seen tx 0, nodes in other unl have seen
            // tx 1
            for (Peer* peer : network)
                peer->delays.recvValidation = validationDelay;
            for (Peer* peer : (minority + majorityA))
                peer->openTxs.insert(Tx{0});
            for (Peer* peer : majorityB)
                peer->openTxs.insert(Tx{1});

            // Run for additional rounds
            // With no validation delay, only 2 more rounds are needed.
            //  1. Round to generate different ledgers
            //  2. Round to detect different prior ledgers (but still generate
            //    wrong ones) and recover within that round since wrong LCL
            //    is detected before we close
            //
            // With a validation delay of ledgerMIN_CLOSE, we need 3 more
            // rounds.
            //  1. Round to generate different ledgers
            //  2. Round to detect different prior ledgers (but still generate
            //     wrong ones) but end up declaring consensus on wrong LCL (but
            //     with the right transaction set!). This is because we detect
            //     the wrong LCL after we have closed the ledger, so we declare
            //     consensus based solely on our peer proposals. But we haven't
            //     had time to acquire the right ledger.
            //  3. Round to correct
            sim.run(3);

            // The network never actually forks, since node 0-1 never see a
            // quorum of validations to fully validate the incorrect chain.

            // However, for a non zero-validation delay, the network is not
            // synchronized because nodes 0 and 1 are running one ledger behind
            if (BEAST_EXPECT(sim.branches() == 1))
            {
                for (Peer const* peer : majority)
                {
                    // No jumps for majority nodes
                    BEAST_EXPECT(jumps[peer->id].closeJumps.empty());
                    BEAST_EXPECT(jumps[peer->id].fullyValidatedJumps.empty());
                }
                for (Peer const* peer : minority)
                {
                    auto& peerJumps = jumps[peer->id];
                    // last closed ledger jump between chains
                    {
                        if (BEAST_EXPECT(peerJumps.closeJumps.size() == 1))
                        {
                            JumpCollector::Jump const& jump =
                                peerJumps.closeJumps.front();
                            // Jump is to a different chain
                            BEAST_EXPECT(jump.from.seq() <= jump.to.seq());
                            BEAST_EXPECT(!jump.to.isAncestor(jump.from));
                        }
                    }
                    // fully validated jump forward in same chain
                    {
                        if (BEAST_EXPECT(
                                peerJumps.fullyValidatedJumps.size() == 1))
                        {
                            JumpCollector::Jump const& jump =
                                peerJumps.fullyValidatedJumps.front();
                            // Jump is to a different chain with same seq
                            BEAST_EXPECT(jump.from.seq() < jump.to.seq());
                            BEAST_EXPECT(jump.to.isAncestor(jump.from));
                        }
                    }
                }
            }
        }

        {
            // Additional test engineered to switch LCL during the establish
            // phase. This was added to trigger a scenario that previously
            // crashed, in which switchLCL switched from establish to open
            // phase, but still processed the establish phase logic.

            // Loner node will accept an initial ledger A, but all other nodes
            // accept ledger B a bit later. By delaying the time it takes
            // to process a validation, loner node will detect the wrongLCL
            // after it is already in the establish phase of the next round.

            Sim sim;
            PeerGroup loner = sim.createGroup(1);
            PeerGroup friends = sim.createGroup(3);
            loner.trust(loner + friends);

            PeerGroup others = sim.createGroup(6);
            PeerGroup clique = friends + others;
            clique.trust(clique);

            PeerGroup network = loner + clique;
            network.connect(
                network, round<milliseconds>(0.2 * parms.ledgerGRANULARITY));

            // initial round to set prior state
            sim.run(1);
            for (Peer* peer : (loner + friends))
                peer->openTxs.insert(Tx(0));
            for (Peer* peer : others)
                peer->openTxs.insert(Tx(1));

            // Delay validation processing
            for (Peer* peer : network)
                peer->delays.recvValidation = parms.ledgerGRANULARITY;

            // additional rounds to generate wrongLCL and recover
            sim.run(2);

            // Check all peers recovered
            for (Peer* p : network)
                BEAST_EXPECT(p->prevLedgerID() == network[0]->prevLedgerID());
        }
    }

    void
    testConsensusCloseTimeRounding()
    {
        using namespace csf;
        using namespace std::chrono;
        testcase("consensus close time rounding");

        // This is a specialized test engineered to yield ledgers with different
        // close times even though the peers believe they had close time
        // consensus on the ledger.
        ConsensusParms parms;

        Sim sim;

        // This requires a group of 4 fast and 2 slow peers to create a
        // situation in which a subset of peers requires seeing additional
        // proposals to declare consensus.
        PeerGroup slow = sim.createGroup(2);
        PeerGroup fast = sim.createGroup(4);
        PeerGroup network = fast + slow;

        // Connected trust graph
        network.trust(network);

        // Fast and slow network connections
        fast.connect(fast, round<milliseconds>(0.2 * parms.ledgerGRANULARITY));
        slow.connect(
            network, round<milliseconds>(1.1 * parms.ledgerGRANULARITY));

        // Run to the ledger *prior* to decreasing the resolution
        sim.run(increaseLedgerTimeResolutionEvery - 2);

        // In order to create the discrepency, we want a case where if
        //   X = effCloseTime(closeTime, resolution, parentCloseTime)
        //   X != effCloseTime(X, resolution, parentCloseTime)
        //
        // That is, the effective close time is not a fixed point. This can
        // happen if X = parentCloseTime + 1, but a subsequent rounding goes
        // to the next highest multiple of resolution.

        // So we want to find an offset  (now + offset) % 30s = 15
        //                               (now + offset) % 20s = 15
        // This way, the next ledger will close and round up   Due to the
        // network delay settings, the round of consensus will take 5s, so
        // the next ledger's close time will

        NetClock::duration when = network[0]->now().time_since_epoch();

        // Check we are before the 30s to 20s transition
        NetClock::duration resolution =
            network[0]->lastClosedLedger.closeTimeResolution();
        BEAST_EXPECT(resolution == NetClock::duration{30s});

        while (((when % NetClock::duration{30s}) != NetClock::duration{15s}) ||
               ((when % NetClock::duration{20s}) != NetClock::duration{15s}))
            when += 1s;
        // Advance the clock without consensus running (IS THIS WHAT
        // PREVENTS IT IN PRACTICE?)
        sim.scheduler.step_for(NetClock::time_point{when} - network[0]->now());

        // Run one more ledger with 30s resolution
        sim.run(1);
        if (BEAST_EXPECT(sim.synchronized()))
        {
            // close time should be ahead of clock time since we engineered
            // the close time to round up
            for (Peer* peer : network)
            {
                BEAST_EXPECT(peer->lastClosedLedger.closeTime() > peer->now());
                BEAST_EXPECT(peer->lastClosedLedger.closeAgree());
            }
        }

        // All peers submit their own ID as a transaction
        for (Peer* peer : network)
            peer->submit(Tx{static_cast<std::uint32_t>(peer->id)});

        // Run 1 more round, this time it will have a decreased
        // resolution of 20 seconds.

        // The network delays are engineered so that the slow peers
        // initially have the wrong tx hash, but they see a majority
        // of agreement from their peers and declare consensus
        //
        // The trick is that everyone starts with a raw close time of
        //  84681s
        // Which has
        //   effCloseTime(86481s, 20s,  86490s) = 86491s
        // However, when the slow peers update their position, they change
        // the close time to 86451s. The fast peers declare consensus with
        // the 86481s as their position still.
        //
        // When accepted the ledger
        // - fast peers use eff(86481s) -> 86491s as the close time
        // - slow peers use eff(eff(86481s)) -> eff(86491s) -> 86500s!

        sim.run(1);

        BEAST_EXPECT(sim.synchronized());
    }

    void
    testFork()
    {
        using namespace csf;
        using namespace std::chrono;
        testcase("fork");

        std::uint32_t numPeers = 10;
        // Vary overlap between two UNLs
        for (std::uint32_t overlap = 0; overlap <= numPeers; ++overlap)
        {
            ConsensusParms const parms{};
            Sim sim;

            std::uint32_t numA = (numPeers - overlap) / 2;
            std::uint32_t numB = numPeers - numA - overlap;

            PeerGroup aOnly = sim.createGroup(numA);
            PeerGroup bOnly = sim.createGroup(numB);
            PeerGroup commonOnly = sim.createGroup(overlap);

            PeerGroup a = aOnly + commonOnly;
            PeerGroup b = bOnly + commonOnly;

            PeerGroup network = a + b;

            SimDuration delay =
                round<milliseconds>(0.2 * parms.ledgerGRANULARITY);
            a.trustAndConnect(a, delay);
            b.trustAndConnect(b, delay);

            // Initial round to set prior state
            sim.run(1);
            for (Peer* peer : network)
            {
                // Nodes have only seen transactions from their neighbors
                peer->openTxs.insert(Tx{static_cast<std::uint32_t>(peer->id)});
                for (Peer* to : sim.trustGraph.trustedPeers(peer))
                    peer->openTxs.insert(
                        Tx{static_cast<std::uint32_t>(to->id)});
            }
            sim.run(1);

            // Fork should not happen for 40% or greater overlap
            // Since the overlapped nodes have a UNL that is the union of the
            // two cliques, the maximum sized UNL list is the number of peers
            if (overlap > 0.4 * numPeers)
                BEAST_EXPECT(sim.synchronized());
            else
            {
                // Even if we do fork, there shouldn't be more than 3 ledgers
                // One for cliqueA, one for cliqueB and one for nodes in both
                BEAST_EXPECT(sim.branches() <= 3);
            }
        }
    }

    void
    testHubNetwork()
    {
        using namespace csf;
        using namespace std::chrono;
        testcase("hub network");

        // Simulate a set of 5 validators that aren't directly connected but
        // rely on a single hub node for communication

        ConsensusParms const parms{};
        Sim sim;
        PeerGroup validators = sim.createGroup(5);
        PeerGroup center = sim.createGroup(1);
        validators.trust(validators);
        center.trust(validators);

        SimDuration delay = round<milliseconds>(0.2 * parms.ledgerGRANULARITY);
        validators.connect(center, delay);

        center[0]->runAsValidator = false;

        // prep round to set initial state.
        sim.run(1);

        // everyone submits their own ID as a TX and relay it to peers
        for (Peer* p : validators)
            p->submit(Tx(static_cast<std::uint32_t>(p->id)));

        sim.run(1);

        // All peers are in sync
        BEAST_EXPECT(sim.synchronized());
    }

    // Helper collector for testPreferredByBranch
    // Invasively disconnects network at bad times to cause splits
    struct Disruptor
    {
        csf::PeerGroup& network;
        csf::PeerGroup& groupCfast;
        csf::PeerGroup& groupCsplit;
        csf::SimDuration delay;
        bool reconnected = false;

        Disruptor(
            csf::PeerGroup& net,
            csf::PeerGroup& c,
            csf::PeerGroup& split,
            csf::SimDuration d)
            : network(net), groupCfast(c), groupCsplit(split), delay(d)
        {
        }

        template <class E>
        void
        on(csf::PeerID, csf::SimTime, E const&)
        {
        }

        void
        on(csf::PeerID who, csf::SimTime, csf::FullyValidateLedger const& e)
        {
            using namespace std::chrono;
            // As soon as the fastC node fully validates C, disconnect
            // ALL c nodes from the network. The fast C node needs to disconnect
            // as well to prevent it from relaying the validations it did see
            if (who == groupCfast[0]->id &&
                e.ledger.seq() == csf::Ledger::Seq{2})
            {
                network.disconnect(groupCsplit);
                network.disconnect(groupCfast);
            }
        }

        void
        on(csf::PeerID who, csf::SimTime, csf::AcceptLedger const& e)
        {
            // As soon as anyone generates a child of B or C, reconnect the
            // network so those validations make it through
            if (!reconnected && e.ledger.seq() == csf::Ledger::Seq{3})
            {
                reconnected = true;
                network.connect(groupCsplit, delay);
            }
        }
    };

    void
    testPreferredByBranch()
    {
        using namespace csf;
        using namespace std::chrono;
        testcase("preferred by branch");

        // Simulate network splits that are prevented from forking when using
        // preferred ledger by trie.  This is a contrived example that involves
        // excessive network splits, but demonstrates the safety improvement
        // from the preferred ledger by trie approach.

        // Consider 10 validating nodes that comprise a single common UNL
        // Ledger history:
        // 1:           A
        //            _/ \_
        // 2:         B    C
        //          _/  _/  \_
        // 3:       D   C'  |||||||| (8 different ledgers)

        // - All nodes generate the common ledger A
        // - 2 nodes generate B and 8 nodes generate C
        // - Only 1 of the C nodes sees all the C validations and fully
        //   validates C. The rest of the C nodes split at just the right time
        //   such that they never see any C validations but their own.
        // - The C nodes continue and generate 8 different child ledgers.
        // - Meanwhile, the D nodes only saw 1 validation for C and 2
        // validations
        //   for B.
        // - The network reconnects and the validations for generation 3 ledgers
        //   are observed (D and the 8 C's)
        // - In the old approach, 2 votes for D outweights 1 vote for each C'
        //   so the network would avalanche towards D and fully validate it
        //   EVEN though C was fully validated by one node
        // - In the new approach, 2 votes for D are not enough to outweight the
        //   8 implicit votes for C, so nodes will avalanche to C instead

        ConsensusParms const parms{};
        Sim sim;

        // Goes A->B->D
        PeerGroup groupABD = sim.createGroup(2);
        // Single node that initially fully validates C before the split
        PeerGroup groupCfast = sim.createGroup(1);
        // Generates C, but fails to fully validate before the split
        PeerGroup groupCsplit = sim.createGroup(7);

        PeerGroup groupNotFastC = groupABD + groupCsplit;
        PeerGroup network = groupABD + groupCsplit + groupCfast;

        SimDuration delay = round<milliseconds>(0.2 * parms.ledgerGRANULARITY);
        SimDuration fDelay = round<milliseconds>(0.1 * parms.ledgerGRANULARITY);

        network.trust(network);
        // C must have a shorter delay to see all the validations before the
        // other nodes
        network.connect(groupCfast, fDelay);
        // The rest of the network is connected at the same speed
        groupNotFastC.connect(groupNotFastC, delay);

        Disruptor dc(network, groupCfast, groupCsplit, delay);
        sim.collectors.add(dc);

        // Consensus round to generate ledger A
        sim.run(1);
        BEAST_EXPECT(sim.synchronized());

        // Next round generates B and C
        // To force B, we inject an extra transaction in to those nodes
        for (Peer* peer : groupABD)
        {
            peer->txInjections.emplace(peer->lastClosedLedger.seq(), Tx{42});
        }
        // The Disruptor will ensure that nodes disconnect before the C
        // validations make it to all but the fastC node
        sim.run(1);

        // We are no longer in sync, but have not yet forked:
        // 9 nodes consider A the last fully validated ledger and fastC sees C
        BEAST_EXPECT(!sim.synchronized());
        BEAST_EXPECT(sim.branches() == 1);

        //  Run another round to generate the 8 different C' ledgers
        for (Peer* p : network)
            p->submit(Tx(static_cast<std::uint32_t>(p->id)));
        sim.run(1);

        // Still not forked
        BEAST_EXPECT(!sim.synchronized());
        BEAST_EXPECT(sim.branches() == 1);

        // Disruptor will reconnect all but the fastC node
        sim.run(1);

        if (BEAST_EXPECT(sim.branches() == 1))
        {
            BEAST_EXPECT(sim.synchronized());
        }
        else  // old approach caused a fork
        {
            BEAST_EXPECT(sim.branches(groupNotFastC) == 1);
            BEAST_EXPECT(sim.synchronized(groupNotFastC) == 1);
        }
    }

    // Helper collector for testPauseForLaggards
    // This will remove the ledgerAccept delay used to
    // initially create the slow vs. fast validator groups.
    struct UndoDelay
    {
        csf::PeerGroup& g;

        UndoDelay(csf::PeerGroup& a) : g(a)
        {
        }

        template <class E>
        void
        on(csf::PeerID, csf::SimTime, E const&)
        {
        }

        void
        on(csf::PeerID who, csf::SimTime, csf::AcceptLedger const& e)
        {
            for (csf::Peer* p : g)
            {
                if (p->id == who)
                    p->delays.ledgerAccept = std::chrono::seconds{0};
            }
        }
    };

    void
    testPauseForLaggards()
    {
        using namespace csf;
        using namespace std::chrono;
        testcase("pause for laggards");

        // Test that validators that jump ahead of the network slow
        // down.

        // We engineer the following validated ledger history scenario:
        //
        //  / --> B1 --> C1 --> ... -> G1  "ahead"
        // A
        //  \ --> B2 --> C2 "behind"
        //
        // After validating a common ledger A, a set of "behind" validators
        // briefly run slower and validate the lower chain of ledgers.
        // The "ahead" validators run normal speed and run ahead validating the
        // upper chain of ledgers.
        //
        // Due to the uncommited support definition of the preferred branch
        // protocol, even if the "behind" validators are a majority, the "ahead"
        // validators cannot jump to the proper branch until the "behind"
        // validators catch up to the same sequence number. For this test to
        // succeed, the ahead validators need to briefly slow down consensus.

        ConsensusParms const parms{};
        Sim sim;
        SimDuration delay = round<milliseconds>(0.2 * parms.ledgerGRANULARITY);

        PeerGroup behind = sim.createGroup(3);
        PeerGroup ahead = sim.createGroup(2);
        PeerGroup network = ahead + behind;

        hash_set<Peer::NodeKey_t> trustedKeys;
        for (Peer* p : network)
            trustedKeys.insert(p->key);
        for (Peer* p : network)
            p->trustedKeys = trustedKeys;

        network.trustAndConnect(network, delay);

        // Initial seed round to set prior state
        sim.run(1);

        // Have the "behind" group initially take a really long time to
        // accept a ledger after ending deliberation
        for (Peer* p : behind)
            p->delays.ledgerAccept = 20s;

        // Use the collector to revert the delay after the single
        // slow ledger is generated
        UndoDelay undoDelay{behind};
        sim.collectors.add(undoDelay);

#if 0
        // Have all beast::journal output printed to stdout
        for (Peer* p : network)
            p->sink.threshold(beast::severities::kAll);

        // Print ledger accept and fully validated events to stdout
        StreamCollector sc{std::cout};
        sim.collectors.add(sc);
#endif
        // Run the simulation for 100 seconds of simulation time with
        std::chrono::nanoseconds const simDuration = 100s;

        // Simulate clients submitting 1 tx every 5 seconds to a random
        // validator
        Rate const rate{1, 5s};
        auto peerSelector = makeSelector(
            network.begin(),
            network.end(),
            std::vector<double>(network.size(), 1.),
            sim.rng);
        auto txSubmitter = makeSubmitter(
            ConstantDistribution{rate.inv()},
            sim.scheduler.now(),
            sim.scheduler.now() + simDuration,
            peerSelector,
            sim.scheduler,
            sim.rng);

        // Run simulation
        sim.run(simDuration);

        // Verify that the network recovered
        BEAST_EXPECT(sim.synchronized());
    }

    void
    testDisputes()
    {
        testcase("disputes");

        using namespace csf;

        // Test dispute objects directly
        using Dispute = DisputedTx<Tx, PeerID>;

        Tx const txTrue{99};
        Tx const txFalse{98};
        Tx const txFollowingTrue{97};
        Tx const txFollowingFalse{96};
        int const numPeers = 100;
        ConsensusParms p;
        std::size_t peersUnchanged = 0;

        // Three cases:
        // 1 proposing, initial vote yes
        // 2 proposing, initial vote no
        // 3 not proposing, initial vote doesn't matter after the first update,
        // use yes
        {
            Dispute proposingTrue{txTrue.id(), true, numPeers, journal_};
            Dispute proposingFalse{txFalse.id(), false, numPeers, journal_};
            Dispute followingTrue{
                txFollowingTrue.id(), true, numPeers, journal_};
            Dispute followingFalse{
                txFollowingFalse.id(), false, numPeers, journal_};
            BEAST_EXPECT(proposingTrue.ID() == 99);
            BEAST_EXPECT(proposingFalse.ID() == 98);
            BEAST_EXPECT(followingTrue.ID() == 97);
            BEAST_EXPECT(followingFalse.ID() == 96);

            // Create an even split in the peer votes
            for (int i = 0; i < numPeers; ++i)
            {
                BEAST_EXPECT(proposingTrue.setVote(PeerID(i), i < 50));
                BEAST_EXPECT(proposingFalse.setVote(PeerID(i), i < 50));
                BEAST_EXPECT(followingTrue.setVote(PeerID(i), i < 50));
                BEAST_EXPECT(followingFalse.setVote(PeerID(i), i < 50));
            }
            // Switch the middle vote to match mine
            BEAST_EXPECT(proposingTrue.setVote(PeerID(50), true));
            BEAST_EXPECT(proposingFalse.setVote(PeerID(49), false));
            BEAST_EXPECT(followingTrue.setVote(PeerID(50), true));
            BEAST_EXPECT(followingFalse.setVote(PeerID(49), false));

            // no changes yet
            BEAST_EXPECT(proposingTrue.getOurVote() == true);
            BEAST_EXPECT(proposingFalse.getOurVote() == false);
            BEAST_EXPECT(followingTrue.getOurVote() == true);
            BEAST_EXPECT(followingFalse.getOurVote() == false);
            BEAST_EXPECT(!proposingTrue.stalled(p, true, peersUnchanged));
            BEAST_EXPECT(!proposingFalse.stalled(p, true, peersUnchanged));
            BEAST_EXPECT(!followingTrue.stalled(p, false, peersUnchanged));
            BEAST_EXPECT(!followingFalse.stalled(p, false, peersUnchanged));

            // I'm in the majority, my vote should not change
            BEAST_EXPECT(!proposingTrue.updateVote(5, true, p));
            BEAST_EXPECT(!proposingFalse.updateVote(5, true, p));
            BEAST_EXPECT(!followingTrue.updateVote(5, false, p));
            BEAST_EXPECT(!followingFalse.updateVote(5, false, p));

            BEAST_EXPECT(!proposingTrue.updateVote(10, true, p));
            BEAST_EXPECT(!proposingFalse.updateVote(10, true, p));
            BEAST_EXPECT(!followingTrue.updateVote(10, false, p));
            BEAST_EXPECT(!followingFalse.updateVote(10, false, p));

            peersUnchanged = 2;
            BEAST_EXPECT(!proposingTrue.stalled(p, true, peersUnchanged));
            BEAST_EXPECT(!proposingFalse.stalled(p, true, peersUnchanged));
            BEAST_EXPECT(!followingTrue.stalled(p, false, peersUnchanged));
            BEAST_EXPECT(!followingFalse.stalled(p, false, peersUnchanged));

            // Right now, the vote is 51%. The requirement is about to jump to
            // 65%
            BEAST_EXPECT(proposingTrue.updateVote(55, true, p));
            BEAST_EXPECT(!proposingFalse.updateVote(55, true, p));
            BEAST_EXPECT(!followingTrue.updateVote(55, false, p));
            BEAST_EXPECT(!followingFalse.updateVote(55, false, p));

            BEAST_EXPECT(proposingTrue.getOurVote() == false);
            BEAST_EXPECT(proposingFalse.getOurVote() == false);
            BEAST_EXPECT(followingTrue.getOurVote() == true);
            BEAST_EXPECT(followingFalse.getOurVote() == false);
            // 16 validators change their vote to match my original vote
            for (int i = 0; i < 16; ++i)
            {
                auto pTrue = PeerID(numPeers - i - 1);
                auto pFalse = PeerID(i);
                BEAST_EXPECT(proposingTrue.setVote(pTrue, true));
                BEAST_EXPECT(proposingFalse.setVote(pFalse, false));
                BEAST_EXPECT(followingTrue.setVote(pTrue, true));
                BEAST_EXPECT(followingFalse.setVote(pFalse, false));
            }
            // The vote should now be 66%, threshold is 65%
            BEAST_EXPECT(proposingTrue.updateVote(60, true, p));
            BEAST_EXPECT(!proposingFalse.updateVote(60, true, p));
            BEAST_EXPECT(!followingTrue.updateVote(60, false, p));
            BEAST_EXPECT(!followingFalse.updateVote(60, false, p));

            BEAST_EXPECT(proposingTrue.getOurVote() == true);
            BEAST_EXPECT(proposingFalse.getOurVote() == false);
            BEAST_EXPECT(followingTrue.getOurVote() == true);
            BEAST_EXPECT(followingFalse.getOurVote() == false);

            // Threshold jumps to 70%
            BEAST_EXPECT(proposingTrue.updateVote(86, true, p));
            BEAST_EXPECT(!proposingFalse.updateVote(86, true, p));
            BEAST_EXPECT(!followingTrue.updateVote(86, false, p));
            BEAST_EXPECT(!followingFalse.updateVote(86, false, p));

            BEAST_EXPECT(proposingTrue.getOurVote() == false);
            BEAST_EXPECT(proposingFalse.getOurVote() == false);
            BEAST_EXPECT(followingTrue.getOurVote() == true);
            BEAST_EXPECT(followingFalse.getOurVote() == false);

            // 5 more validators change their vote to match my original vote
            for (int i = 16; i < 21; ++i)
            {
                auto pTrue = PeerID(numPeers - i - 1);
                auto pFalse = PeerID(i);
                BEAST_EXPECT(proposingTrue.setVote(pTrue, true));
                BEAST_EXPECT(proposingFalse.setVote(pFalse, false));
                BEAST_EXPECT(followingTrue.setVote(pTrue, true));
                BEAST_EXPECT(followingFalse.setVote(pFalse, false));
            }

            // The vote should now be 71%, threshold is 70%
            BEAST_EXPECT(proposingTrue.updateVote(90, true, p));
            BEAST_EXPECT(!proposingFalse.updateVote(90, true, p));
            BEAST_EXPECT(!followingTrue.updateVote(90, false, p));
            BEAST_EXPECT(!followingFalse.updateVote(90, false, p));

            BEAST_EXPECT(proposingTrue.getOurVote() == true);
            BEAST_EXPECT(proposingFalse.getOurVote() == false);
            BEAST_EXPECT(followingTrue.getOurVote() == true);
            BEAST_EXPECT(followingFalse.getOurVote() == false);

            // The vote should now be 71%, threshold is 70%
            BEAST_EXPECT(!proposingTrue.updateVote(150, true, p));
            BEAST_EXPECT(!proposingFalse.updateVote(150, true, p));
            BEAST_EXPECT(!followingTrue.updateVote(150, false, p));
            BEAST_EXPECT(!followingFalse.updateVote(150, false, p));

            BEAST_EXPECT(proposingTrue.getOurVote() == true);
            BEAST_EXPECT(proposingFalse.getOurVote() == false);
            BEAST_EXPECT(followingTrue.getOurVote() == true);
            BEAST_EXPECT(followingFalse.getOurVote() == false);

            // The vote should now be 71%, threshold is 70%
            BEAST_EXPECT(!proposingTrue.updateVote(190, true, p));
            BEAST_EXPECT(!proposingFalse.updateVote(190, true, p));
            BEAST_EXPECT(!followingTrue.updateVote(190, false, p));
            BEAST_EXPECT(!followingFalse.updateVote(190, false, p));

            BEAST_EXPECT(proposingTrue.getOurVote() == true);
            BEAST_EXPECT(proposingFalse.getOurVote() == false);
            BEAST_EXPECT(followingTrue.getOurVote() == true);
            BEAST_EXPECT(followingFalse.getOurVote() == false);

            peersUnchanged = 3;
            BEAST_EXPECT(!proposingTrue.stalled(p, true, peersUnchanged));
            BEAST_EXPECT(!proposingFalse.stalled(p, true, peersUnchanged));
            BEAST_EXPECT(!followingTrue.stalled(p, false, peersUnchanged));
            BEAST_EXPECT(!followingFalse.stalled(p, false, peersUnchanged));

            // Threshold jumps to 95%
            BEAST_EXPECT(proposingTrue.updateVote(220, true, p));
            BEAST_EXPECT(!proposingFalse.updateVote(220, true, p));
            BEAST_EXPECT(!followingTrue.updateVote(220, false, p));
            BEAST_EXPECT(!followingFalse.updateVote(220, false, p));

            BEAST_EXPECT(proposingTrue.getOurVote() == false);
            BEAST_EXPECT(proposingFalse.getOurVote() == false);
            BEAST_EXPECT(followingTrue.getOurVote() == true);
            BEAST_EXPECT(followingFalse.getOurVote() == false);

            // 25 more validators change their vote to match my original vote
            for (int i = 21; i < 46; ++i)
            {
                auto pTrue = PeerID(numPeers - i - 1);
                auto pFalse = PeerID(i);
                BEAST_EXPECT(proposingTrue.setVote(pTrue, true));
                BEAST_EXPECT(proposingFalse.setVote(pFalse, false));
                BEAST_EXPECT(followingTrue.setVote(pTrue, true));
                BEAST_EXPECT(followingFalse.setVote(pFalse, false));
            }

            // The vote should now be 96%, threshold is 95%
            BEAST_EXPECT(proposingTrue.updateVote(250, true, p));
            BEAST_EXPECT(!proposingFalse.updateVote(250, true, p));
            BEAST_EXPECT(!followingTrue.updateVote(250, false, p));
            BEAST_EXPECT(!followingFalse.updateVote(250, false, p));

            BEAST_EXPECT(proposingTrue.getOurVote() == true);
            BEAST_EXPECT(proposingFalse.getOurVote() == false);
            BEAST_EXPECT(followingTrue.getOurVote() == true);
            BEAST_EXPECT(followingFalse.getOurVote() == false);

            for (peersUnchanged = 0; peersUnchanged < 6; ++peersUnchanged)
            {
                BEAST_EXPECT(!proposingTrue.stalled(p, true, peersUnchanged));
                BEAST_EXPECT(!proposingFalse.stalled(p, true, peersUnchanged));
                BEAST_EXPECT(!followingTrue.stalled(p, false, peersUnchanged));
                BEAST_EXPECT(!followingFalse.stalled(p, false, peersUnchanged));
            }

            for (int i = 0; i < 1; ++i)
            {
                BEAST_EXPECT(!proposingTrue.updateVote(250 + 10 * i, true, p));
                BEAST_EXPECT(!proposingFalse.updateVote(250 + 10 * i, true, p));
                BEAST_EXPECT(!followingTrue.updateVote(250 + 10 * i, false, p));
                BEAST_EXPECT(
                    !followingFalse.updateVote(250 + 10 * i, false, p));

                BEAST_EXPECT(proposingTrue.getOurVote() == true);
                BEAST_EXPECT(proposingFalse.getOurVote() == false);
                BEAST_EXPECT(followingTrue.getOurVote() == true);
                BEAST_EXPECT(followingFalse.getOurVote() == false);

                // true vote has changed recently, so not stalled
                BEAST_EXPECT(!proposingTrue.stalled(p, true, 0));
                // remaining votes have been unchanged in so long that we only
                // need to hit the second round at 95% to be stalled, regardless
                // of peers
                BEAST_EXPECT(proposingFalse.stalled(p, true, 0));
                BEAST_EXPECT(followingTrue.stalled(p, false, 0));
                BEAST_EXPECT(followingFalse.stalled(p, false, 0));

                // true vote has changed recently, so not stalled
                BEAST_EXPECT(!proposingTrue.stalled(p, true, peersUnchanged));
                // remaining votes have been unchanged in so long that we only
                // need to hit the second round at 95% to be stalled, regardless
                // of peers
                BEAST_EXPECT(proposingFalse.stalled(p, true, peersUnchanged));
                BEAST_EXPECT(followingTrue.stalled(p, false, peersUnchanged));
                BEAST_EXPECT(followingFalse.stalled(p, false, peersUnchanged));
            }
            for (int i = 1; i < 3; ++i)
            {
                BEAST_EXPECT(!proposingTrue.updateVote(250 + 10 * i, true, p));
                BEAST_EXPECT(!proposingFalse.updateVote(250 + 10 * i, true, p));
                BEAST_EXPECT(!followingTrue.updateVote(250 + 10 * i, false, p));
                BEAST_EXPECT(
                    !followingFalse.updateVote(250 + 10 * i, false, p));

                BEAST_EXPECT(proposingTrue.getOurVote() == true);
                BEAST_EXPECT(proposingFalse.getOurVote() == false);
                BEAST_EXPECT(followingTrue.getOurVote() == true);
                BEAST_EXPECT(followingFalse.getOurVote() == false);

                // true vote changed 2 rounds ago, and peers are changing, so
                // not stalled
                BEAST_EXPECT(!proposingTrue.stalled(p, true, 0));
                // still stalled
                BEAST_EXPECT(proposingFalse.stalled(p, true, 0));
                BEAST_EXPECT(followingTrue.stalled(p, false, 0));
                BEAST_EXPECT(followingFalse.stalled(p, false, 0));

                // true vote changed 2 rounds ago, and peers are NOT changing,
                // so stalled
                BEAST_EXPECT(proposingTrue.stalled(p, true, peersUnchanged));
                // still stalled
                BEAST_EXPECT(proposingFalse.stalled(p, true, peersUnchanged));
                BEAST_EXPECT(followingTrue.stalled(p, false, peersUnchanged));
                BEAST_EXPECT(followingFalse.stalled(p, false, peersUnchanged));
            }
            for (int i = 3; i < 5; ++i)
            {
                BEAST_EXPECT(!proposingTrue.updateVote(250 + 10 * i, true, p));
                BEAST_EXPECT(!proposingFalse.updateVote(250 + 10 * i, true, p));
                BEAST_EXPECT(!followingTrue.updateVote(250 + 10 * i, false, p));
                BEAST_EXPECT(
                    !followingFalse.updateVote(250 + 10 * i, false, p));

                BEAST_EXPECT(proposingTrue.getOurVote() == true);
                BEAST_EXPECT(proposingFalse.getOurVote() == false);
                BEAST_EXPECT(followingTrue.getOurVote() == true);
                BEAST_EXPECT(followingFalse.getOurVote() == false);

                BEAST_EXPECT(proposingTrue.stalled(p, true, 0));
                BEAST_EXPECT(proposingFalse.stalled(p, true, 0));
                BEAST_EXPECT(followingTrue.stalled(p, false, 0));
                BEAST_EXPECT(followingFalse.stalled(p, false, 0));

                BEAST_EXPECT(proposingTrue.stalled(p, true, peersUnchanged));
                BEAST_EXPECT(proposingFalse.stalled(p, true, peersUnchanged));
                BEAST_EXPECT(followingTrue.stalled(p, false, peersUnchanged));
                BEAST_EXPECT(followingFalse.stalled(p, false, peersUnchanged));
            }
        }
    }

    void
    run() override
    {
        // testShouldCloseLedger();
        // testCheckConsensus();

        // testStandalone();
        // testPeersAgree();
        // testSlowPeers();
        // testCloseTimeDisagree();
        // testWrongLCL();
        // testConsensusCloseTimeRounding();
        // testFork();
        // testHubNetwork();
        // testPreferredByBranch();
        // testPauseForLaggards();
        //testDisputes();
        }
};

BEAST_DEFINE_TESTSUITE(Consensus, consensus, ripple);
}  // namespace test
}  // namespace ripple
