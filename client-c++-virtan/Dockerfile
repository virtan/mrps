FROM alpine:3.9

LABEL name="client-cpp-virtan"

ADD ["CMakeLists.txt", "/tmp/client-cpp-virtan/"]
ADD ["client.cc", "/tmp/client-cpp-virtan/"]

RUN apk add --no-cache libstdc++ &&\
    apk add --no-cache --virtual build-dependencies g++ make cmake boost-dev boost-static libstdc++ &&\
    cd /tmp/client-cpp-virtan &&\
    mkdir build && cd build &&\
    cmake -D CMAKE_SKIP_BUILD_RPATH=ON -D CMAKE_BUILD_TYPE=Release .. &&\
    cmake --build . &&\
    mkdir -p /opt/client-cpp-virtan &&\
    mv -f client /opt/client-cpp-virtan/client &&\
    cd /tmp &&\
    rm -rf client-cpp-virtan &&\
    apk del build-dependencies

ENTRYPOINT ["/opt/client-cpp-virtan/client"]
