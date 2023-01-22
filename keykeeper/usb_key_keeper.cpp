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
#include "../hw_crypto/keykeeper.h"

#ifdef WIN32

#	include <WinSock2.h>
#	include <Hidsdi.h>
#	include <SetupAPI.h>
#	pragma comment (lib, "hid")
#	pragma comment (lib, "SetupAPI")

static std::string string_from_WStr(const wchar_t* wsz)
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
#	ifdef __APPLE__
#		ifdef __MACH__
#			include <IOKit/hid/IOHIDManager.h>
#			include <IOKit/hid/IOHIDKeys.h>
#			include <IOKit/IOKitLib.h>
#			include <CoreFoundation/CoreFoundation.h>
#		endif //__MACH__
#	else // __APPLE__
#		include <sys/ioctl.h>
#		ifndef __EMSCRIPTEN__
#			include <linux/hidraw.h>
#			define ENUM_VIA_HIDRAW
#			ifdef ENUM_VIA_UDEV // currently disabled
#				include <libudev.h>
#			endif // ENUM_VIA_UDEV
#		endif // __EMSCRIPTEN__
#	endif // __APPLE__
#endif // WIN32

namespace beam::wallet {

std::vector<HidInfo::Entry> HidInfo::EnumSupported()
{
	return Enum(0x2c97); // supported vendor (Ledger)
}

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

#ifdef __MACH__

