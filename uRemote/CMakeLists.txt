# CMakeList.txt : CMake project for uRemote, include source and define
# project specific logic here.
#

# Add source to this project's executable.
add_executable (uRemote "uRemote.cpp" "uRemote.h" "network.h" "network.cpp" "BaseConnection.h" "BaseConnection.cpp" "Server.h" "Server.cpp" "Client.h" "Client.cpp")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET uRemote PROPERTY CXX_STANDARD 20)
endif()

# Find and link dependencies provided by vcpkg (or the system).
find_package(glew REQUIRED)

# GLFW: vcpkg supplies a CMake config target `glfw3::glfw3`.
find_package(glfw3 CONFIG REQUIRED)

# ImGui: vcpkg supplies a CMake config target `imgui::imgui`.
find_package(imgui CONFIG REQUIRED)

# OpenSSL (vcpkg provides OpenSSL with standard CMake targets).
find_package(OpenSSL REQUIRED)

# Boost: use Boost.System which is commonly required by Asio.
find_package(Boost REQUIRED COMPONENTS system)

# Attempt to find FFmpeg libraries (avformat, avcodec, avutil).
# vcpkg installs them as standard libraries; find them by name.
find_library(AVFORMAT_LIB NAMES avformat)
find_library(AVCODEC_LIB NAMES avcodec)
find_library(AVUTIL_LIB NAMES avutil)

# Link libraries to the uRemote target.
target_link_libraries(uRemote
  PRIVATE
    GLEW::GLEW
    glfw
    imgui::imgui
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::system
    ${AVFORMAT_LIB}
    ${AVCODEC_LIB}
    ${AVUTIL_LIB}
)

# TODO: Add tests and install targets if needed.
