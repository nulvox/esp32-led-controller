FROM python:3.12-slim

RUN apt-get update \
    && apt-get install --no-install-recommends -y git ca-certificates \
    && rm -rf /var/lib/apt/lists/*

ENV PIO_HOME_DIR=/opt/platformio \
    PLATFORMIO_CORE_DIR=/opt/platformio

RUN pip install --no-cache-dir platformio==6.1.18 \
    && useradd --create-home --uid 1000 builder \
    && mkdir -p /workspace /opt/platformio \
    && chown -R builder:builder /workspace /opt/platformio

WORKDIR /workspace
USER builder
ENTRYPOINT ["pio"]
