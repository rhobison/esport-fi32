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
ARG TARGETARCH
ARG CMAKE_VERSION
ARG DOXYGEN_VERSION
ARG PYTHON_VERSION
ARG ESPRESSIF_IDF_VERSION

# =============================================================================
# ENVIRONMENT SETUP
# =============================================================================

# Combine all environment variables into a single layer
ENV DEBIAN_FRONTEND=noninteractive \
    TZ=UTC \
    LANG=en_US.UTF-8 \
    LC_ALL=en_US.UTF-8 \
    LANGUAGE=en_US:en

# WORKDIR creates parent directories automatically; no separate RUN mkdir needed
WORKDIR /workspace/esport-fi32

# =============================================================================
# SYSTEM PACKAGES, LOCALE & SYMLINKS
# =============================================================================

# Add git-core PPA to get the latest Git release (Ubuntu base ships an older version)
RUN apt-get update && apt-get install -y --no-install-recommends software-properties-common && \
    add-apt-repository ppa:git-core/ppa && \
    rm -rf /var/lib/apt/lists/*

# Merge locale generation and tool symlinks into the apt layer so all package
# side-effects are committed in one filesystem layer.
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
    # Version control (latest available from git-core PPA)
    git \
    # Python and pip (version controlled via PYTHON_VERSION build arg)
    python${PYTHON_VERSION} \
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
    && locale-gen en_US.UTF-8 \
    && update-locale LANG=en_US.UTF-8 \
    && ln -sf /usr/bin/clangd-18 /usr/bin/clangd \
    && ln -sf /usr/bin/clang-format-18 /usr/bin/clang-format \
    && ln -sf /usr/bin/python${PYTHON_VERSION} /usr/bin/python3 \
    && ln -sf /usr/bin/python3 /usr/bin/python \
    && rm -rf /var/lib/apt/lists/*

# =============================================================================
# CMAKE INSTALLATION
# =============================================================================

# Download and install CMake (architecture-aware)
# Note: Ensure /usr/local/man is a directory (not a file) before installing
RUN case "${TARGETARCH}" in \
        amd64) CMAKE_ARCH="x86_64" ;; \
        arm64) CMAKE_ARCH="aarch64" ;; \
        *) echo "Unsupported architecture: ${TARGETARCH}"; exit 1 ;; \
    esac && \
    rm -f /usr/local/man && mkdir -p /usr/local/man && \
    cd /tmp && \
    wget -q "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-${CMAKE_ARCH}.tar.gz" && \
    tar -xf cmake-${CMAKE_VERSION}-linux-${CMAKE_ARCH}.tar.gz && \
    cp -r cmake-${CMAKE_VERSION}-linux-${CMAKE_ARCH}/* /usr/local/ && \
    rm -rf /tmp/cmake-*

# =============================================================================
# DOXYGEN INSTALLATION
# =============================================================================

# Build Doxygen from source to use the exact DOXYGEN_VERSION on all architectures.
# (The official pre-built binary is x86_64-only; the Ubuntu apt package ships an older version.)
# Required build tools (cmake, flex, bison, g++) are already installed above.
RUN cd /tmp && \
    wget -q "https://www.doxygen.nl/files/doxygen-${DOXYGEN_VERSION}.src.tar.gz" && \
    tar -xf doxygen-${DOXYGEN_VERSION}.src.tar.gz && \
    cmake -S doxygen-${DOXYGEN_VERSION} -B doxygen-${DOXYGEN_VERSION}/build \
          -DCMAKE_BUILD_TYPE=Release -Dbuild_doc=OFF -Dbuild_wizard=OFF && \
    cmake --build doxygen-${DOXYGEN_VERSION}/build -j$(nproc) && \
    cmake --install doxygen-${DOXYGEN_VERSION}/build && \
    rm -rf /tmp/doxygen-*

# =============================================================================
# PYTHON PACKAGES
# =============================================================================

# Install Python packages for running scripts
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

# Create user/group and set workspace ownership in a single layer to avoid
# duplicating inode metadata across separate chown layers.
RUN set -e && \
    if ! getent group ${USER_GID} >/dev/null; then \
        groupadd --gid ${USER_GID} ${USERNAME}; \
    fi && \
    if ! getent passwd ${USER_UID} >/dev/null; then \
        useradd --uid ${USER_UID} --gid ${USER_GID} -m ${USERNAME}; \
    else \
        usermod -g ${USER_GID} -l ${USERNAME} $(getent passwd ${USER_UID} | cut -d: -f1) || true; \
    fi && \
    echo ${USERNAME} ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/${USERNAME} && \
    chmod 0440 /etc/sudoers.d/${USERNAME} && \
    chown -R ${USERNAME}:${USER_GID} /workspace

# =============================================================================
# ESP-IDF INSTALLATION
# =============================================================================

# IDF_PATH and IDF_TOOLS_PATH in one layer; PATH references the already-committed IDF_PATH
ENV IDF_PATH=/opt/esp-idf \
    IDF_TOOLS_PATH=/opt/esp-idf-tools
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
    rm -rf ${IDF_TOOLS_PATH}/dist ${IDF_PATH}/docs && \
    chown -R ${USERNAME}:${USER_GID} ${IDF_PATH} ${IDF_TOOLS_PATH}

# =============================================================================
# FINAL CONFIGURATION
# =============================================================================

# Write all profile.d scripts in one layer
RUN printf 'export IDF_PATH=/opt/esp-idf\nsource /opt/esp-idf/export.sh\n' \
        > /etc/profile.d/esp-idf.sh \
    && chmod +x /etc/profile.d/esp-idf.sh \
    && printf 'export DOCKER_HOST_IP_ADDRESS=`/usr/bin/getent ahostsv4 host.docker.internal | tail -n 1 | awk '"'"'{ print $1 }'"'"'`\n' \
        > /etc/profile.d/docker-host-ip.sh

# Switch to development user
USER ${USERNAME}

# Configure git, bash prompt and completions in a single user-layer
RUN mkdir -p ~/.git-completion \
    && curl -fsSL -o ~/.git-completion/git-completion.bash \
        https://raw.githubusercontent.com/git/git/master/contrib/completion/git-completion.bash \
    && curl -fsSL -o ~/.git-completion/git-prompt.sh \
        https://raw.githubusercontent.com/git/git/master/contrib/completion/git-prompt.sh \
    && printf '%s\n' \
        '# Git bash completion and prompt' \
        'source ~/.git-completion/git-completion.bash' \
        'source ~/.git-completion/git-prompt.sh' \
        'export GIT_PS1_SHOWDIRTYSTATE=1' \
        'export GIT_PS1_SHOWSTASHSTATE=1' \
        'export GIT_PS1_SHOWUNTRACKEDFILES=1' \
        'export GIT_PS1_SHOWUPSTREAM="auto"' \
        'export PS1='\''\[\033[01;32m\]\u@\h\[\033[00m\]:\[\033[01;34m\]\w\[\033[01;31m\]$(__git_ps1 " [%s]")\[\033[00m\]\n\$ '\''' \
        >> ~/.bashrc \
    && git config --global color.ui true \
    && git config --global diff.tool vscode \
    && git config --global difftool.prompt false \
    && git config --global difftool.vscode.cmd 'code --wait --diff "$LOCAL" "$REMOTE"' \
    && git config --global merge.tool vscode \
    && git config --global mergetool.prompt false \
    && git config --global mergetool.keepBackup false \
    && git config --global mergetool.vscode.cmd 'code --wait --merge $REMOTE $LOCAL $BASE $MERGED' \
    && git config --global core.editor 'code --wait' \
    && git config --global rerere.enabled true \
    && git config --global pull.ff true

# Set default command
CMD ["/bin/bash"]