	IOHIDManagerRef hMgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
	if (hMgr)
	{
		IOHIDManagerSetDeviceMatching(hMgr, nullptr);
		IOHIDManagerScheduleWithRunLoop(hMgr, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

		CFSetRef hSet = IOHIDManagerCopyDevices(hMgr);
		if (hSet)
		{
			uint32_t nDevs = CFSetGetCount(hSet);

			std::vector<IOHIDDeviceRef> vDevs;
			vDevs.resize(nDevs);

			CFSetGetValues(hSet, (const void**) (nDevs ? &vDevs.front() : nullptr));

			for (IOHIDDeviceRef hDev : vDevs)
			{
				if (!hDev)
					continue;

				struct PropReader
				{
					static int32_t get_Int(IOHIDDeviceRef hDev, const CFStringRef& key)
					{
						int32_t nVal = 0;
						CFTypeRef hVal = IOHIDDeviceGetProperty(hDev, key);
						if (hVal && (CFGetTypeID(hVal) == CFNumberGetTypeID()))
							CFNumberGetValue((CFNumberRef) hVal, kCFNumberSInt32Type, &nVal);

						return nVal;
					}

					static std::string get_Str(IOHIDDeviceRef hDev, const CFStringRef& key)
					{
						std::string sRet;

						CFTypeRef hVal = IOHIDDeviceGetProperty(hDev, key);
						if (hVal)
						{
							//CFIndex nLen = CFStringGetLength(hVal);

							char szBuf[0x100];
							if (CFStringGetCString((CFStringRef) hVal, szBuf, sizeof(szBuf), kCFStringEncodingUTF8))
								sRet = szBuf;
						}

						return sRet;
					}
				};

				uint16_t vid = (uint16_t) PropReader::get_Int(hDev, CFSTR(kIOHIDVendorIDKey));

				if (vid != nVendor)
					continue;

				// discover path
				io_string_t szPath;
				if (IORegistryEntryGetPath(IOHIDDeviceGetService(hDev), kIOServicePlane, szPath) != KERN_SUCCESS)
					continue;

				auto& x = ret.emplace_back();
				x.m_sPath = szPath;
				x.m_Version = 0; // unsupported atm
				x.m_Vendor = vid;
				x.m_Product = (uint16_t) PropReader::get_Int(hDev, CFSTR(kIOHIDProductIDKey));

				x.m_sManufacturer = PropReader::get_Str(hDev, CFSTR(kIOHIDManufacturerKey));
				x.m_sProduct = PropReader::get_Str(hDev, CFSTR(kIOHIDProductKey));
			}

			CFRelease(hSet);
		}

		// cleanup
		IOHIDManagerClose(hMgr, kIOHIDOptionsTypeNone);
		CFRelease(hMgr);
	}

#endif // __MACH__

#ifdef ENUM_VIA_UDEV
	udev* udevCtx = udev_new();
	if (udevCtx)
	{
		udev_enumerate* pEnum = udev_enumerate_new(udevCtx);
		if (pEnum)
		{
			udev_enumerate_add_match_subsystem(pEnum, "usb");
			udev_enumerate_scan_devices(pEnum);

			udev_list_entry* pList = udev_enumerate_get_list_entry(pEnum);
			udev_list_entry* pEntry;

			udev_list_entry_foreach(pEntry, pList)
			{
				udev_device* pDev = udev_device_new_from_syspath(udevCtx, udev_list_entry_get_name(pEntry));
				if (pDev)
				{
					const char* sz = udev_device_get_devnode(pDev);
					if (sz)
					{
						auto& x = ret.emplace_back();
						x.m_sPath = sz;
						x.m_Version = 0; // unsupported atm

						sz = udev_device_get_sysattr_value(pDev, "idVendor");
						x.m_Vendor = sz ? ((uint16_t)x.Str2Hex(sz)) : 0;

						sz = udev_device_get_sysattr_value(pDev, "idProduct");
						x.m_Product = sz ? ((uint16_t)x.Str2Hex(sz)) : 0;


						sz = udev_device_get_sysattr_value(pDev, "manufacturer");
						if (sz)
							x.m_sManufacturer = sz;

						sz = udev_device_get_sysattr_value(pDev, "product");
						if (sz)
							x.m_sProduct = sz;

						//sz = udev_device_get_sysattr_value(pDev, "serial");
						//if (sz)
						//	x.m_sSerialNumber = sz;

						if (nVendor && (nVendor != x.m_Vendor))
							ret.pop_back();
					}

					udev_device_unref(pDev);
				}
			}

			udev_enumerate_unref(pEnum);
		}

		udev_unref(udevCtx);
	}

#endif // ENUM_VIA_UDEV

#ifdef ENUM_VIA_HIDRAW

	for (uint32_t iDev = 0; iDev < 64; iDev++)
	{
		std::string sPath = "/dev/hidraw" + std::to_string(iDev);
		int hFile = open(sPath.c_str(), O_RDONLY);
		if (hFile < 0)
		{
			// can fail because of permissions, or simply becase there's a gap in device numbering. Continue search anyway
			continue;
			//break;
		}

		hidraw_devinfo hidInfo;
		memset(&hidInfo, 0, sizeof(hidInfo));

		if (!ioctl(hFile, HIDIOCGRAWINFO, &hidInfo) && (!nVendor || (hidInfo.vendor == nVendor)))
		{
			auto& x = ret.emplace_back();
			x.m_sPath = std::move(sPath);
			x.m_Version = 0; // unsupported atm
			x.m_Vendor = hidInfo.vendor;
			x.m_Product = hidInfo.product;

			char szName[0x100] = { 0 };
			if (ioctl(hFile, HIDIOCGRAWNAME(sizeof(szName)), szName) >= 0)
			{
				szName[_countof(szName) - 1] = 0;
				x.m_sProduct = szName; // manufacturer + name, single string
			}
		}

		close(hFile);
	}
	
#endif // ENUM_VIA_HIDRAW

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
	,m_hEvent(NULL)
#else // WIN32
#	ifdef __MACH__
	:m_hDev(nullptr)
#	else // __MACH__
	:m_hFile(-1)
#	endif // _MACH__
#endif // WIN32
{
}

UsbIO::~UsbIO()
{
#ifdef WIN32

	if (INVALID_HANDLE_VALUE != m_hFile)
		CloseHandle(m_hFile);

	if (m_hEvent)
		CloseHandle(m_hEvent);

#else // WIN32
#	ifdef __MACH__
	if (m_hDev)
		CFRelease(m_hDev);
#	else // __MACH__
	if (m_hFile >= 0)
		close(m_hFile);
#	endif // __MACH__
#endif // WIN32
}

#ifdef __MACH__

void ReportCallback(void* pCtx, IOReturn result, void* pSender, IOHIDReportType type, uint32_t reportID, uint8_t* pReport, CFIndex nReport)
{
	UsbIO* pDev = reinterpret_cast<UsbIO*>(pCtx);

	auto& x = pDev->m_qDone.emplace();
	x = pDev->m_Chunk0;
}

#endif // __MACH__

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
#	ifdef __MACH__

	auto pEntry = IORegistryEntryFromPath(kIOMainPortDefault, szPath);
	if (pEntry)
	{
		m_hDev = IOHIDDeviceCreate(kCFAllocatorDefault, pEntry);

		if (m_hDev)
		{
			IOHIDDeviceOpen(m_hDev, kIOHIDOptionsTypeSeizeDevice);

			IOHIDDeviceRegisterInputReportCallback(m_hDev, m_Chunk0.m_p, sizeof(m_Chunk0.m_p), ReportCallback, this);

			IOHIDDeviceScheduleWithRunLoop(m_hDev, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
		}

		IOObjectRelease(pEntry);
	}

	if (!pEntry)
		std::ThrowLastError();

#	else // __MACH__
	m_hFile = open(szPath, O_RDWR);
	if (m_hFile < 0)
		std::ThrowLastError();
#	endif // __MACH__


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
#	ifdef __MACH__

	if (!n)
		throw std::runtime_error("empty report");

	const uint8_t* pPtr = (const uint8_t*) p;
	auto res = IOHIDDeviceSetReport(m_hDev, kIOHIDReportTypeOutput, pPtr[0], pPtr, n);
	if (kIOReturnSuccess != res)
		std::ThrowLastError();

#	else // _MACH__
	auto wr = write(m_hFile, p, n);
	if (wr < 0)
		std::ThrowLastError();
#	endif // __MACH__
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
#	ifdef __MACH__

	while (m_qDone.empty())
	{
		auto res = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1, true);

		switch (res)
		{
		case kCFRunLoopRunHandledSource:
		case kCFRunLoopRunTimedOut:
			break;
		default:
			std::ThrowLastError();
		}
	}

	auto& x = m_qDone.front();
	memcpy(p, x.m_p, std::min<uint16_t>(sizeof(x.m_p), n));
	m_qDone.pop();

	return sizeof(x.m_p);
/*
	//IOHIDDeviceRegisterInputReportCallback
	CFIndex len = n;
	auto res = IOHIDDeviceGetReport(m_hDev, kIOHIDReportTypeInput, 1, (uint8_t*) p, &len);
	if (kIOReturnSuccess != res)
		std::ThrowLastError();
	return res;
*/	

#	else // __MACH__
	int bytes_read = read(m_hFile, p, n);
	if (bytes_read >= 0)
		return static_cast<uint16_t>(bytes_read);

	if ((errno != EAGAIN) && (errno == EINPROGRESS))
		std::ThrowLastError();

	return 0;
#	endif // __MACH__
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

std::shared_ptr<UsbKeyKeeper> UsbKeyKeeper::Open(const std::string& sPath)
{
	auto pRet = std::make_shared<UsbKeyKeeper_ToConsole>();
	pRet->m_sPath = sPath;
	return pRet;
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
	return PopLocked(pTask);
}

bool UsbKeyKeeper::TaskList::PopLocked(Task::Ptr& pTask)
{
	if (empty())
		return false;

	pTask.reset(&front());
	pop_front();
	return true;
}

void UsbKeyKeeper::SendRequestAsync(void* pBuf, uint32_t nRequest, uint32_t nResponse, const Handler::Ptr& pHandler)
{
	Task::Ptr pTask(new Task);
	pTask->m_pBuf = reinterpret_cast<uint8_t*>(pBuf);
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


struct HwMsgs
{

#pragma pack (push, 1)

#define THE_MACRO_Field(type, name) type m_##name;
#define THE_MACRO(id, name) \
	struct name { \
		struct Out { \
			uint8_t m_OpCode = id; \
			BeamCrypto_ProtoRequest_##name(THE_MACRO_Field) \
		}; \
		struct In { \
			uint8_t m_StatusCode; \
			BeamCrypto_ProtoResponse_##name(THE_MACRO_Field) \
		}; \
	};

	BeamCrypto_ProtoMethods(THE_MACRO)
#undef THE_MACRO
#undef THE_MACRO_Field
#pragma pack (pop)

};



void UsbKeyKeeper::RunThreadGuarded()
{
	std::string sLastPath;

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
				bool m_Stall = false;

				static void TestDisconnect(uint16_t nSizeRead)
				{
					if (!nSizeRead)
						throw std::runtime_error("Device disconnected");
				}

				static void ThrowBadSig()
				{
					throw std::runtime_error("Beam app not running");
				}

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
#	ifdef __MACH__
					return m_Usbio.Read(p, n);
#	else // __MACH__
					// wait for data
					if (!m_This.WaitEvent(&m_Usbio.m_hFile, pTimeout_ms))
						return 0;

					// read once
					return m_Usbio.Read(p, n);
#	endif // __MACH__
#endif // WIN32
				}

				uint16_t ReadRaw(void* p, uint16_t n) override
				{
					if (!m_Stall)
					{
						uint32_t nTimeout_ms = 3000;
						uint16_t res = ReadInternal(p, n, &nTimeout_ms);
						if (res)
							return res;

						m_This.NotifyState(nullptr, DevState::Stalled);
						m_Stall = true;
					}

					uint16_t ret = ReadInternal(p, n, nullptr);
					if (ret && m_Stall)
					{
						m_This.NotifyState(nullptr, DevState::Connected);
						m_Stall = false;
					}
					return ret;
				}

				uint8_t m_pFrame[0x400]; // actual size is much smaller

				uint16_t ReadApduResponse()
				{
					uint16_t sw;
					uint16_t nLen = ReadFrame(m_pFrame, sizeof(m_pFrame));
					if ((nLen < sizeof(sw)) || (nLen > sizeof(m_pFrame)))
						ThrowBadSig();

					memcpy(&sw, m_pFrame + nLen - sizeof(sw), sizeof(sw));
					sw = ByteOrder::from_be(sw);

					switch (sw)
					{
					case 0x9000:
						break; // ok

					case 0x6d02:
						throw std::runtime_error("Beam app not running");
						// no break;

					case 0x5515:
						throw std::runtime_error("device locked");
						// no break;

					default:
						throw std::runtime_error(std::string("Device status: ") + uintBigFrom(sw).str());
					}

					nLen -= sizeof(sw);
					if (!nLen)
						ThrowBadSig();

					if (m_pFrame[0] != c_KeyKeeper_Status_Ok)
					{
						// proper error message should be returned
						if ((4 != nLen) ||
							('b' != m_pFrame[2]) ||
							('F' != m_pFrame[3]))
							AsyncReader::ThrowBadSig();
					}

					return nLen;
				}

				void WriteApdu(const void* p, uint16_t n)
				{
					m_pFrame[0] = 0xE0; // cla
					m_pFrame[1] = 'B'; // ins
					m_pFrame[2] = 0;
					m_pFrame[3] = 0;
					m_pFrame[4] = (uint8_t) n;

					memcpy(m_pFrame + 5, p, (uint8_t) n);
					m_Usbio.WriteFrame(m_pFrame, n + 5);
				}

			} reader(*this);

			{
				std::string sPath;
				if (m_sPath.empty())
				{
					if (sLastPath.empty())
					{
						auto v = HidInfo::EnumSupported();
						if (v.empty())
							throw std::runtime_error("no supported devices found");

						sPath = v.front().m_sPath;
					}
					else
						sPath.swap(sLastPath);
				}

				reader.m_Usbio.Open((m_sPath.empty() ? sPath : m_sPath).c_str());

				// verify dev signature
				HwMsgs::Version::Out msgOut;
				reader.WriteApdu(&msgOut, sizeof(msgOut));

				uint16_t nLen = reader.ReadApduResponse();
				if (sizeof(HwMsgs::Version::In) != nLen)
					AsyncReader::ThrowBadSig();

				const HwMsgs::Version::In* pMsg = (const HwMsgs::Version::In*) reader.m_pFrame;
				if ((c_KeyKeeper_Status_Ok != pMsg->m_StatusCode) ||
					memcmp(pMsg->m_Signature, BeamCrypto_Signature, sizeof(pMsg->m_Signature)))
					AsyncReader::ThrowBadSig();

				uint32_t nVer = ByteOrder::from_le(pMsg->m_Version); // unaligned, don't care
				if (nVer != BeamCrypto_CurrentVersion)
					throw std::runtime_error("Unsupported vesion: " + std::to_string(nVer));

				if (m_sPath.empty())
					sLastPath = std::move(sPath);
			}


			NotifyState(nullptr, DevState::Connected);

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

				if ((pTask->m_nRequest > 0xff) ||
					(pTask->m_nResponse > sizeof(reader.m_pFrame) - 5))
					// too big (should not happen), just skip it
					pTask->m_eRes = Status::NotImplemented;
				else
				{

					// send request
					auto pBuf = pTask->m_pBuf;
					reader.WriteApdu(pBuf, (uint16_t) pTask->m_nRequest);

					uint16_t nLen = reader.ReadApduResponse();
					if (!nLen)
						AsyncReader::ThrowBadSig();

					if (reader.m_pFrame[0] != c_KeyKeeper_Status_Ok)
					{
						pTask->m_Dbg.m_OpCode = pBuf[0];
						pTask->m_Dbg.m_Major = reader.m_pFrame[0];
						pTask->m_Dbg.m_Minor = reader.m_pFrame[1];

						nLen = 1;
					}

					memcpy(pBuf, reader.m_pFrame, std::min(nLen, static_cast<uint16_t>(pTask->m_nResponse)));

					pTask->m_eRes = DeduceStatus(pBuf, pTask->m_nResponse, nLen);
				}

				if (m_lstDone.Push(pTask))
					m_pEvt->post();

			}
		}
		catch (const std::exception& e)
		{
			std::string sErr = e.what();
			NotifyState(&sErr, DevState::Disconnected);

			uint32_t nTimeout_ms = 1000;
			WaitEvent(nullptr, &nTimeout_ms);
		}
	}
}

