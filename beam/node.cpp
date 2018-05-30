#include "node.h"
#include "../core/serialization_adapters.h"
#include "../core/proto.h"
#include "../core/ecc_native.h"

#include "../p2p/protocol.h"
#include "../p2p/connection.h"

#include "../utility/logger.h"
#include "../utility/io/tcpserver.h"
#include "../utility/logger.h"
#include "../utility/logger_checkpoints.h"

namespace beam {

void Node::RefreshCongestions()
{
	for (TaskSet::iterator it = m_setTasks.begin(); m_setTasks.end() != it; it++)
		it->m_bRelevant = false;

	m_Processor.EnumCongestions();

	for (TaskList::iterator it = m_lstTasksUnassigned.begin(); m_lstTasksUnassigned.end() != it; )
	{
		TaskList::iterator itThis = it++;
		if (!itThis->m_bRelevant)
			DeleteUnassignedTask(*itThis);
	}
}

void Node::DeleteUnassignedTask(Task& t)
{
	assert(!t.m_pOwner);
	m_lstTasksUnassigned.erase(TaskList::s_iterator_to(t));
	m_setTasks.erase(TaskSet::s_iterator_to(t));
	delete &t;
}

void Node::TryAssignTask(Task& t, const NodeDB::PeerID* pPeerID)
{
	while (true)
	{
		Peer* pBestMatch = NULL;

		for (PeerList::iterator it = m_lstPeers.begin(); m_lstPeers.end() != it; it++)
		{
			Peer& p = *it;
			if (pBestMatch)
			{
				assert(pPeerID);

				NodeDB::PeerID id;
				p.get_ID(id);
				if (id != *pPeerID)
					continue;
			}

			if (ShouldAssignTask(t, p))
			{
				pBestMatch = &p;
				if (!pPeerID)
					break;
			}
		}

		if (!pBestMatch)
			break;

		try {
			AssignTask(t, *pBestMatch);
			return; // done

		} catch (...) {
			pBestMatch->OnPostError();
		}
	}
}

void Node::AssignTask(Task& t, Peer& p)
{
	if (t.m_Key.second)
	{
		proto::GetBody msg;
		msg.m_ID = t.m_Key.first;
		p.Send(msg);
	}
	else
	{
		proto::GetHdr msg;
		msg.m_ID = t.m_Key.first;
		p.Send(msg);
	}

	bool bEmpty = p.m_lstTasks.empty();

	assert(!t.m_pOwner);
	t.m_pOwner = &p;

	m_lstTasksUnassigned.erase(TaskList::s_iterator_to(t));
	p.m_lstTasks.push_back(t);

	if (bEmpty)
		p.SetTimerWrtFirstTask();
}

void Node::Peer::SetTimerWrtFirstTask()
{
	if (m_lstTasks.empty())
		KillTimer();
	else
		SetTimer(m_lstTasks.front().m_Key.second ? m_pThis->m_Cfg.m_Timeout.m_GetBlock_ms : m_pThis->m_Cfg.m_Timeout.m_GetState_ms);
}

bool Node::ShouldAssignTask(Task& t, Peer& p)
{
	if (State::Connected != p.m_eState)
		return false;

	if (p.m_TipHeight < t.m_Key.first.m_Height)
		return false;

	// check if the peer currently transfers a block
	for (TaskList::iterator it = p.m_lstTasks.begin(); p.m_lstTasks.end() != it; it++)
		if (it->m_Key.second)
			return false;

	return p.m_setRejected.end() == p.m_setRejected.find(t.m_Key);
}

void Node::Processor::RequestData(const Block::SystemState::ID& id, bool bBlock, const PeerID* pPreferredPeer)
{
	Task tKey;
	tKey.m_Key.first = id;
	tKey.m_Key.second = bBlock;

	TaskSet::iterator it = get_ParentObj().m_setTasks.find(tKey);
	if (get_ParentObj().m_setTasks.end() == it)
	{
		LOG_INFO() << "Requesting " << (bBlock ? "block" : "header") << " " << id;

		Task* pTask = new Task;
		pTask->m_Key = tKey.m_Key;
		pTask->m_bRelevant = true;
		pTask->m_pOwner = NULL;

		get_ParentObj().m_setTasks.insert(*pTask);
		get_ParentObj().m_lstTasksUnassigned.push_back(*pTask);

		get_ParentObj().TryAssignTask(*pTask, pPreferredPeer);

	} else
		it->m_bRelevant = true;
}

void Node::Processor::OnPeerInsane(const PeerID& peerID)
{
	Peer* pPeer = get_ParentObj().FindPeer(peerID);
	if (pPeer && (State::Snoozed != pPeer->m_eState))
		pPeer->OnClosed(-1);
}

void Node::Processor::OnNewState()
{
	proto::Hdr msgHdr;
	if (!get_CurrentState(msgHdr.m_Description))
		return;

	proto::NewTip msg;
	msgHdr.m_Description.get_ID(msg.m_ID);

	LOG_INFO() << "My Tip: " << msg.m_ID;

	get_ParentObj().m_TxPool.DeleteOutOfBound(msg.m_ID.m_Height + 1);

	get_ParentObj().m_Miner.HardAbortSafe();
	
	get_ParentObj().m_Miner.SetTimer(0, true); // don't start mined block construction, because we're called in the context of NodeProcessor, which holds the DB transaction.

	for (PeerList::iterator it = get_ParentObj().m_lstPeers.begin(); get_ParentObj().m_lstPeers.end() != it; )
	{
		PeerList::iterator itThis = it;
		it++;
		Peer& peer = *itThis;

		try {
			if ((State::Connected == peer.m_eState) && (peer.m_TipHeight <= msg.m_ID.m_Height))
			{
				peer.Send(msg);

				if (peer.m_Config.m_AutoSendHdr)
					peer.Send(msgHdr);
			}
		} catch (...) {
			peer.OnPostError();
		}
	}

	get_ParentObj().RefreshCongestions();
}

bool Node::Processor::VerifyBlock(const std::shared_ptr<Block::Body>& pBlock, Height h0, Height h1)
{
	uint32_t nThreads = get_ParentObj().m_Cfg.m_VerificationThreads;
	if (!nThreads)
		return NodeProcessor::VerifyBlock(pBlock, h0, h1);

	VerifierContext::Ptr pContext(std::make_shared<VerifierContext>());

	pContext->m_pTx = pBlock;
	pContext->m_bAbort = false;
	pContext->m_Remaining = nThreads;
	pContext->m_Context.m_bRangeMode = true;
	pContext->m_Context.m_hMin = h0;
	pContext->m_Context.m_hMax = h1;
	pContext->m_Context.m_nVerifiers = nThreads;

	std::vector<std::thread> vThreads;
	vThreads.resize(nThreads);

	std::unique_lock<std::mutex> scope(pContext->m_Mutex);

	for (uint32_t i = 0; i < nThreads; i++)
	{
		std::thread& t = vThreads[i];
		t = std::thread(&VerifierContext::Proceed, pContext, i);
	}

	while (true)
	{
		pContext->m_Cond.wait(scope);

		if (!pContext->m_Remaining || pContext->m_bAbort)
			break;
	}

	for (uint32_t i = 0; i < nThreads; i++)
		vThreads[i].join();

	return !pContext->m_bAbort && pContext->m_Context.IsValidBlock(*pBlock);
}

void Node::Processor::VerifierContext::Proceed(const std::shared_ptr<VerifierContext> pContext, uint32_t iVerifier)
{
	TxBase::Context ctx;
	ctx.m_bRangeMode = true;
	ctx.m_hMin = pContext->m_Context.m_hMin;
	ctx.m_hMax = pContext->m_Context.m_hMax;
	ctx.m_nVerifiers = pContext->m_Context.m_nVerifiers;
	ctx.m_iVerifier = iVerifier;
	ctx.m_pAbort = &pContext->m_bAbort;

	bool bValid = pContext->m_pTx->ValidateAndSummarize(ctx);

	{
		std::unique_lock<std::mutex> scope(pContext->m_Mutex);

		verify(pContext->m_Remaining--);

		if (bValid && !pContext->m_bAbort)
			bValid = pContext->m_Context.Merge(ctx);

		if (!bValid)
			pContext->m_bAbort = true;
	}

	pContext->m_Cond.notify_one();
}

Node::Peer* Node::AllocPeer()
{
	Peer* pPeer = new Peer;
	m_lstPeers.push_back(*pPeer);

	pPeer->m_pTimer = io::Timer::create(io::Reactor::get_Current().shared_from_this());

	pPeer->m_eState = State::Idle;
	pPeer->m_pThis = this;

	return pPeer;
}

void Node::DeletePeer(Peer* p)
{
	m_lstPeers.erase(PeerList::s_iterator_to(*p));
	delete p;
}

Node::Peer* Node::FindPeer(const Processor::PeerID& peerID)
{
	// current interpretation (naive): just assume that peerID is the index
	for (PeerList::iterator it = m_lstPeers.begin(); m_lstPeers.end() != it; it++)
	{
		Processor::PeerID id2;
		id2 = (uint32_t) it->m_iPeer;
		if (peerID == id2)
			return &*it;
	}
	return NULL;
}

void Node::Initialize()
{
	m_Processor.m_Horizon = m_Cfg.m_Horizon;
	m_Processor.Initialize(m_Cfg.m_sPathLocal.c_str());
    m_Processor.m_Kdf.m_Secret = m_Cfg.m_WalletKey;

	Block::SystemState::ID id;
	if (!m_Processor.get_CurrentState(id))
		ZeroObject(id);

	LOG_INFO() << "Initial Tip: " << id;

	RefreshCongestions();

	if (m_Cfg.m_Listen.port())
		m_Server.Listen(m_Cfg.m_Listen);

	for (size_t i = 0; i < m_Cfg.m_Connect.size(); i++)
	{
		Peer* p = AllocPeer();
		p->m_iPeer = i;

		p->OnTimer(); // initiate connect
	}

	if (m_Cfg.m_MiningThreads)
	{
		m_Miner.m_pEvtMined = io::AsyncEvent::create(io::Reactor::get_Current().shared_from_this(), [this]() { m_Miner.OnMined(); });

		m_Miner.m_vThreads.resize(m_Cfg.m_MiningThreads);
		for (uint32_t i = 0; i < m_Cfg.m_MiningThreads; i++)
		{
			Miner::PerThread& pt = m_Miner.m_vThreads[i];
			pt.m_pReactor = io::Reactor::create();
			pt.m_pEvtRefresh = io::AsyncEvent::create(pt.m_pReactor, [this, i]() { m_Miner.OnRefresh(i); });
			pt.m_Thread = std::thread(&io::Reactor::run, pt.m_pReactor);
		}

		m_Miner.Restart();
	}
}

Node::~Node()
	{
	m_Miner.HardAbortSafe();

	for (size_t i = 0; i < m_Miner.m_vThreads.size(); i++)
		{
		Miner::PerThread& pt = m_Miner.m_vThreads[i];
		if (pt.m_pReactor)
			pt.m_pReactor->stop();

		pt.m_Thread.join();
}
	m_Miner.m_vThreads.clear();

	for (PeerList::iterator it = m_lstPeers.begin(); m_lstPeers.end() != it; it++)
		it->m_eState = State::Snoozed; // prevent re-assigning of tasks in the next loop

	while (!m_lstPeers.empty())
	{
		Peer& p = m_lstPeers.front();
		p.ReleaseTasks();
		DeletePeer(&p);
	}

	while (!m_lstTasksUnassigned.empty())
		DeleteUnassignedTask(m_lstTasksUnassigned.front());

	assert(m_setTasks.empty());
}

void Node::Peer::SetTimer(uint32_t timeout_ms)
{
	assert(m_pTimer);
	m_pTimer->start(timeout_ms, false, [this]() { OnTimer(); });
}

void Node::Peer::KillTimer()
{
	assert(m_pTimer);
	m_pTimer->cancel();
}

void Node::Peer::OnTimer()
{
	switch (m_eState)
	{
	case State::Idle:
	case State::Snoozed:

		assert(m_iPeer >= 0);

		try {
			Connect(m_pThis->m_Cfg.m_Connect[m_iPeer]);
		} catch (...) {
			OnPostError();
		}

		break;

	case State::Connected:

		assert(!m_lstTasks.empty()); // task (request) wasn't handled in time
		OnPostError();
		break;
	}
}

void Node::Peer::OnConnected()
{
	LOG_INFO() << "+Peer " << get_Connection()->peer_address();

	if (State::Connecting == m_eState)
		KillTimer();
	else
		assert(State::Idle == m_eState);

	m_eState = State::Connected;
	m_TipHeight = 0;
	ZeroObject(m_Config);

	proto::Config msgCfg;
	msgCfg.m_SpreadingTransactions = true;
	msgCfg.m_Mining = (m_pThis->m_Cfg.m_MiningThreads > 0);
	msgCfg.m_AutoSendHdr = false;
	Send(msgCfg);

	proto::NewTip msg;
	if (m_pThis->m_Processor.get_CurrentState(msg.m_ID))
		Send(msg);
}

void Node::Peer::OnClosed(int errorCode)
{
	assert(State::Connected == m_eState);
	if (-1 == errorCode) // protocol error
		m_eState = State::Snoozed;
	OnPostError();
}

void Node::Peer::get_ID(NodeProcessor::PeerID& id)
{
	id = (size_t) m_iPeer;
}

void Node::Peer::ReleaseTasks()
{
	while (!m_lstTasks.empty())
		ReleaseTask(m_lstTasks.front());
}

void Node::Peer::ReleaseTask(Task& t)
{
	assert(this == t.m_pOwner);
	t.m_pOwner = NULL;

	m_lstTasks.erase(TaskList::s_iterator_to(t));
	m_pThis->m_lstTasksUnassigned.push_back(t);

	if (t.m_bRelevant)
		m_pThis->TryAssignTask(t, NULL);
	else
		m_pThis->DeleteUnassignedTask(t);
}

void Node::Peer::OnPostError()
{
	const Connection* pConn = get_Connection();
	if (pConn)
		LOG_INFO() << "-Peer " << pConn->peer_address();

	if (State::Snoozed != m_eState)
		m_eState = State::Idle; // prevent reassigning the tasks

	ReleaseTasks();

	if (m_iPeer < 0)
		m_pThis->DeletePeer(this);
	else
	{
		Reset(); // connection layer
		m_setRejected.clear();

		if (State::Snoozed == m_eState)
			SetTimer(m_pThis->m_Cfg.m_Timeout.m_Insane_ms);
		else
		{
			m_eState = State::Idle;
			SetTimer(m_pThis->m_Cfg.m_Timeout.m_Reconnect_ms);
		}
	}
}

void Node::Peer::TakeTasks()
{
	for (TaskList::iterator it = m_pThis->m_lstTasksUnassigned.begin(); m_pThis->m_lstTasksUnassigned.end() != it; )
	{
		Task& t = *it;
		it++;

		if (m_pThis->ShouldAssignTask(t, *this))
			m_pThis->AssignTask(t, *this);
	}
}

void Node::Peer::OnMsg(proto::Ping&&)
{
	proto::Pong msg;
	Send(msg);
}

void Node::Peer::OnMsg(proto::NewTip&& msg)
{
	if (msg.m_ID.m_Height < m_TipHeight)
		ThrowUnexpected();

	m_TipHeight = msg.m_ID.m_Height;
	m_setRejected.clear();

	LOG_INFO() << "Peer " << get_Connection()->peer_address() << " Tip: " << msg.m_ID;

	TakeTasks();

	if (m_pThis->m_Processor.IsStateNeeded(msg.m_ID))
	{
		NodeProcessor::PeerID id;
		get_ID(id);
		m_pThis->m_Processor.RequestData(msg.m_ID, false, &id);
	}
}

void Node::Peer::ThrowUnexpected()
{
	throw std::runtime_error("unexpected");
}

Node::Task& Node::Peer::get_FirstTask()
{
	if (m_lstTasks.empty())
		ThrowUnexpected();
	return m_lstTasks.front();
}

void Node::Peer::OnFirstTaskDone()
{
	ReleaseTask(get_FirstTask());
	SetTimerWrtFirstTask();
}

void Node::Peer::OnMsg(proto::DataMissing&&)
{
	Task& t = get_FirstTask();
	m_setRejected.insert(t.m_Key);

	OnFirstTaskDone();
}

void Node::Peer::OnMsg(proto::GetHdr&& msg)
{
	uint64_t rowid = m_pThis->m_Processor.get_DB().StateFindSafe(msg.m_ID);
	if (rowid)
	{
		proto::Hdr msgHdr;
		m_pThis->m_Processor.get_DB().get_State(rowid, msgHdr.m_Description);
		Send(msgHdr);
	} else
	{
		proto::DataMissing msgMiss;
		Send(msgMiss);
	}
}

void Node::Peer::OnMsg(proto::Hdr&& msg)
{
	Task& t = get_FirstTask();

	if (t.m_Key.second)
		ThrowUnexpected();

	Block::SystemState::ID id;
	msg.m_Description.get_ID(id);
	if (id != t.m_Key.first)
		ThrowUnexpected();

	NodeDB::PeerID pid;
	get_ID(pid);

	NodeProcessor::DataStatus::Enum eStatus = m_pThis->m_Processor.OnState(msg.m_Description, m_pThis->m_Cfg.m_TestMode.m_bFakePoW, pid);
	OnFirstTaskDone(eStatus);
}

void Node::Peer::OnMsg(proto::GetBody&& msg)
{
	uint64_t rowid = m_pThis->m_Processor.get_DB().StateFindSafe(msg.m_ID);
	if (rowid)
	{
		proto::Body msgBody;
		ByteBuffer bbRollback;
		m_pThis->m_Processor.get_DB().GetStateBlock(rowid, msgBody.m_Buffer, bbRollback);

		if (!msgBody.m_Buffer.empty())
		{
			Send(msgBody);
			return;
		}

	}

	proto::DataMissing msgMiss;
	Send(msgMiss);
}

void Node::Peer::OnMsg(proto::Body&& msg)
{
	Task& t = get_FirstTask();

	if (!t.m_Key.second)
		ThrowUnexpected();

	NodeDB::PeerID pid;
	get_ID(pid);

	const Block::SystemState::ID& id = t.m_Key.first;

	NodeProcessor::DataStatus::Enum eStatus = m_pThis->m_Processor.OnBlock(id, msg.m_Buffer, pid);
	OnFirstTaskDone(eStatus);
}

void Node::Peer::OnFirstTaskDone(NodeProcessor::DataStatus::Enum eStatus)
{
	if (NodeProcessor::DataStatus::Invalid == eStatus)
		ThrowUnexpected();

	get_FirstTask().m_bRelevant = false;
	OnFirstTaskDone();

	if (NodeProcessor::DataStatus::Accepted == eStatus)
		m_pThis->RefreshCongestions(); // NOTE! Can call OnPeerInsane()
}

void Node::Peer::OnMsg(proto::NewTransaction&& msg)
{
	// TODO: Verify all the Ptrs for being non-NULL (or better do this within serialization)

	proto::Boolean msgOut;
	msgOut.m_Value = true;

	NodeProcessor::TxPool::Element::Tx key;
	key.m_pValue = std::move(msg.m_Transaction);

	NodeProcessor::TxPool::TxSet::iterator it = m_pThis->m_TxPool.m_setTxs.find(key);
	if (m_pThis->m_TxPool.m_setTxs.end() == it)
	{
		// new transaction

		std::ostringstream os;
		{
			// Log it
			const TxBase& tx = *key.m_pValue;

			os << "Tx from " << get_Connection()->peer_address();

			for (size_t i = 0; i < tx.m_vInputs.size(); i++)
				os << "\n\tI: " << tx.m_vInputs[i]->m_Commitment;

			for (size_t i = 0; i < tx.m_vOutputs.size(); i++)
			{
				const Output& outp = *tx.m_vOutputs[i];
				os << "\n\tO: " << outp.m_Commitment;

				if (outp.m_Incubation)
					os << ", Incubation +" << outp.m_Incubation;

				if (outp.m_pPublic)
					os << ", Sum=" << outp.m_pPublic->m_Value;

				if (outp.m_pConfidential)
					os << ", Confidential";
			}

			for (size_t i = 0; i < tx.m_vKernelsOutput.size(); i++)
				os << "\n\tK: Fee=" << tx.m_vKernelsOutput[i]->m_Fee;
		}

		Height h = m_pThis->m_Processor.get_NextHeight();
		msgOut.m_Value = m_pThis->m_TxPool.AddTx(std::move(key.m_pValue), h);

		os << "\n\tValid: " << msgOut.m_Value;
		LOG_INFO() << os.str();

		if (msgOut.m_Value)
		{
			m_pThis->m_TxPool.ShrinkUpTo(m_pThis->m_Cfg.m_MaxPoolTransactions);
			m_pThis->m_Miner.SetTimer(m_pThis->m_Cfg.m_Timeout.m_MiningSoftRestart_ms, false);

			// Current (naive) design: send it to all other nodes
			for (PeerList::iterator it = m_pThis->m_lstPeers.begin(); m_pThis->m_lstPeers.end() != it; )
			{
				Peer& peer = *it++;
				if (this == &peer)
					continue;
				if (State::Connected != peer.m_eState)
					continue;
				if (!peer.m_Config.m_SpreadingTransactions)
					continue;

				try {
					peer.Send(msg);
				} catch (...) {
					peer.OnPostError();
				}
			}
		}
	}

	Send(msgOut);
}

void Node::Peer::OnMsg(proto::Config&& msg)
{
	if (!m_Config.m_SpreadingTransactions && msg.m_SpreadingTransactions)
	{
		// TODO: decide if/how to sent the pending transactions.
		// maybe this isn't necessary, in this case it'll receive only new transactions.
	}

	if (!m_Config.m_AutoSendHdr && msg.m_AutoSendHdr)
	{
		proto::Hdr msgHdr;
		if (m_pThis->m_Processor.get_CurrentState(msgHdr.m_Description))
			Send(msgHdr);
	}

	m_Config = msg;
}

void Node::Peer::OnMsg(proto::GetMined&& msg)
{
	// TODO: report this only to authenticated users over secure channel
	proto::Mined msgOut;

	NodeDB& db = m_pThis->m_Processor.get_DB();
	NodeDB::WalkerMined wlk(db);
	for (db.EnumMined(wlk, msg.m_HeightMin); wlk.MoveNext(); )
	{
		msgOut.m_Entries.resize(msgOut.m_Entries.size() + 1);
		proto::PerMined& x = msgOut.m_Entries.back();

		x.m_Fees = wlk.m_Amount;
		x.m_Active = 0 != (db.GetStateFlags(wlk.m_Sid.m_Row) & NodeDB::StateFlags::Active);

		Block::SystemState::Full s;
		db.get_State(wlk.m_Sid.m_Row, s);
		s.get_ID(x.m_ID);

		if (msgOut.m_Entries.size() == proto::PerMined::s_EntriesMax)
			break;
	}

	Send(msgOut);
}

void Node::Peer::OnMsg(proto::GetProofState&& msg)
{
	if (msg.m_Height < Block::Rules::HeightGenesis)
		ThrowUnexpected();

	proto::Proof msgOut;

	NodeDB::StateID sid;
	if (m_pThis->m_Processor.get_DB().get_Cursor(sid))
	{
		if (msg.m_Height < sid.m_Height)
			m_pThis->m_Processor.get_DB().get_Proof(msgOut.m_Proof, sid, msg.m_Height);
	}

	Send(msgOut);
}

void Node::Peer::OnMsg(proto::GetProofKernel&& msg)
{
	proto::Proof msgOut;

	RadixHashOnlyTree& t = m_pThis->m_Processor.get_Kernels();

	RadixHashOnlyTree::Cursor cu;
	bool bCreate = false;
	if (t.Find(cu, msg.m_KernelHash, bCreate))
	{
		t.get_Proof(msgOut.m_Proof, cu);

		msgOut.m_Proof.resize(msgOut.m_Proof.size() + 1);
		msgOut.m_Proof.back().first = false;

		m_pThis->m_Processor.get_Utxos().get_Hash(msgOut.m_Proof.back().second);
	}
}

void Node::Peer::OnMsg(proto::GetProofUtxo&& msg)
{
	struct Traveler :public UtxoTree::ITraveler
	{
		proto::ProofUtxo m_Msg;
		UtxoTree* m_pTree;
		Merkle::Hash m_hvKernels;

		virtual bool OnLeaf(const RadixTree::Leaf& x) override {

			const UtxoTree::MyLeaf& v = (UtxoTree::MyLeaf&) x;
			UtxoTree::Key::Data d;
			d = v.m_Key;

			m_Msg.m_Proofs.resize(m_Msg.m_Proofs.size() + 1);
			Input::Proof& ret = m_Msg.m_Proofs.back();

			ret.m_Count = v.m_Value.m_Count;
			ret.m_Maturity = d.m_Maturity;
			m_pTree->get_Proof(ret.m_Proof, *m_pCu);

			ret.m_Proof.resize(ret.m_Proof.size() + 1);
			ret.m_Proof.back().first = true;
			ret.m_Proof.back().second = m_hvKernels;

			return m_Msg.m_Proofs.size() < Input::Proof::s_EntriesMax;
		}
	} t;

	t.m_pTree = &m_pThis->m_Processor.get_Utxos();
	m_pThis->m_Processor.get_Kernels().get_Hash(t.m_hvKernels);

	UtxoTree::Cursor cu;
	t.m_pCu = &cu;

	// bounds
	UtxoTree::Key kMin, kMax;

	UtxoTree::Key::Data d;
	d.m_Commitment = msg.m_Utxo.m_Commitment;
	d.m_Maturity = msg.m_MaturityMin;
	kMin = d;
	d.m_Maturity = Height(-1);
	kMax = d;

	t.m_pBound[0] = kMin.m_pArr;
	t.m_pBound[1] = kMax.m_pArr;

	t.m_pTree->Traverse(t);

	Send(t.m_Msg);
}

void Node::Server::OnAccepted(io::TcpStream::Ptr&& newStream, int errorCode)
{
	if (newStream)
	{
        LOG_DEBUG() << "New peer connected: " << newStream->address();
		Peer* p = get_ParentObj().AllocPeer();
		p->m_iPeer = -1;

		p->Accept(std::move(newStream));
		p->OnConnected();
	}
}

void Node::Miner::OnRefresh(uint32_t iIdx)
{
	while (true)
	{
		Task::Ptr pTask;
		Block::SystemState::Full s;

		{
			std::scoped_lock<std::mutex> scope(m_Mutex);
			if (!m_pTask || *m_pTask->m_pStop)
				break;

			pTask = m_pTask;
			s = pTask->m_Hdr; // local copy
		}

		ECC::Hash::Value hv; // pick pseudo-random initial nonce for mining.
		ECC::Hash::Processor()
			<< get_ParentObj().m_Cfg.m_MinerID
			<< get_ParentObj().m_Processor.m_Kdf.m_Secret.V
			<< iIdx
			<< s.m_Height
			>> hv;

		static_assert(sizeof(s.m_PoW.m_Nonce) <= sizeof(hv));
		memcpy(s.m_PoW.m_Nonce.m_pData, hv.m_pData, sizeof(s.m_PoW.m_Nonce.m_pData));
	
		Block::PoW::Cancel fnCancel = [this, pTask](bool bRetrying)
		{
			if (*pTask->m_pStop)
				return true;

			if (bRetrying)
			{
				std::scoped_lock<std::mutex> scope(m_Mutex);
				if (pTask != m_pTask)
					return true; // soft restart triggered
			}

			return false;
		};

		if (get_ParentObj().m_Cfg.m_TestMode.m_bFakePoW)
		{
			uint32_t timeout_ms = get_ParentObj().m_Cfg.m_TestMode.m_FakePowSolveTime_ms;

			bool bSolved = false;

			//std::chrono::high_resolution_clock::duration
			for (std::chrono::system_clock::time_point tmStart = std::chrono::system_clock::now(); ; )
			{
				if (fnCancel(false))
					break;
				std::this_thread::sleep_for(std::chrono::milliseconds(50));

				std::chrono::system_clock::duration dt = std::chrono::system_clock::now() - tmStart;
				uint32_t dt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dt).count();

				if (dt_ms >= timeout_ms)
				{
					bSolved = true;
					break;
				}
			}

			if (!bSolved)
				continue;

			ZeroObject(s.m_PoW);
		}
		else
		{
			if (!s.GeneratePoW(fnCancel))
				continue;
		}

		std::scoped_lock<std::mutex> scope(m_Mutex);

		if (*pTask->m_pStop)
			continue; // either aborted, or other thread was faster

		pTask->m_Hdr = s; // save the result
		*pTask->m_pStop = true;
		m_pTask = pTask; // In case there was a soft restart we restore the one that we mined.

		m_pEvtMined->trigger();
		break;
	}
}

