# Esport FI32 Development Environment
# Based on Ubuntu with ESP32 development tools

# =============================================================================
# VERSION DEFINITIONS
# =============================================================================

# Tool versions are defined in docker-compose.yml and passed as build arguments
# This ensures a single source of truth for version management
ARG UBUNTU_VERSION
ARG CMAKE_VERSION
ARG DOXYGEN_VERSION
ARG PYTHON_VERSION
ARG ESPRESSIF_IDF_VERSION

FROM ubuntu:${UBUNTU_VERSION}

# Re-declare ARGs after FROM to make them available in build context
ARG CMAKE_VERSION
ARG DOXYGEN_VERSION
ARG PYTHON_VERSION
ARG ESPRESSIF_IDF_VERSION

# =============================================================================
# ENVIRONMENT SETUP
# =============================================================================

# Set environment variables
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC
ENV LANG=en_US.UTF-8
ENV LC_ALL=en_US.UTF-8
ENV LANGUAGE=en_US:en

# Set working directory
WORKDIR /workspace
RUN mkdir -p /workspace/esport-fi32

# =============================================================================
# SYSTEM PACKAGES INSTALLATION
# =============================================================================

# Update package list and install system dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Basic utilities
    curl \
    wget \
    unzip \
    tar \
    xz-utils \
    ca-certificates \
    gnupg \
    lsb-release \
    less \
    # Locale support (required by Python/kconfig tools)
    locales \
    # Build tools
    build-essential \
    make \
    ninja-build \
    # Version control
    git=1:2.43.0-1ubuntu7.3 \
    # Python and pip
    python3=3.12.3-0ubuntu2.1 \
    python3-pip \
    python3-venv \
    # Documentation and visualization
    graphviz=2.42.2-9ubuntu0.1 \
    # Sudo (needed for dev user setup)
    sudo \
    # Code coverage
    lcov=2.0-4ubuntu2 \
    # Ruby
    ruby=1:3.2~ubuntu1 \
    ruby-dev \
    # Clang tools (version 18) - libllvm18 will be installed automatically as dependency
    clangd-18=1:18.1.3-1ubuntu1 \
    clang-format-18=1:18.1.3-1ubuntu1 \
    # ESP-IDF dependencies
    flex \
    bison \
    gperf \
    ccache \
    libusb-1.0-0 \
    python3-cryptography \
    python3-setuptools \
    python3-wheel \
    python3-click \
    python3-pyparsing \
    python3-pyelftools \
    && rm -rf /var/lib/apt/lists/*

# Create symlinks for clang tools
RUN locale-gen en_US.UTF-8 && update-locale LANG=en_US.UTF-8

RUN ln -sf /usr/bin/clangd-18 /usr/bin/clangd && \
    ln -sf /usr/bin/clang-format-18 /usr/bin/clang-format && \
    ln -sf /usr/bin/python3 /usr/bin/python

# =============================================================================
# CMAKE INSTALLATION
# =============================================================================

# Download and install CMake
# Note: Ensure /usr/local/man is a directory (not a file) before installing
RUN rm -f /usr/local/man && mkdir -p /usr/local/man && \
    cd /tmp && \
    wget -q "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz" && \
    tar -xf cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz && \
    cp -r cmake-${CMAKE_VERSION}-linux-x86_64/* /usr/local/ && \
    rm -rf /tmp/cmake-*

# =============================================================================
# DOXYGEN INSTALLATION
# =============================================================================

# Download and install Doxygen
RUN cd /tmp && \
    wget -q "https://www.doxygen.nl/files/doxygen-${DOXYGEN_VERSION}.linux.bin.tar.gz" && \
    tar -xf doxygen-${DOXYGEN_VERSION}.linux.bin.tar.gz && \
    cp doxygen-${DOXYGEN_VERSION}/bin/doxygen /usr/local/bin/ && \
    rm -rf /tmp/doxygen-*

# =============================================================================
# PYTHON PACKAGES
# =============================================================================

# Install Python packages for running scripts (development tools removed to reduce image size)
# Note: Using --break-system-packages is safe in Docker containers (PEP 668)
# Note: Using --ignore-installed to handle system-installed packages like pyyaml
RUN pip3 install --no-cache-dir --break-system-packages --ignore-installed \
    # Serial communication
    pyserial==3.5

# =============================================================================
# DEVELOPMENT USER SETUP
# =============================================================================

# Create a non-root user for development
ARG USERNAME=developer
ARG USER_UID=1000
ARG USER_GID=${USER_UID}

# Create the user and group (handle existing GID/UID gracefully)
RUN set -e && \
    # Check if group exists, if not create it
    if ! getent group ${USER_GID} >/dev/null; then \
    groupadd --gid ${USER_GID} ${USERNAME}; \
    fi && \
    # Check if user exists, if not create it
    if ! getent passwd ${USER_UID} >/dev/null; then \
    useradd --uid ${USER_UID} --gid ${USER_GID} -m ${USERNAME}; \
    else \
    # User exists with this UID, just ensure they're in the right group
    usermod -g ${USER_GID} -l ${USERNAME} $(getent passwd ${USER_UID} | cut -d: -f1) || true; \
    fi && \
    # Add sudo support for the user
    echo ${USERNAME} ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/${USERNAME} && \
    chmod 0440 /etc/sudoers.d/${USERNAME}

# Set permissions for development directories
RUN chown -R ${USERNAME}:${USER_GID} /workspace

# =============================================================================
# ESP-IDF INSTALLATION (latest stable)
# =============================================================================

ENV IDF_PATH=/opt/esp-idf
ENV IDF_TOOLS_PATH=/opt/esp-idf-tools
ENV PATH="${IDF_PATH}/tools:${PATH}"

# Download and install ESP-IDF, then clean up in the same layer to keep image lean:
#   - dist/       downloaded toolchain tarballs (no longer needed after extraction)
#   - docs/       Sphinx/HTML documentation (not needed in dev container)
# NOTE: --recurse-submodules is required — ESP-IDF components (e.g. esp-mqtt) use
# git submodules that must be populated before the build system runs.
# --shallow-submodules limits each submodule to depth 1 to save space.
# .git is intentionally kept: ESP-IDF's CMake scripts query it for version info
# and submodule state at configure time.
# chown is done here too — running it in a separate RUN would duplicate all inode
# metadata for tens of thousands of files via Docker's overlay filesystem.
RUN git clone --depth 1 --recurse-submodules --shallow-submodules \
        --branch ${ESPRESSIF_IDF_VERSION} \
        https://github.com/espressif/esp-idf.git ${IDF_PATH} && \
    cd ${IDF_PATH} && ./install.sh esp32c6 && \
    rm -rf ${IDF_TOOLS_PATH}/dist && \
    rm -rf ${IDF_PATH}/docs && \
    chown -R ${USERNAME}:${USER_GID} ${IDF_PATH} ${IDF_TOOLS_PATH}

# Make ESP-IDF available for all users
RUN echo 'export IDF_PATH=/opt/esp-idf' > /etc/profile.d/esp-idf.sh && \
    echo 'source /opt/esp-idf/export.sh' >> /etc/profile.d/esp-idf.sh && \
    chmod +x /etc/profile.d/esp-idf.sh

# =============================================================================
# FINAL CONFIGURATION
# =============================================================================

# Export DOCKER_HOST_IP_ADDRESS for use inside the container
RUN echo "export DOCKER_HOST_IP_ADDRESS=\`/usr/bin/getent ahostsv4 host.docker.internal | tail -n 1 | awk '{ print \$1 }'\`" > /etc/profile.d/docker-host-ip.sh

# Switch to development user
USER ${USERNAME}

# Install git bash completion and prompt scripts
RUN mkdir -p ~/.git-completion && \
    curl -o ~/.git-completion/git-completion.bash \
        https://raw.githubusercontent.com/git/git/master/contrib/completion/git-completion.bash && \
    curl -o ~/.git-completion/git-prompt.sh \
        https://raw.githubusercontent.com/git/git/master/contrib/completion/git-prompt.sh && \
    echo '# Git bash completion and prompt' >> ~/.bashrc && \
    echo 'source ~/.git-completion/git-completion.bash' >> ~/.bashrc && \
    echo 'source ~/.git-completion/git-prompt.sh' >> ~/.bashrc && \
    echo 'export GIT_PS1_SHOWDIRTYSTATE=1' >> ~/.bashrc && \
    echo 'export GIT_PS1_SHOWSTASHSTATE=1' >> ~/.bashrc && \
    echo 'export GIT_PS1_SHOWUNTRACKEDFILES=1' >> ~/.bashrc && \
    echo 'export GIT_PS1_SHOWUPSTREAM="auto"' >> ~/.bashrc && \
    echo 'export PS1='"'"'\[\033[01;32m\]\u@\h\[\033[00m\]:\[\033[01;34m\]\w\[\033[01;31m\]$(__git_ps1 " [%s]")\[\033[00m\]\$ '"'"'' >> ~/.bashrc

# Verify installations
RUN echo "=== Tool Versions ===" && \
    echo "Ubuntu: $(lsb_release -d | cut -f2)" && \
    echo "CMake: $(cmake --version | head -1)" && \
    echo "Doxygen: $(doxygen --version)" && \
    echo "Git: $(git --version)" && \
    echo "Python: $(python3 --version)" && \
    echo "Ruby: $(ruby --version)" && \
    echo "Clang-Format: $(clang-format --version | head -1)" && \
    echo "Graphviz: $(dot -V 2>&1)" && \
    echo "lcov: $(lcov --version | head -1)"

# Configure git to use VSCode as default editor and diff/merge tool
RUN git config --global color.ui true && \
    git config --global diff.tool vscode && \
    git config --global difftool.prompt false && \
    git config --global difftool.vscode.cmd 'code --wait --diff "$LOCAL" "$REMOTE"' && \
    git config --global merge.tool vscode && \
    git config --global mergetool.prompt false && \
    git config --global mergetool.keepBackup false && \
    git config --global mergetool.vscode.cmd 'code --wait --merge $REMOTE $LOCAL $BASE $MERGED' && \
    git config --global core.editor 'code --wait' && \
    git config --global rerere.enabled true && \
    git config --global pull.ff true

# Set default command
CMD ["/bin/bash"]