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

#include <xrpld/app/consensus/RCLValidations.h>
#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/InboundTransactions.h>
#include <xrpld/app/ledger/LedgerCleaner.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerReplayer.h>
#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/ledger/OrderBookDB.h>
#include <xrpld/app/ledger/PendingSaves.h>
#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/main/BasicApp.h>
#include <xrpld/app/main/DBInit.h>
#include <xrpld/app/main/GRPCServer.h>
#include <xrpld/app/main/LoadManager.h>
#include <xrpld/app/main/NodeIdentity.h>
#include <xrpld/app/main/NodeStoreScheduler.h>
#include <xrpld/app/misc/AmendmentTable.h>
#include <xrpld/app/misc/HashRouter.h>
#include <xrpld/app/misc/LoadFeeTrack.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/app/misc/SHAMapStore.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/app/misc/ValidatorKeys.h>
#include <xrpld/app/misc/ValidatorSite.h>
#include <xrpld/app/paths/PathRequests.h>
#include <xrpld/app/rdb/RelationalDatabase.h>
#include <xrpld/app/rdb/Wallet.h>
#include <xrpld/app/tx/apply.h>
#include <xrpld/core/DatabaseCon.h>
#include <xrpld/nodestore/DummyScheduler.h>
#include <xrpld/overlay/Cluster.h>
#include <xrpld/overlay/PeerReservationTable.h>
#include <xrpld/overlay/PeerSet.h>
#include <xrpld/overlay/make_Overlay.h>
#include <xrpld/perflog/PerfLog.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/shamap/NodeFamily.h>

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/ResolverAsio.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/asio/io_latency_probe.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/crypto/csprng.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/protocol/BuildInfo.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/resource/Fees.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

#include <date/date.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <utility>

namespace ripple {

static void
fixConfigPorts(Config& config, Endpoints const& endpoints);

// VFALCO TODO Move the function definitions into the class declaration
class ApplicationImp : public Application, public BasicApp
{
private:
    class io_latency_sampler
    {
    private:
        beast::insight::Event m_event;
        beast::Journal m_journal;
        beast::io_latency_probe<std::chrono::steady_clock> m_probe;
        std::atomic<std::chrono::milliseconds> lastSample_;

    public:
        io_latency_sampler(
            beast::insight::Event ev,
            beast::Journal journal,
            std::chrono::milliseconds interval,
            boost::asio::io_service& ios)
            : m_event(ev)
            , m_journal(journal)
            , m_probe(interval, ios)
            , lastSample_{}
        {
        }

        void
        start()
        {
            m_probe.sample(std::ref(*this));
        }

        template <class Duration>
        void
        operator()(Duration const& elapsed)
        {
            using namespace std::chrono;
            auto const lastSample = ceil<milliseconds>(elapsed);

            lastSample_ = lastSample;

            if (lastSample >= 10ms)
                m_event.notify(lastSample);
            if (lastSample >= 500ms)
            {
                JLOG(m_journal.warn())
                    << "io_service latency = " << lastSample.count();
            }
        }

        std::chrono::milliseconds
        get() const
        {
            return lastSample_.load();
        }

        void
        cancel()
        {
            m_probe.cancel();
        }

        void
        cancel_async()
        {
            m_probe.cancel_async();
        }
    };

public:
    std::unique_ptr<Config> config_;
    std::unique_ptr<Logs> logs_;
    std::unique_ptr<TimeKeeper> timeKeeper_;

    std::uint64_t const instanceCookie_;

    beast::Journal m_journal;
    std::unique_ptr<perf::PerfLog> perfLog_;
    Application::MutexType m_masterMutex;

    // Required by the SHAMapStore
    TransactionMaster m_txMaster;

    std::unique_ptr<CollectorManager> m_collectorManager;
    std::unique_ptr<JobQueue> m_jobQueue;
    NodeStoreScheduler m_nodeStoreScheduler;
    std::unique_ptr<SHAMapStore> m_shaMapStore;
    PendingSaves pendingSaves_;
    std::optional<OpenLedger> openLedger_;

    NodeCache m_tempNodeCache;
    CachedSLEs cachedSLEs_;
    std::optional<std::pair<PublicKey, SecretKey>> nodeIdentity_;
    ValidatorKeys const validatorKeys_;

    std::unique_ptr<Resource::Manager> m_resourceManager;

    std::unique_ptr<NodeStore::Database> m_nodeStore;
    NodeFamily nodeFamily_;
    // VFALCO TODO Make OrderBookDB abstract
    OrderBookDB m_orderBookDB;
    std::unique_ptr<PathRequests> m_pathRequests;
    std::unique_ptr<LedgerMaster> m_ledgerMaster;
    std::unique_ptr<LedgerCleaner> ledgerCleaner_;
    std::unique_ptr<InboundLedgers> m_inboundLedgers;
    std::unique_ptr<InboundTransactions> m_inboundTransactions;
    std::unique_ptr<LedgerReplayer> m_ledgerReplayer;
    TaggedCache<uint256, AcceptedLedger> m_acceptedLedgerCache;
    std::unique_ptr<NetworkOPs> m_networkOPs;
    std::unique_ptr<Cluster> cluster_;
    std::unique_ptr<PeerReservationTable> peerReservations_;
    std::unique_ptr<ManifestCache> validatorManifests_;
    std::unique_ptr<ManifestCache> publisherManifests_;
    std::unique_ptr<ValidatorList> validators_;
    std::unique_ptr<ValidatorSite> validatorSites_;
    std::unique_ptr<ServerHandler> serverHandler_;
    std::unique_ptr<AmendmentTable> m_amendmentTable;
    std::unique_ptr<LoadFeeTrack> mFeeTrack;
    std::unique_ptr<HashRouter> hashRouter_;
    RCLValidations mValidations;
    std::unique_ptr<LoadManager> m_loadManager;
    std::unique_ptr<TxQ> txQ_;
    ClosureCounter<void, boost::system::error_code const&> waitHandlerCounter_;
    boost::asio::steady_timer sweepTimer_;
    boost::asio::steady_timer entropyTimer_;

    std::unique_ptr<RelationalDatabase> mRelationalDatabase;
    std::unique_ptr<DatabaseCon> mWalletDB;
    std::unique_ptr<Overlay> overlay_;
    std::optional<uint256> trapTxID_;

    boost::asio::signal_set m_signals;

    // Once we get C++20, we could use `std::atomic_flag` for `isTimeToStop`
    // and eliminate the need for the condition variable and the mutex.
    std::condition_variable stoppingCondition_;
    mutable std::mutex stoppingMutex_;
    std::atomic<bool> isTimeToStop = false;

    std::atomic<bool> checkSigs_;

    std::unique_ptr<ResolverAsio> m_resolver;

    io_latency_sampler m_io_latency_sampler;

    std::unique_ptr<GRPCServer> grpcServer_;

    //--------------------------------------------------------------------------

    static std::size_t
    numberOfThreads(Config const& config)
    {
#if RIPPLE_SINGLE_IO_SERVICE_THREAD
        return 1;
#else

        if (config.IO_WORKERS > 0)
            return config.IO_WORKERS;

        auto const cores = std::thread::hardware_concurrency();

        // Use a single thread when running on under-provisioned systems
        // or if we are configured to use minimal resources.
        if ((cores == 1) || ((config.NODE_SIZE == 0) && (cores == 2)))
            return 1;

        // Otherwise, prefer six threads.
        return 6;
#endif
    }

    //--------------------------------------------------------------------------

