# Building

## Requirements

1. C++ toolchain (compiler, linker, standard C++ library)
1. Make
1. [CMake](https://cmake.org/)
1. [Boost](https://www.boost.org/)

## Steps to build

Assuming current directory is directory where this file is located

```bash
cmake -D CMAKE_SKIP_BUILD_RPATH=ON -D CMAKE_BUILD_TYPE=Release .
cmake --build .
```

# Docker image

## Building

Assuming current directory is directory where this file is located

```bash
docker build -t cpp-virtan .
```

## Running with Docker

```bash
docker run --rm cpp-virtan $params
```

where `$params` are parameters passed to application