void Node::Miner::HardAbortSafe()
{
	std::scoped_lock<std::mutex> scope(m_Mutex);

	if (m_pTask)
	{
		*m_pTask->m_pStop = true;
		m_pTask = NULL;
	}
}

void Node::Miner::SetTimer(uint32_t timeout_ms, bool bHard)
{
	if (!m_pTimer)
		m_pTimer = io::Timer::create(io::Reactor::get_Current().shared_from_this());
	else
		if (m_bTimerPending && !bHard)
			return;

	m_pTimer->start(timeout_ms, false, [this]() { OnTimer(); });
	m_bTimerPending = true;
}

void Node::Miner::OnTimer()
{
	m_bTimerPending = false;
	Restart();
}

void Node::Miner::Restart()
{
	if (m_vThreads.empty())
		return; //  n/a

	Block::SystemState::ID id;
	if (!get_ParentObj().m_Processor.get_CurrentState(id) && !get_ParentObj().m_Cfg.m_TestMode.m_bMineGenesisBlock)
		return;

	Task::Ptr pTask(std::make_shared<Task>());
	if (!get_ParentObj().m_Processor.GenerateNewBlock(get_ParentObj().m_TxPool, pTask->m_Hdr, pTask->m_Body, pTask->m_Fees))
	{
		LOG_WARNING() << "Block generation failed, can't mine!";
		return;
	}

	pTask->m_Hdr.get_ID(id);

	LOG_INFO() << "Block generated: " << id << ", Fee=" << pTask->m_Fees << ", Difficulty=" << uint32_t(pTask->m_Hdr.m_PoW.m_Difficulty) << ", Size=" << pTask->m_Body.size();

	// let's mine it.
	std::scoped_lock<std::mutex> scope(m_Mutex);

	if (m_pTask)
		pTask->m_pStop = m_pTask->m_pStop; // use the same soft-restart indicator
	else
	{
		pTask->m_pStop.reset(new volatile bool);
		*pTask->m_pStop = false;
	}

	m_pTask = pTask;

	for (size_t i = 0; i < m_vThreads.size(); i++)
		m_vThreads[i].m_pEvtRefresh->trigger();
}

