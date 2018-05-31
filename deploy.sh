#!/bin/bash

cd $HOME

git config --global user.email "vadim@beam-mw.com"
git config --global user.name "Vadim Grigoruk"
git clone https://${GITHUB_API_KEY}@github.com/beam-mw/beam-builds
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

tar -czf $OS_FOLDER/$APP.tar.gz $HOME/travis/build/beam-mw/beam/README.md

git add $OS_FOLDER/$APP.tar.gz
git commit -m "Travis build $TRAVIS_BUILD_NUMBER on $OSTYPE"
git push https://${GITHUB_API_KEY}@github.com/beam-mw/beam-builds master

echo "Done"
