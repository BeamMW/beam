set(QR_SRC
    QRCodeGenerator.cpp
    QRCodeGenerator.h
)

add_library(qrcode STATIC ${QR_SRC})

if(MSVC)
    #target_compile_options(qrcode PRIVATE "/wd4267")
    #target_compile_options(qrcode PRIVATE "/wd4456")
endif()