void Node::Miner::OnMined()
{
	Task::Ptr pTask;
	{
		std::scoped_lock<std::mutex> scope(m_Mutex);
		if (!(m_pTask && *m_pTask->m_pStop))
			return; //?!
		pTask.swap(m_pTask);
	}

	Block::SystemState::ID id;
	pTask->m_Hdr.get_ID(id);

	LOG_INFO() << "New block mined: " << id;

	NodeProcessor::DataStatus::Enum eStatus = get_ParentObj().m_Processor.OnState(pTask->m_Hdr, true, NodeDB::PeerID());
	assert(NodeProcessor::DataStatus::Accepted == eStatus); // Otherwise either the block is invalid (some bug?). Or someone else mined exactly the same block!

	NodeDB::StateID sid;
	verify(sid.m_Row = get_ParentObj().m_Processor.get_DB().StateFindSafe(id));
	sid.m_Height = id.m_Height;

	get_ParentObj().m_Processor.get_DB().SetMined(sid, pTask->m_Fees); // ding!

	eStatus = get_ParentObj().m_Processor.OnBlock(id, pTask->m_Body, NodeDB::PeerID()); // will likely trigger OnNewState(), and spread this block to the network
	assert(NodeProcessor::DataStatus::Accepted == eStatus);
}

} // namespace beam
