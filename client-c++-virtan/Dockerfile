FROM ubuntu:20.04 AS build

ARG CMAKE_VERSION="3.18.3"
ARG BOOST_VERSION="1.74.0"
ARG BOOST_URL="https://dl.bintray.com/mabrarov/generic/boost"

ENV TZ="Europe/Moscow" \
    PATH="/opt/cmake/bin:${PATH}" \
    DEPENDENCIES_DIR="/dependency"

RUN ln -snf "/usr/share/zoneinfo/${TZ}" /etc/localtime && \
    echo "${TZ}" > /etc/timezone && \
    apt-get update && \
    apt-get install -y \
      software-properties-common \
      curl \
      git \
      g++ \
      make \
      libstdc++-9-dev

RUN mkdir -p /opt && \
    cmake_url="https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz" && \
    echo "Downloading CMake ${CMAKE_VERSION} from ${cmake_url}" && \
    curl -jksSL "${cmake_url}" | tar -xzf - -C /opt && \
    mv -f "/opt/cmake-${CMAKE_VERSION}-Linux-x86_64" /opt/cmake

RUN mkdir -p "${DEPENDENCIES_DIR}" && \
    boost_archive_url="${BOOST_URL}/${BOOST_VERSION}/boost-${BOOST_VERSION}-x64-gcc$(gcc -dumpversion | sed -r 's/^([[:digit:]]+)(\..*)?$/\1/;t;d').tar.gz" && \
    echo "Downloading Boost from ${boost_archive_url}" && \
    curl --connect-timeout 300 \
      --max-time 1800 \
      --retry 10 \
      --retry-delay 10 \
      -jksSL \
      "${boost_archive_url}" | tar -xz -C "${DEPENDENCIES_DIR}"

ADD ["CMakeLists.txt", "/client-cpp-virtan/"]
ADD ["client.cc", "/client-cpp-virtan/"]

RUN mkdir -p "/client-cpp-virtan/build" && \
    cd "/client-cpp-virtan/build" && \
    boost_dir="${DEPENDENCIES_DIR}/boost-${BOOST_VERSION}-x64-gcc$(gcc -dumpversion | sed -r 's/^([[:digit:]]+)(\..*)?$/\1/;t;d')" && \
    cmake \
      -D CMAKE_SKIP_BUILD_RPATH=ON \
      -D CMAKE_BUILD_TYPE=Release \
      -D Boost_USE_MULTITHREADED=ON \
      -D Boost_USE_STATIC_LIBS=ON \
      -D Boost_NO_SYSTEM_PATHS=ON \
      -D BOOST_INCLUDEDIR="${boost_dir}/include" \
      -D BOOST_LIBRARYDIR="${boost_dir}/lib" \
      .. && \
    cmake --build .

FROM gcr.io/distroless/cc-debian10

COPY --from=build /client-cpp-virtan/build/client /opt/client-cpp-virtan/client

ENTRYPOINT ["/opt/client-cpp-virtan/client"]

CMD ["--help"]

LABEL name="client-cpp-virtan"
