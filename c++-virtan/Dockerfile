FROM alpine:3.9

LABEL name="abrarov/cpp-virtan"

ADD ["CMakeLists.txt", "/tmp/cpp-virtan/"]
ADD ["server.cc", "/tmp/cpp-virtan/"]

RUN apk add --no-cache libstdc++ &&\
    apk add --no-cache --virtual build-dependencies g++ make cmake boost-dev boost-static libstdc++ &&\
    cd /tmp/cpp-virtan &&\
    mkdir build && cd build &&\
    cmake -D CMAKE_SKIP_BUILD_RPATH=ON -D CMAKE_BUILD_TYPE=Release .. &&\
    cmake --build . &&\
    mkdir -p /opt/cpp-virtan &&\
    mv -f server /opt/cpp-virtan/server &&\
    cd /tmp &&\
    rm -rf cpp-virtan &&\
    apk del build-dependencies

ENTRYPOINT ["/opt/cpp-virtan/server"]
