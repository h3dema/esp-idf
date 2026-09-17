# Dockerfile
FROM espressif/idf:latest

# Install additional packages if needed
RUN apt-get update && apt-get install -y \
    git \
    vim \
    nano \
    curl \
    wget \
    minicom \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /project

# Optional: Add custom scripts or configurations
# COPY ./scripts/ /opt/scripts/
# RUN chmod +x /opt/scripts/*.sh

# Default command (can be overridden)
CMD ["idf.py", "build"]