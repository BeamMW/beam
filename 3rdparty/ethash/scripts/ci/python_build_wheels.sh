#!/usr/bin/env bash
# -*- coding: utf-8 -*-

# ethash: C/C++ implementation of Ethash, the Ethereum Proof of Work algorithm.
# Copyright 2019-2020 Pawel Bylica.
# Licensed under the Apache License, Version 2.0.

set -eo pipefail

if [ -n "$APPVEYOR" ]; then
    PYTHON_PATHS="/c/Python37-x64 /c/Python36-x64 /c/Python35-x64"
elif [ -n "$CIRCLECI" ]; then
    if [ "$OSTYPE" = "linux-gnu" ]; then
        PYTHON_PATHS="/opt/python/cp37-cp37m/bin /opt/python/cp36-cp36m/bin /opt/python/cp35-cp35m/bin"
    else
        # The continuation of the script expects "python" executable name,
        # so make link python -> python3.
        mkdir -p $HOME/bin
        ln -s $(which python3) $HOME/bin/python
        PYTHON_PATHS="$HOME/bin"
    fi
fi

PATH_ORIG=$PATH
for p in $PYTHON_PATHS
do
    PATH="$p:$PATH_ORIG"
    echo '***'
    python --version
    which python
    python -m pip --version
    echo '***'
    python -m pip install wheel
    python setup.py build_ext --skip-cmake-build
    python setup.py bdist_wheel --skip-build
done
