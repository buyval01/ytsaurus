# Arguments used in FROM statements need to be declared before the first FROM statement.

# Args for ytsaurus-server-override.
ARG BASE_REPOSITORY="ghcr.io/ytsaurus/ytsaurus-nightly"
ARG BASE_IMAGE="latest"

# Args for base-server-crio
ARG K8S_VERSION="v1.32"
ARG CRIO_VERSION="v1.32"

# Args for odin.
ARG PYTHON_RUNTIME_VERSION="3.11"

ARG ODIN_ROOT="/ytsaurus"
ARG ODIN_VIRTUAL_ENV="${ODIN_ROOT}/virtualenv"
ARG ODIN_RUNTIME_ROOT="/var/odin"
ARG ODIN_CHECKS_DIR="${ODIN_RUNTIME_ROOT}/checks"
ARG ODIN_CHECKS_DATA_DIR="${ODIN_RUNTIME_ROOT}/checks-data"

# Flow control args.
# copy-ytsaurus-python-binaries or build-ytsaurus-python-binaries
ARG PYTHON_BUILD_BASE="copy-ytsaurus-python-binaries"
# base-server or base-server-crio
ARG SERVER_IMAGE_BASE="base-server"

##########################################################################################

FROM ubuntu:focal AS ubuntu-base

ARG APT_MIRROR="http://archive.ubuntu.com/"

RUN sed -i "s|http://archive.ubuntu.com/|${APT_MIRROR}|g" /etc/apt/sources.list

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get install -y \
      python3-pip \
      python3.8  \
      python3.8-distutils \
      linux-tools-generic

##########################################################################################

FROM golang:1.22.12 AS pprof-builder
ENV CGO_ENABLED=0
ENV GOBIN=/
RUN go install github.com/google/pprof@v0.0.0-20200417002340-c6e0a841f49a

##########################################################################################

FROM ubuntu-base AS base

USER root

WORKDIR /tmp

RUN <<-EOT
#!/bin/bash
set -eux -o pipefail

export DEBIAN_FRONTEND=noninteractive
export TZ=Etc/UTC

apt-get update -q --error-on=any
apt-get install -y curl

# https://launchpad.net/~deadsnakes/+archive/ubuntu/ppa
mkdir -p /etc/apt/keyrings
curl -fsSL "https://keyserver.ubuntu.com/pks/lookup?op=get&search=0xF23C5A6CF475977595C89F51BA6932366A755776" |
  tee /etc/apt/keyrings/deadsnakes-ubuntu-ppa.asc

cat <<EOF >/etc/apt/sources.list.d/deadsnakes-ubuntu-ppa-focal.list
deb [signed-by=/etc/apt/keyrings/deadsnakes-ubuntu-ppa.asc] https://ppa.launchpadcontent.net/deadsnakes/ppa/ubuntu focal main
# deb-src [signed-by=/etc/apt/keyrings/deadsnakes-ubuntu-ppa.asc] https://ppa.launchpadcontent.net/deadsnakes/ppa/ubuntu focal main
EOF

apt-get update -q --error-on=any

packages=(
  curl
  dnsutils
  gdb
  iproute2
  iputils-ping
  less
  libidn11-dev
  lsb-release
  lsof
  python3.11
  python3.11-venv
  strace
  telnet
  tini
  unzip
  zstd
  google-perftools
  procps
  jq
  htop
  vim
  ncdu
  iperf
  netcat-openbsd
  tcpdump
  wget
  llvm
  git
)

apt-get install -y "${packages[@]}"