void UsbKeyKeeper::NotifyState(std::string* pErr, DevState eState)
{
	bool bNotify = false;

	{
		std::scoped_lock l(m_lstDone.m_Mutex);

		if (pErr)
		{
			if (m_sLastError != *pErr)
			{
				pErr->swap(m_sLastError);
				bNotify = true;
			}
		}
		else
		{
			if (!m_sLastError.empty())
			{
				m_sLastError.clear();
				bNotify = true;
			}
		}

		if (m_State != eState)
		{
			m_State = eState;
			bNotify = true;
		}

		if (bNotify)
		{
			if (m_NotifyStatePending)
				bNotify = false; // already pending
			else
				m_NotifyStatePending = true;

			if (!m_lstDone.empty())
				bNotify = false; // already pending
		}
	}

	if (bNotify)
		m_pEvt->post();
}

thread_local UsbKeyKeeper::IEvents* UsbKeyKeeper_ToConsole::s_pEvents = nullptr;

void UsbKeyKeeper_ToConsole::Events::OnDevState(const std::string& sErr, DevState eState)
{
	if (s_pEvents)
		return s_pEvents->OnDevState(sErr, eState);

	switch (eState)
	{
	case DevState::Connected:
		std::cout << "HW Wallet connected" << std::endl;
		break;

	case DevState::Stalled:
		std::cout << "HW Wallet needs user interaction" << std::endl;
		break;

	default:
		// no break;
	case DevState::Disconnected:
		std::cout << "HW Wallet disconnected: " << sErr << std::endl;
		break;
	}
}

