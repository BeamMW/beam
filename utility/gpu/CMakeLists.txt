if(BEAM_USE_GPU)

    #find_package(CUDA)

    #include_directories(${CUDA_INCLUDE_DIRS})

    #set(CUDA_NVCC_FLAGS ${CUDA_NVCC_FLAGS}; --disable-warnings; --ptxas-options=-v; -use_fast_math; -lineinfo)

    #set(CUDA_NVCC_FLAGS ${CUDA_NVCC_FLAGS};; -gencode arch=compute_30,code=sm_30; -gencode arch=compute_35,code=sm_35; -gencode arch=compute_37,code=sm_37; -gencode arch=compute_50,code=sm_50; -gencode arch=compute_52,code=sm_52; -gencode arch=compute_60,code=sm_60; -gencode arch=compute_61,code=sm_61; -gencode arch=compute_70,code=sm_70; -gencode arch=compute_72,code=sm_72;)

    #set(SRC 
    #    gpu_tools.cu
    #    gpu_tools.h)

    #CUDA_ADD_LIBRARY(gpu_tools STATIC ${SRC})

endif()
