//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <xrpld/app/consensus/RCLConsensus.h>
#include <xrpld/app/consensus/RCLValidations.h>
#include <xrpld/app/ledger/AcceptedLedger.h>
#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/app/ledger/LocalTxs.h>
#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/ledger/OrderBookDB.h>
#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/main/LoadManager.h>
#include <xrpld/app/main/Tuning.h>
#include <xrpld/app/misc/AmendmentTable.h>
#include <xrpld/app/misc/DeliverMax.h>
#include <xrpld/app/misc/HashRouter.h>
#include <xrpld/app/misc/LoadFeeTrack.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/app/misc/ValidatorKeys.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/app/misc/detail/AccountTxPaging.h>
#include <xrpld/app/rdb/backend/SQLiteDatabase.h>
#include <xrpld/app/tx/apply.h>
#include <xrpld/consensus/Consensus.h>
#include <xrpld/consensus/ConsensusParms.h>
#include <xrpld/overlay/Cluster.h>
#include <xrpld/overlay/Overlay.h>
#include <xrpld/overlay/predicates.h>
#include <xrpld/perflog/PerfLog.h>
#include <xrpld/rpc/BookChanges.h>
#include <xrpld/rpc/CTID.h>
#include <xrpld/rpc/DeliveredAmount.h>
#include <xrpld/rpc/MPTokenIssuanceID.h>
#include <xrpld/rpc/ServerHandler.h>

#include <xrpl/basics/UptimeClock.h>
#include <xrpl/basics/mulDiv.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/basics/scope.h>
#include <xrpl/beast/utility/rngfill.h>
#include <xrpl/crypto/RFC1751.h>
#include <xrpl/crypto/csprng.h>
#include <xrpl/protocol/BuildInfo.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/MultiApiJson.h>
#include <xrpl/protocol/NFTSyntheticSerializer.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/resource/ResourceManager.h>

#include <boost/asio/ip/host_name.hpp>
#include <boost/asio/steady_timer.hpp>

#include <algorithm>
#include <exception>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>

namespace ripple {

class NetworkOPsImp final : public NetworkOPs
{
    /**
     * Transaction with input flags and results to be applied in batches.
     */

    class TransactionStatus
    {
    public:
        std::shared_ptr<Transaction> const transaction;
        bool const admin;
        bool const local;
        FailHard const failType;
        bool applied = false;
        TER result;

        TransactionStatus(
            std::shared_ptr<Transaction> t,
            bool a,
            bool l,
            FailHard f)
            : transaction(t), admin(a), local(l), failType(f)
        {
            XRPL_ASSERT(
                local || failType == FailHard::no,
                "ripple::NetworkOPsImp::TransactionStatus::TransactionStatus : "
                "valid inputs");
        }
    };

    /**
     * Synchronization states for transaction batches.
     */
    enum class DispatchState : unsigned char {
        none,
        scheduled,
        running,
    };

    static std::array<char const*, 5> const states_;

    /**
     * State accounting records two attributes for each possible server state:
     * 1) Amount of time spent in each state (in microseconds). This value is
     *    updated upon each state transition.
     * 2) Number of transitions to each state.
     *
     * This data can be polled through server_info and represented by
     * monitoring systems similarly to how bandwidth, CPU, and other
     * counter-based metrics are managed.
     *
     * State accounting is more accurate than periodic sampling of server
     * state. With periodic sampling, it is very likely that state transitions
     * are missed, and accuracy of time spent in each state is very rough.
     */
    class StateAccounting
    {
        struct Counters
        {
            explicit Counters() = default;

            std::uint64_t transitions = 0;
            std::chrono::microseconds dur = std::chrono::microseconds(0);
        };

        OperatingMode mode_ = OperatingMode::DISCONNECTED;
        std::array<Counters, 5> counters_;
        mutable std::mutex mutex_;
        std::chrono::steady_clock::time_point start_ =
            std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point const processStart_ = start_;
        std::uint64_t initialSyncUs_{0};
        static std::array<Json::StaticString const, 5> const states_;

    public:
        explicit StateAccounting()
        {
            counters_[static_cast<std::size_t>(OperatingMode::DISCONNECTED)]
                .transitions = 1;
        }

        /**
         * Record state transition. Update duration spent in previous
         * state.
         *
         * @param om New state.
         */
        void
        mode(OperatingMode om);

        /**
         * Output state counters in JSON format.
         *
         * @obj Json object to which to add state accounting data.
         */
        void
        json(Json::Value& obj) const;

        struct CounterData
        {
            decltype(counters_) counters;
            decltype(mode_) mode;
            decltype(start_) start;
            decltype(initialSyncUs_) initialSyncUs;
        };

        CounterData
        getCounterData() const
        {
            std::lock_guard lock(mutex_);
            return {counters_, mode_, start_, initialSyncUs_};
        }
    };

    //! Server fees published on `server` subscription
    struct ServerFeeSummary
    {
        ServerFeeSummary() = default;

        ServerFeeSummary(
            XRPAmount fee,
            TxQ::Metrics&& escalationMetrics,
            LoadFeeTrack const& loadFeeTrack);
        bool
        operator!=(ServerFeeSummary const& b) const;

        bool
        operator==(ServerFeeSummary const& b) const
        {
            return !(*this != b);
        }

        std::uint32_t loadFactorServer = 256;
        std::uint32_t loadBaseServer = 256;
        XRPAmount baseFee{10};
        std::optional<TxQ::Metrics> em = std::nullopt;
    };

public:
    NetworkOPsImp(
        Application& app,
        NetworkOPs::clock_type& clock,
        bool standalone,
        std::size_t minPeerCount,
        bool start_valid,
        JobQueue& job_queue,
        LedgerMaster& ledgerMaster,
        ValidatorKeys const& validatorKeys,
        boost::asio::io_service& io_svc,
        beast::Journal journal,
        beast::insight::Collector::ptr const& collector)
        : app_(app)
        , m_journal(journal)
        , m_localTX(make_LocalTxs())
        , mMode(start_valid ? OperatingMode::FULL : OperatingMode::DISCONNECTED)
        , heartbeatTimer_(io_svc)
        , clusterTimer_(io_svc)
        , accountHistoryTxTimer_(io_svc)
        , mConsensus(
              app,
              make_FeeVote(
                  setup_FeeVote(app_.config().section("voting")),
                  app_.logs().journal("FeeVote")),
              ledgerMaster,
              *m_localTX,
              app.getInboundTransactions(),
              beast::get_abstract_clock<std::chrono::steady_clock>(),
              validatorKeys,
              app_.logs().journal("LedgerConsensus"))
        , validatorPK_(
              validatorKeys.keys ? validatorKeys.keys->publicKey
                                 : decltype(validatorPK_){})
        , validatorMasterPK_(
              validatorKeys.keys ? validatorKeys.keys->masterPublicKey
                                 : decltype(validatorMasterPK_){})
        , m_ledgerMaster(ledgerMaster)
        , m_job_queue(job_queue)
        , m_standalone(standalone)
        , minPeerCount_(start_valid ? 0 : minPeerCount)
        , m_stats(std::bind(&NetworkOPsImp::collect_metrics, this), collector)
    {
    }

    ~NetworkOPsImp() override
    {
        // This clear() is necessary to ensure the shared_ptrs in this map get
        // destroyed NOW because the objects in this map invoke methods on this
        // class when they are destroyed
        mRpcSubMap.clear();
    }

public:
    OperatingMode
    getOperatingMode() const override;

    std::string
    strOperatingMode(OperatingMode const mode, bool const admin) const override;

    std::string
    strOperatingMode(bool const admin = false) const override;

    //
    // Transaction operations.
    //

    // Must complete immediately.
    void
    submitTransaction(std::shared_ptr<STTx const> const&) override;

    void
    processTransaction(
        std::shared_ptr<Transaction>& transaction,
        bool bUnlimited,
        bool bLocal,
        FailHard failType) override;

    void
    processTransactionSet(CanonicalTXSet const& set) override;

    /**
     * For transactions submitted directly by a client, apply batch of
     * transactions and wait for this transaction to complete.
     *
     * @param transaction Transaction object.
     * @param bUnliimited Whether a privileged client connection submitted it.
     * @param failType fail_hard setting from transaction submission.
     */
    void
    doTransactionSync(
        std::shared_ptr<Transaction> transaction,
        bool bUnlimited,
        FailHard failType);

    /**
     * For transactions not submitted by a locally connected client, fire and
     * forget. Add to batch and trigger it to be processed if there's no batch
     * currently being applied.
     *
     * @param transaction Transaction object
     * @param bUnlimited Whether a privileged client connection submitted it.
     * @param failType fail_hard setting from transaction submission.
     */
    void
    doTransactionAsync(
        std::shared_ptr<Transaction> transaction,
        bool bUnlimited,
        FailHard failtype);

private:
    bool
    preProcessTransaction(std::shared_ptr<Transaction>& transaction);

    void
    doTransactionSyncBatch(
        std::unique_lock<std::mutex>& lock,
        std::function<bool(std::unique_lock<std::mutex> const&)> retryCallback);

public:
    /**
     * Apply transactions in batches. Continue until none are queued.
     */
    void
    transactionBatch();

    /**
     * Attempt to apply transactions and post-process based on the results.
     *
     * @param Lock that protects the transaction batching
     */
    void
    apply(std::unique_lock<std::mutex>& batchLock);

    //
    // Owner functions.
    //

    Json::Value
    getOwnerInfo(
        std::shared_ptr<ReadView const> lpLedger,
        AccountID const& account) override;

    //
    // Book functions.
    //

    void
    getBookPage(
        std::shared_ptr<ReadView const>& lpLedger,
        Book const&,
        AccountID const& uTakerID,
        bool const bProof,
        unsigned int iLimit,
        Json::Value const& jvMarker,
        Json::Value& jvResult) override;

    // Ledger proposal/close functions.
    bool
    processTrustedProposal(RCLCxPeerPos proposal) override;

    bool
    recvValidation(
        std::shared_ptr<STValidation> const& val,
        std::string const& source) override;

    void
    mapComplete(std::shared_ptr<SHAMap> const& map, bool fromAcquire) override;

    // Network state machine.

    // Used for the "jump" case.
private:
    void
    switchLastClosedLedger(std::shared_ptr<Ledger const> const& newLCL);
    bool
    checkLastClosedLedger(Overlay::PeerSequence const&, uint256& networkClosed);

public:
    bool
    beginConsensus(
        uint256 const& networkClosed,
        std::unique_ptr<std::stringstream> const& clog) override;
    void
    endConsensus(std::unique_ptr<std::stringstream> const& clog) override;
    void
    setStandAlone() override;

    /** Called to initially start our timers.
        Not called for stand-alone mode.
    */
    void
    setStateTimer() override;

    void
    setNeedNetworkLedger() override;
    void
    clearNeedNetworkLedger() override;
    bool
    isNeedNetworkLedger() override;
    bool
    isFull() override;

    void
    setMode(OperatingMode om) override;

    bool
    isBlocked() override;
    bool
    isAmendmentBlocked() override;
    void
    setAmendmentBlocked() override;
    bool
    isAmendmentWarned() override;
    void
    setAmendmentWarned() override;
    void
    clearAmendmentWarned() override;
    bool
    isUNLBlocked() override;
    void
    setUNLBlocked() override;
    void
    clearUNLBlocked() override;
    void
    consensusViewChange() override;

    Json::Value
    getConsensusInfo() override;
    Json::Value
    getServerInfo(bool human, bool admin, bool counters) override;
    void
    clearLedgerFetch() override;
    Json::Value
    getLedgerFetchInfo() override;
    std::uint32_t
    acceptLedger(
        std::optional<std::chrono::milliseconds> consensusDelay) override;
    void
    reportFeeChange() override;
    void
    reportConsensusStateChange(ConsensusPhase phase);

    void
    updateLocalTx(ReadView const& view) override;
    std::size_t
    getLocalTxCount() override;

    //
    // Monitoring: publisher side.
    //
    void
    pubLedger(std::shared_ptr<ReadView const> const& lpAccepted) override;
    void
    pubProposedTransaction(
        std::shared_ptr<ReadView const> const& ledger,
        std::shared_ptr<STTx const> const& transaction,
        TER result) override;
    void
    pubValidation(std::shared_ptr<STValidation> const& val) override;

    //--------------------------------------------------------------------------
    //
    // InfoSub::Source.
    //
    void
    subAccount(
        InfoSub::ref ispListener,
        hash_set<AccountID> const& vnaAccountIDs,
        bool rt) override;
    void
    unsubAccount(
        InfoSub::ref ispListener,
        hash_set<AccountID> const& vnaAccountIDs,
        bool rt) override;

    // Just remove the subscription from the tracking
    // not from the InfoSub. Needed for InfoSub destruction
    void
    unsubAccountInternal(
        std::uint64_t seq,
        hash_set<AccountID> const& vnaAccountIDs,
        bool rt) override;

    error_code_i
    subAccountHistory(InfoSub::ref ispListener, AccountID const& account)
        override;
    void
    unsubAccountHistory(
        InfoSub::ref ispListener,
        AccountID const& account,
        bool historyOnly) override;

    void
    unsubAccountHistoryInternal(
        std::uint64_t seq,
        AccountID const& account,
        bool historyOnly) override;

    bool
    subLedger(InfoSub::ref ispListener, Json::Value& jvResult) override;
    bool
    unsubLedger(std::uint64_t uListener) override;

    bool
    subBookChanges(InfoSub::ref ispListener) override;
    bool
    unsubBookChanges(std::uint64_t uListener) override;

    bool
    subServer(InfoSub::ref ispListener, Json::Value& jvResult, bool admin)
        override;
    bool
    unsubServer(std::uint64_t uListener) override;

    bool
    subBook(InfoSub::ref ispListener, Book const&) override;
    bool
    unsubBook(std::uint64_t uListener, Book const&) override;

    bool
    subManifests(InfoSub::ref ispListener) override;
    bool
    unsubManifests(std::uint64_t uListener) override;
    void
    pubManifest(Manifest const&) override;

    bool
    subTransactions(InfoSub::ref ispListener) override;
    bool
    unsubTransactions(std::uint64_t uListener) override;

    bool
    subRTTransactions(InfoSub::ref ispListener) override;
    bool
    unsubRTTransactions(std::uint64_t uListener) override;

    bool
    subValidations(InfoSub::ref ispListener) override;
    bool
    unsubValidations(std::uint64_t uListener) override;

    bool
    subPeerStatus(InfoSub::ref ispListener) override;
    bool
    unsubPeerStatus(std::uint64_t uListener) override;
    void
    pubPeerStatus(std::function<Json::Value(void)> const&) override;

    bool
    subConsensus(InfoSub::ref ispListener) override;
    bool
    unsubConsensus(std::uint64_t uListener) override;

    InfoSub::pointer
    findRpcSub(std::string const& strUrl) override;
    InfoSub::pointer
    addRpcSub(std::string const& strUrl, InfoSub::ref) override;
    bool
    tryRemoveRpcSub(std::string const& strUrl) override;

    void
    stop() override
    {
        {
            boost::system::error_code ec;
            heartbeatTimer_.cancel(ec);
            if (ec)
            {
                JLOG(m_journal.error())
                    << "NetworkOPs: heartbeatTimer cancel error: "
                    << ec.message();
            }

            ec.clear();
            clusterTimer_.cancel(ec);
            if (ec)
            {
                JLOG(m_journal.error())
                    << "NetworkOPs: clusterTimer cancel error: "
                    << ec.message();
            }

            ec.clear();
            accountHistoryTxTimer_.cancel(ec);
            if (ec)
            {
                JLOG(m_journal.error())
                    << "NetworkOPs: accountHistoryTxTimer cancel error: "
                    << ec.message();
            }
        }
        // Make sure that any waitHandlers pending in our timers are done.
        using namespace std::chrono_literals;
        waitHandlerCounter_.join("NetworkOPs", 1s, m_journal);
    }

    void
    stateAccounting(Json::Value& obj) override;

private:
    void
    setTimer(
        boost::asio::steady_timer& timer,
        std::chrono::milliseconds const& expiry_time,
        std::function<void()> onExpire,
        std::function<void()> onError);
    void
    setHeartbeatTimer();
    void
    setClusterTimer();
    void
    processHeartbeatTimer();
    void
    processClusterTimer();

    MultiApiJson
    transJson(
        std::shared_ptr<STTx const> const& transaction,
        TER result,
        bool validated,
        std::shared_ptr<ReadView const> const& ledger,
        std::optional<std::reference_wrapper<TxMeta const>> meta);

    void
    pubValidatedTransaction(
        std::shared_ptr<ReadView const> const& ledger,
        AcceptedLedgerTx const& transaction,
        bool last);

    void
    pubAccountTransaction(
        std::shared_ptr<ReadView const> const& ledger,
        AcceptedLedgerTx const& transaction,
        bool last);

    void
    pubProposedAccountTransaction(
        std::shared_ptr<ReadView const> const& ledger,
        std::shared_ptr<STTx const> const& transaction,
        TER result);

    void
    pubServer();
    void
    pubConsensus(ConsensusPhase phase);

    std::string
    getHostId(bool forAdmin);

private:
    using SubMapType = hash_map<std::uint64_t, InfoSub::wptr>;
    using SubInfoMapType = hash_map<AccountID, SubMapType>;
    using subRpcMapType = hash_map<std::string, InfoSub::pointer>;

    /*
     * With a validated ledger to separate history and future, the node
     * streams historical txns with negative indexes starting from -1,
     * and streams future txns starting from index 0.
     * The SubAccountHistoryIndex struct maintains these indexes.
     * It also has a flag stopHistorical_ for stopping streaming
     * the historical txns.
     */
    struct SubAccountHistoryIndex
    {
        AccountID const accountId_;
        // forward
        std::uint32_t forwardTxIndex_;
        // separate backward and forward
        std::uint32_t separationLedgerSeq_;
        // history, backward
        std::uint32_t historyLastLedgerSeq_;
        std::int32_t historyTxIndex_;
        bool haveHistorical_;
        std::atomic<bool> stopHistorical_;