void UsbKeyKeeper_ToConsole::Events::OnDevReject(const CallStats& stats)
{
	if (s_pEvents)
		return s_pEvents->OnDevReject(stats);

	std::cout << "HW Wallet reject opcode=" << static_cast<uint32_t>(stats.m_Dbg.m_OpCode) << ", I/O sizes " << stats.m_nRequest << '/' << stats.m_nResponse
		<< ", Status=" << static_cast<uint32_t>(stats.m_Dbg.m_Major) << '.' << static_cast<uint32_t>(stats.m_Dbg.m_Minor) << std::endl;
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
	std::string sErr;

	while (true)
	{
		bool bNotify = false;
		DevState eState = DevState::Disconnected;

		Task::Ptr pTask;

		{
			std::scoped_lock l(m_lstDone.m_Mutex);
			m_lstDone.PopLocked(pTask);

			if (m_NotifyStatePending)
			{
				m_NotifyStatePending = false;
				bNotify = true;
				eState = m_State;
				sErr = m_sLastError;
			}
		}

		if (bNotify && m_pEvents)
			m_pEvents->OnDevState(sErr, eState);

		if (!pTask)
			break;

		if ((Status::Success != pTask->m_eRes) && m_pEvents)
			m_pEvents->OnDevReject(*pTask);

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

