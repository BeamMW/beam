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
// limitations under the License

#include "qr.h"

#include <QUrlQuery>
#include <QtGui/qimage.h>
#include "qrcode/QRCodeGenerator.h"
// #include "utility/logger.h"

QR::QR()
{
    update();
}

QR::QR(const QString& addr, uint width, uint height, double amount)
    : m_addr(addr)
    , m_width(width)
    , m_height(height)
    , m_amount(amount)
{
    update();
}

QR::~QR()
{

}

void QR::setAmount(double amount)
{
    m_amount = amount;
    update();
}
void QR::setAddr(const QString& addr)
{
    m_addr = addr;
    update();
}

void QR::setDimensions(uint width, uint height)
{
    m_width = width;
    m_height = height;
    update();
}

QString QR::getEncoded() const
{
    return m_qrData;
}

void QR::update()
{
    QUrlQuery query;
    if (m_amount > 0)
    {
        query.addQueryItem(
                "amount", QLocale("C").toString(m_amount, 'f', -128));
    }
    
    QUrl url;
    url.setScheme("beam");
    url.setPath(m_addr);
    url.setQuery(query);

    CQR_Encode qrEncode;
    QString strAddr = url.toString(QUrl::FullyEncoded);
    bool success = qrEncode.EncodeData(1, 0, true, -1, strAddr.toUtf8().data());

    if (success)
    {
        int qrImageSize = qrEncode.m_nSymbleSize;
        int encodeImageSize = qrImageSize + (QR_MARGIN * 2);
        QImage encodeImage(
                encodeImageSize, encodeImageSize, QImage::Format_ARGB32);
        encodeImage.fill(Qt::white);
        QColor color(Qt::transparent);

        for (int i = 0; i < qrImageSize; i++)
            for (int j = 0; j < qrImageSize; j++)
                if (qrEncode.m_byModuleData[i][j])
                    encodeImage.setPixel(
                            i + QR_MARGIN, j + QR_MARGIN, color.rgba());

        encodeImage = encodeImage.scaled(m_width, m_height);

        QByteArray bArray;
        QBuffer buffer(&bArray);
        buffer.open(QIODevice::WriteOnly);
        encodeImage.save(&buffer, "png");

        m_qrData = "data:image/png;base64,";
        m_qrData.append(QString::fromLatin1(bArray.toBase64().data()));
    }

    emit qrDataChanged();
}