        SubAccountHistoryIndex(AccountID const& accountId)
            : accountId_(accountId)
            , forwardTxIndex_(0)
            , separationLedgerSeq_(0)
            , historyLastLedgerSeq_(0)
            , historyTxIndex_(-1)
            , haveHistorical_(false)
            , stopHistorical_(false)
        {
        }
    };
    struct SubAccountHistoryInfo
    {
        InfoSub::pointer sink_;
        std::shared_ptr<SubAccountHistoryIndex> index_;
    };
    struct SubAccountHistoryInfoWeak
    {
        InfoSub::wptr sinkWptr_;
        std::shared_ptr<SubAccountHistoryIndex> index_;
    };
    using SubAccountHistoryMapType =
        hash_map<AccountID, hash_map<std::uint64_t, SubAccountHistoryInfoWeak>>;

    /**
     * @note called while holding mSubLock
     */
    void
    subAccountHistoryStart(
        std::shared_ptr<ReadView const> const& ledger,
        SubAccountHistoryInfoWeak& subInfo);
    void
    addAccountHistoryJob(SubAccountHistoryInfoWeak subInfo);
    void
    setAccountHistoryJobTimer(SubAccountHistoryInfoWeak subInfo);

    Application& app_;
    beast::Journal m_journal;

    std::unique_ptr<LocalTxs> m_localTX;

    std::recursive_mutex mSubLock;

    std::atomic<OperatingMode> mMode;

    std::atomic<bool> needNetworkLedger_{false};
    std::atomic<bool> amendmentBlocked_{false};
    std::atomic<bool> amendmentWarned_{false};
    std::atomic<bool> unlBlocked_{false};

    ClosureCounter<void, boost::system::error_code const&> waitHandlerCounter_;
    boost::asio::steady_timer heartbeatTimer_;
    boost::asio::steady_timer clusterTimer_;
    boost::asio::steady_timer accountHistoryTxTimer_;

    RCLConsensus mConsensus;

    std::optional<PublicKey> const validatorPK_;
    std::optional<PublicKey> const validatorMasterPK_;

    ConsensusPhase mLastConsensusPhase;

    LedgerMaster& m_ledgerMaster;

    SubInfoMapType mSubAccount;
    SubInfoMapType mSubRTAccount;

    subRpcMapType mRpcSubMap;

    SubAccountHistoryMapType mSubAccountHistory;

    enum SubTypes {
        sLedger,          // Accepted ledgers.
        sManifests,       // Received validator manifests.
        sServer,          // When server changes connectivity state.
        sTransactions,    // All accepted transactions.
        sRTTransactions,  // All proposed and accepted transactions.
        sValidations,     // Received validations.
        sPeerStatus,      // Peer status changes.
        sConsensusPhase,  // Consensus phase
        sBookChanges,     // Per-ledger order book changes
        sLastEntry        // Any new entry must be ADDED ABOVE this one
    };

    std::array<SubMapType, SubTypes::sLastEntry> mStreamMaps;

    ServerFeeSummary mLastFeeSummary;

    JobQueue& m_job_queue;

    // Whether we are in standalone mode.
    bool const m_standalone;

    // The number of nodes that we need to consider ourselves connected.
    std::size_t const minPeerCount_;

    // Transaction batching.
    std::condition_variable mCond;
    std::mutex mMutex;
    DispatchState mDispatchState = DispatchState::none;
    std::vector<TransactionStatus> mTransactions;

    StateAccounting accounting_{};

    std::set<uint256> pendingValidations_;
    std::mutex validationsMutex_;

private:
    struct Stats
    {
        template <class Handler>
        Stats(
            Handler const& handler,
            beast::insight::Collector::ptr const& collector)
            : hook(collector->make_hook(handler))
            , disconnected_duration(collector->make_gauge(
                  "State_Accounting",
                  "Disconnected_duration"))
            , connected_duration(collector->make_gauge(
                  "State_Accounting",
                  "Connected_duration"))
            , syncing_duration(
                  collector->make_gauge("State_Accounting", "Syncing_duration"))
            , tracking_duration(collector->make_gauge(
                  "State_Accounting",
                  "Tracking_duration"))
            , full_duration(
                  collector->make_gauge("State_Accounting", "Full_duration"))
            , disconnected_transitions(collector->make_gauge(
                  "State_Accounting",
                  "Disconnected_transitions"))
            , connected_transitions(collector->make_gauge(
                  "State_Accounting",
                  "Connected_transitions"))
            , syncing_transitions(collector->make_gauge(
                  "State_Accounting",
                  "Syncing_transitions"))
            , tracking_transitions(collector->make_gauge(
                  "State_Accounting",
                  "Tracking_transitions"))
            , full_transitions(
                  collector->make_gauge("State_Accounting", "Full_transitions"))
        {
        }

        beast::insight::Hook hook;
        beast::insight::Gauge disconnected_duration;
        beast::insight::Gauge connected_duration;
        beast::insight::Gauge syncing_duration;
        beast::insight::Gauge tracking_duration;
        beast::insight::Gauge full_duration;

        beast::insight::Gauge disconnected_transitions;
        beast::insight::Gauge connected_transitions;
        beast::insight::Gauge syncing_transitions;
        beast::insight::Gauge tracking_transitions;
        beast::insight::Gauge full_transitions;
    };

