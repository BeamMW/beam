#!/bin/bash

cd $HOME

REPO=https://${GITHUB_API_KEY}@github.com/beam-mw/beam-builds

git config --global user.email "vadim@beam-mw.com"
git config --global user.name "Vadim Grigoruk"
git clone --depth=1 $REPO
cd beam-builds

# create folders for the build
APP=beam
DATE_FOLDER=nightly/$(date +%Y.%m.%d)
RELEASE_FOLDER=$DATE_FOLDER/release

if [[ "$OSTYPE" == "linux"* ]]; then
    OS_FOLDER=$RELEASE_FOLDER/linux
    LATEST_OS_FOLDER=testnet/release/linux
elif [[ "$OSTYPE" == "darwin"* ]]; then
    OS_FOLDER=$RELEASE_FOLDER/mac
    LATEST_OS_FOLDER=testnet/release/mac
else
    echo "Error, OS:$OSTYPE not supported"
    exit 1
fi

mkdir -p $OS_FOLDER
mkdir -p $LATEST_OS_FOLDER

if [[ "$OSTYPE" == "linux"* ]]; then

	tar -cvf $OS_FOLDER/$APP.tar --directory=$HOME/build/beam-mw/beam/beam beam beam.cfg
	gzip -f $OS_FOLDER/$APP.tar

	cp -f $OS_FOLDER/$APP.tar.gz $LATEST_OS_FOLDER/$APP.tar.gz

	git add $OS_FOLDER/$APP.tar.gz $LATEST_OS_FOLDER/$APP.tar.gz

	cp -f "$HOME/build/beam-mw/beam/BeamWallet-0.0.1-Linux.deb" "$OS_FOLDER/Beam Wallet.deb"
	cp -f "$OS_FOLDER/Beam Wallet.deb" "$LATEST_OS_FOLDER/Beam Wallet.deb"
	git add "$OS_FOLDER/Beam Wallet.deb" "$LATEST_OS_FOLDER/Beam Wallet.deb"


elif [[ "$OSTYPE" == "darwin"* ]]; then
	tar -cvf $OS_FOLDER/$APP.tar --directory=$HOME/build/beam-mw/beam/beam beam beam.cfg
	gzip -f $OS_FOLDER/$APP.tar

	cp -f $OS_FOLDER/$APP.tar.gz $LATEST_OS_FOLDER/$APP.tar.gz

	git add $OS_FOLDER/$APP.tar.gz $LATEST_OS_FOLDER/$APP.tar.gz

	cp -f "$HOME/build/beam-mw/beam/BeamWallet-0.0.1-Darwin.dmg" "$OS_FOLDER/Beam Wallet.dmg"
	cp -f "$OS_FOLDER/Beam Wallet.dmg" "$LATEST_OS_FOLDER/Beam Wallet.dmg"
	git add "$OS_FOLDER/Beam Wallet.dmg" "$LATEST_OS_FOLDER/Beam Wallet.dmg"
fi

git commit -m "Travis build $TRAVIS_BUILD_NUMBER on $OSTYPE"
git push $REPO master

echo "Done"
