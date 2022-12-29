// Copyright 2019 The Beam Team
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

#include "usb_key_keeper.h"
#include "utility/byteorder.h"

#ifdef WIN32

#	include <WinSock2.h>
#	include <Hidsdi.h>
#	include <SetupAPI.h>
#	pragma comment (lib, "hid")
#	pragma comment (lib, "SetupAPI")

std::string string_from_WStr(const wchar_t* wsz)
{
	std::string sRet;
	int nLen = WideCharToMultiByte(CP_UTF8, 0, wsz, -1, NULL, 0, NULL, NULL);
	if (nLen > 1)
	{
		sRet.resize(nLen - 1);
		WideCharToMultiByte(CP_UTF8, 0, wsz, -1, &sRet.front(), nLen, NULL, NULL);
	}

	return sRet;
}

#else // WIN32

#	include <poll.h>
#	include <unistd.h>

#endif // WIN32

namespace beam::wallet {

std::vector<HidInfo::Entry> HidInfo::Enum(uint16_t nVendor)
{
	std::vector<Entry> ret;

#ifdef WIN32

	// enum HID devices
	GUID guidHid;
	HidD_GetHidGuid(&guidHid);

	ByteBuffer bufAux;

	HDEVINFO hEnum = SetupDiGetClassDevs(&guidHid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (hEnum)
	{
		for (DWORD dwIndex = 0; ; dwIndex++)
		{
			SP_DEVINFO_DATA devData;
			devData.cbSize = sizeof(devData);

			if (!SetupDiEnumDeviceInfo(hEnum, dwIndex, &devData))
			{
				// GetLastError() should be ERROR_NOT_FOUND
				break;
			}

			SP_DEVICE_INTERFACE_DATA devIntfc;
			ZeroObject(devIntfc);
			devIntfc.cbSize = sizeof(devIntfc);

			if (!SetupDiEnumDeviceInterfaces(hEnum, /*&devData*/NULL, &guidHid, dwIndex, &devIntfc))
				continue; //?!

			DWORD dwSizeIntfc = 0;
			if (!SetupDiGetDeviceInterfaceDetailA(hEnum, &devIntfc, NULL, 0, &dwSizeIntfc, NULL))
			{
				if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
					continue; //?!
			}

			if (dwSizeIntfc < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A))
				continue;

			bufAux.resize(dwSizeIntfc);
			auto* pDevDetail = (SP_DEVICE_INTERFACE_DETAIL_DATA_A*)&bufAux.front();
			pDevDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

			if (!SetupDiGetDeviceInterfaceDetailA(hEnum, &devIntfc, pDevDetail, dwSizeIntfc, &dwSizeIntfc, NULL))
				continue;

			HANDLE hDev = CreateFileA(pDevDetail->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
			if (INVALID_HANDLE_VALUE != hDev)
			{
				HIDD_ATTRIBUTES hidAttr;
				hidAttr.Size = sizeof(hidAttr);

				if (HidD_GetAttributes(hDev, &hidAttr) && (nVendor == hidAttr.VendorID))
				{
					auto& x = ret.emplace_back();
					x.m_Vendor = hidAttr.VendorID;
					x.m_Product = hidAttr.ProductID;
					x.m_Version = hidAttr.VersionNumber;

					x.m_sPath = pDevDetail->DevicePath;

					wchar_t wszStr[256];
					if (HidD_GetManufacturerString(hDev, wszStr, sizeof(wszStr)))
						x.m_sManufacturer = string_from_WStr(wszStr);

					if (HidD_GetProductString(hDev, wszStr, sizeof(wszStr)))
						x.m_sProduct = string_from_WStr(wszStr);
				}


				CloseHandle(hDev);
			}
		}

		SetupDiDestroyDeviceInfoList(hEnum);
	}

#endif // WIN32

	return ret;
}


#pragma pack (push, 1)

struct UsbIO::Frame
{
#ifdef WIN32
	uint8_t m_ReportNo = 0;
#endif // WIN32
	uint8_t m_Magic1 = 0x01;
	uint8_t m_Magic2 = 0x01;
	uint8_t m_Magic3 = 0x05;
	uint16_t m_Seq = 0;
	uint8_t m_pBody[59]; // TODO - make it variable (based on retrieved caps)

	struct Reader
	{
		virtual uint16_t ReadRaw(void* p, uint16_t n) = 0;
		uint16_t ReadFrame(uint8_t* p, uint16_t n);
	};

	struct Writer
	{
		virtual void WriteRaw(const void* p, uint16_t n) = 0;
		void WriteFrame(const uint8_t* p, uint16_t n);
	};
};

#pragma pack (pop)


UsbIO::UsbIO()
#ifdef WIN32
	:m_hFile(INVALID_HANDLE_VALUE)
#else // WIN32
	:m_hFile(-1)
#endif // WIN32
{
}

UsbIO::~UsbIO()
{
#ifdef WIN32

	if (INVALID_HANDLE_VALUE != m_hFile)
	{
		BOOL b = CloseHandle(m_hFile);
		assert(b);
	}

	if (m_hEvent)
		CloseHandle(m_hEvent);

#else // WIN32
	if (m_hFile >= 0)
		close(m_hFile);
#endif // WIN32
}

void UsbIO::Open(const char* szPath)
{
#ifdef WIN32
	m_hFile = CreateFileA(szPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	if (INVALID_HANDLE_VALUE == m_hFile)
		std::ThrowLastError();

	m_hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (!m_hEvent)
		std::ThrowLastError();

	HIDP_CAPS hidCaps;
	ZeroObject(hidCaps);

	PHIDP_PREPARSED_DATA pPrepData = NULL;
	HidD_GetPreparsedData(m_hFile, &pPrepData);
	if (pPrepData)
	{
		HidP_GetCaps(pPrepData, &hidCaps);
		HidD_FreePreparsedData(pPrepData);
	}

	// TODO: check hidCaps.InputReportByteLength, hidCaps.OutputReportByteLength

#else // WIN32

	m_hFile = open(szPath, O_RDWR);
	if (m_hFile < 0)
		std::ThrowLastError();

#endif // WIN32
}

void UsbIO::Frame::Writer::WriteFrame(const uint8_t* p, uint16_t n)
{
	Frame f;

	uint16_t nVal = ByteOrder::to_be(n);
	memcpy(f.m_pBody, &nVal, sizeof(nVal));
	uint8_t nUsed = sizeof(nVal);

	for (uint16_t seq = 0; ; )
	{
		uint8_t nReserve = sizeof(f.m_pBody) - nUsed;
		bool bLast = (nReserve >= n);
		uint8_t nPortion = bLast ? static_cast<uint8_t>(n) : nReserve;

		memcpy(f.m_pBody + nUsed, p, nPortion);
		nUsed += nPortion;

#ifdef WIN32
		if (bLast)
			memset(f.m_pBody + nUsed, 0, sizeof(f.m_pBody) - nUsed);

		WriteRaw(&f, sizeof(f)); // must always send the whole frame
#else // WIN32
		WriteRaw(&f, sizeof(f) - sizeof(f.m_pBody) + nUsed);
#endif // WIN32

		if (bLast)
			break;

		nUsed = 0;
		p += nPortion;
		n -= nPortion;

		f.m_Seq = ByteOrder::to_be(++seq);
	}
}


void UsbIO::WriteFrame(const uint8_t* p, uint16_t n)
{
	struct Writer :public Frame::Writer
	{
		UsbIO& m_This;
		Writer(UsbIO& x) :m_This(x) {}

		void WriteRaw(const void* p, uint16_t n) override
		{
			return m_This.Write(p, n);
		}

	} w(*this);

	w.WriteFrame(p, n);
}

#ifdef WIN32

void UsbIO::EnsureIoPending()
{
	if (GetLastError() != ERROR_IO_PENDING)
		std::ThrowLastError();
}

void UsbIO::WaitSync()
{
	EnsureIoPending();

	if (WAIT_OBJECT_0 != WaitForSingleObject(m_hEvent, INFINITE))
	{
		CancelIo(m_hFile);
		std::ThrowLastError();
	}
}

#endif // WIN32

void UsbIO::Write(const void* p, uint16_t n)
{
#ifdef WIN32

	OVERLAPPED over;
	ZeroObject(over);
	over.hEvent = m_hEvent;

	DWORD dw;
	if (!WriteFile(m_hFile, p, n, &dw, &over))
		WaitSync();
#else // WIN32

	auto wr = write(m_hFile, p, n);
	if (wr < 0)
		std::ThrowLastError();

#endif // WIN32
}

uint16_t UsbIO::Read(void* p, uint16_t n)
{
#ifdef WIN32

	OVERLAPPED over;
	ZeroObject(over);
	over.hEvent = m_hEvent;

	DWORD dw;
	if (ReadFile(m_hFile, p, n, &dw, &over))
		return static_cast<uint16_t>(dw);

	WaitSync();
	return static_cast<uint16_t>(over.InternalHigh);


#else // WIN32

	int bytes_read = read(m_hFile, p, n);
	if (bytes_read >= 0)
		return static_cast<uint16_t>(bytes_read);

	if ((errno != EAGAIN) && (errno == EINPROGRESS))
		std::ThrowLastError();

	return 0;

#endif // WIN32
}

uint16_t UsbIO::Frame::Reader::ReadFrame(uint8_t* p, uint16_t n)
{
	UsbIO::Frame f;

	if (ReadRaw(&f, sizeof(f)) != sizeof(f))
		return 0;

	uint16_t nSize;
	memcpy(&nSize, f.m_pBody, sizeof(nSize));
	nSize = ByteOrder::from_be(nSize);

	uint8_t nUsed = sizeof(nSize);

	for (uint16_t nRemaining = std::min(nSize, n); ; )
	{
		uint8_t nReserve = sizeof(f.m_pBody) - nUsed;
		bool bLast = (nRemaining <= nReserve);
		uint8_t nPortion = bLast ? static_cast<uint8_t>(nRemaining) : nReserve;

		memcpy(p, f.m_pBody + nUsed, nPortion);

		if (bLast)
			break;

		nUsed = 0;
		p += nPortion;
		nRemaining -= nPortion;

		if (ReadRaw(&f, sizeof(f)) != sizeof(f))
			return 0;
	}

	return nSize;
}

uint16_t UsbIO::ReadFrame(uint8_t* p, uint16_t n)
{
	struct Reader :public Frame::Reader
	{
		UsbIO& m_This;
		Reader(UsbIO& x) :m_This(x) {}

		uint16_t ReadRaw(void* p, uint16_t n) override
		{
			return m_This.Read(p, n);
		}

	} r(*this);
	return r.ReadFrame(p, n);
}

UsbKeyKeeper::~UsbKeyKeeper()
{
	Stop();
}

void UsbKeyKeeper::Stop()
{
	if (m_Thread.joinable())
	{
		m_evtShutdown.Set();
		m_Thread.join();
	}
}

void UsbKeyKeeper::StartSafe()
{
	if (!m_Thread.joinable())
	{
		m_evtTask.Create();
		m_evtShutdown.Create();

		m_pEvt = io::AsyncEvent::create(io::Reactor::get_Current(), [this]() { OnEvent(); });


		m_Thread = MyThread(&UsbKeyKeeper::RunThread, this);
	}
}

bool UsbKeyKeeper::TaskList::Push(Task::Ptr& pTask)
{
	std::scoped_lock l(m_Mutex);
	return PushLocked(pTask);
}

bool UsbKeyKeeper::TaskList::PushLocked(Task::Ptr& pTask)
{
	bool bWasEmpty = empty();
	push_back(*pTask.release());

	return bWasEmpty;
}

bool UsbKeyKeeper::TaskList::Pop(Task::Ptr& pTask)
{
	std::scoped_lock l(m_Mutex);
	if (empty())
		return false;

	pTask.reset(&front());
	pop_front();

	return true;
}


void UsbKeyKeeper::SendRequestAsync(void* pBuf, uint32_t nRequest, uint32_t nResponse, const Handler::Ptr& pHandler)
{
	Task::Ptr pTask(new Task);
	pTask->m_pBuf = pBuf;
	pTask->m_nRequest = nRequest;
	pTask->m_nResponse = nResponse;
	pTask->m_pHandler = pHandler;

	bool bWasEmpty = m_lstPending.Push(pTask);
	if (bWasEmpty)
	{
		StartSafe();
		m_evtTask.Set();
	}
}

void UsbKeyKeeper::RunThread()
{
	try
	{
		RunThreadGuarded();
	}
	catch (const ShutdownExc&)
	{
		// ignore
	}
}

void UsbKeyKeeper::RunThreadGuarded()
{
	Task::Ptr pTask;
	while (true)
	{
		try
		{
			struct AsyncReader :public UsbIO::Frame::Reader
			{
				UsbKeyKeeper& m_This;
				AsyncReader(UsbKeyKeeper& x) :m_This(x) {}

				UsbIO m_Usbio;
				bool m_NotifyWait;

				uint16_t ReadInternal(void* p, uint16_t n, const uint32_t* pTimeout_ms)
				{
#ifdef WIN32
					// 1st try read. 
					OVERLAPPED over;
					memset(&over, 0, sizeof(over));
					over.hEvent = m_Usbio.m_hEvent;

					DWORD dw;
					if (ReadFile(m_Usbio.m_hFile, p, n, &dw, &over) && dw)
						return static_cast<uint16_t>(dw);

					UsbIO::EnsureIoPending();

					struct Guard
					{
						HANDLE m_hFile;
						Guard(HANDLE hFile) :m_hFile(hFile) {}
						~Guard()
						{
							if (INVALID_HANDLE_VALUE != m_hFile)
								CancelIo(m_hFile);
						}

					} g(m_Usbio.m_hFile);

					if (!m_This.WaitEvent(&m_Usbio.m_hEvent, pTimeout_ms))
						return 0;

					g.m_hFile = INVALID_HANDLE_VALUE; // dismiss
					return static_cast<uint16_t>(over.InternalHigh);
#else // WIN32
					// wait for data
					if (!m_This.WaitEvent(&m_Usbio.m_hFile, pTimeout_ms))
						return 0;

					// read once
					return m_Usbio.Read(p, n);
#endif // WIN32
				}

				uint16_t ReadRaw(void* p, uint16_t n) override
				{
					if (m_NotifyWait)
					{
						uint32_t nTimeout_ms = 3000;
						uint16_t res = ReadInternal(p, n, &nTimeout_ms);
						if (res)
							return res;

						// notify!
						m_NotifyWait = false;

						{
							std::scoped_lock l(m_This.m_lstDone.m_Mutex);
							m_This.m_WaitUser = true;
						}
						m_This.m_pEvt->post();

					}

					return ReadInternal(p, n, nullptr);
				}

			} reader(*this);

			reader.m_Usbio.Open(m_sPath.c_str());

			{
				std::scoped_lock l(m_lstDone.m_Mutex);
				m_sLastError.clear();
				m_WaitUser = false;
			}

			while (true)
			{
				// take a task
				if (!pTask)
				{
					while (!m_lstPending.Pop(pTask))
					{
						WaitEvent(&m_evtTask.m_hEvt, nullptr);

#ifndef WIN32
						// unset evt
						uint8_t pBuf[0x100];
						auto nRes = read(m_evtTask.m_hEvt, pBuf, sizeof(pBuf));
						(nRes); // don't care
#endif // WIN32
					}
				}

				// send request
				reader.m_Usbio.WriteFrame((uint8_t*) pTask->m_pBuf, static_cast<uint16_t>(pTask->m_nRequest));

				// read response asynchronously
				reader.m_NotifyWait = true;

				uint16_t nLen = reader.ReadFrame((uint8_t*) pTask->m_pBuf, static_cast<uint16_t>(pTask->m_nResponse));

				pTask->m_eRes = DeduceStatus((uint8_t*) pTask->m_pBuf, pTask->m_nResponse, nLen);

				bool bWasEmpty;
				{
					std::scoped_lock l(m_lstDone.m_Mutex);

					bWasEmpty = m_lstDone.PushLocked(pTask);
					m_WaitUser = false;
				}


				if (bWasEmpty)
					// notify
					m_pEvt->post();

			}
		}
		catch (const std::exception& e)
		{
			std::string sErr = e.what();

			{
				std::scoped_lock l(m_lstDone.m_Mutex);
				m_sLastError = std::move(sErr);
			}
			m_pEvt->post();

			uint32_t nTimeout_ms = 1000;
			WaitEvent(nullptr, &nTimeout_ms);
		}
	}
}

bool UsbKeyKeeper::WaitEvent(const Event::Handle* pEvt, const uint32_t* pTimeout_ms)
{
#ifdef WIN32

	HANDLE pWait[2];
	pWait[0] = m_evtShutdown.m_hEvt;
	uint32_t nWait = 1;

	if (pEvt)
	{
		pWait[1] = *pEvt;
		nWait = 2;
	}

	DWORD dw = WaitForMultipleObjects(nWait, pWait, FALSE, pTimeout_ms ? *pTimeout_ms : INFINITE);
	switch (dw)
	{
	case WAIT_OBJECT_0:
		break;

	default:
		std::ThrowLastError();
		// no break;

	case WAIT_OBJECT_0 + 1:
		return true;

	case WAIT_TIMEOUT:
		return false;
	}


#else // WIN32

	struct pollfd pFds[2];
	memset(pFds, 0, sizeof(pFds));

	pFds[0].fd = m_evtShutdown.m_hEvt;
	pFds[0].events = POLLIN;
	uint32_t nWait = 1;

	if (pEvt)
	{
		pFds[1].fd = *pEvt;
		pFds[1].events = POLLIN;
		nWait = 2;
	}

	int ret = poll(pFds, nWait, pTimeout_ms ? *pTimeout_ms : -1);
	if (ret < 0)
		std::ThrowLastError();
	if (!ret)
		return false; // timeout

	if (pFds[1].revents)
		return true;

#endif // WIN32

	ShutdownExc exc;
	throw exc;
}

void UsbKeyKeeper::OnEvent()
{
	while (true)
	{
		Task::Ptr pTask;
		if (!m_lstDone.Pop(pTask))
			break;

		pTask->m_pHandler->OnDone(pTask->m_eRes);
	}
}

#ifdef WIN32

UsbKeyKeeper::Event::Event() :m_hEvt(NULL)
{
}

UsbKeyKeeper::Event::~Event()
{
	if (m_hEvt)
		CloseHandle(m_hEvt);
}

void UsbKeyKeeper::Event::Set()
{
	SetEvent(m_hEvt);
}

void UsbKeyKeeper::Event::Create()
{
	m_hEvt = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (!m_hEvt)
		std::ThrowLastError();
}

#else // WIN32

UsbKeyKeeper::Event::Event() :m_hEvt(-1)
{
}

UsbKeyKeeper::Event::~Event()
{
	if (m_hEvt >= 0)
	{
		close(m_hEvt);
		close(m_hSetter);
	}
}

void UsbKeyKeeper::Event::Set()
{
	uint8_t dummy = 0;
	auto nRes = write(m_hSetter, &dummy, 1);
	(nRes); // ignore
}

void UsbKeyKeeper::Event::Create()
{
	int pFds[2];
	if (pipe(pFds) < 0)
		std::ThrowLastError();

	m_hEvt = pFds[0];
	m_hSetter = pFds[1];
}
#endif // WIN32

} // namespace beam::wallet

