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

void UsbIO::WriteFrame(const uint8_t* p, uint16_t n)
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

		DWORD dw;
		if (!WriteFile(m_hFile, &f, sizeof(f), &dw, NULL)) // must always send the whole frame
			std::ThrowLastError();
#else // WIN32

		auto wr = write(m_hFile, &f, sizeof(f) - sizeof(f.m_pBody) + nUsed);
		if (wr < 0)
			std::ThrowLastError();

#endif // WIN32

		if (bLast)
			break;

		nUsed = 0;
		p += nPortion;
		n -= nPortion;

		f.m_Seq = ByteOrder::to_be(++seq);
	}
}

uint16_t UsbIO::Read(void* p, uint16_t n)
{
#ifdef WIN32

	DWORD dw;
	if (!ReadFile(m_hFile, p, n, &dw, NULL))
		std::ThrowLastError();

	return static_cast<uint16_t>(dw);

#else // WIN32

	int bytes_read = read(m_hFile, p, n);
	if (bytes_read >= 0)
		return static_cast<uint16_t>(bytes_read);

	if ((errno != EAGAIN) && (errno == EINPROGRESS))
		std::ThrowLastError();

	return 0;

#endif // WIN32
}

struct UsbIO::FrameReader
{
	virtual uint16_t ReadRaw(void* p, uint16_t n) = 0;

	uint16_t ReadFrame(uint8_t* p, uint16_t n);
};

uint16_t UsbIO::FrameReader::ReadFrame(uint8_t* p, uint16_t n)
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
	struct Reader :public FrameReader
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

} // namespace beam::wallet

