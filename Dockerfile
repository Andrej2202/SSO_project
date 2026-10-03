FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Europe/Moscow

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libopencv-dev \
    libyaml-cpp-dev \
    libfreetype-dev \
    fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 https://github.com/yhirose/cpp-httplib.git /opt/cpp-httplib

WORKDIR /app

COPY CMakeLists.txt .
COPY src/ src/
COPY config/ config/
COPY templates/ templates/
COPY static/ static/

RUN mkdir -p build && cd build && \
    cmake .. && \
    make

EXPOSE 8080

CMD ["./build/diploma_generator"]