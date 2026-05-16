# ============================================================================
# Dockerfile — Production Linux Deployment
# Multi-stage build for minimal runtime size.
# ============================================================================

# ─── Stage 1: Build Environment ───────────────────────────────────────────────
FROM ubuntu:22.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy project source
COPY CMakeLists.txt .
COPY src/ ./src/
COPY config/ ./config/

# Build using CMake
RUN mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc) ftp_server hash_password

# ─── Stage 2: Minimal Runtime ─────────────────────────────────────────────────
FROM ubuntu:22.04

# Create non-root user for security
RUN useradd -m -s /bin/bash ftpuser

WORKDIR /app

# Copy binaries from builder
COPY --from=builder /app/build/ftp_server /app/ftp_server
COPY --from=builder /app/build/hash_password /app/hash_password

# Copy default configurations
COPY --from=builder /app/config/ /app/config/

# Create the FTP root directory
RUN mkdir -p /app/ftproot && \
    chown -R ftpuser:ftpuser /app

# Switch to non-root user
USER ftpuser

# Expose the standard FTP port and the Admin port
EXPOSE 21
EXPOSE 8080
# Expose passive mode port range (if configured)
EXPOSE 50000-50100

# Default command to run the server
CMD ["./ftp_server", "--config", "config/server.conf"]