    ApplicationImp(
        std::unique_ptr<Config> config,
        std::unique_ptr<Logs> logs,
        std::unique_ptr<TimeKeeper> timeKeeper)
        : BasicApp(numberOfThreads(*config))
        , config_(std::move(config))
        , logs_(std::move(logs))
        , timeKeeper_(std::move(timeKeeper))
        , instanceCookie_(
              1 +
              rand_int(
                  crypto_prng(),
                  std::numeric_limits<std::uint64_t>::max() - 1))
        , m_journal(logs_->journal("Application"))

        // PerfLog must be started before any other threads are launched.
        , perfLog_(perf::make_PerfLog(
              perf::setup_PerfLog(
                  config_->section("perf"),
                  config_->CONFIG_DIR),
              *this,
              logs_->journal("PerfLog"),
              [this] { signalStop("PerfLog"); }))

        , m_txMaster(*this)

        , m_collectorManager(make_CollectorManager(
              config_->section(SECTION_INSIGHT),
              logs_->journal("Collector")))

        , m_jobQueue(std::make_unique<JobQueue>(
              [](std::unique_ptr<Config> const& config) {
                  if (config->standalone() && !config->FORCE_MULTI_THREAD)
                      return 1;

                  if (config->WORKERS)
                      return config->WORKERS;

                  auto count =
                      static_cast<int>(std::thread::hardware_concurrency());

                  // Be more aggressive about the number of threads to use
                  // for the job queue if the server is configured as "large"
                  // or "huge" if there are enough cores.
                  if (config->NODE_SIZE >= 4 && count >= 16)
                      count = 6 + std::min(count, 8);
                  else if (config->NODE_SIZE >= 3 && count >= 8)
                      count = 4 + std::min(count, 6);
                  else
                      count = 2 + std::min(count, 4);

                  return count;
              }(config_),
              m_collectorManager->group("jobq"),
              logs_->journal("JobQueue"),
              *logs_,
              *perfLog_))

        , m_nodeStoreScheduler(*m_jobQueue)

        , m_shaMapStore(make_SHAMapStore(
              *this,
              m_nodeStoreScheduler,
              logs_->journal("SHAMapStore")))

        , m_tempNodeCache(
              "NodeCache",
              16384,
              std::chrono::seconds{90},
              stopwatch(),
              logs_->journal("TaggedCache"))

        , cachedSLEs_(
              "Cached SLEs",
              0,
              std::chrono::minutes(1),
              stopwatch(),
              logs_->journal("CachedSLEs"))

        , validatorKeys_(*config_, m_journal)

        , m_resourceManager(Resource::make_Manager(
              m_collectorManager->collector(),
              logs_->journal("Resource")))

        , m_nodeStore(m_shaMapStore->makeNodeStore(
              config_->PREFETCH_WORKERS > 0 ? config_->PREFETCH_WORKERS : 4))

        , nodeFamily_(*this, *m_collectorManager)

        , m_orderBookDB(*this)

        , m_pathRequests(std::make_unique<PathRequests>(
              *this,
              logs_->journal("PathRequest"),
              m_collectorManager->collector()))

        , m_ledgerMaster(std::make_unique<LedgerMaster>(
              *this,
              stopwatch(),
              m_collectorManager->collector(),
              logs_->journal("LedgerMaster")))

        , ledgerCleaner_(
              make_LedgerCleaner(*this, logs_->journal("LedgerCleaner")))

        // VFALCO NOTE must come before NetworkOPs to prevent a crash due
        //             to dependencies in the destructor.
        //
        , m_inboundLedgers(make_InboundLedgers(
              *this,
              stopwatch(),
              m_collectorManager->collector()))

        , m_inboundTransactions(make_InboundTransactions(
              *this,
              m_collectorManager->collector(),
              [this](std::shared_ptr<SHAMap> const& set, bool fromAcquire) {
                  gotTXSet(set, fromAcquire);
              }))

        , m_ledgerReplayer(std::make_unique<LedgerReplayer>(
              *this,
              *m_inboundLedgers,
              make_PeerSetBuilder(*this)))

        , m_acceptedLedgerCache(
              "AcceptedLedger",
              4,
              std::chrono::minutes{1},
              stopwatch(),
              logs_->journal("TaggedCache"))

        , m_networkOPs(make_NetworkOPs(
              *this,
              stopwatch(),
              config_->standalone(),
              config_->NETWORK_QUORUM,
              config_->START_VALID,
              *m_jobQueue,
              *m_ledgerMaster,
              validatorKeys_,
              get_io_service(),
              logs_->journal("NetworkOPs"),
              m_collectorManager->collector()))

        , cluster_(std::make_unique<Cluster>(logs_->journal("Overlay")))

        , peerReservations_(std::make_unique<PeerReservationTable>(
              logs_->journal("PeerReservationTable")))

        , validatorManifests_(
              std::make_unique<ManifestCache>(logs_->journal("ManifestCache")))

        , publisherManifests_(
              std::make_unique<ManifestCache>(logs_->journal("ManifestCache")))

        , validators_(std::make_unique<ValidatorList>(
              *validatorManifests_,
              *publisherManifests_,
              *timeKeeper_,
              config_->legacy("database_path"),
              logs_->journal("ValidatorList"),
              config_->VALIDATION_QUORUM))

        , validatorSites_(std::make_unique<ValidatorSite>(*this))

        , serverHandler_(make_ServerHandler(
              *this,
              get_io_service(),
              *m_jobQueue,
              *m_networkOPs,
              *m_resourceManager,
              *m_collectorManager))

        , mFeeTrack(
              std::make_unique<LoadFeeTrack>(logs_->journal("LoadManager")))

        , hashRouter_(std::make_unique<HashRouter>(
              setup_HashRouter(*config_),
              stopwatch()))

        , mValidations(
              ValidationParms(),
              stopwatch(),
              *this,
              logs_->journal("Validations"))

        , m_loadManager(make_LoadManager(*this, logs_->journal("LoadManager")))

        , txQ_(
              std::make_unique<TxQ>(setup_TxQ(*config_), logs_->journal("TxQ")))

        , sweepTimer_(get_io_service())

        , entropyTimer_(get_io_service())

        , m_signals(get_io_service())

        , checkSigs_(true)

        , m_resolver(
              ResolverAsio::New(get_io_service(), logs_->journal("Resolver")))

        , m_io_latency_sampler(
              m_collectorManager->collector()->make_event("ios_latency"),
              logs_->journal("Application"),
              std::chrono::milliseconds(100),
              get_io_service())
        , grpcServer_(std::make_unique<GRPCServer>(*this))
    {
        initAccountIdCache(config_->getValueFor(SizedItem::accountIdCacheSize));

        add(m_resourceManager.get());

        //
        // VFALCO - READ THIS!
        //
        //  Do not start threads, open sockets, or do any sort of "real work"
        //  inside the constructor. Put it in start instead. Or if you must,
        //  put it in setup (but everything in setup should be moved to start
        //  anyway.
        //
        //  The reason is that the unit tests require an Application object to
        //  be created. But we don't actually start all the threads, sockets,
        //  and services when running the unit tests. Therefore anything which
        //  needs to be stopped will not get stopped correctly if it is
        //  started in this constructor.
        //

        add(ledgerCleaner_.get());
    }

    //--------------------------------------------------------------------------

    bool
    setup(boost::program_options::variables_map const& cmdline) override;
    void
    start(bool withTimers) override;
    void
    run() override;
    void
    signalStop(std::string msg) override;
    bool
    checkSigs() const override;
    void
    checkSigs(bool) override;
    bool
    isStopping() const override;
    int
    fdRequired() const override;

    //--------------------------------------------------------------------------

