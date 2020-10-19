# CMakeList.txt : CMake project for ImageResizer, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

add_subdirectory(dependencies)

include(ExternalProject)

# Add source to this project's executable.
set(CMAKE_CXX_STANDARD 17)
file(GLOB_RECURSE THREADPOOLSOURCES RELATIVE ${CMAKE_SOURCE_DIR}/ImageResizer "dependencies/ThreadPool/src/*")
file(GLOB_RECURSE LIBSOURCES RELATIVE ${CMAKE_SOURCE_DIR}/ImageResizer "dependencies/lib/src/*")
add_executable (ImageResizer "ImageResizer.cpp" "ImageResizer.h" ${THREADPOOLSOURCES} ${LIBSOURCES})

set(CMAKE_INCLUDE_CURRENT_DIR ON)
include_directories (${CMAKE_BINARY_DIR})

if(MSVC)
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
endif()

target_link_libraries(ImageResizer opencv_core opencv_imgcodecs)

target_include_directories(ImageResizer PUBLIC 
    dependencies/ThreadPool/header)
target_include_directories(ImageResizer PUBLIC 
    dependencies/lib/header)

target_include_directories(ImageResizer PUBLIC 
    dependencies/opencv/modules/calib3d/include
    dependencies/opencv/modules/core/include
    dependencies/opencv/modules/cudaarithm/include
    dependencies/opencv/modules/cudabgsegm/include
    dependencies/opencv/modules/cudacodec/include
    dependencies/opencv/modules/cudafeatures2d/include
    dependencies/opencv/modules/cudafilters/include
    dependencies/opencv/modules/cudaimgproc/include
    dependencies/opencv/modules/cudalegacy/include
    dependencies/opencv/modules/cudaobjdetect/include
    dependencies/opencv/modules/cudaoptflow/include
    dependencies/opencv/modules/cudastereo/include
    dependencies/opencv/modules/cudawarping/include
    dependencies/opencv/modules/cudev/include
    dependencies/opencv/modules/dnn/include
    dependencies/opencv/modules/features2d/include
    dependencies/opencv/modules/flann/include
    dependencies/opencv/modules/highgui/include
    dependencies/opencv/modules/imgcodecs/include
    dependencies/opencv/modules/imgproc/include
    dependencies/opencv/modules/ml/include
    dependencies/opencv/modules/objdetect/include
    dependencies/opencv/modules/photo/include
    dependencies/opencv/modules/shape/include
    dependencies/opencv/modules/stitching/include
    dependencies/opencv/modules/superres/include
    dependencies/opencv/modules/ts/include
    dependencies/opencv/modules/video/include
    dependencies/opencv/modules/videoio/include
    dependencies/opencv/modules/videostab/include
    dependencies/opencv/modules/viz/include
    dependencies/opencv/modules/world/include
)
