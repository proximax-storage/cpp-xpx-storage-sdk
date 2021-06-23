FROM debian:sid

RUN apt-get update && apt-get install --no-install-recommends -y \
    software-properties-common \
    curl \
    git \
    build-essential \
    cmake \
    libboost-atomic-dev \
    libboost-date-time-dev \
    libboost-regex-dev \
    libboost-system-dev \
    libboost-timer-dev \
    libboost-chrono-dev \
    libboost-log-dev \
    libboost-thread-dev \
    libboost-filesystem-dev \
    libboost-program-options-dev \
    libboost-stacktrace-dev \
    libboost-random-dev \
    libssl-dev \
    libssl1.1 \
    openssl\
    # gnutls-bin \
    # guile-gnutls \
    libgnutls-dane0 \
    libgnutls-openssl27 \
    libgnutls28-dev \
    libgnutls30 \
    libgnutlsxx28 \
    libgcrypt20 \
    libgcrypt20-dev
  
WORKDIR /app/src
COPY . .
RUN git submodule init && git submodule update --recursive --remote

## Build Libtorrent
RUN cd libtorrent \
    && rm -rf _build \
    && mkdir _build \
    && cd _build \
    && cmake -DCMAKE_BUILD_TYPE=Release  \
    -DCMAKE_CXX_STANDARD=17 \
    .. \
    && make
    
## Build rpclib
RUN cd rpclib \
    && rm -rf build \
    && mkdir build \
    && cd build \
    && cmake .. \
    && cmake --build .

# Build cpp-xpx-storage-sdk
RUN rm -rf _build \
    && _build \
    && cd _build \
    && cmake .. \
    && make
