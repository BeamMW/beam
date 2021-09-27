cmake_minimum_required(VERSION 3.11)

set(TARGET_NAME wallet-jni)

set(SRC
    jni.cpp 
    wallet_model.h 
    wallet_model.cpp 
    common.h 
    common.cpp 
    node_model.h 
    node_model.cpp
    dao/apps_api_ui.h
    dao/apps_api_ui.cpp
    dao/web_api_creator.h
    dao/web_api_creator.cpp
)

add_library(${TARGET_NAME} SHARED ${SRC})
target_link_libraries(${TARGET_NAME}
    PRIVATE
        wallet_client
        wallet_api
        utility 
        node 
        mnemonic
)

if(ANDROID)
	target_link_libraries(${TARGET_NAME} PRIVATE android log)
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
