#!/bin/bash

cd $HOME

REPO=https://${GITHUB_API_KEY}@github.com/beam-mw/beam-builds

git config --global user.email "vadim@beam-mw.com"
git config --global user.name "Vadim Grigoruk"
git clone $REPO
cd beam-builds

# create folders for the build
APP=beam
DATE_FOLDER=nightly/$(date +%Y.%m.%d)
RELEASE_FOLDER=$DATE_FOLDER/release

if [[ "$OSTYPE" == "linux"* ]]; then
    OS_FOLDER=$RELEASE_FOLDER/linux
elif [[ "$OSTYPE" == "darwin"* ]]; then
    OS_FOLDER=$RELEASE_FOLDER/mac
else
    echo "Error, OS:$OSTYPE not supported"
    exit 1
fi

mkdir -p $OS_FOLDER

tar -czvf $OS_FOLDER/$APP.tar.gz --directory=$HOME/build/beam-mw/beam/beam beam

git add $OS_FOLDER/$APP.tar.gz
git commit -m "Travis build $TRAVIS_BUILD_NUMBER on $OSTYPE"
git push $REPO master

echo "Done"