    std::uint64_t
    instanceID() const override
    {
        return instanceCookie_;
    }

    Logs&
    logs() override
    {
        return *logs_;
    }

    Config&
    config() override
    {
        return *config_;
    }

    CollectorManager&
    getCollectorManager() override
    {
        return *m_collectorManager;
    }

    Family&
    getNodeFamily() override
    {
        return nodeFamily_;
    }

    TimeKeeper&
    timeKeeper() override
    {
        return *timeKeeper_;
    }

    JobQueue&
    getJobQueue() override
    {
        return *m_jobQueue;
    }

    std::pair<PublicKey, SecretKey> const&
    nodeIdentity() override
    {
        if (nodeIdentity_)
            return *nodeIdentity_;

        LogicError(
            "Accessing Application::nodeIdentity() before it is initialized.");
    }

    std::optional<PublicKey const>
    getValidationPublicKey() const override
    {
        if (!validatorKeys_.keys)
            return {};

        return validatorKeys_.keys->publicKey;
    }

    NetworkOPs&
    getOPs() override
    {
        return *m_networkOPs;
    }

    virtual ServerHandler&
    getServerHandler() override
    {
        XRPL_ASSERT(
            serverHandler_,
            "ripple::ApplicationImp::getServerHandler : non-null server "
            "handle");
        return *serverHandler_;
    }

    boost::asio::io_service&
    getIOService() override
    {
        return get_io_service();
    }

    std::chrono::milliseconds
    getIOLatency() override
    {
        return m_io_latency_sampler.get();
    }

    LedgerMaster&
    getLedgerMaster() override
    {
        return *m_ledgerMaster;
    }

    LedgerCleaner&
    getLedgerCleaner() override
    {
        return *ledgerCleaner_;
    }

    LedgerReplayer&
    getLedgerReplayer() override
    {
        return *m_ledgerReplayer;
    }

    InboundLedgers&
    getInboundLedgers() override
    {
        return *m_inboundLedgers;
    }

    InboundTransactions&
    getInboundTransactions() override
    {
        return *m_inboundTransactions;
    }

    TaggedCache<uint256, AcceptedLedger>&
    getAcceptedLedgerCache() override
    {
        return m_acceptedLedgerCache;
    }

    void
    gotTXSet(std::shared_ptr<SHAMap> const& set, bool fromAcquire)
    {
        if (set)
            m_networkOPs->mapComplete(set, fromAcquire);
    }

    TransactionMaster&
    getMasterTransaction() override
    {
        return m_txMaster;
    }

    perf::PerfLog&
    getPerfLog() override
    {
        return *perfLog_;
    }

    NodeCache&
    getTempNodeCache() override
    {
        return m_tempNodeCache;
    }

    NodeStore::Database&
    getNodeStore() override
    {
        return *m_nodeStore;
    }

    Application::MutexType&
    getMasterMutex() override
    {
        return m_masterMutex;
    }

    LoadManager&
    getLoadManager() override
    {
        return *m_loadManager;
    }

    Resource::Manager&
    getResourceManager() override
    {
        return *m_resourceManager;
    }

    OrderBookDB&
    getOrderBookDB() override
    {
        return m_orderBookDB;
    }

    PathRequests&
    getPathRequests() override
    {
        return *m_pathRequests;
    }

    CachedSLEs&
    cachedSLEs() override
    {
        return cachedSLEs_;
    }

    AmendmentTable&
    getAmendmentTable() override
    {
        return *m_amendmentTable;
    }

    LoadFeeTrack&
    getFeeTrack() override
    {
        return *mFeeTrack;
    }

    HashRouter&
    getHashRouter() override
    {
        return *hashRouter_;
    }

    RCLValidations&
    getValidations() override
    {
        return mValidations;
    }

    ValidatorList&
    validators() override
    {
        return *validators_;
    }

    ValidatorSite&
    validatorSites() override
    {
        return *validatorSites_;
    }

    ManifestCache&
    validatorManifests() override
    {
        return *validatorManifests_;
    }

    ManifestCache&
    publisherManifests() override
    {
        return *publisherManifests_;
    }

    Cluster&
    cluster() override
    {
        return *cluster_;
    }

    PeerReservationTable&
    peerReservations() override
    {
        return *peerReservations_;
    }

    SHAMapStore&
    getSHAMapStore() override
    {
        return *m_shaMapStore;
    }

    PendingSaves&
    pendingSaves() override
    {
        return pendingSaves_;
    }

    OpenLedger&
    openLedger() override
    {
        return *openLedger_;
    }

    OpenLedger const&
    openLedger() const override
    {
        return *openLedger_;
    }

    Overlay&
    overlay() override
    {
        XRPL_ASSERT(
            overlay_, "ripple::ApplicationImp::overlay : non-null overlay");
        return *overlay_;
    }

    TxQ&
    getTxQ() override
    {
        XRPL_ASSERT(
            txQ_,
            "ripple::ApplicationImp::getTxQ : non-null transaction queue");
        return *txQ_;
    }

    RelationalDatabase&
    getRelationalDatabase() override
    {
        XRPL_ASSERT(
            mRelationalDatabase,
            "ripple::ApplicationImp::getRelationalDatabase : non-null "
            "relational database");
        return *mRelationalDatabase;
    }

    DatabaseCon&
    getWalletDB() override
    {
        XRPL_ASSERT(
            mWalletDB,
            "ripple::ApplicationImp::getWalletDB : non-null wallet database");
        return *mWalletDB;
    }

    bool
    serverOkay(std::string& reason) override;

    beast::Journal
    journal(std::string const& name) override;

    //--------------------------------------------------------------------------

    bool
    initRelationalDatabase()
    {
        XRPL_ASSERT(
            mWalletDB.get() == nullptr,
            "ripple::ApplicationImp::initRelationalDatabase : null wallet "
            "database");

        try
        {
            mRelationalDatabase =
                RelationalDatabase::init(*this, *config_, *m_jobQueue);

            // wallet database
            auto setup = setup_DatabaseCon(*config_, m_journal);
            setup.useGlobalPragma = false;

            mWalletDB = makeWalletDB(setup, m_journal);
        }
        catch (std::exception const& e)
        {
            JLOG(m_journal.fatal())
                << "Failed to initialize SQL databases: " << e.what();
            return false;
        }

        return true;
    }

    bool
    initNodeStore()
    {
        if (config_->doImport)
        {
            auto j = logs_->journal("NodeObject");
            NodeStore::DummyScheduler dummyScheduler;
            std::unique_ptr<NodeStore::Database> source =
                NodeStore::Manager::instance().make_Database(
                    megabytes(config_->getValueFor(
                        SizedItem::burstSize, std::nullopt)),
                    dummyScheduler,
                    0,
                    config_->section(ConfigSection::importNodeDatabase()),
                    j);

            JLOG(j.warn()) << "Starting node import from '" << source->getName()
                           << "' to '" << m_nodeStore->getName() << "'.";

            using namespace std::chrono;
            auto const start = steady_clock::now();

            m_nodeStore->importDatabase(*source);

            auto const elapsed =
                duration_cast<seconds>(steady_clock::now() - start);
            JLOG(j.warn()) << "Node import from '" << source->getName()
                           << "' took " << elapsed.count() << " seconds.";
        }

        return true;
    }

    //--------------------------------------------------------------------------
    //
    // PropertyStream
    //

    void
    onWrite(beast::PropertyStream::Map& stream) override
    {
    }

    //--------------------------------------------------------------------------

