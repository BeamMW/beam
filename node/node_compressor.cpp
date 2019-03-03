// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "node.h"
#include "../utility/logger.h"

namespace beam {

void Node::Compressor::Init()
{
	ZeroObject(m_hrNew);
	m_bStop = true;
	m_bEnabled =
		get_ParentObj().m_Cfg.m_HistoryCompression.m_bEnabled &&
		!get_ParentObj().m_Cfg.m_HistoryCompression.m_sPathOutput.empty();

	if (m_bEnabled)
	{
		OnRolledBack(); // delete potentially ahead-of-time macroblocks
		Cleanup(); // delete exceeding backlog, broken files

		OnNewState();
	}
}

void Node::Compressor::Cleanup()
{
	// delete missing datas, delete exceeding backlog
	Processor& p = get_ParentObj().m_Processor;

	uint32_t nBacklog = get_ParentObj().m_Cfg.m_HistoryCompression.m_MaxBacklog + 1;

	NodeDB::WalkerState ws(p.get_DB());
	for (p.get_DB().EnumMacroblocks(ws); ws.MoveNext(); )
	{
		if (nBacklog)
		{
			// check if it's valid
			try {

				Block::BodyBase::RW rw;
				FmtPath(rw, ws.m_Sid.m_Height, NULL);
				rw.ROpen();

				Block::BodyBase body;
				Block::SystemState::Sequence::Prefix prf;
				rw.get_Start(body, prf);

				// ok
				nBacklog--;
				continue;

			} catch (const std::exception& e) {
				LOG_WARNING() << "History at height " << ws.m_Sid.m_Height << " corrupted: " << e.what();
			}
		}

		Delete(ws.m_Sid);
	}
}

void Node::Compressor::OnRolledBack()
{
	if (!m_bEnabled)
		return;

	Processor& p = get_ParentObj().m_Processor;

	if (m_hrNew.m_Max > p.m_Cursor.m_ID.m_Height)
		StopCurrent();

	NodeDB::WalkerState ws(p.get_DB());
	p.get_DB().EnumMacroblocks(ws);

	while (ws.MoveNext() && (ws.m_Sid.m_Height > p.m_Cursor.m_ID.m_Height))
		Delete(ws.m_Sid);

	// wait for OnNewState callback to realize new task
}

void Node::Compressor::Delete(const NodeDB::StateID& sid)
{
	NodeDB& db = get_ParentObj().m_Processor.get_DB();
	db.MacroblockDel(sid.m_Row);

	Block::BodyBase::RW rw;
	FmtPath(rw, sid.m_Height, NULL);
	rw.Delete();

	LOG_WARNING() << "History at height " << sid.m_Height << " deleted";
}

void Node::Compressor::OnNewState()
{
	if (!m_bEnabled)
		return;

	if (m_hrNew.m_Max)
		return; // alreaddy in progress

	Processor& p = get_ParentObj().m_Processor;

	const uint32_t nThreshold = Rules::get().Macroblock.MaxRollback;

	if (p.m_Cursor.m_ID.m_Height - Rules::HeightGenesis + 1 < nThreshold)
		return;

	HeightRange hr;
	hr.m_Max = p.m_Cursor.m_ID.m_Height - nThreshold;
	hr.m_Max -= ((hr.m_Max - Rules::HeightGenesis + 1) % Rules::get().Macroblock.Granularity);

	// last macroblock
	NodeDB::WalkerState ws(p.get_DB());
	p.get_DB().EnumMacroblocks(ws);
	hr.m_Min = ws.MoveNext() ? ws.m_Sid.m_Height : 0;

	if (hr.m_Min >= hr.m_Max)
		return;

	LOG_INFO() << "History generation started up to height " << hr.m_Max;

	// Start aggregation
	m_hrNew = hr;
	m_bStop = false;
	m_bSuccess = false;
	ZeroObject(m_hrInplaceRequest);
	get_ParentObj().m_Processor.get_DB().get_StateHash(get_ParentObj().m_Processor.FindActiveAtStrict(hr.m_Max), m_hvTag);

	m_Link.m_pReactor = io::Reactor::get_Current().shared_from_this();
	m_Link.m_pEvt = io::AsyncEvent::create(*m_Link.m_pReactor, [this]() { OnNotify(); });;
	m_Link.m_Thread = std::thread(&Compressor::Proceed, this);
}

void Node::Compressor::FmtPath(Block::BodyBase::RW& rw, Height h, const Height* pH0)
{
	FmtPath(rw.m_sPath, h, pH0);
}

void Node::Compressor::FmtPath(std::string& out, Height h, const Height* pH0)
{
	std::stringstream str;
	if (!pH0)
		str << get_ParentObj().m_Cfg.m_HistoryCompression.m_sPathOutput << "mb_";
	else
		str << get_ParentObj().m_Cfg.m_HistoryCompression.m_sPathTmp << "tmp_" << *pH0 << "_";

	str << h;
	out = str.str();
}

void Node::Compressor::OnNotify()
{
	assert(m_hrNew.m_Max);

	if (m_hrInplaceRequest.m_Max)
	{
		// extract & resume
		try
		{
			Block::Body::RW rw;
			FmtPath(rw, m_hrInplaceRequest.m_Max, &m_hrInplaceRequest.m_Min);
			rw.m_hvContentTag = m_hvTag;
			rw.WCreate();

			get_ParentObj().m_Processor.ExportMacroBlock(rw, m_hrInplaceRequest);
		}
		catch (const std::exception& e) {
			m_bStop = true; // error indication
			LOG_WARNING() << "History add " << e.what();
		}

		{
			// lock is aqcuired by the other thread before it trigger the events. The following line guarantees it won't miss our notification
			std::unique_lock<std::mutex> scope(m_Mutex);
		}

		m_Cond.notify_one();
	}
	else
	{
		Height h = m_hrNew.m_Max;
		StopCurrent();

		if (m_bSuccess)
		{
			Block::Body::RW rwSrc, rwTrg;
			FmtPath(rwSrc, h, &Rules::HeightGenesis);
			FmtPath(rwTrg, h, NULL);

			for (int i = 0; i < Block::Body::RW::Type::count; i++)
			{
				std::string sSrc;
				std::string sTrg;
				rwSrc.GetPath(sSrc, i);
				rwTrg.GetPath(sTrg, i);

#ifdef WIN32
				bool bOk =
					MoveFileExW(Utf8toUtf16(sSrc.c_str()).c_str(), Utf8toUtf16(sTrg.c_str()).c_str(), MOVEFILE_REPLACE_EXISTING) ||
					(GetLastError() == ERROR_FILE_NOT_FOUND);
#else // WIN32
				bool bOk =
					!rename(sSrc.c_str(), sTrg.c_str()) ||
					(ENOENT == errno);
#endif // WIN32

				if (!bOk)
				{
					LOG_WARNING() << "History file move/rename failed";
					m_bSuccess = false;
					break;
				}
			}

			if (!m_bSuccess)
			{
				rwSrc.Delete();
				rwTrg.Delete();
			}
		}

		if (m_bSuccess)
		{
			uint64_t rowid = get_ParentObj().m_Processor.FindActiveAtStrict(h);
			get_ParentObj().m_Processor.get_DB().MacroblockIns(rowid);
			get_ParentObj().m_Processor.FlushDB();

			LOG_INFO() << "History generated up to height " << h;

			Cleanup();
		}
		else
			LOG_WARNING() << "History generation failed";

	}
}

void Node::Compressor::StopCurrent()
{
	if (!m_hrNew.m_Max)
		return;

	{
		std::unique_lock<std::mutex> scope(m_Mutex);
		m_bStop = true;
	}

	m_Cond.notify_one();

	if (m_Link.m_Thread.joinable())
		m_Link.m_Thread.join();

	ZeroObject(m_hrNew);
	m_Link.m_pEvt = NULL; // should prevent "spurious" calls
}

void Node::Compressor::Proceed()
{
	try {
		m_bSuccess = ProceedInternal();
	} catch (const std::exception& e) {
		LOG_WARNING() << e.what();
	}

	if (!(m_bSuccess || m_bStop))
		LOG_WARNING() << "History generation failed";

	ZeroObject(m_hrInplaceRequest);
	m_Link.m_pEvt->post();
}

bool Node::Compressor::ProceedInternal()
{
	assert(m_hrNew.m_Max);
	const Config::HistoryCompression& cfg = get_ParentObj().m_Cfg.m_HistoryCompression;

	std::vector<HeightRange> v;

	uint32_t i = 0;
	for (Height hPos = m_hrNew.m_Min; hPos < m_hrNew.m_Max; i++)
	{
		HeightRange hr;
		hr.m_Min = hPos + 1; // convention is boundary-inclusive, whereas m_hrNew excludes min bound
		hr.m_Max = std::min(hPos + cfg.m_Naggling, m_hrNew.m_Max);

		{
			std::unique_lock<std::mutex> scope(m_Mutex);
			m_hrInplaceRequest = hr;

			m_Link.m_pEvt->post();

			m_Cond.wait(scope);

			if (m_bStop)
				return false;
		}

		v.push_back(hr);
		hPos = hr.m_Max;

		for (uint32_t j = i; 1 & j; j >>= 1)
			SquashOnce(v);
	}

	while (v.size() > 1)
		SquashOnce(v);

	if (m_hrNew.m_Min >= Rules::HeightGenesis)
	{
		Block::Body::RW rw, rwSrc0, rwSrc1;

		FmtPath(rw, m_hrNew.m_Max, &Rules::HeightGenesis);
		FmtPath(rwSrc0, m_hrNew.m_Min, NULL);

		Height h0 = m_hrNew.m_Min + 1;
		FmtPath(rwSrc1, m_hrNew.m_Max, &h0);

		rw.m_bAutoDelete = rwSrc1.m_bAutoDelete = true;

		if (!SquashOnce(rw, rwSrc0, rwSrc1))
			return false;

		rw.m_bAutoDelete = false;
	}

	return true;
}

bool Node::Compressor::SquashOnce(std::vector<HeightRange>& v)
{
	assert(v.size() >= 2);

	HeightRange& hr0 = v[v.size() - 2];
	HeightRange& hr1 = v[v.size() - 1];

	Block::Body::RW rw, rwSrc0, rwSrc1;
	FmtPath(rw, hr1.m_Max, &hr0.m_Min);
	FmtPath(rwSrc0, hr0.m_Max, &hr0.m_Min);
	FmtPath(rwSrc1, hr1.m_Max, &hr1.m_Min);

	hr0.m_Max = hr1.m_Max;
	v.pop_back();

	rw.m_bAutoDelete = rwSrc0.m_bAutoDelete = rwSrc1.m_bAutoDelete = true;

	if (!SquashOnce(rw, rwSrc0, rwSrc1))
		return false;

	rw.m_bAutoDelete = false;
	return true;
}

bool Node::Compressor::SquashOnce(Block::BodyBase::RW& rw, Block::BodyBase::RW& rwSrc0, Block::BodyBase::RW& rwSrc1)
{
	rwSrc0.ROpen();
	rwSrc1.ROpen();

	rw.m_hvContentTag = m_hvTag;
	rw.WCreate();

	if (!rw.CombineHdr(std::move(rwSrc0), std::move(rwSrc1), m_bStop))
		return false;

	if (!rw.Combine(std::move(rwSrc0), std::move(rwSrc1), m_bStop))
		return false;

	return true;
}

uint64_t Node::Compressor::get_SizeTotal(Height h)
{
	uint64_t ret = 0;

	Block::Body::RW rw;
	FmtPath(rw, h, NULL);

	for (uint8_t iData = 0; iData < Block::Body::RW::Type::count; iData++)
	{
		std::string sPath;
		rw.GetPath(sPath, iData);

		std::FStream fs;
		if (fs.Open(sPath.c_str(), true))
			ret += fs.get_Remaining();
	}

	return ret;
}

} // namespace beam