    std::mutex m_statsMutex;  // Mutex to lock m_stats
    Stats m_stats;

private:
    void
    collect_metrics();
};

//------------------------------------------------------------------------------

static std::array<char const*, 5> const stateNames{
    {"disconnected", "connected", "syncing", "tracking", "full"}};

std::array<char const*, 5> const NetworkOPsImp::states_ = stateNames;

std::array<Json::StaticString const, 5> const
    NetworkOPsImp::StateAccounting::states_ = {
        {Json::StaticString(stateNames[0]),
         Json::StaticString(stateNames[1]),
         Json::StaticString(stateNames[2]),
         Json::StaticString(stateNames[3]),
         Json::StaticString(stateNames[4])}};

static auto const genesisAccountId = calcAccountID(
    generateKeyPair(KeyType::secp256k1, generateSeed("masterpassphrase"))
        .first);

//------------------------------------------------------------------------------
inline OperatingMode
NetworkOPsImp::getOperatingMode() const
{
    return mMode;
}

inline std::string
NetworkOPsImp::strOperatingMode(bool const admin /* = false */) const
{
    return strOperatingMode(mMode, admin);
}

inline void
NetworkOPsImp::setStandAlone()
{
    setMode(OperatingMode::FULL);
}

inline void
NetworkOPsImp::setNeedNetworkLedger()
{
    needNetworkLedger_ = true;
}

inline void
NetworkOPsImp::clearNeedNetworkLedger()
{
    needNetworkLedger_ = false;
}

inline bool
NetworkOPsImp::isNeedNetworkLedger()
{
    return needNetworkLedger_;
}

inline bool
NetworkOPsImp::isFull()
{
    return !needNetworkLedger_ && (mMode == OperatingMode::FULL);
}

std::string
NetworkOPsImp::getHostId(bool forAdmin)
{
    static std::string const hostname = boost::asio::ip::host_name();

    if (forAdmin)
        return hostname;

    // For non-admin uses hash the node public key into a
    // single RFC1751 word:
    static std::string const shroudedHostId = [this]() {
        auto const& id = app_.nodeIdentity();

        return RFC1751::getWordFromBlob(id.first.data(), id.first.size());
    }();

    return shroudedHostId;
}

void
NetworkOPsImp::setStateTimer()
{
    setHeartbeatTimer();

    // Only do this work if a cluster is configured
    if (app_.cluster().size() != 0)
        setClusterTimer();
}

void
NetworkOPsImp::setTimer(
    boost::asio::steady_timer& timer,
    std::chrono::milliseconds const& expiry_time,
    std::function<void()> onExpire,
    std::function<void()> onError)
{
    // Only start the timer if waitHandlerCounter_ is not yet joined.
    if (auto optionalCountedHandler = waitHandlerCounter_.wrap(
            [this, onExpire, onError](boost::system::error_code const& e) {
                if ((e.value() == boost::system::errc::success) &&
                    (!m_job_queue.isStopped()))
                {
                    onExpire();
                }
                // Recover as best we can if an unexpected error occurs.
                if (e.value() != boost::system::errc::success &&
                    e.value() != boost::asio::error::operation_aborted)
                {
                    // Try again later and hope for the best.
                    JLOG(m_journal.error())
                        << "Timer got error '" << e.message()
                        << "'.  Restarting timer.";
                    onError();
                }
            }))
    {
        timer.expires_from_now(expiry_time);
        timer.async_wait(std::move(*optionalCountedHandler));
    }
}

void
NetworkOPsImp::setHeartbeatTimer()
{
    setTimer(
        heartbeatTimer_,
        mConsensus.parms().ledgerGRANULARITY,
        [this]() {
            m_job_queue.addJob(jtNETOP_TIMER, "NetOPs.heartbeat", [this]() {
                processHeartbeatTimer();
            });
        },
        [this]() { setHeartbeatTimer(); });
}

void
NetworkOPsImp::setClusterTimer()
{
    using namespace std::chrono_literals;

    setTimer(
        clusterTimer_,
        10s,
        [this]() {
            m_job_queue.addJob(jtNETOP_CLUSTER, "NetOPs.cluster", [this]() {
                processClusterTimer();
            });
        },
        [this]() { setClusterTimer(); });
}

void
NetworkOPsImp::setAccountHistoryJobTimer(SubAccountHistoryInfoWeak subInfo)
{
    JLOG(m_journal.debug()) << "Scheduling AccountHistory job for account "
                            << toBase58(subInfo.index_->accountId_);
    using namespace std::chrono_literals;
    setTimer(
        accountHistoryTxTimer_,
        4s,
        [this, subInfo]() { addAccountHistoryJob(subInfo); },
        [this, subInfo]() { setAccountHistoryJobTimer(subInfo); });
}

void
NetworkOPsImp::processHeartbeatTimer()
{
    RclConsensusLogger clog(
        "Heartbeat Timer", mConsensus.validating(), m_journal);
    {
        std::unique_lock lock{app_.getMasterMutex()};

        // VFALCO NOTE This is for diagnosing a crash on exit
        LoadManager& mgr(app_.getLoadManager());
        mgr.heartbeat();

        std::size_t const numPeers = app_.overlay().size();

        // do we have sufficient peers? If not, we are disconnected.
        if (numPeers < minPeerCount_)
        {
            if (mMode != OperatingMode::DISCONNECTED)
            {
                setMode(OperatingMode::DISCONNECTED);
                std::stringstream ss;
                ss << "Node count (" << numPeers << ") has fallen "
                   << "below required minimum (" << minPeerCount_ << ").";
                JLOG(m_journal.warn()) << ss.str();
                CLOG(clog.ss()) << "set mode to DISCONNECTED: " << ss.str();
            }
            else
            {
                CLOG(clog.ss())
                    << "already DISCONNECTED. too few peers (" << numPeers
                    << "), need at least " << minPeerCount_;
            }

            // MasterMutex lock need not be held to call setHeartbeatTimer()
            lock.unlock();
            // We do not call mConsensus.timerEntry until there are enough
            // peers providing meaningful inputs to consensus
            setHeartbeatTimer();

            return;
        }

        if (mMode == OperatingMode::DISCONNECTED)
        {
            setMode(OperatingMode::CONNECTED);
            JLOG(m_journal.info())
                << "Node count (" << numPeers << ") is sufficient.";
            CLOG(clog.ss()) << "setting mode to CONNECTED based on " << numPeers
                            << " peers. ";
        }

        // Check if the last validated ledger forces a change between these
        // states.
        auto origMode = mMode.load();
        CLOG(clog.ss()) << "mode: " << strOperatingMode(origMode, true);
        if (mMode == OperatingMode::SYNCING)
            setMode(OperatingMode::SYNCING);
        else if (mMode == OperatingMode::CONNECTED)
            setMode(OperatingMode::CONNECTED);
        auto newMode = mMode.load();
        if (origMode != newMode)
        {
            CLOG(clog.ss())
                << ", changing to " << strOperatingMode(newMode, true);
        }
        CLOG(clog.ss()) << ". ";
    }

    mConsensus.timerEntry(app_.timeKeeper().closeTime(), clog.ss());

    CLOG(clog.ss()) << "consensus phase " << to_string(mLastConsensusPhase);
    ConsensusPhase const currPhase = mConsensus.phase();
    if (mLastConsensusPhase != currPhase)
    {
        reportConsensusStateChange(currPhase);
        mLastConsensusPhase = currPhase;
        CLOG(clog.ss()) << " changed to " << to_string(mLastConsensusPhase);
    }
    CLOG(clog.ss()) << ". ";

    setHeartbeatTimer();
}

void
NetworkOPsImp::processClusterTimer()
{
    if (app_.cluster().size() == 0)
        return;

    using namespace std::chrono_literals;

    bool const update = app_.cluster().update(
        app_.nodeIdentity().first,
        "",
        (m_ledgerMaster.getValidatedLedgerAge() <= 4min)
            ? app_.getFeeTrack().getLocalFee()
            : 0,
        app_.timeKeeper().now());

    if (!update)
    {
        JLOG(m_journal.debug()) << "Too soon to send cluster update";
        setClusterTimer();
        return;
    }

    protocol::TMCluster cluster;
    app_.cluster().for_each([&cluster](ClusterNode const& node) {
        protocol::TMClusterNode& n = *cluster.add_clusternodes();
        n.set_publickey(toBase58(TokenType::NodePublic, node.identity()));
        n.set_reporttime(node.getReportTime().time_since_epoch().count());
        n.set_nodeload(node.getLoadFee());
        if (!node.name().empty())
            n.set_nodename(node.name());
    });

    Resource::Gossip gossip = app_.getResourceManager().exportConsumers();
    for (auto& item : gossip.items)
    {
        protocol::TMLoadSource& node = *cluster.add_loadsources();
        node.set_name(to_string(item.address));
        node.set_cost(item.balance);
    }
    app_.overlay().foreach(send_if(
        std::make_shared<Message>(cluster, protocol::mtCLUSTER),
        peer_in_cluster()));
    setClusterTimer();
}

//------------------------------------------------------------------------------

std::string
NetworkOPsImp::strOperatingMode(OperatingMode const mode, bool const admin)
    const
{
    if (mode == OperatingMode::FULL && admin)
    {
        auto const consensusMode = mConsensus.mode();
        if (consensusMode != ConsensusMode::wrongLedger)
        {
            if (consensusMode == ConsensusMode::proposing)
                return "proposing";

            if (mConsensus.validating())
                return "validating";
        }
    }

    return states_[static_cast<std::size_t>(mode)];
}

void
NetworkOPsImp::submitTransaction(std::shared_ptr<STTx const> const& iTrans)
{
    if (isNeedNetworkLedger())
    {
        // Nothing we can do if we've never been in sync
        return;
    }

    // Enforce Network bar for batch txn
    if (iTrans->isFlag(tfInnerBatchTxn) &&
        m_ledgerMaster.getValidatedRules().enabled(featureBatch))
    {
        JLOG(m_journal.error())
            << "Submitted transaction invalid: tfInnerBatchTxn flag present.";
        return;
    }

    // this is an asynchronous interface
    auto const trans = sterilize(*iTrans);

    auto const txid = trans->getTransactionID();
    auto const flags = app_.getHashRouter().getFlags(txid);

    if ((flags & SF_BAD) != 0)
    {
        JLOG(m_journal.warn()) << "Submitted transaction cached bad";
        return;
    }

    try
    {
        auto const [validity, reason] = checkValidity(
            app_.getHashRouter(),
            *trans,
            m_ledgerMaster.getValidatedRules(),
            app_.config());

        if (validity != Validity::Valid)
        {
            JLOG(m_journal.warn())
                << "Submitted transaction invalid: " << reason;
            return;
        }
    }
    catch (std::exception const& ex)
    {
        JLOG(m_journal.warn())
            << "Exception checking transaction " << txid << ": " << ex.what();

        return;
    }

    std::string reason;

    auto tx = std::make_shared<Transaction>(trans, reason, app_);

    m_job_queue.addJob(jtTRANSACTION, "submitTxn", [this, tx]() {
        auto t = tx;
        processTransaction(t, false, false, FailHard::no);
    });
}

bool
NetworkOPsImp::preProcessTransaction(std::shared_ptr<Transaction>& transaction)
{
    auto const newFlags = app_.getHashRouter().getFlags(transaction->getID());

    if ((newFlags & SF_BAD) != 0)
    {
        // cached bad
        JLOG(m_journal.warn()) << transaction->getID() << ": cached bad!\n";
        transaction->setStatus(INVALID);
        transaction->setResult(temBAD_SIGNATURE);
        return false;
    }

    auto const view = m_ledgerMaster.getCurrentLedger();

    // This function is called by several different parts of the codebase
    // under no circumstances will we ever accept an inner txn within a batch
    // txn from the network.
    auto const sttx = *transaction->getSTransaction();
    if (sttx.isFlag(tfInnerBatchTxn) && view->rules().enabled(featureBatch))
    {
        transaction->setStatus(INVALID);
        transaction->setResult(temINVALID_FLAG);
        app_.getHashRouter().setFlags(transaction->getID(), SF_BAD);
        return false;
    }

    // NOTE eahennis - I think this check is redundant,
    // but I'm not 100% sure yet.
    // If so, only cost is looking up HashRouter flags.
    auto const [validity, reason] =
        checkValidity(app_.getHashRouter(), sttx, view->rules(), app_.config());
    XRPL_ASSERT(
        validity == Validity::Valid,
        "ripple::NetworkOPsImp::processTransaction : valid validity");

    // Not concerned with local checks at this point.
    if (validity == Validity::SigBad)
    {
        JLOG(m_journal.info()) << "Transaction has bad signature: " << reason;
        transaction->setStatus(INVALID);
        transaction->setResult(temBAD_SIGNATURE);
        app_.getHashRouter().setFlags(transaction->getID(), SF_BAD);
        return false;
    }

    // canonicalize can change our pointer
    app_.getMasterTransaction().canonicalize(&transaction);

    return true;
}

void
NetworkOPsImp::processTransaction(
    std::shared_ptr<Transaction>& transaction,
    bool bUnlimited,
    bool bLocal,
    FailHard failType)
{
    auto ev = m_job_queue.makeLoadEvent(jtTXN_PROC, "ProcessTXN");

    // preProcessTransaction can change our pointer
    if (!preProcessTransaction(transaction))
        return;

    if (bLocal)
        doTransactionSync(transaction, bUnlimited, failType);
    else
        doTransactionAsync(transaction, bUnlimited, failType);
}

void
NetworkOPsImp::doTransactionAsync(
    std::shared_ptr<Transaction> transaction,
    bool bUnlimited,
    FailHard failType)
{
    std::lock_guard lock(mMutex);

    if (transaction->getApplying())
        return;

    mTransactions.push_back(
        TransactionStatus(transaction, bUnlimited, false, failType));
    transaction->setApplying();

    if (mDispatchState == DispatchState::none)
    {
        if (m_job_queue.addJob(
                jtBATCH, "transactionBatch", [this]() { transactionBatch(); }))
        {
            mDispatchState = DispatchState::scheduled;
        }
    }
}

void
NetworkOPsImp::doTransactionSync(
    std::shared_ptr<Transaction> transaction,
    bool bUnlimited,
    FailHard failType)
{
    std::unique_lock<std::mutex> lock(mMutex);

    if (!transaction->getApplying())
    {
        mTransactions.push_back(
            TransactionStatus(transaction, bUnlimited, true, failType));
        transaction->setApplying();
    }

    doTransactionSyncBatch(
        lock, [&transaction](std::unique_lock<std::mutex> const&) {
            return transaction->getApplying();
        });
}

void
NetworkOPsImp::doTransactionSyncBatch(
    std::unique_lock<std::mutex>& lock,
    std::function<bool(std::unique_lock<std::mutex> const&)> retryCallback)
{
    do
    {
        if (mDispatchState == DispatchState::running)
        {
            // A batch processing job is already running, so wait.
            mCond.wait(lock);
        }
        else
        {
            apply(lock);

            if (mTransactions.size())
            {
                // More transactions need to be applied, but by another job.
                if (m_job_queue.addJob(jtBATCH, "transactionBatch", [this]() {
                        transactionBatch();
                    }))
                {
                    mDispatchState = DispatchState::scheduled;
                }
            }
        }
    } while (retryCallback(lock));
}

void
NetworkOPsImp::processTransactionSet(CanonicalTXSet const& set)
{
    auto ev = m_job_queue.makeLoadEvent(jtTXN_PROC, "ProcessTXNSet");
    std::vector<std::shared_ptr<Transaction>> candidates;
    candidates.reserve(set.size());
    for (auto const& [_, tx] : set)
    {
        std::string reason;
        auto transaction = std::make_shared<Transaction>(tx, reason, app_);

        if (transaction->getStatus() == INVALID)
        {
            if (!reason.empty())
            {
                JLOG(m_journal.trace())
                    << "Exception checking transaction: " << reason;
            }
            app_.getHashRouter().setFlags(tx->getTransactionID(), SF_BAD);
            continue;
        }

        // preProcessTransaction can change our pointer
        if (!preProcessTransaction(transaction))
            continue;

        candidates.emplace_back(transaction);
    }

    std::vector<TransactionStatus> transactions;
    transactions.reserve(candidates.size());

    std::unique_lock lock(mMutex);

    for (auto& transaction : candidates)
    {
        if (!transaction->getApplying())
        {
            transactions.emplace_back(transaction, false, false, FailHard::no);
            transaction->setApplying();
        }
    }

    if (mTransactions.empty())
        mTransactions.swap(transactions);
    else
    {
        mTransactions.reserve(mTransactions.size() + transactions.size());
        for (auto& t : transactions)
            mTransactions.push_back(std::move(t));
    }

    doTransactionSyncBatch(lock, [&](std::unique_lock<std::mutex> const&) {
        XRPL_ASSERT(
            lock.owns_lock(),
            "ripple::NetworkOPsImp::processTransactionSet has lock");
        return std::any_of(
            mTransactions.begin(), mTransactions.end(), [](auto const& t) {
                return t.transaction->getApplying();
            });
    });
}

void
NetworkOPsImp::transactionBatch()
{
    std::unique_lock<std::mutex> lock(mMutex);

    if (mDispatchState == DispatchState::running)
        return;

    while (mTransactions.size())
    {
        apply(lock);
    }
}

void
NetworkOPsImp::apply(std::unique_lock<std::mutex>& batchLock)
{
    std::vector<TransactionStatus> submit_held;
    std::vector<TransactionStatus> transactions;
    mTransactions.swap(transactions);
    XRPL_ASSERT(
        !transactions.empty(),
        "ripple::NetworkOPsImp::apply : non-empty transactions");
    XRPL_ASSERT(
        mDispatchState != DispatchState::running,
        "ripple::NetworkOPsImp::apply : is not running");

    mDispatchState = DispatchState::running;

    batchLock.unlock();

    {
        std::unique_lock masterLock{app_.getMasterMutex(), std::defer_lock};
        bool changed = false;
        {
            std::unique_lock ledgerLock{
                m_ledgerMaster.peekMutex(), std::defer_lock};
            std::lock(masterLock, ledgerLock);

            app_.openLedger().modify([&](OpenView& view, beast::Journal j) {
                for (TransactionStatus& e : transactions)
                {
                    // we check before adding to the batch
                    ApplyFlags flags = tapNONE;
                    if (e.admin)
                        flags |= tapUNLIMITED;

                    if (e.failType == FailHard::yes)
                        flags |= tapFAIL_HARD;

                    auto const result = app_.getTxQ().apply(
                        app_, view, e.transaction->getSTransaction(), flags, j);
                    e.result = result.ter;
                    e.applied = result.applied;
                    changed = changed || result.applied;
                }
                return changed;
            });
        }
        if (changed)
            reportFeeChange();

        std::optional<LedgerIndex> validatedLedgerIndex;
        if (auto const l = m_ledgerMaster.getValidatedLedger())
            validatedLedgerIndex = l->info().seq;

        auto newOL = app_.openLedger().current();
        for (TransactionStatus& e : transactions)
        {
            e.transaction->clearSubmitResult();

            if (e.applied)
            {
                pubProposedTransaction(
                    newOL, e.transaction->getSTransaction(), e.result);
                e.transaction->setApplied();
            }

            e.transaction->setResult(e.result);

            if (isTemMalformed(e.result))
                app_.getHashRouter().setFlags(e.transaction->getID(), SF_BAD);

#ifdef DEBUG
            if (e.result != tesSUCCESS)
            {
                std::string token, human;

                if (transResultInfo(e.result, token, human))
                {
                    JLOG(m_journal.info())
                        << "TransactionResult: " << token << ": " << human;
                }
            }
#endif

            bool addLocal = e.local;

            if (e.result == tesSUCCESS)
            {
                JLOG(m_journal.debug())
                    << "Transaction is now included in open ledger";
                e.transaction->setStatus(INCLUDED);

                // Pop as many "reasonable" transactions for this account as
                // possible. "Reasonable" means they have sequential sequence
                // numbers, or use tickets.
                auto const& txCur = e.transaction->getSTransaction();

                std::size_t count = 0;
                for (auto txNext = m_ledgerMaster.popAcctTransaction(txCur);
                     txNext && count < maxPoppedTransactions;
                     txNext = m_ledgerMaster.popAcctTransaction(txCur), ++count)
                {
                    if (!batchLock.owns_lock())
                        batchLock.lock();
                    std::string reason;
                    auto const trans = sterilize(*txNext);
                    auto t = std::make_shared<Transaction>(trans, reason, app_);
                    if (t->getApplying())
                        break;
                    submit_held.emplace_back(t, false, false, FailHard::no);
                    t->setApplying();
                }
                if (batchLock.owns_lock())
                    batchLock.unlock();
            }
            else if (e.result == tefPAST_SEQ)
            {
                // duplicate or conflict
                JLOG(m_journal.info()) << "Transaction is obsolete";
                e.transaction->setStatus(OBSOLETE);
            }
            else if (e.result == terQUEUED)
            {
                JLOG(m_journal.debug())
                    << "Transaction is likely to claim a"
                    << " fee, but is queued until fee drops";

                e.transaction->setStatus(HELD);
                // Add to held transactions, because it could get
                // kicked out of the queue, and this will try to
                // put it back.
                m_ledgerMaster.addHeldTransaction(e.transaction);
                e.transaction->setQueued();
                e.transaction->setKept();
            }
            else if (
                isTerRetry(e.result) || isTelLocal(e.result) ||
                isTefFailure(e.result))
            {
                if (e.failType != FailHard::yes)
                {
                    auto const lastLedgerSeq =
                        e.transaction->getSTransaction()->at(
                            ~sfLastLedgerSequence);
                    auto const ledgersLeft = lastLedgerSeq
                        ? *lastLedgerSeq -
                            m_ledgerMaster.getCurrentLedgerIndex()
                        : std::optional<LedgerIndex>{};
                    // If any of these conditions are met, the transaction can
                    // be held:
                    // 1. It was submitted locally. (Note that this flag is only
                    //    true on the initial submission.)
                    // 2. The transaction has a LastLedgerSequence, and the
                    //    LastLedgerSequence is fewer than LocalTxs::holdLedgers
                    //    (5) ledgers into the future. (Remember that an
                    //    unseated optional compares as less than all seated
                    //    values, so it has to be checked explicitly first.)
                    // 3. The SF_HELD flag is not set on the txID. (setFlags
                    //    checks before setting. If the flag is set, it returns
                    //    false, which means it's been held once without one of
                    //    the other conditions, so don't hold it again. Time's
                    //    up!)
                    //
                    if (e.local ||
                        (ledgersLeft && ledgersLeft <= LocalTxs::holdLedgers) ||
                        app_.getHashRouter().setFlags(
                            e.transaction->getID(), SF_HELD))
                    {
                        // transaction should be held
                        JLOG(m_journal.debug())
                            << "Transaction should be held: " << e.result;
                        e.transaction->setStatus(HELD);
                        m_ledgerMaster.addHeldTransaction(e.transaction);
                        e.transaction->setKept();
                    }
                    else
                        JLOG(m_journal.debug())
                            << "Not holding transaction "
                            << e.transaction->getID() << ": "
                            << (e.local ? "local" : "network") << ", "
                            << "result: " << e.result << " ledgers left: "
                            << (ledgersLeft ? to_string(*ledgersLeft)
                                            : "unspecified");
                }
            }
            else
            {
                JLOG(m_journal.debug())
                    << "Status other than success " << e.result;
                e.transaction->setStatus(INVALID);
            }

            auto const enforceFailHard =
                e.failType == FailHard::yes && !isTesSuccess(e.result);

            if (addLocal && !enforceFailHard)
            {
                m_localTX->push_back(
                    m_ledgerMaster.getCurrentLedgerIndex(),
                    e.transaction->getSTransaction());
                e.transaction->setKept();
            }

            if ((e.applied ||
                 ((mMode != OperatingMode::FULL) &&
                  (e.failType != FailHard::yes) && e.local) ||
                 (e.result == terQUEUED)) &&
                !enforceFailHard)
            {
                auto const toSkip =
                    app_.getHashRouter().shouldRelay(e.transaction->getID());
                if (auto const sttx = *(e.transaction->getSTransaction());
                    toSkip &&
                    // Skip relaying if it's an inner batch txn and batch
                    // feature is enabled
                    !(sttx.isFlag(tfInnerBatchTxn) &&
                      newOL->rules().enabled(featureBatch)))
                {
                    protocol::TMTransaction tx;
                    Serializer s;

                    sttx.add(s);
                    tx.set_rawtransaction(s.data(), s.size());
                    tx.set_status(protocol::tsCURRENT);
                    tx.set_receivetimestamp(
                        app_.timeKeeper().now().time_since_epoch().count());
                    tx.set_deferred(e.result == terQUEUED);
                    // FIXME: This should be when we received it
                    app_.overlay().relay(e.transaction->getID(), tx, *toSkip);
                    e.transaction->setBroadcast();
                }
            }

            if (validatedLedgerIndex)
            {
                auto [fee, accountSeq, availableSeq] =
                    app_.getTxQ().getTxRequiredFeeAndSeq(
                        *newOL, e.transaction->getSTransaction());
                e.transaction->setCurrentLedgerState(
                    *validatedLedgerIndex, fee, accountSeq, availableSeq);
            }
        }
    }

    batchLock.lock();

    for (TransactionStatus& e : transactions)
        e.transaction->clearApplying();

    if (!submit_held.empty())
    {
        if (mTransactions.empty())
            mTransactions.swap(submit_held);
        else
        {
            mTransactions.reserve(mTransactions.size() + submit_held.size());
            for (auto& e : submit_held)
                mTransactions.push_back(std::move(e));
        }
    }

    mCond.notify_all();

    mDispatchState = DispatchState::none;
}

//
// Owner functions
//

Json::Value
NetworkOPsImp::getOwnerInfo(
    std::shared_ptr<ReadView const> lpLedger,
    AccountID const& account)
{
    Json::Value jvObjects(Json::objectValue);
    auto root = keylet::ownerDir(account);
    auto sleNode = lpLedger->read(keylet::page(root));
    if (sleNode)
    {
        std::uint64_t uNodeDir;

        do
        {
            for (auto const& uDirEntry : sleNode->getFieldV256(sfIndexes))
            {
                auto sleCur = lpLedger->read(keylet::child(uDirEntry));
                XRPL_ASSERT(
                    sleCur,
                    "ripple::NetworkOPsImp::getOwnerInfo : non-null child SLE");

                switch (sleCur->getType())
                {
                    case ltOFFER:
                        if (!jvObjects.isMember(jss::offers))
                            jvObjects[jss::offers] =
                                Json::Value(Json::arrayValue);

                        jvObjects[jss::offers].append(
                            sleCur->getJson(JsonOptions::none));
                        break;

                    case ltRIPPLE_STATE:
                        if (!jvObjects.isMember(jss::ripple_lines))
                        {
                            jvObjects[jss::ripple_lines] =
                                Json::Value(Json::arrayValue);
                        }

                        jvObjects[jss::ripple_lines].append(
                            sleCur->getJson(JsonOptions::none));
                        break;

                    case ltACCOUNT_ROOT:
                    case ltDIR_NODE:
                    default:
                        UNREACHABLE(
                            "ripple::NetworkOPsImp::getOwnerInfo : invalid "
                            "type");
                        break;
                }
            }

            uNodeDir = sleNode->getFieldU64(sfIndexNext);

            if (uNodeDir)
            {
                sleNode = lpLedger->read(keylet::page(root, uNodeDir));
                XRPL_ASSERT(
                    sleNode,
                    "ripple::NetworkOPsImp::getOwnerInfo : read next page");
            }
        } while (uNodeDir);
    }

    return jvObjects;
}

//
// Other
//

inline bool
NetworkOPsImp::isBlocked()
{
    return isAmendmentBlocked() || isUNLBlocked();
}

inline bool
NetworkOPsImp::isAmendmentBlocked()
{
    return amendmentBlocked_;
}

void
NetworkOPsImp::setAmendmentBlocked()
{
    amendmentBlocked_ = true;
    setMode(OperatingMode::CONNECTED);
}

inline bool
NetworkOPsImp::isAmendmentWarned()
{
    return !amendmentBlocked_ && amendmentWarned_;
}

inline void
NetworkOPsImp::setAmendmentWarned()
{
    amendmentWarned_ = true;
}

inline void
NetworkOPsImp::clearAmendmentWarned()
{
    amendmentWarned_ = false;
}

inline bool
NetworkOPsImp::isUNLBlocked()
{
    return unlBlocked_;
}

void
NetworkOPsImp::setUNLBlocked()
{
    unlBlocked_ = true;
    setMode(OperatingMode::CONNECTED);
}

inline void
NetworkOPsImp::clearUNLBlocked()
{
    unlBlocked_ = false;
}

bool
NetworkOPsImp::checkLastClosedLedger(
    Overlay::PeerSequence const& peerList,
    uint256& networkClosed)
{
    // Returns true if there's an *abnormal* ledger issue, normal changing in
    // TRACKING mode should return false.  Do we have sufficient validations for
    // our last closed ledger? Or do sufficient nodes agree? And do we have no
    // better ledger available?  If so, we are either tracking or full.

    JLOG(m_journal.trace()) << "NetworkOPsImp::checkLastClosedLedger";

    auto const ourClosed = m_ledgerMaster.getClosedLedger();

    if (!ourClosed)
        return false;

    uint256 closedLedger = ourClosed->info().hash;
    uint256 prevClosedLedger = ourClosed->info().parentHash;
    JLOG(m_journal.trace()) << "OurClosed:  " << closedLedger;
    JLOG(m_journal.trace()) << "PrevClosed: " << prevClosedLedger;

    //-------------------------------------------------------------------------
    // Determine preferred last closed ledger

    auto& validations = app_.getValidations();
    JLOG(m_journal.debug())
        << "ValidationTrie " << Json::Compact(validations.getJsonTrie());

    // Will rely on peer LCL if no trusted validations exist
    hash_map<uint256, std::uint32_t> peerCounts;
    peerCounts[closedLedger] = 0;
    if (mMode >= OperatingMode::TRACKING)
        peerCounts[closedLedger]++;

    for (auto& peer : peerList)
    {
        uint256 peerLedger = peer->getClosedLedgerHash();

        if (peerLedger.isNonZero())
            ++peerCounts[peerLedger];
    }

    for (auto const& it : peerCounts)
        JLOG(m_journal.debug()) << "L: " << it.first << " n=" << it.second;

    uint256 preferredLCL = validations.getPreferredLCL(
        RCLValidatedLedger{ourClosed, validations.adaptor().journal()},
        m_ledgerMaster.getValidLedgerIndex(),
        peerCounts);

    bool switchLedgers = preferredLCL != closedLedger;
    if (switchLedgers)
        closedLedger = preferredLCL;
    //-------------------------------------------------------------------------
    if (switchLedgers && (closedLedger == prevClosedLedger))
    {
        // don't switch to our own previous ledger
        JLOG(m_journal.info()) << "We won't switch to our own previous ledger";
        networkClosed = ourClosed->info().hash;
        switchLedgers = false;
    }
    else
        networkClosed = closedLedger;

    if (!switchLedgers)
        return false;

    auto consensus = m_ledgerMaster.getLedgerByHash(closedLedger);

    if (!consensus)
        consensus = app_.getInboundLedgers().acquire(
            closedLedger, 0, InboundLedger::Reason::CONSENSUS);

    if (consensus &&
        (!m_ledgerMaster.canBeCurrent(consensus) ||
         !m_ledgerMaster.isCompatible(
             *consensus, m_journal.debug(), "Not switching")))
    {
        // Don't switch to a ledger not on the validated chain
        // or with an invalid close time or sequence
        networkClosed = ourClosed->info().hash;
        return false;
    }

    JLOG(m_journal.warn()) << "We are not running on the consensus ledger";
    JLOG(m_journal.info()) << "Our LCL: " << ourClosed->info().hash
                           << getJson({*ourClosed, {}});
    JLOG(m_journal.info()) << "Net LCL " << closedLedger;

    if ((mMode == OperatingMode::TRACKING) || (mMode == OperatingMode::FULL))
    {
        setMode(OperatingMode::CONNECTED);
    }

    if (consensus)
    {
        // FIXME: If this rewinds the ledger sequence, or has the same
        // sequence, we should update the status on any stored transactions
        // in the invalidated ledgers.
        switchLastClosedLedger(consensus);
    }

    return true;
}

void
NetworkOPsImp::switchLastClosedLedger(
    std::shared_ptr<Ledger const> const& newLCL)
{
    // set the newLCL as our last closed ledger -- this is abnormal code
    JLOG(m_journal.error())
        << "JUMP last closed ledger to " << newLCL->info().hash;

    clearNeedNetworkLedger();

    // Update fee computations.
    app_.getTxQ().processClosedLedger(app_, *newLCL, true);

    // Caller must own master lock
    {
        // Apply tx in old open ledger to new
        // open ledger. Then apply local tx.

        auto retries = m_localTX->getTxSet();
        auto const lastVal = app_.getLedgerMaster().getValidatedLedger();
        std::optional<Rules> rules;
        if (lastVal)
            rules = makeRulesGivenLedger(*lastVal, app_.config().features);
        else
            rules.emplace(app_.config().features);
        app_.openLedger().accept(
            app_,
            *rules,
            newLCL,
            OrderedTxs({}),
            false,
            retries,
            tapNONE,
            "jump",
            [&](OpenView& view, beast::Journal j) {
                // Stuff the ledger with transactions from the queue.
                return app_.getTxQ().accept(app_, view);
            });
    }

    m_ledgerMaster.switchLCL(newLCL);

    protocol::TMStatusChange s;
    s.set_newevent(protocol::neSWITCHED_LEDGER);
    s.set_ledgerseq(newLCL->info().seq);
    s.set_networktime(app_.timeKeeper().now().time_since_epoch().count());
    s.set_ledgerhashprevious(
        newLCL->info().parentHash.begin(), newLCL->info().parentHash.size());
    s.set_ledgerhash(newLCL->info().hash.begin(), newLCL->info().hash.size());

    app_.overlay().foreach(
        send_always(std::make_shared<Message>(s, protocol::mtSTATUS_CHANGE)));
}

bool
NetworkOPsImp::beginConsensus(
    uint256 const& networkClosed,
    std::unique_ptr<std::stringstream> const& clog)
{
    XRPL_ASSERT(
        networkClosed.isNonZero(),
        "ripple::NetworkOPsImp::beginConsensus : nonzero input");

    auto closingInfo = m_ledgerMaster.getCurrentLedger()->info();

    JLOG(m_journal.info()) << "Consensus time for #" << closingInfo.seq
                           << " with LCL " << closingInfo.parentHash;

    auto prevLedger = m_ledgerMaster.getLedgerByHash(closingInfo.parentHash);

    if (!prevLedger)
    {
        // this shouldn't happen unless we jump ledgers
        if (mMode == OperatingMode::FULL)
        {
            JLOG(m_journal.warn()) << "Don't have LCL, going to tracking";
            setMode(OperatingMode::TRACKING);
            CLOG(clog) << "beginConsensus Don't have LCL, going to tracking. ";
        }

        CLOG(clog) << "beginConsensus no previous ledger. ";
        return false;
    }

    XRPL_ASSERT(
        prevLedger->info().hash == closingInfo.parentHash,
        "ripple::NetworkOPsImp::beginConsensus : prevLedger hash matches "
        "parent");
    XRPL_ASSERT(
        closingInfo.parentHash == m_ledgerMaster.getClosedLedger()->info().hash,
        "ripple::NetworkOPsImp::beginConsensus : closedLedger parent matches "
        "hash");

    if (prevLedger->rules().enabled(featureNegativeUNL))
        app_.validators().setNegativeUNL(prevLedger->negativeUNL());
    TrustChanges const changes = app_.validators().updateTrusted(
        app_.getValidations().getCurrentNodeIDs(),
        closingInfo.parentCloseTime,
        *this,
        app_.overlay(),
        app_.getHashRouter());

    if (!changes.added.empty() || !changes.removed.empty())
    {
        app_.getValidations().trustChanged(changes.added, changes.removed);
        // Update the AmendmentTable so it tracks the current validators.
        app_.getAmendmentTable().trustChanged(
            app_.validators().getQuorumKeys().second);
    }

    mConsensus.startRound(
        app_.timeKeeper().closeTime(),
        networkClosed,
        prevLedger,
        changes.removed,
        changes.added,
        clog);

    ConsensusPhase const currPhase = mConsensus.phase();
    if (mLastConsensusPhase != currPhase)
    {
        reportConsensusStateChange(currPhase);
        mLastConsensusPhase = currPhase;
    }

    JLOG(m_journal.debug()) << "Initiating consensus engine";
    return true;
}

bool
NetworkOPsImp::processTrustedProposal(RCLCxPeerPos peerPos)
{
    auto const& peerKey = peerPos.publicKey();
    if (validatorPK_ == peerKey || validatorMasterPK_ == peerKey)
    {
        // Could indicate a operator misconfiguration where two nodes are
        // running with the same validator key configured, so this isn't fatal,
        // and it doesn't necessarily indicate peer misbehavior. But since this
        // is a trusted message, it could be a very big deal. Either way, we
        // don't want to relay the proposal. Note that the byzantine behavior
        // detection in handleNewValidation will notify other peers.
        //
        // Another, innocuous explanation is unusual message routing and delays,
        // causing this node to receive its own messages back.
        JLOG(m_journal.error())
            << "Received a proposal signed by MY KEY from a peer. This may "
               "indicate a misconfiguration where another node has the same "
               "validator key, or may be caused by unusual message routing and "
               "delays.";
        return false;
    }

    return mConsensus.peerProposal(app_.timeKeeper().closeTime(), peerPos);
}

void
NetworkOPsImp::mapComplete(std::shared_ptr<SHAMap> const& map, bool fromAcquire)
{
    // We now have an additional transaction set
    // either created locally during the consensus process
    // or acquired from a peer

    // Inform peers we have this set
    protocol::TMHaveTransactionSet msg;
    msg.set_hash(map->getHash().as_uint256().begin(), 256 / 8);
    msg.set_status(protocol::tsHAVE);
    app_.overlay().foreach(
        send_always(std::make_shared<Message>(msg, protocol::mtHAVE_SET)));

    // We acquired it because consensus asked us to
    if (fromAcquire)
        mConsensus.gotTxSet(app_.timeKeeper().closeTime(), RCLTxSet{map});
}

void
NetworkOPsImp::endConsensus(std::unique_ptr<std::stringstream> const& clog)
{
    uint256 deadLedger = m_ledgerMaster.getClosedLedger()->info().parentHash;

    for (auto const& it : app_.overlay().getActivePeers())
    {
        if (it && (it->getClosedLedgerHash() == deadLedger))
        {
            JLOG(m_journal.trace()) << "Killing obsolete peer status";
            it->cycleStatus();
        }
    }

    uint256 networkClosed;
    bool ledgerChange =
        checkLastClosedLedger(app_.overlay().getActivePeers(), networkClosed);

    if (networkClosed.isZero())
    {
        CLOG(clog) << "endConsensus last closed ledger is zero. ";
        return;
    }

    // WRITEME: Unless we are in FULL and in the process of doing a consensus,
    // we must count how many nodes share our LCL, how many nodes disagree with
    // our LCL, and how many validations our LCL has. We also want to check
    // timing to make sure there shouldn't be a newer LCL. We need this
    // information to do the next three tests.

    if (((mMode == OperatingMode::CONNECTED) ||
         (mMode == OperatingMode::SYNCING)) &&
        !ledgerChange)
    {
        // Count number of peers that agree with us and UNL nodes whose
        // validations we have for LCL.  If the ledger is good enough, go to
        // TRACKING - TODO
        if (!needNetworkLedger_)
            setMode(OperatingMode::TRACKING);
    }

    if (((mMode == OperatingMode::CONNECTED) ||
         (mMode == OperatingMode::TRACKING)) &&
        !ledgerChange)
    {
        // check if the ledger is good enough to go to FULL
        // Note: Do not go to FULL if we don't have the previous ledger
        // check if the ledger is bad enough to go to CONNECTE  D -- TODO
        auto current = m_ledgerMaster.getCurrentLedger();
        if (app_.timeKeeper().now() < (current->info().parentCloseTime +
                                       2 * current->info().closeTimeResolution))
        {
            setMode(OperatingMode::FULL);
        }
    }

    beginConsensus(networkClosed, clog);
}

void
NetworkOPsImp::consensusViewChange()
{
    if ((mMode == OperatingMode::FULL) || (mMode == OperatingMode::TRACKING))
    {
        setMode(OperatingMode::CONNECTED);
    }
}

void
NetworkOPsImp::pubManifest(Manifest const& mo)
{
    // VFALCO consider std::shared_mutex
    std::lock_guard sl(mSubLock);

    if (!mStreamMaps[sManifests].empty())
    {
        Json::Value jvObj(Json::objectValue);

        jvObj[jss::type] = "manifestReceived";
        jvObj[jss::master_key] = toBase58(TokenType::NodePublic, mo.masterKey);
        if (mo.signingKey)
            jvObj[jss::signing_key] =
                toBase58(TokenType::NodePublic, *mo.signingKey);
        jvObj[jss::seq] = Json::UInt(mo.sequence);
        if (auto sig = mo.getSignature())
            jvObj[jss::signature] = strHex(*sig);
        jvObj[jss::master_signature] = strHex(mo.getMasterSignature());
        if (!mo.domain.empty())
            jvObj[jss::domain] = mo.domain;
        jvObj[jss::manifest] = strHex(mo.serialized);

        for (auto i = mStreamMaps[sManifests].begin();
             i != mStreamMaps[sManifests].end();)
        {
            if (auto p = i->second.lock())
            {
                p->send(jvObj, true);
                ++i;
            }
            else
            {
                i = mStreamMaps[sManifests].erase(i);
            }
        }
    }
}

NetworkOPsImp::ServerFeeSummary::ServerFeeSummary(
    XRPAmount fee,
    TxQ::Metrics&& escalationMetrics,
    LoadFeeTrack const& loadFeeTrack)
    : loadFactorServer{loadFeeTrack.getLoadFactor()}
    , loadBaseServer{loadFeeTrack.getLoadBase()}
    , baseFee{fee}
    , em{std::move(escalationMetrics)}
{
}

bool
NetworkOPsImp::ServerFeeSummary::operator!=(
    NetworkOPsImp::ServerFeeSummary const& b) const
{
    if (loadFactorServer != b.loadFactorServer ||
        loadBaseServer != b.loadBaseServer || baseFee != b.baseFee ||
        em.has_value() != b.em.has_value())
        return true;

    if (em && b.em)
    {
        return (
            em->minProcessingFeeLevel != b.em->minProcessingFeeLevel ||
            em->openLedgerFeeLevel != b.em->openLedgerFeeLevel ||
            em->referenceFeeLevel != b.em->referenceFeeLevel);
    }

    return false;
}

// Need to cap to uint64 to uint32 due to JSON limitations
static std::uint32_t
trunc32(std::uint64_t v)
{
    constexpr std::uint64_t max32 = std::numeric_limits<std::uint32_t>::max();

    return std::min(max32, v);
};

void
NetworkOPsImp::pubServer()
{
    // VFALCO TODO Don't hold the lock across calls to send...make a copy of the
    //             list into a local array while holding the lock then release
    //             the lock and call send on everyone.
    //
    std::lock_guard sl(mSubLock);

    if (!mStreamMaps[sServer].empty())
    {
        Json::Value jvObj(Json::objectValue);

        ServerFeeSummary f{
            app_.openLedger().current()->fees().base,
            app_.getTxQ().getMetrics(*app_.openLedger().current()),
            app_.getFeeTrack()};

        jvObj[jss::type] = "serverStatus";
        jvObj[jss::server_status] = strOperatingMode();
        jvObj[jss::load_base] = f.loadBaseServer;
        jvObj[jss::load_factor_server] = f.loadFactorServer;
        jvObj[jss::base_fee] = f.baseFee.jsonClipped();

        if (f.em)
        {
            auto const loadFactor = std::max(
                safe_cast<std::uint64_t>(f.loadFactorServer),
                mulDiv(
                    f.em->openLedgerFeeLevel,
                    f.loadBaseServer,
                    f.em->referenceFeeLevel)
                    .value_or(ripple::muldiv_max));

            jvObj[jss::load_factor] = trunc32(loadFactor);
            jvObj[jss::load_factor_fee_escalation] =
                f.em->openLedgerFeeLevel.jsonClipped();
            jvObj[jss::load_factor_fee_queue] =
                f.em->minProcessingFeeLevel.jsonClipped();
            jvObj[jss::load_factor_fee_reference] =
                f.em->referenceFeeLevel.jsonClipped();
        }
        else
            jvObj[jss::load_factor] = f.loadFactorServer;

        mLastFeeSummary = f;

        for (auto i = mStreamMaps[sServer].begin();
             i != mStreamMaps[sServer].end();)
        {
            InfoSub::pointer p = i->second.lock();

            // VFALCO TODO research the possibility of using thread queues and
            //             linearizing the deletion of subscribers with the
            //             sending of JSON data.
            if (p)
            {
                p->send(jvObj, true);
                ++i;
            }
            else
            {
                i = mStreamMaps[sServer].erase(i);
            }
        }
    }
}

void
NetworkOPsImp::pubConsensus(ConsensusPhase phase)
{
    std::lock_guard sl(mSubLock);

    auto& streamMap = mStreamMaps[sConsensusPhase];
    if (!streamMap.empty())
    {
        Json::Value jvObj(Json::objectValue);
        jvObj[jss::type] = "consensusPhase";
        jvObj[jss::consensus] = to_string(phase);

        for (auto i = streamMap.begin(); i != streamMap.end();)
        {
            if (auto p = i->second.lock())
            {
                p->send(jvObj, true);
                ++i;
            }
            else
            {
                i = streamMap.erase(i);
            }
        }
    }
}

void
NetworkOPsImp::pubValidation(std::shared_ptr<STValidation> const& val)
{
    // VFALCO consider std::shared_mutex
    std::lock_guard sl(mSubLock);

    if (!mStreamMaps[sValidations].empty())
    {
        Json::Value jvObj(Json::objectValue);

        auto const signerPublic = val->getSignerPublic();

        jvObj[jss::type] = "validationReceived";
        jvObj[jss::validation_public_key] =
            toBase58(TokenType::NodePublic, signerPublic);
        jvObj[jss::ledger_hash] = to_string(val->getLedgerHash());
        jvObj[jss::signature] = strHex(val->getSignature());
        jvObj[jss::full] = val->isFull();
        jvObj[jss::flags] = val->getFlags();
        jvObj[jss::signing_time] = *(*val)[~sfSigningTime];
        jvObj[jss::data] = strHex(val->getSerializer().slice());

        if (auto version = (*val)[~sfServerVersion])
            jvObj[jss::server_version] = std::to_string(*version);

        if (auto cookie = (*val)[~sfCookie])
            jvObj[jss::cookie] = std::to_string(*cookie);

        if (auto hash = (*val)[~sfValidatedHash])
            jvObj[jss::validated_hash] = strHex(*hash);

        auto const masterKey =
            app_.validatorManifests().getMasterKey(signerPublic);

        if (masterKey != signerPublic)
            jvObj[jss::master_key] = toBase58(TokenType::NodePublic, masterKey);

        // NOTE *seq is a number, but old API versions used string. We replace
        // number with a string using MultiApiJson near end of this function
        if (auto const seq = (*val)[~sfLedgerSequence])
            jvObj[jss::ledger_index] = *seq;

        if (val->isFieldPresent(sfAmendments))
        {
            jvObj[jss::amendments] = Json::Value(Json::arrayValue);
            for (auto const& amendment : val->getFieldV256(sfAmendments))
                jvObj[jss::amendments].append(to_string(amendment));
        }

        if (auto const closeTime = (*val)[~sfCloseTime])
            jvObj[jss::close_time] = *closeTime;

        if (auto const loadFee = (*val)[~sfLoadFee])
            jvObj[jss::load_fee] = *loadFee;

        if (auto const baseFee = val->at(~sfBaseFee))
            jvObj[jss::base_fee] = static_cast<double>(*baseFee);

        if (auto const reserveBase = val->at(~sfReserveBase))
            jvObj[jss::reserve_base] = *reserveBase;

        if (auto const reserveInc = val->at(~sfReserveIncrement))
            jvObj[jss::reserve_inc] = *reserveInc;

        // (The ~ operator converts the Proxy to a std::optional, which
        //  simplifies later operations)
        if (auto const baseFeeXRP = ~val->at(~sfBaseFeeDrops);
            baseFeeXRP && baseFeeXRP->native())
            jvObj[jss::base_fee] = baseFeeXRP->xrp().jsonClipped();

        if (auto const reserveBaseXRP = ~val->at(~sfReserveBaseDrops);
            reserveBaseXRP && reserveBaseXRP->native())
            jvObj[jss::reserve_base] = reserveBaseXRP->xrp().jsonClipped();

        if (auto const reserveIncXRP = ~val->at(~sfReserveIncrementDrops);
            reserveIncXRP && reserveIncXRP->native())
            jvObj[jss::reserve_inc] = reserveIncXRP->xrp().jsonClipped();

        // NOTE Use MultiApiJson to publish two slightly different JSON objects
        // for consumers supporting different API versions
        MultiApiJson multiObj{jvObj};
        multiObj.visit(
            RPC::apiVersion<1>,  //
            [](Json::Value& jvTx) {
                // Type conversion for older API versions to string
                if (jvTx.isMember(jss::ledger_index))
                {
                    jvTx[jss::ledger_index] =
                        std::to_string(jvTx[jss::ledger_index].asUInt());
                }
            });

        for (auto i = mStreamMaps[sValidations].begin();
             i != mStreamMaps[sValidations].end();)
        {
            if (auto p = i->second.lock())
            {
                multiObj.visit(
                    p->getApiVersion(),  //
                    [&](Json::Value const& jv) { p->send(jv, true); });
                ++i;
            }
            else
            {
                i = mStreamMaps[sValidations].erase(i);
            }
        }
    }
}

void
NetworkOPsImp::pubPeerStatus(std::function<Json::Value(void)> const& func)
{
    std::lock_guard sl(mSubLock);

    if (!mStreamMaps[sPeerStatus].empty())
    {
        Json::Value jvObj(func());

        jvObj[jss::type] = "peerStatusChange";

        for (auto i = mStreamMaps[sPeerStatus].begin();
             i != mStreamMaps[sPeerStatus].end();)
        {
            InfoSub::pointer p = i->second.lock();

            if (p)
            {
                p->send(jvObj, true);
                ++i;
            }
            else
            {
                i = mStreamMaps[sPeerStatus].erase(i);
            }
        }
    }
}

void
NetworkOPsImp::setMode(OperatingMode om)
{
    using namespace std::chrono_literals;
    if (om == OperatingMode::CONNECTED)
    {
        if (app_.getLedgerMaster().getValidatedLedgerAge() < 1min)
            om = OperatingMode::SYNCING;
    }
    else if (om == OperatingMode::SYNCING)
    {
        if (app_.getLedgerMaster().getValidatedLedgerAge() >= 1min)
            om = OperatingMode::CONNECTED;
    }

    if ((om > OperatingMode::CONNECTED) && isBlocked())
        om = OperatingMode::CONNECTED;

    if (mMode == om)
        return;

    mMode = om;

    accounting_.mode(om);

    JLOG(m_journal.info()) << "STATE->" << strOperatingMode();
    pubServer();
}

bool
NetworkOPsImp::recvValidation(
    std::shared_ptr<STValidation> const& val,
    std::string const& source)
{
    JLOG(m_journal.trace())
        << "recvValidation " << val->getLedgerHash() << " from " << source;

    std::unique_lock lock(validationsMutex_);
    BypassAccept bypassAccept = BypassAccept::no;
    try
    {
        if (pendingValidations_.contains(val->getLedgerHash()))
            bypassAccept = BypassAccept::yes;
        else
            pendingValidations_.insert(val->getLedgerHash());
        scope_unlock unlock(lock);
        handleNewValidation(app_, val, source, bypassAccept, m_journal);
    }
    catch (std::exception const& e)
    {
        JLOG(m_journal.warn())
            << "Exception thrown for handling new validation "
            << val->getLedgerHash() << ": " << e.what();
    }
    catch (...)
    {
        JLOG(m_journal.warn())
            << "Unknown exception thrown for handling new validation "
            << val->getLedgerHash();
    }
    if (bypassAccept == BypassAccept::no)
    {
        pendingValidations_.erase(val->getLedgerHash());
    }
    lock.unlock();

    pubValidation(val);

    JLOG(m_journal.debug()) << [this, &val]() -> auto {
        std::stringstream ss;
        ss << "VALIDATION: " << val->render() << " master_key: ";
        auto master = app_.validators().getTrustedKey(val->getSignerPublic());
        if (master)
        {
            ss << toBase58(TokenType::NodePublic, *master);
        }
        else
        {
            ss << "none";
        }
        return ss.str();
    }();

    // We will always relay trusted validations; if configured, we will
    // also relay all untrusted validations.
    return app_.config().RELAY_UNTRUSTED_VALIDATIONS == 1 || val->isTrusted();
}

Json::Value
NetworkOPsImp::getConsensusInfo()
{
    return mConsensus.getJson(true);
}

Json::Value
NetworkOPsImp::getServerInfo(bool human, bool admin, bool counters)
{
    Json::Value info = Json::objectValue;

    // System-level warnings
    {
        Json::Value warnings{Json::arrayValue};
        if (isAmendmentBlocked())
        {
            Json::Value& w = warnings.append(Json::objectValue);
            w[jss::id] = warnRPC_AMENDMENT_BLOCKED;
            w[jss::message] =
                "This server is amendment blocked, and must be updated to be "
                "able to stay in sync with the network.";
        }
        if (isUNLBlocked())
        {
            Json::Value& w = warnings.append(Json::objectValue);
            w[jss::id] = warnRPC_EXPIRED_VALIDATOR_LIST;
            w[jss::message] =
                "This server has an expired validator list. validators.txt "
                "may be incorrectly configured or some [validator_list_sites] "
                "may be unreachable.";
        }
        if (admin && isAmendmentWarned())
        {
            Json::Value& w = warnings.append(Json::objectValue);
            w[jss::id] = warnRPC_UNSUPPORTED_MAJORITY;
            w[jss::message] =
                "One or more unsupported amendments have reached majority. "
                "Upgrade to the latest version before they are activated "
                "to avoid being amendment blocked.";
            if (auto const expected =
                    app_.getAmendmentTable().firstUnsupportedExpected())
            {
                auto& d = w[jss::details] = Json::objectValue;
                d[jss::expected_date] = expected->time_since_epoch().count();
                d[jss::expected_date_UTC] = to_string(*expected);
            }
        }

        if (warnings.size())
            info[jss::warnings] = std::move(warnings);
    }

    // hostid: unique string describing the machine
    if (human)
        info[jss::hostid] = getHostId(admin);

    // domain: if configured with a domain, report it:
    if (!app_.config().SERVER_DOMAIN.empty())
        info[jss::server_domain] = app_.config().SERVER_DOMAIN;

    info[jss::build_version] = BuildInfo::getVersionString();

    info[jss::server_state] = strOperatingMode(admin);

    info[jss::time] = to_string(std::chrono::floor<std::chrono::microseconds>(
        std::chrono::system_clock::now()));

    if (needNetworkLedger_)
        info[jss::network_ledger] = "waiting";

    info[jss::validation_quorum] =
        static_cast<Json::UInt>(app_.validators().quorum());

    if (admin)
    {
        switch (app_.config().NODE_SIZE)
        {
            case 0:
                info[jss::node_size] = "tiny";
                break;
            case 1:
                info[jss::node_size] = "small";
                break;
            case 2:
                info[jss::node_size] = "medium";
                break;
            case 3:
                info[jss::node_size] = "large";
                break;
            case 4:
                info[jss::node_size] = "huge";
                break;
        }

        auto when = app_.validators().expires();

        if (!human)
        {
            if (when)
                info[jss::validator_list_expires] =
                    safe_cast<Json::UInt>(when->time_since_epoch().count());
            else
                info[jss::validator_list_expires] = 0;
        }
        else
        {
            auto& x = (info[jss::validator_list] = Json::objectValue);

            x[jss::count] = static_cast<Json::UInt>(app_.validators().count());

            if (when)
            {
                if (*when == TimeKeeper::time_point::max())
                {
                    x[jss::expiration] = "never";
                    x[jss::status] = "active";
                }
                else
                {
                    x[jss::expiration] = to_string(*when);

                    if (*when > app_.timeKeeper().now())
                        x[jss::status] = "active";
                    else
                        x[jss::status] = "expired";
                }
            }
            else
            {
                x[jss::status] = "unknown";
                x[jss::expiration] = "unknown";
            }
        }

#if defined(GIT_COMMIT_HASH) || defined(GIT_BRANCH)
        {
            auto& x = (info[jss::git] = Json::objectValue);
#ifdef GIT_COMMIT_HASH
            x[jss::hash] = GIT_COMMIT_HASH;
#endif
#ifdef GIT_BRANCH
            x[jss::branch] = GIT_BRANCH;
#endif
        }
#endif
    }
    info[jss::io_latency_ms] =
        static_cast<Json::UInt>(app_.getIOLatency().count());

    if (admin)
    {
        if (auto const localPubKey = app_.validators().localPublicKey();
            localPubKey && app_.getValidationPublicKey())
        {
            info[jss::pubkey_validator] =
                toBase58(TokenType::NodePublic, localPubKey.value());
        }
        else
        {
            info[jss::pubkey_validator] = "none";
        }
    }

    if (counters)
    {
        info[jss::counters] = app_.getPerfLog().countersJson();

        Json::Value nodestore(Json::objectValue);
        app_.getNodeStore().getCountsJson(nodestore);
        info[jss::counters][jss::nodestore] = nodestore;
        info[jss::current_activities] = app_.getPerfLog().currentJson();
    }

    info[jss::pubkey_node] =
        toBase58(TokenType::NodePublic, app_.nodeIdentity().first);

    info[jss::complete_ledgers] = app_.getLedgerMaster().getCompleteLedgers();

    if (amendmentBlocked_)
        info[jss::amendment_blocked] = true;

    auto const fp = m_ledgerMaster.getFetchPackCacheSize();

    if (fp != 0)
        info[jss::fetch_pack] = Json::UInt(fp);

    info[jss::peers] = Json::UInt(app_.overlay().size());

    Json::Value lastClose = Json::objectValue;
    lastClose[jss::proposers] = Json::UInt(mConsensus.prevProposers());

    if (human)
    {
        lastClose[jss::converge_time_s] =
            std::chrono::duration<double>{mConsensus.prevRoundTime()}.count();
    }
    else
    {
        lastClose[jss::converge_time] =
            Json::Int(mConsensus.prevRoundTime().count());
    }

    info[jss::last_close] = lastClose;

    //  info[jss::consensus] = mConsensus.getJson();

    if (admin)
        info[jss::load] = m_job_queue.getJson();

    if (auto const netid = app_.overlay().networkID())
        info[jss::network_id] = static_cast<Json::UInt>(*netid);

    auto const escalationMetrics =
        app_.getTxQ().getMetrics(*app_.openLedger().current());

    auto const loadFactorServer = app_.getFeeTrack().getLoadFactor();
    auto const loadBaseServer = app_.getFeeTrack().getLoadBase();
    /* Scale the escalated fee level to unitless "load factor".
       In practice, this just strips the units, but it will continue
       to work correctly if either base value ever changes. */
    auto const loadFactorFeeEscalation =
        mulDiv(
            escalationMetrics.openLedgerFeeLevel,
            loadBaseServer,
            escalationMetrics.referenceFeeLevel)
            .value_or(ripple::muldiv_max);

    auto const loadFactor = std::max(
        safe_cast<std::uint64_t>(loadFactorServer), loadFactorFeeEscalation);

    if (!human)
    {
        info[jss::load_base] = loadBaseServer;
        info[jss::load_factor] = trunc32(loadFactor);
        info[jss::load_factor_server] = loadFactorServer;

        /* Json::Value doesn't support uint64, so clamp to max
            uint32 value. This is mostly theoretical, since there
            probably isn't enough extant XRP to drive the factor
            that high.
        */
        info[jss::load_factor_fee_escalation] =
            escalationMetrics.openLedgerFeeLevel.jsonClipped();
        info[jss::load_factor_fee_queue] =
            escalationMetrics.minProcessingFeeLevel.jsonClipped();
        info[jss::load_factor_fee_reference] =
            escalationMetrics.referenceFeeLevel.jsonClipped();
    }
    else
    {
        info[jss::load_factor] =
            static_cast<double>(loadFactor) / loadBaseServer;

        if (loadFactorServer != loadFactor)
            info[jss::load_factor_server] =
                static_cast<double>(loadFactorServer) / loadBaseServer;

        if (admin)
        {
            std::uint32_t fee = app_.getFeeTrack().getLocalFee();
            if (fee != loadBaseServer)
                info[jss::load_factor_local] =
                    static_cast<double>(fee) / loadBaseServer;
            fee = app_.getFeeTrack().getRemoteFee();
            if (fee != loadBaseServer)
                info[jss::load_factor_net] =
                    static_cast<double>(fee) / loadBaseServer;
            fee = app_.getFeeTrack().getClusterFee();
            if (fee != loadBaseServer)
                info[jss::load_factor_cluster] =
                    static_cast<double>(fee) / loadBaseServer;
        }
        if (escalationMetrics.openLedgerFeeLevel !=
                escalationMetrics.referenceFeeLevel &&
            (admin || loadFactorFeeEscalation != loadFactor))
            info[jss::load_factor_fee_escalation] =
                escalationMetrics.openLedgerFeeLevel.decimalFromReference(
                    escalationMetrics.referenceFeeLevel);
        if (escalationMetrics.minProcessingFeeLevel !=
            escalationMetrics.referenceFeeLevel)
            info[jss::load_factor_fee_queue] =
                escalationMetrics.minProcessingFeeLevel.decimalFromReference(
                    escalationMetrics.referenceFeeLevel);
    }

    bool valid = false;
    auto lpClosed = m_ledgerMaster.getValidatedLedger();

    if (lpClosed)
        valid = true;
    else
        lpClosed = m_ledgerMaster.getClosedLedger();

    if (lpClosed)
    {
        XRPAmount const baseFee = lpClosed->fees().base;
        Json::Value l(Json::objectValue);
        l[jss::seq] = Json::UInt(lpClosed->info().seq);
        l[jss::hash] = to_string(lpClosed->info().hash);

        if (!human)
        {
            l[jss::base_fee] = baseFee.jsonClipped();
            l[jss::reserve_base] =
                lpClosed->fees().accountReserve(0).jsonClipped();
            l[jss::reserve_inc] = lpClosed->fees().increment.jsonClipped();
            l[jss::close_time] = Json::Value::UInt(
                lpClosed->info().closeTime.time_since_epoch().count());
        }
        else
        {
            l[jss::base_fee_xrp] = baseFee.decimalXRP();
            l[jss::reserve_base_xrp] =
                lpClosed->fees().accountReserve(0).decimalXRP();
            l[jss::reserve_inc_xrp] = lpClosed->fees().increment.decimalXRP();

            if (auto const closeOffset = app_.timeKeeper().closeOffset();
                std::abs(closeOffset.count()) >= 60)
                l[jss::close_time_offset] =
                    static_cast<std::uint32_t>(closeOffset.count());

            constexpr std::chrono::seconds highAgeThreshold{1000000};
            if (m_ledgerMaster.haveValidated())
            {
                auto const age = m_ledgerMaster.getValidatedLedgerAge();
                l[jss::age] =
                    Json::UInt(age < highAgeThreshold ? age.count() : 0);
            }
            else
            {
                auto lCloseTime = lpClosed->info().closeTime;
                auto closeTime = app_.timeKeeper().closeTime();
                if (lCloseTime <= closeTime)
                {
                    using namespace std::chrono_literals;
                    auto age = closeTime - lCloseTime;
                    l[jss::age] =
                        Json::UInt(age < highAgeThreshold ? age.count() : 0);
                }
            }
        }

        if (valid)
            info[jss::validated_ledger] = l;
        else
            info[jss::closed_ledger] = l;

        auto lpPublished = m_ledgerMaster.getPublishedLedger();
        if (!lpPublished)
            info[jss::published_ledger] = "none";
        else if (lpPublished->info().seq != lpClosed->info().seq)
            info[jss::published_ledger] = lpPublished->info().seq;
    }

    accounting_.json(info);
    info[jss::uptime] = UptimeClock::now().time_since_epoch().count();
    info[jss::jq_trans_overflow] =
        std::to_string(app_.overlay().getJqTransOverflow());
    info[jss::peer_disconnects] =
        std::to_string(app_.overlay().getPeerDisconnect());
    info[jss::peer_disconnects_resources] =
        std::to_string(app_.overlay().getPeerDisconnectCharges());

    // This array must be sorted in increasing order.
    static constexpr std::array<std::string_view, 7> protocols{
        "http", "https", "peer", "ws", "ws2", "wss", "wss2"};
    static_assert(std::is_sorted(std::begin(protocols), std::end(protocols)));
    {
        Json::Value ports{Json::arrayValue};
        for (auto const& port : app_.getServerHandler().setup().ports)
        {
            // Don't publish admin ports for non-admin users
            if (!admin &&
                !(port.admin_nets_v4.empty() && port.admin_nets_v6.empty() &&
                  port.admin_user.empty() && port.admin_password.empty()))
                continue;
            std::vector<std::string> proto;
            std::set_intersection(
                std::begin(port.protocol),
                std::end(port.protocol),
                std::begin(protocols),
                std::end(protocols),
                std::back_inserter(proto));
            if (!proto.empty())
            {
                auto& jv = ports.append(Json::Value(Json::objectValue));
                jv[jss::port] = std::to_string(port.port);
                jv[jss::protocol] = Json::Value{Json::arrayValue};
                for (auto const& p : proto)
                    jv[jss::protocol].append(p);
            }
        }

        if (app_.config().exists(SECTION_PORT_GRPC))
        {
            auto const& grpcSection = app_.config().section(SECTION_PORT_GRPC);
            auto const optPort = grpcSection.get("port");
            if (optPort && grpcSection.get("ip"))
            {
                auto& jv = ports.append(Json::Value(Json::objectValue));
                jv[jss::port] = *optPort;
                jv[jss::protocol] = Json::Value{Json::arrayValue};
                jv[jss::protocol].append("grpc");
            }
        }
        info[jss::ports] = std::move(ports);
    }

    return info;
}

void
NetworkOPsImp::clearLedgerFetch()
{
    app_.getInboundLedgers().clearFailures();
}

Json::Value
NetworkOPsImp::getLedgerFetchInfo()
{
    return app_.getInboundLedgers().getInfo();
}

void
NetworkOPsImp::pubProposedTransaction(
    std::shared_ptr<ReadView const> const& ledger,
    std::shared_ptr<STTx const> const& transaction,
    TER result)
{
    // never publish an inner txn inside a batch txn
    if (transaction->isFlag(tfInnerBatchTxn) &&
        ledger->rules().enabled(featureBatch))
        return;

    MultiApiJson jvObj =
        transJson(transaction, result, false, ledger, std::nullopt);

    {
        std::lock_guard sl(mSubLock);

        auto it = mStreamMaps[sRTTransactions].begin();
        while (it != mStreamMaps[sRTTransactions].end())
        {
            InfoSub::pointer p = it->second.lock();

            if (p)
            {
                jvObj.visit(
                    p->getApiVersion(),  //
                    [&](Json::Value const& jv) { p->send(jv, true); });
                ++it;
            }
            else
            {
                it = mStreamMaps[sRTTransactions].erase(it);
            }
        }
    }

    pubProposedAccountTransaction(ledger, transaction, result);
}

void
NetworkOPsImp::pubLedger(std::shared_ptr<ReadView const> const& lpAccepted)
{
    // Ledgers are published only when they acquire sufficient validations
    // Holes are filled across connection loss or other catastrophe

    std::shared_ptr<AcceptedLedger> alpAccepted =
        app_.getAcceptedLedgerCache().fetch(lpAccepted->info().hash);
    if (!alpAccepted)
    {
        alpAccepted = std::make_shared<AcceptedLedger>(lpAccepted, app_);
        app_.getAcceptedLedgerCache().canonicalize_replace_client(
            lpAccepted->info().hash, alpAccepted);
    }

    XRPL_ASSERT(
        alpAccepted->getLedger().get() == lpAccepted.get(),
        "ripple::NetworkOPsImp::pubLedger : accepted input");

    {
        JLOG(m_journal.debug())
            << "Publishing ledger " << lpAccepted->info().seq << " "
            << lpAccepted->info().hash;

        std::lock_guard sl(mSubLock);

        if (!mStreamMaps[sLedger].empty())
        {
            Json::Value jvObj(Json::objectValue);

            jvObj[jss::type] = "ledgerClosed";
            jvObj[jss::ledger_index] = lpAccepted->info().seq;
            jvObj[jss::ledger_hash] = to_string(lpAccepted->info().hash);
            jvObj[jss::ledger_time] = Json::Value::UInt(
                lpAccepted->info().closeTime.time_since_epoch().count());

            if (!lpAccepted->rules().enabled(featureXRPFees))
                jvObj[jss::fee_ref] = Config::FEE_UNITS_DEPRECATED;
            jvObj[jss::fee_base] = lpAccepted->fees().base.jsonClipped();
            jvObj[jss::reserve_base] =
                lpAccepted->fees().accountReserve(0).jsonClipped();
            jvObj[jss::reserve_inc] =
                lpAccepted->fees().increment.jsonClipped();

            jvObj[jss::txn_count] = Json::UInt(alpAccepted->size());

            if (mMode >= OperatingMode::SYNCING)
            {
                jvObj[jss::validated_ledgers] =
                    app_.getLedgerMaster().getCompleteLedgers();
            }

            auto it = mStreamMaps[sLedger].begin();
            while (it != mStreamMaps[sLedger].end())
            {
                InfoSub::pointer p = it->second.lock();
                if (p)
                {
                    p->send(jvObj, true);
                    ++it;
                }
                else
                    it = mStreamMaps[sLedger].erase(it);
            }
        }

        if (!mStreamMaps[sBookChanges].empty())
        {
            Json::Value jvObj = ripple::RPC::computeBookChanges(lpAccepted);

            auto it = mStreamMaps[sBookChanges].begin();
            while (it != mStreamMaps[sBookChanges].end())
            {
                InfoSub::pointer p = it->second.lock();
                if (p)
                {
                    p->send(jvObj, true);
                    ++it;
                }
                else
                    it = mStreamMaps[sBookChanges].erase(it);
            }
        }

        {
            static bool firstTime = true;
            if (firstTime)
            {
                // First validated ledger, start delayed SubAccountHistory
                firstTime = false;
                for (auto& outer : mSubAccountHistory)
                {
                    for (auto& inner : outer.second)
                    {
                        auto& subInfo = inner.second;
                        if (subInfo.index_->separationLedgerSeq_ == 0)
                        {
                            subAccountHistoryStart(
                                alpAccepted->getLedger(), subInfo);
                        }
                    }
                }
            }
        }
    }

    // Don't lock since pubAcceptedTransaction is locking.
    for (auto const& accTx : *alpAccepted)
    {
        JLOG(m_journal.trace()) << "pubAccepted: " << accTx->getJson();
        pubValidatedTransaction(
            lpAccepted, *accTx, accTx == *(--alpAccepted->end()));
    }
}

void
NetworkOPsImp::reportFeeChange()
{
    ServerFeeSummary f{
        app_.openLedger().current()->fees().base,
        app_.getTxQ().getMetrics(*app_.openLedger().current()),
        app_.getFeeTrack()};

    // only schedule the job if something has changed
    if (f != mLastFeeSummary)
    {
        m_job_queue.addJob(
            jtCLIENT_FEE_CHANGE, "reportFeeChange->pubServer", [this]() {
                pubServer();
            });
    }
}

void
NetworkOPsImp::reportConsensusStateChange(ConsensusPhase phase)
{
    m_job_queue.addJob(
        jtCLIENT_CONSENSUS,
        "reportConsensusStateChange->pubConsensus",
        [this, phase]() { pubConsensus(phase); });
}

inline void
NetworkOPsImp::updateLocalTx(ReadView const& view)
{
    m_localTX->sweep(view);
}
inline std::size_t
NetworkOPsImp::getLocalTxCount()
{
    return m_localTX->size();
}

// This routine should only be used to publish accepted or validated
// transactions.
MultiApiJson
NetworkOPsImp::transJson(
    std::shared_ptr<STTx const> const& transaction,
    TER result,
    bool validated,
    std::shared_ptr<ReadView const> const& ledger,
    std::optional<std::reference_wrapper<TxMeta const>> meta)
{
    Json::Value jvObj(Json::objectValue);
    std::string sToken;
    std::string sHuman;

    transResultInfo(result, sToken, sHuman);

    jvObj[jss::type] = "transaction";
    // NOTE jvObj is not a finished object for either API version. After
    // it's populated, we need to finish it for a specific API version. This is
    // done in a loop, near the end of this function.
    jvObj[jss::transaction] =
        transaction->getJson(JsonOptions::disable_API_prior_V2, false);

    if (meta)
    {
        jvObj[jss::meta] = meta->get().getJson(JsonOptions::none);
        RPC::insertDeliveredAmount(
            jvObj[jss::meta], *ledger, transaction, meta->get());
        RPC::insertNFTSyntheticInJson(jvObj, transaction, meta->get());
        RPC::insertMPTokenIssuanceID(
            jvObj[jss::meta], transaction, meta->get());
    }

    // add CTID where the needed data for it exists
    if (auto const& lookup = ledger->txRead(transaction->getTransactionID());
        lookup.second && lookup.second->isFieldPresent(sfTransactionIndex))
    {
        uint32_t const txnSeq = lookup.second->getFieldU32(sfTransactionIndex);
        uint32_t netID = app_.config().NETWORK_ID;
        if (transaction->isFieldPresent(sfNetworkID))
            netID = transaction->getFieldU32(sfNetworkID);

        if (std::optional<std::string> ctid =
                RPC::encodeCTID(ledger->info().seq, txnSeq, netID);
            ctid)
            jvObj[jss::ctid] = *ctid;
    }
    if (!ledger->open())
        jvObj[jss::ledger_hash] = to_string(ledger->info().hash);

    if (validated)
    {
        jvObj[jss::ledger_index] = ledger->info().seq;
        jvObj[jss::transaction][jss::date] =
            ledger->info().closeTime.time_since_epoch().count();
        jvObj[jss::validated] = true;
        jvObj[jss::close_time_iso] = to_string_iso(ledger->info().closeTime);

        // WRITEME: Put the account next seq here
    }
    else
    {
        jvObj[jss::validated] = false;
        jvObj[jss::ledger_current_index] = ledger->info().seq;
    }

    jvObj[jss::status] = validated ? "closed" : "proposed";
    jvObj[jss::engine_result] = sToken;
    jvObj[jss::engine_result_code] = result;
    jvObj[jss::engine_result_message] = sHuman;

    if (transaction->getTxnType() == ttOFFER_CREATE)
    {
        auto const account = transaction->getAccountID(sfAccount);
        auto const amount = transaction->getFieldAmount(sfTakerGets);

        // If the offer create is not self funded then add the owner balance
        if (account != amount.issue().account)
        {
            auto const ownerFunds = accountFunds(
                *ledger,
                account,
                amount,
                fhIGNORE_FREEZE,
                app_.journal("View"));
            jvObj[jss::transaction][jss::owner_funds] = ownerFunds.getText();
        }
    }

    std::string const hash = to_string(transaction->getTransactionID());
    MultiApiJson multiObj{jvObj};
    forAllApiVersions(
        multiObj.visit(),  //
        [&]<unsigned Version>(
            Json::Value& jvTx, std::integral_constant<unsigned, Version>) {
            RPC::insertDeliverMax(
                jvTx[jss::transaction], transaction->getTxnType(), Version);

            if constexpr (Version > 1)
            {
                jvTx[jss::tx_json] = jvTx.removeMember(jss::transaction);
                jvTx[jss::hash] = hash;
            }
            else
            {
                jvTx[jss::transaction][jss::hash] = hash;
            }
        });

    return multiObj;
}

void
NetworkOPsImp::pubValidatedTransaction(
    std::shared_ptr<ReadView const> const& ledger,
    AcceptedLedgerTx const& transaction,
    bool last)
{
    auto const& stTxn = transaction.getTxn();

    // Create two different Json objects, for different API versions
    auto const metaRef = std::ref(transaction.getMeta());
    auto const trResult = transaction.getResult();
    MultiApiJson jvObj = transJson(stTxn, trResult, true, ledger, metaRef);

    {
        std::lock_guard sl(mSubLock);

        auto it = mStreamMaps[sTransactions].begin();
        while (it != mStreamMaps[sTransactions].end())
        {
            InfoSub::pointer p = it->second.lock();

            if (p)
            {
                jvObj.visit(
                    p->getApiVersion(),  //
                    [&](Json::Value const& jv) { p->send(jv, true); });
                ++it;
            }
            else
                it = mStreamMaps[sTransactions].erase(it);
        }

        it = mStreamMaps[sRTTransactions].begin();

        while (it != mStreamMaps[sRTTransactions].end())
        {
            InfoSub::pointer p = it->second.lock();

            if (p)
            {
                jvObj.visit(
                    p->getApiVersion(),  //
                    [&](Json::Value const& jv) { p->send(jv, true); });
                ++it;
            }
            else
                it = mStreamMaps[sRTTransactions].erase(it);
        }
    }

    if (transaction.getResult() == tesSUCCESS)
        app_.getOrderBookDB().processTxn(ledger, transaction, jvObj);

    pubAccountTransaction(ledger, transaction, last);
}

void
NetworkOPsImp::pubAccountTransaction(
    std::shared_ptr<ReadView const> const& ledger,
    AcceptedLedgerTx const& transaction,
    bool last)
{
    hash_set<InfoSub::pointer> notify;
    int iProposed = 0;
    int iAccepted = 0;

    std::vector<SubAccountHistoryInfo> accountHistoryNotify;
    auto const currLedgerSeq = ledger->seq();
    {
        std::lock_guard sl(mSubLock);

        if (!mSubAccount.empty() || !mSubRTAccount.empty() ||
            !mSubAccountHistory.empty())
        {
            for (auto const& affectedAccount : transaction.getAffected())
            {
                if (auto simiIt = mSubRTAccount.find(affectedAccount);
                    simiIt != mSubRTAccount.end())
                {
                    auto it = simiIt->second.begin();

                    while (it != simiIt->second.end())
                    {
                        InfoSub::pointer p = it->second.lock();

                        if (p)
                        {
                            notify.insert(p);
                            ++it;
                            ++iProposed;
                        }
                        else
                            it = simiIt->second.erase(it);
                    }
                }

                if (auto simiIt = mSubAccount.find(affectedAccount);
                    simiIt != mSubAccount.end())
                {
                    auto it = simiIt->second.begin();
                    while (it != simiIt->second.end())
                    {
                        InfoSub::pointer p = it->second.lock();

                        if (p)
                        {
                            notify.insert(p);
                            ++it;
                            ++iAccepted;
                        }
                        else
                            it = simiIt->second.erase(it);
                    }
                }

                if (auto histoIt = mSubAccountHistory.find(affectedAccount);
                    histoIt != mSubAccountHistory.end())
                {
                    auto& subs = histoIt->second;
                    auto it = subs.begin();
                    while (it != subs.end())
                    {
                        SubAccountHistoryInfoWeak const& info = it->second;
                        if (currLedgerSeq <= info.index_->separationLedgerSeq_)
                        {
                            ++it;
                            continue;
                        }

                        if (auto isSptr = info.sinkWptr_.lock(); isSptr)
                        {
                            accountHistoryNotify.emplace_back(
                                SubAccountHistoryInfo{isSptr, info.index_});
                            ++it;
                        }
                        else
                        {
                            it = subs.erase(it);
                        }
                    }
                    if (subs.empty())
                        mSubAccountHistory.erase(histoIt);
                }
            }
        }
    }

    JLOG(m_journal.trace())
        << "pubAccountTransaction: "
        << "proposed=" << iProposed << ", accepted=" << iAccepted;

    if (!notify.empty() || !accountHistoryNotify.empty())
    {
        auto const& stTxn = transaction.getTxn();

        // Create two different Json objects, for different API versions
        auto const metaRef = std::ref(transaction.getMeta());
        auto const trResult = transaction.getResult();
        MultiApiJson jvObj = transJson(stTxn, trResult, true, ledger, metaRef);

        for (InfoSub::ref isrListener : notify)
        {
            jvObj.visit(
                isrListener->getApiVersion(),  //
                [&](Json::Value const& jv) { isrListener->send(jv, true); });
        }

        if (last)
            jvObj.set(jss::account_history_boundary, true);

        XRPL_ASSERT(
            jvObj.isMember(jss::account_history_tx_stream) ==
                MultiApiJson::none,
            "ripple::NetworkOPsImp::pubAccountTransaction : "
            "account_history_tx_stream not set");
        for (auto& info : accountHistoryNotify)
        {
            auto& index = info.index_;
            if (index->forwardTxIndex_ == 0 && !index->haveHistorical_)
                jvObj.set(jss::account_history_tx_first, true);

            jvObj.set(jss::account_history_tx_index, index->forwardTxIndex_++);

            jvObj.visit(
                info.sink_->getApiVersion(),  //
                [&](Json::Value const& jv) { info.sink_->send(jv, true); });
        }
    }
}

void
NetworkOPsImp::pubProposedAccountTransaction(
    std::shared_ptr<ReadView const> const& ledger,
    std::shared_ptr<STTx const> const& tx,
    TER result)
{
    hash_set<InfoSub::pointer> notify;
    int iProposed = 0;

    std::vector<SubAccountHistoryInfo> accountHistoryNotify;

    {
        std::lock_guard sl(mSubLock);

        if (mSubRTAccount.empty())
            return;

        if (!mSubAccount.empty() || !mSubRTAccount.empty() ||
            !mSubAccountHistory.empty())
        {
            for (auto const& affectedAccount : tx->getMentionedAccounts())
            {
                if (auto simiIt = mSubRTAccount.find(affectedAccount);
                    simiIt != mSubRTAccount.end())
                {
                    auto it = simiIt->second.begin();

                    while (it != simiIt->second.end())
                    {
                        InfoSub::pointer p = it->second.lock();

                        if (p)
                        {
                            notify.insert(p);
                            ++it;
                            ++iProposed;
                        }
                        else
                            it = simiIt->second.erase(it);
                    }
                }
            }
        }
    }

    JLOG(m_journal.trace()) << "pubProposedAccountTransaction: " << iProposed;

    if (!notify.empty() || !accountHistoryNotify.empty())
    {
        // Create two different Json objects, for different API versions
        MultiApiJson jvObj = transJson(tx, result, false, ledger, std::nullopt);

        for (InfoSub::ref isrListener : notify)
            jvObj.visit(
                isrListener->getApiVersion(),  //
                [&](Json::Value const& jv) { isrListener->send(jv, true); });

        XRPL_ASSERT(
            jvObj.isMember(jss::account_history_tx_stream) ==
                MultiApiJson::none,
            "ripple::NetworkOPs::pubProposedAccountTransaction : "
            "account_history_tx_stream not set");
        for (auto& info : accountHistoryNotify)
        {
            auto& index = info.index_;
            if (index->forwardTxIndex_ == 0 && !index->haveHistorical_)
                jvObj.set(jss::account_history_tx_first, true);
            jvObj.set(jss::account_history_tx_index, index->forwardTxIndex_++);
            jvObj.visit(
                info.sink_->getApiVersion(),  //
                [&](Json::Value const& jv) { info.sink_->send(jv, true); });
        }
    }
}

//
// Monitoring
//

void
NetworkOPsImp::subAccount(
    InfoSub::ref isrListener,
    hash_set<AccountID> const& vnaAccountIDs,
    bool rt)
{
    SubInfoMapType& subMap = rt ? mSubRTAccount : mSubAccount;

    for (auto const& naAccountID : vnaAccountIDs)
    {
        JLOG(m_journal.trace())
            << "subAccount: account: " << toBase58(naAccountID);

        isrListener->insertSubAccountInfo(naAccountID, rt);
    }

    std::lock_guard sl(mSubLock);

    for (auto const& naAccountID : vnaAccountIDs)
    {
        auto simIterator = subMap.find(naAccountID);
        if (simIterator == subMap.end())
        {
            // Not found, note that account has a new single listner.
            SubMapType usisElement;
            usisElement[isrListener->getSeq()] = isrListener;
            // VFALCO NOTE This is making a needless copy of naAccountID
            subMap.insert(simIterator, make_pair(naAccountID, usisElement));
        }
        else
        {
            // Found, note that the account has another listener.
            simIterator->second[isrListener->getSeq()] = isrListener;
        }
    }
}

void
NetworkOPsImp::unsubAccount(
    InfoSub::ref isrListener,
    hash_set<AccountID> const& vnaAccountIDs,
    bool rt)
{
    for (auto const& naAccountID : vnaAccountIDs)
    {
        // Remove from the InfoSub
        isrListener->deleteSubAccountInfo(naAccountID, rt);
    }

    // Remove from the server
    unsubAccountInternal(isrListener->getSeq(), vnaAccountIDs, rt);
}

void
NetworkOPsImp::unsubAccountInternal(
    std::uint64_t uSeq,
    hash_set<AccountID> const& vnaAccountIDs,
    bool rt)
{
    std::lock_guard sl(mSubLock);

    SubInfoMapType& subMap = rt ? mSubRTAccount : mSubAccount;

    for (auto const& naAccountID : vnaAccountIDs)
    {
        auto simIterator = subMap.find(naAccountID);

        if (simIterator != subMap.end())
        {
            // Found
            simIterator->second.erase(uSeq);

            if (simIterator->second.empty())
            {
                // Don't need hash entry.
                subMap.erase(simIterator);
            }
        }
    }
}

void
NetworkOPsImp::addAccountHistoryJob(SubAccountHistoryInfoWeak subInfo)
{
    enum DatabaseType { Sqlite, None };
    static auto const databaseType = [&]() -> DatabaseType {
        // Use a dynamic_cast to return DatabaseType::None
        // on failure.
        if (dynamic_cast<SQLiteDatabase*>(&app_.getRelationalDatabase()))
        {
            return DatabaseType::Sqlite;
        }
        return DatabaseType::None;
    }();

    if (databaseType == DatabaseType::None)
    {
        JLOG(m_journal.error())
            << "AccountHistory job for account "
            << toBase58(subInfo.index_->accountId_) << " no database";
        if (auto sptr = subInfo.sinkWptr_.lock(); sptr)
        {
            sptr->send(rpcError(rpcINTERNAL), true);
            unsubAccountHistory(sptr, subInfo.index_->accountId_, false);
        }
        return;
    }

    app_.getJobQueue().addJob(
        jtCLIENT_ACCT_HIST,
        "AccountHistoryTxStream",
        [this, dbType = databaseType, subInfo]() {
            auto const& accountId = subInfo.index_->accountId_;
            auto& lastLedgerSeq = subInfo.index_->historyLastLedgerSeq_;
            auto& txHistoryIndex = subInfo.index_->historyTxIndex_;

            JLOG(m_journal.trace())
                << "AccountHistory job for account " << toBase58(accountId)
                << " started. lastLedgerSeq=" << lastLedgerSeq;

            auto isFirstTx = [&](std::shared_ptr<Transaction> const& tx,
                                 std::shared_ptr<TxMeta> const& meta) -> bool {
                /*
                 * genesis account: first tx is the one with seq 1
                 * other account: first tx is the one created the account
                 */
                if (accountId == genesisAccountId)
                {
                    auto stx = tx->getSTransaction();
                    if (stx->getAccountID(sfAccount) == accountId &&
                        stx->getSeqValue() == 1)
                        return true;
                }

                for (auto& node : meta->getNodes())
                {
                    if (node.getFieldU16(sfLedgerEntryType) != ltACCOUNT_ROOT)
                        continue;

                    if (node.isFieldPresent(sfNewFields))
                    {
                        if (auto inner = dynamic_cast<STObject const*>(
                                node.peekAtPField(sfNewFields));
                            inner)
                        {
                            if (inner->isFieldPresent(sfAccount) &&
                                inner->getAccountID(sfAccount) == accountId)
                            {
                                return true;
                            }
                        }
                    }
                }

                return false;
            };

            auto send = [&](Json::Value const& jvObj,
                            bool unsubscribe) -> bool {
                if (auto sptr = subInfo.sinkWptr_.lock())
                {
                    sptr->send(jvObj, true);
                    if (unsubscribe)
                        unsubAccountHistory(sptr, accountId, false);
                    return true;
                }

                return false;
            };

            auto sendMultiApiJson = [&](MultiApiJson const& jvObj,
                                        bool unsubscribe) -> bool {
                if (auto sptr = subInfo.sinkWptr_.lock())
                {
                    jvObj.visit(
                        sptr->getApiVersion(),  //
                        [&](Json::Value const& jv) { sptr->send(jv, true); });

                    if (unsubscribe)
                        unsubAccountHistory(sptr, accountId, false);
                    return true;
                }

                return false;
            };

            auto getMoreTxns =
                [&](std::uint32_t minLedger,
                    std::uint32_t maxLedger,
                    std::optional<RelationalDatabase::AccountTxMarker> marker)
                -> std::optional<std::pair<
                    RelationalDatabase::AccountTxs,
                    std::optional<RelationalDatabase::AccountTxMarker>>> {
                switch (dbType)
                {
                    case Sqlite: {
                        auto db = static_cast<SQLiteDatabase*>(
                            &app_.getRelationalDatabase());
                        RelationalDatabase::AccountTxPageOptions options{
                            accountId, minLedger, maxLedger, marker, 0, true};
                        return db->newestAccountTxPage(options);
                    }
                    default: {
                        UNREACHABLE(
                            "ripple::NetworkOPsImp::addAccountHistoryJob::"
                            "getMoreTxns : invalid database type");
                        return {};
                    }
                }
            };

            /*
             * search backward until the genesis ledger or asked to stop
             */
            while (lastLedgerSeq >= 2 && !subInfo.index_->stopHistorical_)
            {
                int feeChargeCount = 0;
                if (auto sptr = subInfo.sinkWptr_.lock(); sptr)
                {
                    sptr->getConsumer().charge(Resource::feeMediumBurdenRPC);
                    ++feeChargeCount;
                }
                else
                {
                    JLOG(m_journal.trace())
                        << "AccountHistory job for account "
                        << toBase58(accountId) << " no InfoSub. Fee charged "
                        << feeChargeCount << " times.";
                    return;
                }

                // try to search in 1024 ledgers till reaching genesis ledgers
                auto startLedgerSeq =
                    (lastLedgerSeq > 1024 + 2 ? lastLedgerSeq - 1024 : 2);
                JLOG(m_journal.trace())
                    << "AccountHistory job for account " << toBase58(accountId)
                    << ", working on ledger range [" << startLedgerSeq << ","
                    << lastLedgerSeq << "]";

                auto haveRange = [&]() -> bool {
                    std::uint32_t validatedMin = UINT_MAX;
                    std::uint32_t validatedMax = 0;
                    auto haveSomeValidatedLedgers =
                        app_.getLedgerMaster().getValidatedRange(
                            validatedMin, validatedMax);

                    return haveSomeValidatedLedgers &&
                        validatedMin <= startLedgerSeq &&
                        lastLedgerSeq <= validatedMax;
                }();

                if (!haveRange)
                {
                    JLOG(m_journal.debug())
                        << "AccountHistory reschedule job for account "
                        << toBase58(accountId) << ", incomplete ledger range ["
                        << startLedgerSeq << "," << lastLedgerSeq << "]";
                    setAccountHistoryJobTimer(subInfo);
                    return;
                }

                std::optional<RelationalDatabase::AccountTxMarker> marker{};
                while (!subInfo.index_->stopHistorical_)
                {
                    auto dbResult =
                        getMoreTxns(startLedgerSeq, lastLedgerSeq, marker);
                    if (!dbResult)
                    {
                        JLOG(m_journal.debug())
                            << "AccountHistory job for account "
                            << toBase58(accountId) << " getMoreTxns failed.";
                        send(rpcError(rpcINTERNAL), true);
                        return;
                    }

                    auto const& txns = dbResult->first;
                    marker = dbResult->second;
                    size_t num_txns = txns.size();
                    for (size_t i = 0; i < num_txns; ++i)
                    {
                        auto const& [tx, meta] = txns[i];

                        if (!tx || !meta)
                        {
                            JLOG(m_journal.debug())
                                << "AccountHistory job for account "
                                << toBase58(accountId) << " empty tx or meta.";
                            send(rpcError(rpcINTERNAL), true);
                            return;
                        }
                        auto curTxLedger =
                            app_.getLedgerMaster().getLedgerBySeq(
                                tx->getLedger());
                        if (!curTxLedger)
                        {
                            JLOG(m_journal.debug())
                                << "AccountHistory job for account "
                                << toBase58(accountId) << " no ledger.";
                            send(rpcError(rpcINTERNAL), true);
                            return;
                        }
                        std::shared_ptr<STTx const> stTxn =
                            tx->getSTransaction();
                        if (!stTxn)
                        {
                            JLOG(m_journal.debug())
                                << "AccountHistory job for account "
                                << toBase58(accountId)
                                << " getSTransaction failed.";
                            send(rpcError(rpcINTERNAL), true);
                            return;
                        }

                        auto const mRef = std::ref(*meta);
                        auto const trR = meta->getResultTER();
                        MultiApiJson jvTx =
                            transJson(stTxn, trR, true, curTxLedger, mRef);

                        jvTx.set(
                            jss::account_history_tx_index, txHistoryIndex--);
                        if (i + 1 == num_txns ||
                            txns[i + 1].first->getLedger() != tx->getLedger())
                            jvTx.set(jss::account_history_boundary, true);

                        if (isFirstTx(tx, meta))
                        {
                            jvTx.set(jss::account_history_tx_first, true);
                            sendMultiApiJson(jvTx, false);

                            JLOG(m_journal.trace())
                                << "AccountHistory job for account "
                                << toBase58(accountId)
                                << " done, found last tx.";
                            return;
                        }
                        else
                        {
                            sendMultiApiJson(jvTx, false);
                        }
                    }

                    if (marker)
                    {
                        JLOG(m_journal.trace())
                            << "AccountHistory job for account "
                            << toBase58(accountId)
                            << " paging, marker=" << marker->ledgerSeq << ":"
                            << marker->txnSeq;
                    }
                    else
                    {
                        break;
                    }
                }

                if (!subInfo.index_->stopHistorical_)
                {
                    lastLedgerSeq = startLedgerSeq - 1;
                    if (lastLedgerSeq <= 1)
                    {
                        JLOG(m_journal.trace())
                            << "AccountHistory job for account "
                            << toBase58(accountId)
                            << " done, reached genesis ledger.";
                        return;
                    }
                }
            }
        });
}

void
NetworkOPsImp::subAccountHistoryStart(
    std::shared_ptr<ReadView const> const& ledger,
    SubAccountHistoryInfoWeak& subInfo)
{
    subInfo.index_->separationLedgerSeq_ = ledger->seq();
    auto const& accountId = subInfo.index_->accountId_;
    auto const accountKeylet = keylet::account(accountId);
    if (!ledger->exists(accountKeylet))
    {
        JLOG(m_journal.debug())
            << "subAccountHistoryStart, no account " << toBase58(accountId)
            << ", no need to add AccountHistory job.";
        return;
    }
    if (accountId == genesisAccountId)
    {
        if (auto const sleAcct = ledger->read(accountKeylet); sleAcct)
        {
            if (sleAcct->getFieldU32(sfSequence) == 1)
            {
                JLOG(m_journal.debug())
                    << "subAccountHistoryStart, genesis account "
                    << toBase58(accountId)
                    << " does not have tx, no need to add AccountHistory job.";
                return;
            }
        }
        else
        {
            UNREACHABLE(
                "ripple::NetworkOPsImp::subAccountHistoryStart : failed to "
                "access genesis account");
            return;
        }
    }
    subInfo.index_->historyLastLedgerSeq_ = ledger->seq();
    subInfo.index_->haveHistorical_ = true;

    JLOG(m_journal.debug())
        << "subAccountHistoryStart, add AccountHistory job: accountId="
        << toBase58(accountId) << ", currentLedgerSeq=" << ledger->seq();

    addAccountHistoryJob(subInfo);
}

error_code_i
NetworkOPsImp::subAccountHistory(
    InfoSub::ref isrListener,
    AccountID const& accountId)
{
    if (!isrListener->insertSubAccountHistory(accountId))
    {
        JLOG(m_journal.debug())
            << "subAccountHistory, already subscribed to account "
            << toBase58(accountId);
        return rpcINVALID_PARAMS;
    }

    std::lock_guard sl(mSubLock);
    SubAccountHistoryInfoWeak ahi{
        isrListener, std::make_shared<SubAccountHistoryIndex>(accountId)};
    auto simIterator = mSubAccountHistory.find(accountId);
    if (simIterator == mSubAccountHistory.end())
    {
        hash_map<std::uint64_t, SubAccountHistoryInfoWeak> inner;
        inner.emplace(isrListener->getSeq(), ahi);
        mSubAccountHistory.insert(
            simIterator, std::make_pair(accountId, inner));
    }
    else
    {
        simIterator->second.emplace(isrListener->getSeq(), ahi);
    }

    auto const ledger = app_.getLedgerMaster().getValidatedLedger();
    if (ledger)
    {
        subAccountHistoryStart(ledger, ahi);
    }
    else
    {
        // The node does not have validated ledgers, so wait for
        // one before start streaming.
        // In this case, the subscription is also considered successful.
        JLOG(m_journal.debug())
            << "subAccountHistory, no validated ledger yet, delay start";
    }

    return rpcSUCCESS;
}

void
NetworkOPsImp::unsubAccountHistory(
    InfoSub::ref isrListener,
    AccountID const& account,
    bool historyOnly)
{
    if (!historyOnly)
        isrListener->deleteSubAccountHistory(account);
    unsubAccountHistoryInternal(isrListener->getSeq(), account, historyOnly);
}

void
NetworkOPsImp::unsubAccountHistoryInternal(
    std::uint64_t seq,
    AccountID const& account,
    bool historyOnly)
{
    std::lock_guard sl(mSubLock);
    auto simIterator = mSubAccountHistory.find(account);
    if (simIterator != mSubAccountHistory.end())
    {
        auto& subInfoMap = simIterator->second;
        auto subInfoIter = subInfoMap.find(seq);
        if (subInfoIter != subInfoMap.end())
        {
            subInfoIter->second.index_->stopHistorical_ = true;
        }

        if (!historyOnly)
        {
            simIterator->second.erase(seq);
            if (simIterator->second.empty())
            {
                mSubAccountHistory.erase(simIterator);
            }
        }
        JLOG(m_journal.debug())
            << "unsubAccountHistory, account " << toBase58(account)
            << ", historyOnly = " << (historyOnly ? "true" : "false");
    }
}

bool
NetworkOPsImp::subBook(InfoSub::ref isrListener, Book const& book)
{
    if (auto listeners = app_.getOrderBookDB().makeBookListeners(book))
        listeners->addSubscriber(isrListener);
    else
        UNREACHABLE("ripple::NetworkOPsImp::subBook : null book listeners");
    return true;
}

bool
NetworkOPsImp::unsubBook(std::uint64_t uSeq, Book const& book)
{
    if (auto listeners = app_.getOrderBookDB().getBookListeners(book))
        listeners->removeSubscriber(uSeq);

    return true;
}

std::uint32_t
NetworkOPsImp::acceptLedger(
    std::optional<std::chrono::milliseconds> consensusDelay)
{
    // This code-path is exclusively used when the server is in standalone
    // mode via `ledger_accept`
    XRPL_ASSERT(
        m_standalone, "ripple::NetworkOPsImp::acceptLedger : is standalone");

    if (!m_standalone)
        Throw<std::runtime_error>(
            "Operation only possible in STANDALONE mode.");

    // FIXME Could we improve on this and remove the need for a specialized
    // API in Consensus?
    beginConsensus(m_ledgerMaster.getClosedLedger()->info().hash, {});
    mConsensus.simulate(app_.timeKeeper().closeTime(), consensusDelay);
    return m_ledgerMaster.getCurrentLedger()->info().seq;
}

// <-- bool: true=added, false=already there
bool
NetworkOPsImp::subLedger(InfoSub::ref isrListener, Json::Value& jvResult)
{
    if (auto lpClosed = m_ledgerMaster.getValidatedLedger())
    {
        jvResult[jss::ledger_index] = lpClosed->info().seq;
        jvResult[jss::ledger_hash] = to_string(lpClosed->info().hash);
        jvResult[jss::ledger_time] = Json::Value::UInt(
            lpClosed->info().closeTime.time_since_epoch().count());
        if (!lpClosed->rules().enabled(featureXRPFees))
            jvResult[jss::fee_ref] = Config::FEE_UNITS_DEPRECATED;
        jvResult[jss::fee_base] = lpClosed->fees().base.jsonClipped();
        jvResult[jss::reserve_base] =
            lpClosed->fees().accountReserve(0).jsonClipped();
        jvResult[jss::reserve_inc] = lpClosed->fees().increment.jsonClipped();
    }

    if ((mMode >= OperatingMode::SYNCING) && !isNeedNetworkLedger())
    {
        jvResult[jss::validated_ledgers] =
            app_.getLedgerMaster().getCompleteLedgers();
    }

    std::lock_guard sl(mSubLock);
    return mStreamMaps[sLedger]
        .emplace(isrListener->getSeq(), isrListener)
        .second;
}

// <-- bool: true=added, false=already there
bool
NetworkOPsImp::subBookChanges(InfoSub::ref isrListener)
{
    std::lock_guard sl(mSubLock);
    return mStreamMaps[sBookChanges]
        .emplace(isrListener->getSeq(), isrListener)
        .second;
}

// <-- bool: true=erased, false=was not there
bool
NetworkOPsImp::unsubLedger(std::uint64_t uSeq)
{
    std::lock_guard sl(mSubLock);
    return mStreamMaps[sLedger].erase(uSeq);
}

// <-- bool: true=erased, false=was not there
bool
NetworkOPsImp::unsubBookChanges(std::uint64_t uSeq)
{
    std::lock_guard sl(mSubLock);
    return mStreamMaps[sBookChanges].erase(uSeq);
}

// <-- bool: true=added, false=already there
bool
NetworkOPsImp::subManifests(InfoSub::ref isrListener)
{
    std::lock_guard sl(mSubLock);
    return mStreamMaps[sManifests]
        .emplace(isrListener->getSeq(), isrListener)
        .second;
}

// <-- bool: true=erased, false=was not there
bool
NetworkOPsImp::unsubManifests(std::uint64_t uSeq)
{
    std::lock_guard sl(mSubLock);
    return mStreamMaps[sManifests].erase(uSeq);
}

// <-- bool: true=added, false=already there
bool
NetworkOPsImp::subServer(
    InfoSub::ref isrListener,
    Json::Value& jvResult,
    bool admin)
{
    uint256 uRandom;

    if (m_standalone)
        jvResult[jss::stand_alone] = m_standalone;

    // CHECKME: is it necessary to provide a random number here?
    beast::rngfill(uRandom.begin(), uRandom.size(), crypto_prng());

    auto const& feeTrack = app_.getFeeTrack();
    jvResult[jss::random] = to_string(uRandom);
    jvResult[jss::server_status] = strOperatingMode(admin);
    jvResult[jss::load_base] = feeTrack.getLoadBase();
    jvResult[jss::load_factor] = feeTrack.getLoadFactor();
    jvResult[jss::hostid] = getHostId(admin);
    jvResult[jss::pubkey_node] =
        toBase58(TokenType::NodePublic, app_.nodeIdentity().first);

    std::lock_guard sl(mSubLock);
    return mStreamMaps[sServer]
        .emplace(isrListener->getSeq(), isrListener)
        .second;
}

// <-- bool: true=erased, false=was not there
bool
NetworkOPsImp::unsubServer(std::uint64_t uSeq)
{
    std::lock_guard sl(mSubLock);
    return mStreamMaps[sServer].erase(uSeq);
}

// <-- bool: true=added, false=already there
bool
NetworkOPsImp::subTransactions(InfoSub::ref isrListener)
{
    std::lock_guard sl(mSubLock);
    return mStreamMaps[sTransactions]
        .emplace(isrListener->getSeq(), isrListener)
        .second;
}

// <-- bool: true=erased, false=was not there
bool
NetworkOPsImp::unsubTransactions(std::uint64_t uSeq)
{
    std::lock_guard sl(mSubLock);
    return mStreamMaps[sTransactions].erase(uSeq);
}

// <-- bool: true=added, false=already there
bool
NetworkOPsImp::subRTTransactions(InfoSub::ref isrListener)
{
    std::lock_guard sl(mSubLock);
    return mStreamMaps[sRTTransactions]
        .emplace(isrListener->getSeq(), isrListener)
        .second;
}

// <-- bool: true=erased, false=was not there
bool
NetworkOPsImp::unsubRTTransactions(std::uint64_t uSeq)
{
    std::lock_guard sl(mSubLock);
    return mStreamMaps[sRTTransactions].erase(uSeq);
}

// <-- bool: true=added, false=already there
bool
NetworkOPsImp::subValidations(InfoSub::ref isrListener)
{
    std::lock_guard sl(mSubLock);
    return mStreamMaps[sValidations]
        .emplace(isrListener->getSeq(), isrListener)
        .second;
}

void
NetworkOPsImp::stateAccounting(Json::Value& obj)
{
    accounting_.json(obj);
}

// <-- bool: true=erased, false=was not there
bool
NetworkOPsImp::unsubValidations(std::uint64_t uSeq)
{
    std::lock_guard sl(mSubLock);
    return mStreamMaps[sValidations].erase(uSeq);
}

// <-- bool: true=added, false=already there
bool
NetworkOPsImp::subPeerStatus(InfoSub::ref isrListener)
{
    std::lock_guard sl(mSubLock);
    return mStreamMaps[sPeerStatus]
        .emplace(isrListener->getSeq(), isrListener)
        .second;
}

// <-- bool: true=erased, false=was not there
bool
NetworkOPsImp::unsubPeerStatus(std::uint64_t uSeq)
{
    std::lock_guard sl(mSubLock);
    return mStreamMaps[sPeerStatus].erase(uSeq);
}

// <-- bool: true=added, false=already there
bool
NetworkOPsImp::subConsensus(InfoSub::ref isrListener)
{
    std::lock_guard sl(mSubLock);
    return mStreamMaps[sConsensusPhase]
        .emplace(isrListener->getSeq(), isrListener)
        .second;
}

// <-- bool: true=erased, false=was not there
bool
NetworkOPsImp::unsubConsensus(std::uint64_t uSeq)
{
    std::lock_guard sl(mSubLock);
    return mStreamMaps[sConsensusPhase].erase(uSeq);
}

InfoSub::pointer
NetworkOPsImp::findRpcSub(std::string const& strUrl)
{
    std::lock_guard sl(mSubLock);

    subRpcMapType::iterator it = mRpcSubMap.find(strUrl);

    if (it != mRpcSubMap.end())
        return it->second;

    return InfoSub::pointer();
}

InfoSub::pointer
NetworkOPsImp::addRpcSub(std::string const& strUrl, InfoSub::ref rspEntry)
{
    std::lock_guard sl(mSubLock);

    mRpcSubMap.emplace(strUrl, rspEntry);

    return rspEntry;
}

bool
NetworkOPsImp::tryRemoveRpcSub(std::string const& strUrl)
{
    std::lock_guard sl(mSubLock);
    auto pInfo = findRpcSub(strUrl);

    if (!pInfo)
        return false;

    // check to see if any of the stream maps still hold a weak reference to
    // this entry before removing
    for (SubMapType const& map : mStreamMaps)
    {
        if (map.find(pInfo->getSeq()) != map.end())
            return false;
    }
    mRpcSubMap.erase(strUrl);
    return true;
}

#ifndef USE_NEW_BOOK_PAGE

// NIKB FIXME this should be looked at. There's no reason why this shouldn't
//            work, but it demonstrated poor performance.
//
void
NetworkOPsImp::getBookPage(
    std::shared_ptr<ReadView const>& lpLedger,
    Book const& book,
    AccountID const& uTakerID,
    bool const bProof,
    unsigned int iLimit,
    Json::Value const& jvMarker,
    Json::Value& jvResult)
{  // CAUTION: This is the old get book page logic
    Json::Value& jvOffers =
        (jvResult[jss::offers] = Json::Value(Json::arrayValue));

    std::unordered_map<AccountID, STAmount> umBalance;
    uint256 const uBookBase = getBookBase(book);
    uint256 const uBookEnd = getQualityNext(uBookBase);
    uint256 uTipIndex = uBookBase;

    if (auto stream = m_journal.trace())
    {
        stream << "getBookPage:" << book;
        stream << "getBookPage: uBookBase=" << uBookBase;
        stream << "getBookPage: uBookEnd=" << uBookEnd;
        stream << "getBookPage: uTipIndex=" << uTipIndex;
    }

    ReadView const& view = *lpLedger;

    bool const bGlobalFreeze = isGlobalFrozen(view, book.out.account) ||
        isGlobalFrozen(view, book.in.account);

    bool bDone = false;
    bool bDirectAdvance = true;

    std::shared_ptr<SLE const> sleOfferDir;
    uint256 offerIndex;
    unsigned int uBookEntry;
    STAmount saDirRate;

    auto const rate = transferRate(view, book.out.account);
    auto viewJ = app_.journal("View");

    while (!bDone && iLimit-- > 0)
    {
        if (bDirectAdvance)
        {
            bDirectAdvance = false;

            JLOG(m_journal.trace()) << "getBookPage: bDirectAdvance";

            auto const ledgerIndex = view.succ(uTipIndex, uBookEnd);
            if (ledgerIndex)
                sleOfferDir = view.read(keylet::page(*ledgerIndex));
            else
                sleOfferDir.reset();

            if (!sleOfferDir)
            {
                JLOG(m_journal.trace()) << "getBookPage: bDone";
                bDone = true;
            }
            else
            {
                uTipIndex = sleOfferDir->key();
                saDirRate = amountFromQuality(getQuality(uTipIndex));

                cdirFirst(view, uTipIndex, sleOfferDir, uBookEntry, offerIndex);

                JLOG(m_journal.trace())
                    << "getBookPage:   uTipIndex=" << uTipIndex;
                JLOG(m_journal.trace())
                    << "getBookPage: offerIndex=" << offerIndex;
            }
        }

        if (!bDone)
        {
            auto sleOffer = view.read(keylet::offer(offerIndex));

            if (sleOffer)
            {
                auto const uOfferOwnerID = sleOffer->getAccountID(sfAccount);
                auto const& saTakerGets = sleOffer->getFieldAmount(sfTakerGets);
                auto const& saTakerPays = sleOffer->getFieldAmount(sfTakerPays);
                STAmount saOwnerFunds;
                bool firstOwnerOffer(true);

                if (book.out.account == uOfferOwnerID)
                {
                    // If an offer is selling issuer's own IOUs, it is fully
                    // funded.
                    saOwnerFunds = saTakerGets;
                }
                else if (bGlobalFreeze)
                {
                    // If either asset is globally frozen, consider all offers
                    // that aren't ours to be totally unfunded
                    saOwnerFunds.clear(book.out);
                }
                else
                {
                    auto umBalanceEntry = umBalance.find(uOfferOwnerID);
                    if (umBalanceEntry != umBalance.end())
                    {
                        // Found in running balance table.

                        saOwnerFunds = umBalanceEntry->second;
                        firstOwnerOffer = false;
                    }
                    else
                    {
                        // Did not find balance in table.

                        saOwnerFunds = accountHolds(
                            view,
                            uOfferOwnerID,
                            book.out.currency,
                            book.out.account,
                            fhZERO_IF_FROZEN,
                            viewJ);

                        if (saOwnerFunds < beast::zero)
                        {
                            // Treat negative funds as zero.

                            saOwnerFunds.clear();
                        }
                    }
                }

                Json::Value jvOffer = sleOffer->getJson(JsonOptions::none);

                STAmount saTakerGetsFunded;
                STAmount saOwnerFundsLimit = saOwnerFunds;
                Rate offerRate = parityRate;

                if (rate != parityRate
                    // Have a tranfer fee.
                    && uTakerID != book.out.account
                    // Not taking offers of own IOUs.
                    && book.out.account != uOfferOwnerID)
                // Offer owner not issuing ownfunds
                {
                    // Need to charge a transfer fee to offer owner.
                    offerRate = rate;
                    saOwnerFundsLimit = divide(saOwnerFunds, offerRate);
                }

                if (saOwnerFundsLimit >= saTakerGets)
                {
                    // Sufficient funds no shenanigans.
                    saTakerGetsFunded = saTakerGets;
                }
                else
                {
                    // Only provide, if not fully funded.

                    saTakerGetsFunded = saOwnerFundsLimit;

                    saTakerGetsFunded.setJson(jvOffer[jss::taker_gets_funded]);
                    std::min(
                        saTakerPays,
                        multiply(
                            saTakerGetsFunded, saDirRate, saTakerPays.issue()))
                        .setJson(jvOffer[jss::taker_pays_funded]);
                }

                STAmount saOwnerPays = (parityRate == offerRate)
                    ? saTakerGetsFunded
                    : std::min(
                          saOwnerFunds, multiply(saTakerGetsFunded, offerRate));

                umBalance[uOfferOwnerID] = saOwnerFunds - saOwnerPays;

                // Include all offers funded and unfunded
                Json::Value& jvOf = jvOffers.append(jvOffer);
                jvOf[jss::quality] = saDirRate.getText();

                if (firstOwnerOffer)
                    jvOf[jss::owner_funds] = saOwnerFunds.getText();
            }
            else
            {
                JLOG(m_journal.warn()) << "Missing offer";
            }

            if (!cdirNext(view, uTipIndex, sleOfferDir, uBookEntry, offerIndex))
            {
                bDirectAdvance = true;
            }
            else
            {
                JLOG(m_journal.trace())
                    << "getBookPage: offerIndex=" << offerIndex;
            }
        }
    }

    //  jvResult[jss::marker]  = Json::Value(Json::arrayValue);
    //  jvResult[jss::nodes]   = Json::Value(Json::arrayValue);
}

#else

// This is the new code that uses the book iterators
// It has temporarily been disabled

void
NetworkOPsImp::getBookPage(
    std::shared_ptr<ReadView const> lpLedger,
    Book const& book,
    AccountID const& uTakerID,
    bool const bProof,
    unsigned int iLimit,
    Json::Value const& jvMarker,
    Json::Value& jvResult)
{
    auto& jvOffers = (jvResult[jss::offers] = Json::Value(Json::arrayValue));

    std::map<AccountID, STAmount> umBalance;

    MetaView lesActive(lpLedger, tapNONE, true);
    OrderBookIterator obIterator(lesActive, book);

    auto const rate = transferRate(lesActive, book.out.account);

    bool const bGlobalFreeze = lesActive.isGlobalFrozen(book.out.account) ||
        lesActive.isGlobalFrozen(book.in.account);

    while (iLimit-- > 0 && obIterator.nextOffer())
    {
        SLE::pointer sleOffer = obIterator.getCurrentOffer();
        if (sleOffer)
        {
            auto const uOfferOwnerID = sleOffer->getAccountID(sfAccount);
            auto const& saTakerGets = sleOffer->getFieldAmount(sfTakerGets);
            auto const& saTakerPays = sleOffer->getFieldAmount(sfTakerPays);
            STAmount saDirRate = obIterator.getCurrentRate();
            STAmount saOwnerFunds;

            if (book.out.account == uOfferOwnerID)
            {
                // If offer is selling issuer's own IOUs, it is fully funded.
                saOwnerFunds = saTakerGets;
            }
            else if (bGlobalFreeze)
            {
                // If either asset is globally frozen, consider all offers
                // that aren't ours to be totally unfunded
                saOwnerFunds.clear(book.out);
            }
            else
            {
                auto umBalanceEntry = umBalance.find(uOfferOwnerID);

                if (umBalanceEntry != umBalance.end())
                {
                    // Found in running balance table.

                    saOwnerFunds = umBalanceEntry->second;
                }
                else
                {
                    // Did not find balance in table.

                    saOwnerFunds = lesActive.accountHolds(
                        uOfferOwnerID,
                        book.out.currency,
                        book.out.account,
                        fhZERO_IF_FROZEN);

                    if (saOwnerFunds.isNegative())
                    {
                        // Treat negative funds as zero.

                        saOwnerFunds.zero();
                    }
                }
            }

            Json::Value jvOffer = sleOffer->getJson(JsonOptions::none);

            STAmount saTakerGetsFunded;
            STAmount saOwnerFundsLimit = saOwnerFunds;
            Rate offerRate = parityRate;

            if (rate != parityRate
                // Have a tranfer fee.
                && uTakerID != book.out.account
                // Not taking offers of own IOUs.
                && book.out.account != uOfferOwnerID)
            // Offer owner not issuing ownfunds
            {
                // Need to charge a transfer fee to offer owner.
                offerRate = rate;
                saOwnerFundsLimit = divide(saOwnerFunds, offerRate);
            }

            if (saOwnerFundsLimit >= saTakerGets)
            {
                // Sufficient funds no shenanigans.
                saTakerGetsFunded = saTakerGets;
            }
            else
            {
                // Only provide, if not fully funded.
                saTakerGetsFunded = saOwnerFundsLimit;

                saTakerGetsFunded.setJson(jvOffer[jss::taker_gets_funded]);

                // TOOD(tom): The result of this expression is not used - what's
                // going on here?
                std::min(
                    saTakerPays,
                    multiply(saTakerGetsFunded, saDirRate, saTakerPays.issue()))
                    .setJson(jvOffer[jss::taker_pays_funded]);
            }

            STAmount saOwnerPays = (parityRate == offerRate)
                ? saTakerGetsFunded
                : std::min(
                      saOwnerFunds, multiply(saTakerGetsFunded, offerRate));

            umBalance[uOfferOwnerID] = saOwnerFunds - saOwnerPays;

            if (!saOwnerFunds.isZero() || uOfferOwnerID == uTakerID)
            {
                // Only provide funded offers and offers of the taker.
                Json::Value& jvOf = jvOffers.append(jvOffer);
                jvOf[jss::quality] = saDirRate.getText();
            }
        }
    }

    //  jvResult[jss::marker]  = Json::Value(Json::arrayValue);
    //  jvResult[jss::nodes]   = Json::Value(Json::arrayValue);
}

#endif

inline void
NetworkOPsImp::collect_metrics()
{
    auto [counters, mode, start, initialSync] = accounting_.getCounterData();
    auto const current = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);
    counters[static_cast<std::size_t>(mode)].dur += current;