    void
    setSweepTimer()
    {
        // Only start the timer if waitHandlerCounter_ is not yet joined.
        if (auto optionalCountedHandler = waitHandlerCounter_.wrap(
                [this](boost::system::error_code const& e) {
                    if (e.value() == boost::system::errc::success)
                    {
                        m_jobQueue->addJob(
                            jtSWEEP, "sweep", [this]() { doSweep(); });
                    }
                    // Recover as best we can if an unexpected error occurs.
                    if (e.value() != boost::system::errc::success &&
                        e.value() != boost::asio::error::operation_aborted)
                    {
                        // Try again later and hope for the best.
                        JLOG(m_journal.error())
                            << "Sweep timer got error '" << e.message()
                            << "'.  Restarting timer.";
                        setSweepTimer();
                    }
                }))
        {
            using namespace std::chrono;
            sweepTimer_.expires_from_now(
                seconds{config_->SWEEP_INTERVAL.value_or(
                    config_->getValueFor(SizedItem::sweepInterval))});
            sweepTimer_.async_wait(std::move(*optionalCountedHandler));
        }
    }

    void
    setEntropyTimer()
    {
        // Only start the timer if waitHandlerCounter_ is not yet joined.
        if (auto optionalCountedHandler = waitHandlerCounter_.wrap(
                [this](boost::system::error_code const& e) {
                    if (e.value() == boost::system::errc::success)
                    {
                        crypto_prng().mix_entropy();
                        setEntropyTimer();
                    }
                    // Recover as best we can if an unexpected error occurs.
                    if (e.value() != boost::system::errc::success &&
                        e.value() != boost::asio::error::operation_aborted)
                    {
                        // Try again later and hope for the best.
                        JLOG(m_journal.error())
                            << "Entropy timer got error '" << e.message()
                            << "'.  Restarting timer.";
                        setEntropyTimer();
                    }
                }))
        {
            using namespace std::chrono_literals;
            entropyTimer_.expires_from_now(5min);
            entropyTimer_.async_wait(std::move(*optionalCountedHandler));
        }
    }

    void
    doSweep()
    {
        if (!config_->standalone() &&
            !getRelationalDatabase().transactionDbHasSpace(*config_))
        {
            signalStop("Out of transaction DB space");
        }

        // VFALCO NOTE Does the order of calls matter?
        // VFALCO TODO fix the dependency inversion using an observer,
        //         have listeners register for "onSweep ()" notification.

        {
            std::shared_ptr<FullBelowCache const> const fullBelowCache =
                nodeFamily_.getFullBelowCache();

            std::shared_ptr<TreeNodeCache const> const treeNodeCache =
                nodeFamily_.getTreeNodeCache();

            std::size_t const oldFullBelowSize = fullBelowCache->size();
            std::size_t const oldTreeNodeSize = treeNodeCache->size();

            nodeFamily_.sweep();

            JLOG(m_journal.debug())
                << "NodeFamily::FullBelowCache sweep.  Size before: "
                << oldFullBelowSize
                << "; size after: " << fullBelowCache->size();

            JLOG(m_journal.debug())
                << "NodeFamily::TreeNodeCache sweep.  Size before: "
                << oldTreeNodeSize << "; size after: " << treeNodeCache->size();
        }
        {
            TaggedCache<uint256, Transaction> const& masterTxCache =
                getMasterTransaction().getCache();

            std::size_t const oldMasterTxSize = masterTxCache.size();

            getMasterTransaction().sweep();

            JLOG(m_journal.debug())
                << "MasterTransaction sweep.  Size before: " << oldMasterTxSize
                << "; size after: " << masterTxCache.size();
        }
        {
            // Does not appear to have an associated cache.
            getNodeStore().sweep();
        }
        {
            std::size_t const oldLedgerMasterCacheSize =
                getLedgerMaster().getFetchPackCacheSize();

            getLedgerMaster().sweep();

            JLOG(m_journal.debug())
                << "LedgerMaster sweep.  Size before: "
                << oldLedgerMasterCacheSize << "; size after: "
                << getLedgerMaster().getFetchPackCacheSize();
        }
        {
            // NodeCache == TaggedCache<SHAMapHash, Blob>
            std::size_t const oldTempNodeCacheSize = getTempNodeCache().size();

            getTempNodeCache().sweep();

            JLOG(m_journal.debug())
                << "TempNodeCache sweep.  Size before: " << oldTempNodeCacheSize
                << "; size after: " << getTempNodeCache().size();
        }
        {
            std::size_t const oldCurrentCacheSize =
                getValidations().sizeOfCurrentCache();
            std::size_t const oldSizeSeqEnforcesSize =
                getValidations().sizeOfSeqEnforcersCache();
            std::size_t const oldByLedgerSize =
                getValidations().sizeOfByLedgerCache();
            std::size_t const oldBySequenceSize =
                getValidations().sizeOfBySequenceCache();

            getValidations().expire(m_journal);

            JLOG(m_journal.debug())
                << "Validations Current expire.  Size before: "
                << oldCurrentCacheSize
                << "; size after: " << getValidations().sizeOfCurrentCache();

            JLOG(m_journal.debug())
                << "Validations SeqEnforcer expire.  Size before: "
                << oldSizeSeqEnforcesSize << "; size after: "
                << getValidations().sizeOfSeqEnforcersCache();

            JLOG(m_journal.debug())
                << "Validations ByLedger expire.  Size before: "
                << oldByLedgerSize
                << "; size after: " << getValidations().sizeOfByLedgerCache();

            JLOG(m_journal.debug())
                << "Validations BySequence expire.  Size before: "
                << oldBySequenceSize
                << "; size after: " << getValidations().sizeOfBySequenceCache();
        }
        {
            std::size_t const oldInboundLedgersSize =
                getInboundLedgers().cacheSize();

            getInboundLedgers().sweep();

            JLOG(m_journal.debug())
                << "InboundLedgers sweep.  Size before: "
                << oldInboundLedgersSize
                << "; size after: " << getInboundLedgers().cacheSize();
        }
        {
            size_t const oldTasksSize = getLedgerReplayer().tasksSize();
            size_t const oldDeltasSize = getLedgerReplayer().deltasSize();
            size_t const oldSkipListsSize = getLedgerReplayer().skipListsSize();

            getLedgerReplayer().sweep();

            JLOG(m_journal.debug())
                << "LedgerReplayer tasks sweep.  Size before: " << oldTasksSize
                << "; size after: " << getLedgerReplayer().tasksSize();

            JLOG(m_journal.debug())
                << "LedgerReplayer deltas sweep.  Size before: "
                << oldDeltasSize
                << "; size after: " << getLedgerReplayer().deltasSize();

            JLOG(m_journal.debug())
                << "LedgerReplayer skipLists sweep.  Size before: "
                << oldSkipListsSize
                << "; size after: " << getLedgerReplayer().skipListsSize();
        }
        {
            std::size_t const oldAcceptedLedgerSize =
                m_acceptedLedgerCache.size();

            m_acceptedLedgerCache.sweep();

            JLOG(m_journal.debug())
                << "AcceptedLedgerCache sweep.  Size before: "
                << oldAcceptedLedgerSize
                << "; size after: " << m_acceptedLedgerCache.size();
        }
        {
            std::size_t const oldCachedSLEsSize = cachedSLEs_.size();

            cachedSLEs_.sweep();

            JLOG(m_journal.debug())
                << "CachedSLEs sweep.  Size before: " << oldCachedSLEsSize
                << "; size after: " << cachedSLEs_.size();
        }

        // Set timer to do another sweep later.
        setSweepTimer();
    }

    LedgerIndex
    getMaxDisallowedLedger() override
    {
        return maxDisallowedLedger_;
    }

