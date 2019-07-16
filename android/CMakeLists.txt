cmake_minimum_required(VERSION 3.1)

set(TARGET_NAME wallet-jni)

set(SRC
    jni.cpp 
    wallet_model.h 
    wallet_model.cpp 
    common.h 
    common.cpp 
    node_model.h 
    node_model.cpp
)

add_library(${TARGET_NAME} SHARED ${SRC})
add_dependencies(${TARGET_NAME} wallet utility node)
target_link_libraries(${TARGET_NAME} wallet utility node)

configure_file("${PROJECT_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h")
target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

if(ANDROID)
	target_link_libraries(${TARGET_NAME} android log)
else()
	add_subdirectory(unittests)

	find_package(Java REQUIRED)
	find_package(JNI REQUIRED)
	include(UseJava)
	include_directories(${JNI_INCLUDE_DIRS})

	set(CMAKE_JAVA_JAR_ENTRY_POINT WalletJNI)

	add_jar(WalletJNI 
		SOURCES 
			WalletJNI.java 
			com/mw/beam/beamwallet/core/Api.java
			com/mw/beam/beamwallet/core/entities/Wallet.java
			com/mw/beam/beamwallet/core/entities/dto/UtxoDTO.java
			com/mw/beam/beamwallet/core/entities/dto/TxDescriptionDTO.java
			com/mw/beam/beamwallet/core/entities/dto/SystemStateDTO.java
			com/mw/beam/beamwallet/core/entities/dto/WalletStatusDTO.java
            com/mw/beam/beamwallet/core/entities/dto/WalletAddressDTO.java
            com/mw/beam/beamwallet/core/entities/dto/PaymentInfoDTO.java
			com/mw/beam/beamwallet/core/listeners/WalletListener.java
		ENTRY_POINT WalletJNI
	)

	add_dependencies(WalletJNI wallet-jni)
endif()