    std::lock_guard lock(m_statsMutex);
    m_stats.disconnected_duration.set(
        counters[static_cast<std::size_t>(OperatingMode::DISCONNECTED)]
            .dur.count());
    m_stats.connected_duration.set(
        counters[static_cast<std::size_t>(OperatingMode::CONNECTED)]
            .dur.count());
    m_stats.syncing_duration.set(
        counters[static_cast<std::size_t>(OperatingMode::SYNCING)].dur.count());
    m_stats.tracking_duration.set(
        counters[static_cast<std::size_t>(OperatingMode::TRACKING)]
            .dur.count());
    m_stats.full_duration.set(
        counters[static_cast<std::size_t>(OperatingMode::FULL)].dur.count());

    m_stats.disconnected_transitions.set(
        counters[static_cast<std::size_t>(OperatingMode::DISCONNECTED)]
            .transitions);
    m_stats.connected_transitions.set(
        counters[static_cast<std::size_t>(OperatingMode::CONNECTED)]
            .transitions);
    m_stats.syncing_transitions.set(
        counters[static_cast<std::size_t>(OperatingMode::SYNCING)].transitions);
    m_stats.tracking_transitions.set(
        counters[static_cast<std::size_t>(OperatingMode::TRACKING)]
            .transitions);
    m_stats.full_transitions.set(
        counters[static_cast<std::size_t>(OperatingMode::FULL)].transitions);
}

