FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive

RUN dpkg --add-architecture i386 && \
    apt-get update && apt-get install -y \
    mingw-w64 \
    wine \
    wine64 \
    wine32 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .
RUN mkdir -p build

RUN x86_64-w64-mingw32-g++ -std=c++14 -O2 -Isrc \
    -o build/ftp_server.exe \
    src/common/platform.cpp \
    src/server/thread_pool.cpp \
    src/server/auth.cpp \
    src/server/session.cpp \
    src/server/admin_server.cpp \
    src/server/ftp_server.cpp \
    src/server/main.cpp \
    -lws2_32 -lmswsock

ENV WINEDEBUG=-all
RUN mkdir -p ftproot

EXPOSE 21
EXPOSE 8080
EXPOSE 50000-50100

CMD ["wine", "build/ftp_server.exe"]
