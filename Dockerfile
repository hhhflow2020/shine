# syntax=docker/dockerfile:1.6

# ---------- build stage ----------
FROM ubuntu:24.04 AS build
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        pkg-config \
        python3 \
        python3-pip \
        python3-venv \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install Conan in an isolated venv so we don't fight the system python.
RUN python3 -m venv /opt/conan-venv \
 && /opt/conan-venv/bin/pip install --no-cache-dir "conan>=2.2,<3"
ENV PATH="/opt/conan-venv/bin:${PATH}"

WORKDIR /src

# Detect profile + cache dep graph separately from source changes.
RUN conan profile detect --force \
 && sed -i 's/^compiler.cppstd=.*/compiler.cppstd=20/' /root/.conan2/profiles/default

# Copy only recipe first for dependency layer caching.
COPY conanfile.py CMakeLists.txt ./
COPY cmake ./cmake
RUN conan install . --build=missing -s build_type=Release \
    -c tools.system.package_manager:mode=install \
    -c tools.system.package_manager:sudo=False

COPY src ./src
COPY include ./include
COPY tests ./tests
COPY configs ./configs

RUN cmake --preset conan-release \
 && cmake --build --preset conan-release -j "$(nproc)" \
 && ctest --preset conan-release --output-on-failure

# ---------- runtime stage ----------
FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        tini \
    && rm -rf /var/lib/apt/lists/*

RUN useradd --system --create-home --shell /usr/sbin/nologin shine
WORKDIR /app

COPY --from=build /src/build/Release/shine /usr/local/bin/shine
# COPY --from=build /src/configs/example.yaml /app/example.yaml

USER shine

# SOCKS5(1080), HTTP-CONNECT(1081), shine(7011), metrics(9100)
# EXPOSE 1080 1081 7011 9100

ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/shine"]
CMD ["-c", "/app/shine.yaml"]