void
NetworkOPsImp::StateAccounting::mode(OperatingMode om)
{
    auto now = std::chrono::steady_clock::now();

    std::lock_guard lock(mutex_);
    ++counters_[static_cast<std::size_t>(om)].transitions;
    if (om == OperatingMode::FULL &&
        counters_[static_cast<std::size_t>(om)].transitions == 1)
    {
        initialSyncUs_ = std::chrono::duration_cast<std::chrono::microseconds>(
                             now - processStart_)
                             .count();
    }
    counters_[static_cast<std::size_t>(mode_)].dur +=
        std::chrono::duration_cast<std::chrono::microseconds>(now - start_);

    mode_ = om;
    start_ = now;
}

void
NetworkOPsImp::StateAccounting::json(Json::Value& obj) const
{
    auto [counters, mode, start, initialSync] = getCounterData();
    auto const current = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);
    counters[static_cast<std::size_t>(mode)].dur += current;

    obj[jss::state_accounting] = Json::objectValue;
    for (std::size_t i = static_cast<std::size_t>(OperatingMode::DISCONNECTED);
         i <= static_cast<std::size_t>(OperatingMode::FULL);
         ++i)
    {
        obj[jss::state_accounting][states_[i]] = Json::objectValue;
        auto& state = obj[jss::state_accounting][states_[i]];
        state[jss::transitions] = std::to_string(counters[i].transitions);
        state[jss::duration_us] = std::to_string(counters[i].dur.count());
    }
    obj[jss::server_state_duration_us] = std::to_string(current.count());
    if (initialSync)
        obj[jss::initial_sync_duration_us] = std::to_string(initialSync);
}

//------------------------------------------------------------------------------

std::unique_ptr<NetworkOPs>
make_NetworkOPs(
    Application& app,
    NetworkOPs::clock_type& clock,
    bool standalone,
    std::size_t minPeerCount,
    bool startvalid,
    JobQueue& job_queue,
    LedgerMaster& ledgerMaster,
    ValidatorKeys const& validatorKeys,
    boost::asio::io_service& io_svc,
    beast::Journal journal,
    beast::insight::Collector::ptr const& collector)
{
    return std::make_unique<NetworkOPsImp>(
        app,
        clock,
        standalone,
        minPeerCount,
        startvalid,
        job_queue,
        ledgerMaster,
        validatorKeys,
        io_svc,
        journal,
        collector);
}

}  // namespace ripple
