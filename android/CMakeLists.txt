set(TARGET_NAME wallet-jni)

add_library(${TARGET_NAME} SHARED jni.cpp)
add_dependencies(${TARGET_NAME} wallet utility)
target_link_libraries(${TARGET_NAME} wallet utility)

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
			com/mw/beam/beamwallet/core/entities/Utxo.java
			com/mw/beam/beamwallet/core/entities/TxDescription.java
			com/mw/beam/beamwallet/core/entities/SystemState.java
			com/mw/beam/beamwallet/core/entities/WalletStatus.java
            com/mw/beam/beamwallet/core/entities/WalletAddress.java
			com/mw/beam/beamwallet/core/listeners/WalletListener.java
		ENTRY_POINT WalletJNI
	)

	add_dependencies(WalletJNI wallet-jni)
endif()
