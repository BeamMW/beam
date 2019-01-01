if(BEAM_USE_GPU)

    set(SRC 
        gpu_tools.cpp
        gpu_tools.h)

    add_library(gpu_tools STATIC ${SRC})
    target_include_directories(gpu_tools PRIVATE ${PROJECT_SOURCE_DIR}/3rdparty/opencl-miner/clHeaders)

endif()