    virtual std::optional<uint256> const&
    trapTxID() const override
    {
        return trapTxID_;
    }

private:
    // For a newly-started validator, this is the greatest persisted ledger
    // and new validations must be greater than this.
    std::atomic<LedgerIndex> maxDisallowedLedger_{0};

    void
    startGenesisLedger();

    std::shared_ptr<Ledger>
    getLastFullLedger();

    std::shared_ptr<Ledger>
    loadLedgerFromFile(std::string const& ledgerID);

    bool
    loadOldLedger(
        std::string const& ledgerID,
        bool replay,
        bool isFilename,
        std::optional<uint256> trapTxID);

    void
    setMaxDisallowedLedger();
};

//------------------------------------------------------------------------------

// TODO Break this up into smaller, more digestible initialization segments.
bool
ApplicationImp::setup(boost::program_options::variables_map const& cmdline)
{
    // We want to intercept CTRL-C and the standard termination signal SIGTERM
    // and terminate the process. This handler will NEVER be invoked twice.
    //
    // Note that async_wait is "one-shot": for each call, the handler will be
    // invoked exactly once, either when one of the registered signals in the
    // signal set occurs or the signal set is cancelled. Subsequent signals are
    // effectively ignored (technically, they are queued up, waiting for a call
    // to async_wait).
    m_signals.add(SIGINT);
    m_signals.add(SIGTERM);
    m_signals.async_wait(
        [this](boost::system::error_code const& ec, int signum) {
            // Indicates the signal handler has been aborted; do nothing
            if (ec == boost::asio::error::operation_aborted)
                return;

            JLOG(m_journal.info()) << "Received signal " << signum;

            if (signum == SIGTERM || signum == SIGINT)
                signalStop("Signal: " + to_string(signum));
        });

    auto debug_log = config_->getDebugLogFile();

    if (!debug_log.empty())
    {
        // Let debug messages go to the file but only WARNING or higher to
        // regular output (unless verbose)

        if (!logs_->open(debug_log))
            std::cerr << "Can't open log file " << debug_log << '\n';

        using namespace beast::severities;
        if (logs_->threshold() > kDebug)
            logs_->threshold(kDebug);
    }

    JLOG(m_journal.info()) << "Process starting: "
                           << BuildInfo::getFullVersionString()
                           << ", Instance Cookie: " << instanceCookie_;

    if (numberOfThreads(*config_) < 2)
    {
        JLOG(m_journal.warn()) << "Limited to a single I/O service thread by "
                                  "system configuration.";
    }

    // Optionally turn off logging to console.
    logs_->silent(config_->silent());

    if (!initRelationalDatabase() || !initNodeStore())
        return false;

    if (!peerReservations_->load(getWalletDB()))
    {
        JLOG(m_journal.fatal()) << "Cannot find peer reservations!";
        return false;
    }

    if (validatorKeys_.keys)
        setMaxDisallowedLedger();

    // Configure the amendments the server supports
    {
        auto const supported = []() {
            auto const& amendments = detail::supportedAmendments();
            std::vector<AmendmentTable::FeatureInfo> supported;
            supported.reserve(amendments.size());
            for (auto const& [a, vote] : amendments)
            {
                auto const f = ripple::getRegisteredFeature(a);
                XRPL_ASSERT(
                    f, "ripple::ApplicationImp::setup : registered feature");
                if (f)
                    supported.emplace_back(a, *f, vote);
            }
            return supported;
        }();
        Section const& downVoted = config_->section(SECTION_VETO_AMENDMENTS);

        Section const& upVoted = config_->section(SECTION_AMENDMENTS);

        m_amendmentTable = make_AmendmentTable(
            *this,
            config().AMENDMENT_MAJORITY_TIME,
            supported,
            upVoted,
            downVoted,
            logs_->journal("Amendments"));
    }

    Pathfinder::initPathTable();

    auto const startUp = config_->START_UP;
    JLOG(m_journal.debug()) << "startUp: " << startUp;
    if (startUp == Config::FRESH)
    {
        JLOG(m_journal.info()) << "Starting new Ledger";

        startGenesisLedger();
    }
    else if (
        startUp == Config::LOAD || startUp == Config::LOAD_FILE ||
        startUp == Config::REPLAY)
    {
        JLOG(m_journal.info()) << "Loading specified Ledger";

        if (!loadOldLedger(
                config_->START_LEDGER,
                startUp == Config::REPLAY,
                startUp == Config::LOAD_FILE,
                config_->TRAP_TX_HASH))
        {
            JLOG(m_journal.error())
                << "The specified ledger could not be loaded.";
            if (config_->FAST_LOAD)
            {
                // Fall back to syncing from the network, such as
                // when there's no existing data.
                startGenesisLedger();
            }
            else
            {
                return false;
            }
        }
    }
    else if (startUp == Config::NETWORK)
    {
        // This should probably become the default once we have a stable
        // network.
        if (!config_->standalone())
            m_networkOPs->setNeedNetworkLedger();

        startGenesisLedger();
    }
    else
    {
        startGenesisLedger();
    }

    if (auto const& forcedRange = config().FORCED_LEDGER_RANGE_PRESENT)
    {
        m_ledgerMaster->setLedgerRangePresent(
            forcedRange->first, forcedRange->second);
    }

    m_orderBookDB.setup(getLedgerMaster().getCurrentLedger());

    nodeIdentity_ = getNodeIdentity(*this, cmdline);

    if (!cluster_->load(config().section(SECTION_CLUSTER_NODES)))
    {
        JLOG(m_journal.fatal()) << "Invalid entry in cluster configuration.";
        return false;
    }

    {
        if (validatorKeys_.configInvalid())
            return false;

        if (!validatorManifests_->load(
                getWalletDB(),
                "ValidatorManifests",
                validatorKeys_.manifest,
                config().section(SECTION_VALIDATOR_KEY_REVOCATION).values()))
        {
            JLOG(m_journal.fatal()) << "Invalid configured validator manifest.";
            return false;
        }

        publisherManifests_->load(getWalletDB(), "PublisherManifests");

        // It is possible to have a valid ValidatorKeys object without
        // setting the signingKey or masterKey. This occurs if the
        // configuration file does not have either
        // SECTION_VALIDATOR_TOKEN or SECTION_VALIDATION_SEED section.

        // masterKey for the configuration-file specified validator keys
        std::optional<PublicKey> localSigningKey;
        if (validatorKeys_.keys)
            localSigningKey = validatorKeys_.keys->publicKey;

        // Setup trusted validators
        if (!validators_->load(
                localSigningKey,
                config().section(SECTION_VALIDATORS).values(),
                config().section(SECTION_VALIDATOR_LIST_KEYS).values(),
                config().VALIDATOR_LIST_THRESHOLD))
        {
            JLOG(m_journal.fatal())
                << "Invalid entry in validator configuration.";
            return false;
        }
    }

    if (!validatorSites_->load(
            config().section(SECTION_VALIDATOR_LIST_SITES).values()))
    {
        JLOG(m_journal.fatal())
            << "Invalid entry in [" << SECTION_VALIDATOR_LIST_SITES << "]";
        return false;
    }

    // Tell the AmendmentTable who the trusted validators are.
    m_amendmentTable->trustChanged(validators_->getQuorumKeys().second);

    //----------------------------------------------------------------------
    //
    // Server
    //
    //----------------------------------------------------------------------

    // VFALCO NOTE Unfortunately, in stand-alone mode some code still
    //             foolishly calls overlay(). When this is fixed we can
    //             move the instantiation inside a conditional:
    //
    //             if (!config_.standalone())
    overlay_ = make_Overlay(
        *this,
        setup_Overlay(*config_),
        *serverHandler_,
        *m_resourceManager,
        *m_resolver,
        get_io_service(),
        *config_,
        m_collectorManager->collector());
    add(*overlay_);  // add to PropertyStream

    // start first consensus round
    if (!m_networkOPs->beginConsensus(
            m_ledgerMaster->getClosedLedger()->info().hash, {}))
    {
        JLOG(m_journal.fatal()) << "Unable to start consensus";
        return false;
    }

    {
        try
        {
            auto setup = setup_ServerHandler(
                *config_, beast::logstream{m_journal.error()});
            setup.makeContexts();
            serverHandler_->setup(setup, m_journal);
            fixConfigPorts(*config_, serverHandler_->endpoints());
        }
        catch (std::exception const& e)
        {
            if (auto stream = m_journal.fatal())
            {
                stream << "Unable to setup server handler";
                if (std::strlen(e.what()) > 0)
                    stream << ": " << e.what();
            }
            return false;
        }
    }

    // Begin connecting to network.
    if (!config_->standalone())
    {
        // Should this message be here, conceptually? In theory this sort
        // of message, if displayed, should be displayed from PeerFinder.
        if (config_->PEER_PRIVATE && config_->IPS_FIXED.empty())
        {
            JLOG(m_journal.warn())
                << "No outbound peer connections will be made";
        }

        // VFALCO NOTE the state timer resets the deadlock detector.
        //
        m_networkOPs->setStateTimer();
    }
    else
    {
        JLOG(m_journal.warn()) << "Running in standalone mode";

        m_networkOPs->setStandAlone();
    }

    if (config_->canSign())
    {
        JLOG(m_journal.warn()) << "*** The server is configured to allow the "
                                  "'sign' and 'sign_for'";
        JLOG(m_journal.warn()) << "*** commands. These commands have security "
                                  "implications and have";
        JLOG(m_journal.warn()) << "*** been deprecated. They will be removed "
                                  "in a future release of";
        JLOG(m_journal.warn()) << "*** rippled.";
        JLOG(m_journal.warn()) << "*** If you do not use them to sign "
                                  "transactions please edit your";
        JLOG(m_journal.warn())
            << "*** configuration file and remove the [enable_signing] stanza.";
        JLOG(m_journal.warn()) << "*** If you do use them to sign transactions "
                                  "please migrate to a";
        JLOG(m_journal.warn())
            << "*** standalone signing solution as soon as possible.";
    }

    //
    // Execute start up rpc commands.
    //
    for (auto cmd : config_->section(SECTION_RPC_STARTUP).lines())
    {
        Json::Reader jrReader;
        Json::Value jvCommand;

        if (!jrReader.parse(cmd, jvCommand))
        {
            JLOG(m_journal.fatal()) << "Couldn't parse entry in ["
                                    << SECTION_RPC_STARTUP << "]: '" << cmd;
        }

        if (!config_->quiet())
        {
            JLOG(m_journal.fatal())
                << "Startup RPC: " << jvCommand << std::endl;
        }

        Resource::Charge loadType = Resource::feeReferenceRPC;
        Resource::Consumer c;
        RPC::JsonContext context{
            {journal("RPCHandler"),
             *this,
             loadType,
             getOPs(),
             getLedgerMaster(),
             c,
             Role::ADMIN,
             {},
             {},
             RPC::apiMaximumSupportedVersion},
            jvCommand};

        Json::Value jvResult;
        RPC::doCommand(context, jvResult);

        if (!config_->quiet())
        {
            JLOG(m_journal.fatal()) << "Result: " << jvResult << std::endl;
        }
    }

    validatorSites_->start();

    return true;
}

void
ApplicationImp::start(bool withTimers)
{
    JLOG(m_journal.info()) << "Application starting. Version is "
                           << BuildInfo::getVersionString();

    if (withTimers)
    {
        setSweepTimer();
        setEntropyTimer();
    }

    m_io_latency_sampler.start();
    m_resolver->start();
    m_loadManager->start();
    m_shaMapStore->start();
    if (overlay_)
        overlay_->start();

    if (grpcServer_->start())
        fixConfigPorts(
            *config_, {{SECTION_PORT_GRPC, grpcServer_->getEndpoint()}});

    ledgerCleaner_->start();
    perfLog_->start();
}

void
ApplicationImp::run()
{
    if (!config_->standalone())
    {
        // VFALCO NOTE This seems unnecessary. If we properly refactor the load
        //             manager then the stall detector can just always be
        //             "armed"
        //
        getLoadManager().activateStallDetector();
    }

    {
        std::unique_lock<std::mutex> lk{stoppingMutex_};
        stoppingCondition_.wait(lk, [this] { return isTimeToStop.load(); });
    }

    JLOG(m_journal.debug()) << "Application stopping";

    m_io_latency_sampler.cancel_async();

    // VFALCO Enormous hack, we have to force the probe to cancel
    //        before we stop the io_service queue or else it never
    //        unblocks in its destructor. The fix is to make all
    //        io_objects gracefully handle exit so that we can
    //        naturally return from io_service::run() instead of
    //        forcing a call to io_service::stop()
    m_io_latency_sampler.cancel();

    m_resolver->stop_async();

    // NIKB This is a hack - we need to wait for the resolver to
    //      stop. before we stop the io_server_queue or weird
    //      things will happen.
    m_resolver->stop();

    {
        boost::system::error_code ec;
        sweepTimer_.cancel(ec);
        if (ec)
        {
            JLOG(m_journal.error())
                << "Application: sweepTimer cancel error: " << ec.message();
        }

        ec.clear();
        entropyTimer_.cancel(ec);
        if (ec)
        {
            JLOG(m_journal.error())
                << "Application: entropyTimer cancel error: " << ec.message();
        }
    }

    // Make sure that any waitHandlers pending in our timers are done
    // before we declare ourselves stopped.
    using namespace std::chrono_literals;

    waitHandlerCounter_.join("Application", 1s, m_journal);

    mValidations.flush();

    validatorSites_->stop();

    // TODO Store manifests in manifests.sqlite instead of wallet.db
    validatorManifests_->save(
        getWalletDB(), "ValidatorManifests", [this](PublicKey const& pubKey) {
            return validators().listed(pubKey);
        });

    publisherManifests_->save(
        getWalletDB(), "PublisherManifests", [this](PublicKey const& pubKey) {
            return validators().trustedPublisher(pubKey);
        });

    // The order of these stop calls is delicate.
    // Re-ordering them risks undefined behavior.
    m_loadManager->stop();
    m_shaMapStore->stop();
    m_jobQueue->stop();
    if (overlay_)
        overlay_->stop();
    grpcServer_->stop();
    m_networkOPs->stop();
    serverHandler_->stop();
    m_ledgerReplayer->stop();
    m_inboundTransactions->stop();
    m_inboundLedgers->stop();
    ledgerCleaner_->stop();
    m_nodeStore->stop();
    perfLog_->stop();

    JLOG(m_journal.info()) << "Done.";
}

void
ApplicationImp::signalStop(std::string msg)
{
    if (!isTimeToStop.exchange(true))
    {
        if (msg.empty())
            JLOG(m_journal.warn()) << "Server stopping";
        else
            JLOG(m_journal.warn()) << "Server stopping: " << msg;

        stoppingCondition_.notify_all();
    }
}

bool
ApplicationImp::checkSigs() const
{
    return checkSigs_;
}

void
ApplicationImp::checkSigs(bool check)
{
    checkSigs_ = check;
}

bool
ApplicationImp::isStopping() const
{
    return isTimeToStop.load();
}

int
ApplicationImp::fdRequired() const
{
    // Standard handles, config file, misc I/O etc:
    int needed = 128;

    // 2x the configured peer limit for peer connections:
    if (overlay_)
        needed += 2 * overlay_->limit();

    // the number of fds needed by the backend (internally
    // doubled if online delete is enabled).
    needed += std::max(5, m_shaMapStore->fdRequired());

    // One fd per incoming connection a port can accept, or
    // if no limit is set, assume it'll handle 256 clients.
    for (auto const& p : serverHandler_->setup().ports)
        needed += std::max(256, p.limit);

    // The minimum number of file descriptors we need is 1024:
    return std::max(1024, needed);
}

//------------------------------------------------------------------------------

void
ApplicationImp::startGenesisLedger()
{
    std::vector<uint256> const initialAmendments =
        (config_->START_UP == Config::FRESH) ? m_amendmentTable->getDesired()
                                             : std::vector<uint256>{};

    std::shared_ptr<Ledger> const genesis = std::make_shared<Ledger>(
        create_genesis, *config_, initialAmendments, nodeFamily_);
    m_ledgerMaster->storeLedger(genesis);

    auto const next =
        std::make_shared<Ledger>(*genesis, timeKeeper().closeTime());
    next->updateSkipList();
    XRPL_ASSERT(
        next->info().seq < XRP_LEDGER_EARLIEST_FEES ||
            next->read(keylet::fees()),
        "ripple::ApplicationImp::startGenesisLedger : valid ledger fees");
    next->setImmutable();
    openLedger_.emplace(next, cachedSLEs_, logs_->journal("OpenLedger"));
    m_ledgerMaster->storeLedger(next);
    m_ledgerMaster->switchLCL(next);
}

std::shared_ptr<Ledger>
ApplicationImp::getLastFullLedger()
{
    auto j = journal("Ledger");

    try
    {
        auto const [ledger, seq, hash] = getLatestLedger(*this);

        if (!ledger)
            return ledger;

        XRPL_ASSERT(
            ledger->info().seq < XRP_LEDGER_EARLIEST_FEES ||
                ledger->read(keylet::fees()),
            "ripple::ApplicationImp::getLastFullLedger : valid ledger fees");
        ledger->setImmutable();

        if (getLedgerMaster().haveLedger(seq))
            ledger->setValidated();

        if (ledger->info().hash == hash)
        {
            JLOG(j.trace()) << "Loaded ledger: " << hash;
            return ledger;
        }

        if (auto stream = j.error())
        {
            stream << "Failed on ledger";
            Json::Value p;
            addJson(p, {*ledger, nullptr, LedgerFill::full});
            stream << p;
        }

        return {};
    }
    catch (SHAMapMissingNode const& mn)
    {
        JLOG(j.warn()) << "Ledger in database: " << mn.what();
        return {};
    }
}

std::shared_ptr<Ledger>
ApplicationImp::loadLedgerFromFile(std::string const& name)
{
    try
    {
        std::ifstream ledgerFile(name, std::ios::in);

        if (!ledgerFile)
        {
            JLOG(m_journal.fatal()) << "Unable to open file '" << name << "'";
            return nullptr;
        }

        Json::Reader reader;
        Json::Value jLedger;

        if (!reader.parse(ledgerFile, jLedger))
        {
            JLOG(m_journal.fatal()) << "Unable to parse ledger JSON";
            return nullptr;
        }

        std::reference_wrapper<Json::Value> ledger(jLedger);

        // accept a wrapped ledger
        if (ledger.get().isMember("result"))
            ledger = ledger.get()["result"];

        if (ledger.get().isMember("ledger"))
            ledger = ledger.get()["ledger"];

        std::uint32_t seq = 1;
        auto closeTime = timeKeeper().closeTime();
        using namespace std::chrono_literals;
        auto closeTimeResolution = 30s;
        bool closeTimeEstimated = false;
        std::uint64_t totalDrops = 0;

        if (ledger.get().isMember("accountState"))
        {
            if (ledger.get().isMember(jss::ledger_index))
            {
                seq = ledger.get()[jss::ledger_index].asUInt();
            }

            if (ledger.get().isMember("close_time"))
            {
                using tp = NetClock::time_point;
                using d = tp::duration;
                closeTime = tp{d{ledger.get()["close_time"].asUInt()}};
            }
            if (ledger.get().isMember("close_time_resolution"))
            {
                using namespace std::chrono;
                closeTimeResolution =
                    seconds{ledger.get()["close_time_resolution"].asUInt()};
            }
            if (ledger.get().isMember("close_time_estimated"))
            {
                closeTimeEstimated =
                    ledger.get()["close_time_estimated"].asBool();
            }
            if (ledger.get().isMember("total_coins"))
            {
                totalDrops = beast::lexicalCastThrow<std::uint64_t>(
                    ledger.get()["total_coins"].asString());
            }

            ledger = ledger.get()["accountState"];
        }

        if (!ledger.get().isArrayOrNull())
        {
            JLOG(m_journal.fatal()) << "State nodes must be an array";
            return nullptr;
        }

        auto loadLedger =
            std::make_shared<Ledger>(seq, closeTime, *config_, nodeFamily_);
        loadLedger->setTotalDrops(totalDrops);

        for (Json::UInt index = 0; index < ledger.get().size(); ++index)
        {
            Json::Value& entry = ledger.get()[index];

            if (!entry.isObjectOrNull())
            {
                JLOG(m_journal.fatal()) << "Invalid entry in ledger";
                return nullptr;
            }

            uint256 uIndex;

            if (!uIndex.parseHex(entry[jss::index].asString()))
            {
                JLOG(m_journal.fatal()) << "Invalid entry in ledger";
                return nullptr;
            }

            entry.removeMember(jss::index);

            STParsedJSONObject stp("sle", ledger.get()[index]);

            if (!stp.object || uIndex.isZero())
            {
                JLOG(m_journal.fatal()) << "Invalid entry in ledger";
                return nullptr;
            }

            // VFALCO TODO This is the only place that
            //             constructor is used, try to remove it
            STLedgerEntry sle(*stp.object, uIndex);

            if (!loadLedger->addSLE(sle))
            {
                JLOG(m_journal.fatal())
                    << "Couldn't add serialized ledger: " << uIndex;
                return nullptr;
            }
        }

        loadLedger->stateMap().flushDirty(hotACCOUNT_NODE);

        XRPL_ASSERT(
            loadLedger->info().seq < XRP_LEDGER_EARLIEST_FEES ||
                loadLedger->read(keylet::fees()),
            "ripple::ApplicationImp::loadLedgerFromFile : valid ledger fees");
        loadLedger->setAccepted(
            closeTime, closeTimeResolution, !closeTimeEstimated);

        return loadLedger;
    }
    catch (std::exception const& x)
    {
        JLOG(m_journal.fatal()) << "Ledger contains invalid data: " << x.what();
        return nullptr;
    }
}

bool
ApplicationImp::loadOldLedger(
    std::string const& ledgerID,
    bool replay,
    bool isFileName,
    std::optional<uint256> trapTxID)
{
    try
    {
        std::shared_ptr<Ledger const> loadLedger, replayLedger;

        if (isFileName)
        {
            if (!ledgerID.empty())
                loadLedger = loadLedgerFromFile(ledgerID);
        }
        else if (ledgerID.length() == 64)
        {
            uint256 hash;

            if (hash.parseHex(ledgerID))
            {
                loadLedger = loadByHash(hash, *this);

                if (!loadLedger)
                {
                    // Try to build the ledger from the back end
                    auto il = std::make_shared<InboundLedger>(
                        *this,
                        hash,
                        0,
                        InboundLedger::Reason::GENERIC,
                        stopwatch(),
                        make_DummyPeerSet(*this));
                    if (il->checkLocal())
                        loadLedger = il->getLedger();
                }
            }
        }
        else if (ledgerID.empty() || boost::iequals(ledgerID, "latest"))
        {
            loadLedger = getLastFullLedger();
        }
        else
        {
            // assume by sequence
            std::uint32_t index;

            if (beast::lexicalCastChecked(index, ledgerID))
                loadLedger = loadByIndex(index, *this);
        }

        if (!loadLedger)
            return false;

        if (replay)
        {
            // Replay a ledger close with same prior ledger and transactions

            // this ledger holds the transactions we want to replay
            replayLedger = loadLedger;

            JLOG(m_journal.info()) << "Loading parent ledger";

            loadLedger = loadByHash(replayLedger->info().parentHash, *this);
            if (!loadLedger)
            {
                JLOG(m_journal.info())
                    << "Loading parent ledger from node store";

                // Try to build the ledger from the back end
                auto il = std::make_shared<InboundLedger>(
                    *this,
                    replayLedger->info().parentHash,
                    0,
                    InboundLedger::Reason::GENERIC,
                    stopwatch(),
                    make_DummyPeerSet(*this));

                if (il->checkLocal())
                    loadLedger = il->getLedger();

                if (!loadLedger)
                {
                    JLOG(m_journal.fatal()) << "Replay ledger missing/damaged";
                    UNREACHABLE(
                        "ripple::ApplicationImp::loadOldLedger : replay ledger "
                        "missing/damaged");
                    return false;
                }
            }
        }
        using namespace std::chrono_literals;
        using namespace date;
        static constexpr NetClock::time_point ledgerWarnTimePoint{
            sys_days{January / 1 / 2018} - sys_days{January / 1 / 2000}};
        if (loadLedger->info().closeTime < ledgerWarnTimePoint)
        {
            JLOG(m_journal.fatal())
                << "\n\n***  WARNING   ***\n"
                   "You are replaying a ledger from before "
                << to_string(ledgerWarnTimePoint)
                << " UTC.\n"
                   "This replay will not handle your ledger as it was "
                   "originally "
                   "handled.\nConsider running an earlier version of rippled "
                   "to "
                   "get the older rules.\n*** CONTINUING ***\n";
        }

        JLOG(m_journal.info()) << "Loading ledger " << loadLedger->info().hash
                               << " seq:" << loadLedger->info().seq;

        if (loadLedger->info().accountHash.isZero())
        {
            JLOG(m_journal.fatal()) << "Ledger is empty.";
            UNREACHABLE(
                "ripple::ApplicationImp::loadOldLedger : ledger is empty");
            return false;
        }

        if (!loadLedger->walkLedger(journal("Ledger"), true))
        {
            JLOG(m_journal.fatal()) << "Ledger is missing nodes.";
            UNREACHABLE(
                "ripple::ApplicationImp::loadOldLedger : ledger is missing "
                "nodes");
            return false;
        }

        if (!loadLedger->assertSensible(journal("Ledger")))
        {
            JLOG(m_journal.fatal()) << "Ledger is not sensible.";
            UNREACHABLE(
                "ripple::ApplicationImp::loadOldLedger : ledger is not "
                "sensible");
            return false;
        }

        m_ledgerMaster->setLedgerRangePresent(
            loadLedger->info().seq, loadLedger->info().seq);

        m_ledgerMaster->switchLCL(loadLedger);
        loadLedger->setValidated();
        m_ledgerMaster->setFullLedger(loadLedger, true, false);
        openLedger_.emplace(
            loadLedger, cachedSLEs_, logs_->journal("OpenLedger"));

        if (replay)
        {
            // inject transaction(s) from the replayLedger into our open ledger
            // and build replay structure
            auto replayData =
                std::make_unique<LedgerReplay>(loadLedger, replayLedger);

            for (auto const& [_, tx] : replayData->orderedTxns())
            {
                (void)_;
                auto txID = tx->getTransactionID();
                if (trapTxID == txID)
                {
                    trapTxID_ = txID;
                    JLOG(m_journal.debug()) << "Trap transaction set: " << txID;
                }

                auto s = std::make_shared<Serializer>();
                tx->add(*s);

                forceValidity(getHashRouter(), txID, Validity::SigGoodOnly);

                openLedger_->modify(
                    [&txID, &s](OpenView& view, beast::Journal j) {
                        view.rawTxInsert(txID, std::move(s), nullptr);
                        return true;
                    });
            }

            m_ledgerMaster->takeReplay(std::move(replayData));

            if (trapTxID && !trapTxID_)
            {
                JLOG(m_journal.fatal())
                    << "Ledger " << replayLedger->info().seq
                    << " does not contain the transaction hash " << *trapTxID;
                return false;
            }
        }
    }
    catch (SHAMapMissingNode const& mn)
    {
        JLOG(m_journal.fatal())
            << "While loading specified ledger: " << mn.what();
        return false;
    }
    catch (boost::bad_lexical_cast&)
    {
        JLOG(m_journal.fatal())
            << "Ledger specified '" << ledgerID << "' is not valid";
        return false;
    }

    return true;
}

bool
ApplicationImp::serverOkay(std::string& reason)
{
    if (!config().ELB_SUPPORT)
        return true;

    if (isStopping())
    {
        reason = "Server is shutting down";
        return false;
    }

    if (getOPs().isNeedNetworkLedger())
    {
        reason = "Not synchronized with network yet";
        return false;
    }

    if (getOPs().isAmendmentBlocked())
    {
        reason = "Server version too old";
        return false;
    }

    if (getOPs().isUNLBlocked())
    {
        reason = "No valid validator list available";
        return false;
    }

    if (getOPs().getOperatingMode() < OperatingMode::SYNCING)
    {
        reason = "Not synchronized with network";
        return false;
    }

    if (!getLedgerMaster().isCaughtUp(reason))
        return false;

    if (getFeeTrack().isLoadedLocal())
    {
        reason = "Too much load";
        return false;
    }

    return true;
}

beast::Journal
ApplicationImp::journal(std::string const& name)
{
    return logs_->journal(name);
}

void
ApplicationImp::setMaxDisallowedLedger()
{
    auto seq = getRelationalDatabase().getMaxLedgerSeq();
    if (seq)
        maxDisallowedLedger_ = *seq;

    JLOG(m_journal.trace())
        << "Max persisted ledger is " << maxDisallowedLedger_;
}

//------------------------------------------------------------------------------

Application::Application() : beast::PropertyStream::Source("app")
{
}

//------------------------------------------------------------------------------

std::unique_ptr<Application>
make_Application(
    std::unique_ptr<Config> config,
    std::unique_ptr<Logs> logs,
    std::unique_ptr<TimeKeeper> timeKeeper)
{
    return std::make_unique<ApplicationImp>(
        std::move(config), std::move(logs), std::move(timeKeeper));
}

void
fixConfigPorts(Config& config, Endpoints const& endpoints)
{
    for (auto const& [name, ep] : endpoints)
    {
        if (!config.exists(name))
            continue;

        auto& section = config[name];
        auto const optPort = section.get("port");
        if (optPort)
        {
            std::uint16_t const port =
                beast::lexicalCast<std::uint16_t>(*optPort);
            if (!port)
                section.set("port", std::to_string(ep.port()));
        }
    }
}

}  // namespace ripple