apt-get clean
rm -rf /var/lib/apt/lists/*

EOT

COPY --from=pprof-builder /pprof /usr/local/bin/pprof

##########################################################################################

FROM base AS job-environment

# Libpython shared objects that are needed for YQL system python UDFs to work.
RUN apt-get update && DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get install -y \
  libpython3.8 \
  libpython3.9 \
  libpython3.10 \
  libpython3.11 \
  libpython3.12

RUN apt-get update && DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get install -y \
  openjdk-11-jdk \
  openjdk-17-jdk \
  java-common

RUN HOST_ARCH=$(dpkg --print-architecture) \
  && ln -s /usr/lib/jvm/java-11-openjdk-${HOST_ARCH} /opt/jdk11 \
  && ln -s /usr/lib/jvm/java-17-openjdk-${HOST_ARCH} /opt/jdk17 \
  && update-java-alternatives --set java-1.11.0-openjdk-${HOST_ARCH}

##########################################################################################

FROM base AS build-ytsaurus-python-binaries

ARG PROTOC_VERSION="3.20.1"

RUN PROTOC_ARCH=$(uname -m | sed -e 's/aarch64/aarch_64/') \
    && curl -sL -o protoc.zip https://github.com/protocolbuffers/protobuf/releases/download/v${PROTOC_VERSION}/protoc-${PROTOC_VERSION}-linux-${PROTOC_ARCH}.zip \
    && unzip protoc.zip -d /usr/local \
    && rm protoc.zip

ARG SOURCE_ROOT=/ytsaurus

RUN mkdir -p ${SOURCE_ROOT}
COPY ./yt/python ${SOURCE_ROOT}/yt/python
COPY ./yt/yt/python ${SOURCE_ROOT}/yt/yt/python
COPY ./certs/cacert.pem ${SOURCE_ROOT}/certs/cacert.pem

ARG BUILD_ROOT=/tmp

RUN mkdir -p ${BUILD_ROOT}
COPY ./artifacts/libyson_lib.so ${BUILD_ROOT}/yt/yt/python/yson_shared/libyson_lib.so
COPY ./artifacts/libdriver_lib.so ${BUILD_ROOT}/yt/yt/python/driver/native_shared/libdriver_lib.so
COPY ./artifacts/libdriver_rpc_lib.so ${BUILD_ROOT}/yt/yt/python/driver/rpc_shared/libdriver_rpc_lib.so

RUN python3.11 -m venv package-env
RUN . package-env/bin/activate; \
    ${SOURCE_ROOT}/yt/python/packages/build_ytsaurus_packages.sh --ytsaurus-source-path ${SOURCE_ROOT} --ytsaurus-build-path ${BUILD_ROOT} --apply-auditwheel

##########################################################################################

FROM base AS copy-ytsaurus-python-binaries

COPY ./ytsaurus_python /tmp/ytsaurus_python

##########################################################################################

FROM ${PYTHON_BUILD_BASE} AS base-ytsaurus-python-packages

RUN for package in client yson local native_driver; \
  do \
    dist_dir="/tmp/ytsaurus_python/ytsaurus_${package}_dist"; \
    wheel_path="${dist_dir}/$(ls ${dist_dir} | grep "^ytsaurus_$package.*whl$")"; \
    python3.8 -m pip install ${wheel_path}; \
  done

RUN python3.8 -m pip install packaging==24.1

RUN ln -s /usr/local/bin/yt /usr/bin/yt -f

RUN rm -rf /tmp/ytsaurus_python

##########################################################################################

FROM base-ytsaurus-python-packages AS base-server

RUN apt-get update && DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get install -y \
  containerd \
  openjdk-11-jdk \
  openjdk-17-jdk \
  java-common

RUN HOST_ARCH=$(dpkg --print-architecture) \
  && ln -s /usr/lib/jvm/java-11-openjdk-${HOST_ARCH} /opt/jdk11 \
  && ln -s /usr/lib/jvm/java-17-openjdk-${HOST_ARCH} /opt/jdk17 \
  && update-java-alternatives --set java-1.11.0-openjdk-${HOST_ARCH}

# Default python to be used by python3 jobs, for compatibility with jupyter tutorial.
RUN ln -s /usr/bin/python3.8 /usr/bin/python3 -f
# Force lsb_release to use python it was born to use.
RUN sed -i 's/python3/python3.8/' /usr/bin/lsb_release

##########################################################################################

FROM base-server AS base-server-crio

ARG K8S_VERSION
ARG CRIO_VERSION

ADD --chmod=644 https://pkgs.k8s.io/core:/stable:/${K8S_VERSION}/deb/Release.key \
            /etc/apt/keyrings/kubernetes-apt-keyring.asc

ADD --chmod=644 https://download.opensuse.org/repositories/isv:/cri-o:/stable:/${CRIO_VERSION}/deb/Release.key \
            /etc/apt/keyrings/cri-o-apt-keyring.asc

# Install CRI-O, see https://cri-o.io/
RUN <<-EOT
#!/bin/bash
set -eux -o pipefail

cat <<EOF >/etc/apt/sources.list.d/kubernetes.list
deb [signed-by=/etc/apt/keyrings/kubernetes-apt-keyring.asc] https://pkgs.k8s.io/core:/stable:/${K8S_VERSION}/deb/ /
EOF

cat <<EOF >/etc/apt/sources.list.d/cri-o.list
deb [signed-by=/etc/apt/keyrings/cri-o-apt-keyring.asc] https://download.opensuse.org/repositories/isv:/cri-o:/stable:/${CRIO_VERSION}/deb/ /
EOF

export DEBIAN_FRONTEND=noninteractive
apt-get update -q --error-on=any
apt-get install -y cri-tools cri-o
EOT

##########################################################################################

FROM ${SERVER_IMAGE_BASE} AS ytsaurus-bloated

# YTsaurus binary.
COPY ./ytserver-all /usr/bin/ytserver-all
RUN ln -s /usr/bin/ytserver-all /usr/bin/ytserver-master && \
    ln -s /usr/bin/ytserver-all /usr/bin/ytserver-master-cache && \
    ln -s /usr/bin/ytserver-all /usr/bin/ytserver-clock && \
    ln -s /usr/bin/ytserver-all /usr/bin/ytserver-discovery && \
    ln -s /usr/bin/ytserver-all /usr/bin/ytserver-node && \
    ln -s /usr/bin/ytserver-all /usr/bin/ytserver-job-proxy && \
    ln -s /usr/bin/ytserver-all /usr/bin/ytserver-exec && \
    ln -s /usr/bin/ytserver-all /usr/bin/ytserver-proxy && \
    ln -s /usr/bin/ytserver-all /usr/bin/ytserver-http-proxy && \
    ln -s /usr/bin/ytserver-all /usr/bin/ytserver-kafka-proxy && \
    ln -s /usr/bin/ytserver-all /usr/bin/ytserver-tools && \
    ln -s /usr/bin/ytserver-all /usr/bin/ytserver-scheduler && \
    ln -s /usr/bin/ytserver-all /usr/bin/ytserver-controller-agent && \
    ln -s /usr/bin/ytserver-all /usr/bin/ytserver-queue-agent && \
    ln -s /usr/bin/ytserver-all /usr/bin/ytserver-tcp-proxy && \
    ln -s /usr/bin/ytserver-all /usr/bin/ytserver-tablet-balancer

# YTsaurus credits files.
COPY ./credits/ytserver-all.CREDITS /usr/bin/ytserver-all.CREDITS

# Scripts for init jobs.
COPY ./init_queue_agent_state.py /usr/bin/init_queue_agent_state
COPY ./init_operations_archive.py /usr/bin/init_operations_archive
RUN ln -s /usr/bin/init_operations_archive /usr/bin/init_operation_archive

FROM scratch AS ytsaurus
COPY --from=ytsaurus-bloated / /

##########################################################################################

FROM base AS chyt

# CHYT binaries.
COPY ./clickhouse-trampoline.py /usr/bin/clickhouse-trampoline
COPY ./ytserver-clickhouse /usr/bin/ytserver-clickhouse

# CHYT credits files.
COPY ./credits/ytserver-clickhouse.CREDITS /usr/bin/ytserver-clickhouse.CREDITS

# Install YT client for setup script.
RUN python3.8 -m pip install ytsaurus-client
RUN ln -s /usr/local/bin/yt /usr/bin/yt -f

# Setup script.
COPY ./setup_cluster_for_chyt.sh /setup_cluster_for_chyt.sh
RUN chmod 755 /setup_cluster_for_chyt.sh

##########################################################################################

FROM base-ytsaurus-python-packages AS base-query-tracker

# Libpython shared objects that are needed for YQL system python UDFs to work.
# TODO(aleksandr.gaev) use stubs instead. We don't need working libpythons here.
RUN apt-get update && DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get install -y \
  libpython3.8 \
  libpython3.9 \
  libpython3.10 \
  libpython3.11 \
  libpython3.12

##########################################################################################

FROM base-query-tracker AS query-tracker-bloated

# Query tracker binaries.
COPY ./ytserver-all /usr/bin/ytserver-all
RUN ln -s /usr/bin/ytserver-all /usr/bin/ytserver-query-tracker

COPY ./ytserver-yql-agent /usr/bin/ytserver-yql-agent
COPY ./mrjob /usr/bin/mrjob
COPY ./dq_vanilla_job /usr/bin/dq_vanilla_job
COPY ./dq_vanilla_job.lite /usr/bin/dq_vanilla_job.lite
COPY ./yql /usr/lib/yql

# Scripts for init jobs.
COPY ./init_query_tracker_state.py /usr/bin/init_query_tracker_state

# Query tracker credits files.
COPY ./credits/ytserver-all.CREDITS /usr/bin/ytserver-all.CREDITS

FROM scratch AS query-tracker
COPY --from=query-tracker-bloated / /

##########################################################################################

FROM base AS strawberry

# Strawberry binary.
COPY ./chyt-controller /usr/bin/strawberry-controller
RUN ln -s /usr/bin/strawberry-controller /usr/bin/chyt-controller

# Strawberry credits files.
COPY ./credits/chyt-controller.CREDITS /usr/bin/strawberry-controller.CREDITS

##########################################################################################

FROM ${SERVER_IMAGE_BASE} AS local-bloated

COPY ./ytserver-all /usr/bin/ytserver-all
COPY ./ytserver-yql-agent /usr/bin/ytserver-yql-agent
COPY ./credits/*.CREDITS /usr/bin
COPY ./mrjob /usr/bin/mrjob
COPY ./dq_vanilla_job /usr/bin/dq_vanilla_job
COPY ./dq_vanilla_job.lite /usr/bin/dq_vanilla_job.lite
COPY ./yql/* /usr/bin

# Libpython shared objects that are needed for YQL system python UDFs to work.
# TODO(aleksandr.gaev) use stubs instead. We don't need working libpythons here.
RUN apt-get update && DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get install -y \
  libpython3.8 \
  libpython3.9 \
  libpython3.10 \
  libpython3.11 \
  libpython3.12

FROM scratch AS local
COPY --from=local-bloated / /

WORKDIR /tmp

COPY ./configure.sh .
RUN ./configure.sh /var/lib/yt/local-cypress
RUN rm /tmp/configure.sh

COPY ./start.sh /usr/bin/start.sh

VOLUME /var/lib/yt/local-cypress
EXPOSE 80

ENTRYPOINT ["bash", "/usr/bin/start.sh"]

##########################################################################################

FROM ${BASE_REPOSITORY}:${BASE_IMAGE} AS ytsaurus-server-override-bloated

USER root

# Override binaries built from source.
COPY ./ytserver-all /usr/bin/ytserver-all
COPY ./init_queue_agent_state.py /usr/bin/init_queue_agent_state
COPY ./init_operations_archive.py /usr/bin/init_operations_archive

FROM scratch AS ytsaurus-server-override
COPY --from=ytsaurus-server-override-bloated / /

##########################################################################################

FROM base as odin-installer

ARG ODIN_ROOT
ARG ODIN_VIRTUAL_ENV
ARG ODIN_CHECKS_DIR
ARG ODIN_CHECKS_DATA_DIR

ARG SOURCE_ROOT="${ODIN_ROOT}/ytsaurus"
ARG ODIN_SOURCE_ROOT="${SOURCE_ROOT}/yt/odin"
ARG BUILD_ROOT="${ODIN_ROOT}/build"
ARG PYTHON_ROOT="${ODIN_ROOT}/python"

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get install -y --no-install-recommends \
      ca-certificates \
      curl \
      build-essential \
      libcap-dev \
    && rm -rf /var/lib/apt/lists/*

COPY --link ./ ${SOURCE_ROOT}/

WORKDIR ${ODIN_ROOT}

RUN pip install virtualenv \
    && virtualenv ${ODIN_VIRTUAL_ENV}

RUN . ${ODIN_VIRTUAL_ENV}/bin/activate \
    && mkdir ${PYTHON_ROOT} \
    && cd ${SOURCE_ROOT} && pip install -e yt/python/packages \
    && cd ${PYTHON_ROOT} \
    && python ${ODIN_SOURCE_ROOT}/packages/prepare_python_modules.py --source-root "$SOURCE_ROOT" --output-path "$PYTHON_ROOT" \
    && cp "${ODIN_SOURCE_ROOT}/packages/setup.py" ./ && python setup.py bdist_wheel --universal

RUN . ${ODIN_VIRTUAL_ENV}/bin/activate \
    && pip install -r ${ODIN_SOURCE_ROOT}/requirements.txt \
    && pip install ytsaurus-client ytsaurus-yson ytsaurus-rpc-driver \
    && pip install $PYTHON_ROOT/dist/*

RUN mkdir -p ${ODIN_CHECKS_DIR} \
    && mkdir -p ${ODIN_CHECKS_DATA_DIR} \
    && for CHECK in $(find ${ODIN_SOURCE_ROOT}/checks/bin -mindepth 1 -maxdepth 1 -type d); do \
      CHECK=$(basename ${CHECK}); \
      mkdir -p ${ODIN_CHECKS_DIR}/${CHECK}; \
      ln -sf ${ODIN_VIRTUAL_ENV}/bin/${CHECK} ${ODIN_CHECKS_DIR}/${CHECK}/${CHECK}; \
    done

##########################################################################################

FROM base as odin

ARG ODIN_VIRTUAL_ENV
ENV ODIN_VIRTUAL_ENV $ODIN_VIRTUAL_ENV

ARG ODIN_RUNTIME_ROOT
ENV ODIN_RUNTIME_ROOT $ODIN_RUNTIME_ROOT

COPY --from=odin-installer ${ODIN_VIRTUAL_ENV}/ ${ODIN_VIRTUAL_ENV}/
COPY --from=odin-installer ${ODIN_RUNTIME_ROOT}/ ${ODIN_RUNTIME_ROOT}/

RUN mkdir -p ${ODIN_RUNTIME_ROOT}/log \
    && ln -sf /dev/stdout ${ODIN_RUNTIME_ROOT}/log/odin.log \
    && ln -sf /dev/stdout ${ODIN_RUNTIME_ROOT}/log/webservice.log

ENV PATH "${ODIN_VIRTUAL_ENV}/bin:${PATH}"
# This is important for some checks to work correctly and find odin modules.
ENV VIRTUAL_ENV ${ODIN_VIRTUAL_ENV}

WORKDIR ${ODIN_RUNTIME_ROOT}

##########################################################################################
