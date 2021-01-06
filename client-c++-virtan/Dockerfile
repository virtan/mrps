FROM ubuntu:20.04 AS build

RUN apt-get update && \
    apt-get install -y --no-install-suggests --no-install-recommends \
      ca-certificates \
      curl \
      git \
      g++ \
      make \
      libstdc++-9-dev && \
    rm -rf /var/lib/apt/lists/*

ENV CMAKE_HOME="/opt/cmake"

ARG CMAKE_VERSION="3.18.4"

RUN mkdir -p "${CMAKE_HOME}" && \
    cmake_url="https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz" && \
    echo "Downloading CMake ${CMAKE_VERSION} from ${cmake_url} to ${CMAKE_HOME}" && \
    curl -jksSL "${cmake_url}" | tar -xzf - -C "${CMAKE_HOME}" --strip-components 1

ENV PATH="${CMAKE_HOME}/bin:${PATH}" \
    BOOST_HOME="/usr/lib/boost"

ARG BOOST_VERSION="1.74.0"
ARG BOOST_URL="https://dl.bintray.com/mabrarov/generic/boost"

RUN mkdir -p "${BOOST_HOME}" && \
    boost_archive_url="${BOOST_URL}/${BOOST_VERSION}/boost-${BOOST_VERSION}-x64-gcc$(gcc -dumpversion | sed -r 's/^([[:digit:]]+)(\..*)?$/\1/;t;d').tar.gz" && \
    echo "Downloading Boost from ${boost_archive_url} to ${BOOST_HOME}" && \
    curl --connect-timeout 300 --max-time 1800 --retry 10 --retry-delay 10 \
      -jksSL "${boost_archive_url}" \
      | tar -xz -C "${BOOST_HOME}" --strip-components 1

ADD ["CMakeLists.txt", "client.cc", "/usr/src/client-cpp-virtan/"]

RUN source_dir="/usr/src/client-cpp-virtan" && \
    build_dir="${source_dir}/build" && \
    mkdir -p "${build_dir}" && \
    cmake \
      -D CMAKE_SKIP_BUILD_RPATH=ON \
      -D CMAKE_BUILD_TYPE=Release \
      -D Boost_USE_MULTITHREADED=ON \
      -D Boost_USE_STATIC_LIBS=ON \
      -D Boost_NO_SYSTEM_PATHS=ON \
      -D BOOST_INCLUDEDIR="${BOOST_HOME}/include" \
      -D BOOST_LIBRARYDIR="${BOOST_HOME}/lib" \
      -S "${source_dir}" \
      -B "${build_dir}" && \
    cmake --build "${build_dir}"

FROM gcr.io/distroless/cc-debian10

COPY --from=build ["/usr/src/client-cpp-virtan/build/client", "/opt/client-cpp-virtan/client"]

ENTRYPOINT ["/opt/client-cpp-virtan/client"]

CMD ["--help"]

LABEL name="client-cpp-virtan"
