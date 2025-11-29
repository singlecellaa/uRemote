本项目用到的库
    ImGui 图形库
        github: https://github.com/ocornut/imgui/
        glossary: [Glossary · ocornut/imgui Wiki](https://github.com/ocornut/imgui/wiki/Glossary)
        api: [API ImGui - ImGui v1.77](https://pixtur.github.io/mkdocs-for-imgui/site/api-imgui/ImGui--Dear-ImGui-end-user/)
    boost.asio 网路库
        教程：[（C++后台开发）C++网络编程：网络库 Boost.Asio入门、应用案例_哔哩哔哩_bilibili](https://www.bilibili.com/video/BV12X4y187gd/?vd_source=e490166531fece48433df8df0a0e17cd)
    openssl
    ffmpeg
vcpkg and cmake in windows
    准备cmake (v3.11左右) 和 visual studio 2022
    cd D:/SDK
    git clone https://github.com/microsoft/vcpkg.git
    cd vcpkg
    bootstrap-vcpkg.bat
    添加环境变量VCPKG_ROOT值为D:/SDK/vcpkg
    cd path/to/your/workdir
    git clone git@github.com:singlecellaa/uRemote.git
    cd uRemote
    vcpkg install --x-install-root=$VCPKG
    cmake -B build
    cd build
    双击 .sln 文件，visual studio中打开项目
    在左侧solution explorer，在ALL_BUILD,uRemote,ZERO_CHECK中，右键uRemote, 点击 "Set as Startup Project"
vcpkg and cmake in linux
    sudo apt-get install curl zip unzip tar              #for bootstrap-vcpkg.sh
    sudo apt install ninja-build nasm                    #for vcpkg install --x-install-root=$VCPKG
    sudo apt install libxinerama-dev libxcursor-dev xorg-dev libglu1-mesa-dev pkg-config    #for vcpkg install --x-install-root=$VCPKG
    cd ~
    git clone https://github.com/microsoft/vcpkg.git
    cd vcpkg
    ./bootstrap-vcpkg.sh
    export VCPKG_ROOT=~/vcpkg
    export PATH=$VCPKG_ROOT:$PATH
    cd ~
    wget https://github.com/Kitware/CMake/releases/download/v3.31.10/cmake-3.31.10-linux-x86_64.tar.gz
    tar -xzvf cmake-3.31.10-linux-x86_64.tar.gz
    export CMAKE_ROOT=~/cmake-3.31.10-linux-x86_64/bin
    export PATH=$CMAKE_ROOT:$PATH
    cd ~/Desktop
    git clone git@github.com:singlecellaa/uRemote.git
    cd uRemote
    vcpkg x-update-baseline --add-initial-baseline
    vcpkg install --x-install-root=$VCPKG
    cmake -B build                #generate Makefile
    cd build
    make
