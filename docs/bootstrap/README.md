# Bootstrap using docker

Create multiple replicator containers in a single docker network on a single host.

## Pre-requisites
- docker

## Build replicator docker image

In the `cpp-xpx-storage-sdk` directory:

```bash
docker build -t xpx-storage-replicator -f .ci/Dockerfile .
```

## Create docker network

```bash
docker network create -d bridge xpx-so-network
```

## Create replicator container in docker network

E.g. create 3 replicator in the same docker network:

```bash
docker run --name replicator-1 \
    -d \
    -p 5550:5550 \
    --network xpx-so-network \
    xpx-storage-replicator:latest

docker run --name replicator-1 \
    -d \
    -p 5551:5550 \
    --network xpx-so-network \
    xpx-storage-replicator:latest

docker run --name replicator-3 \
    -d \
    -p 5552:5550 \
    --network xpx-so-network \
    xpx-storage-replicator:latest
```