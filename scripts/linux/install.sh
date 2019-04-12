#!/bin/bash

set -ev

SCRIPTDIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
source $SCRIPTDIR/../lib.sh

#Start our docker
docker pull calaos/calaos_base

docker run -t --name calaos_base -d \
    -v $(pwd):/calaos_base \
    calaos/calaos_base

docker ps

