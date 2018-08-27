#!/bin/bash

cd $HOME

REPO=https://${GITHUB_API_KEY}@github.com/beam-mw/beam-builds

git config --global user.email "vadim@beam-mw.com"
git config --global user.name "Vadim Grigoruk"
git clone --depth=1 $REPO
cd beam-builds

# create folders for the build
BEAM_NODE=beam-node
BEAM_WALLET=beam-wallet
DATE_FOLDER=nightly/$(date +%Y.%m.%d)
RELEASE_FOLDER=$DATE_FOLDER/release

if [[ "$OSTYPE" == "linux"* ]]; then
    OS_FOLDER=$RELEASE_FOLDER/linux
    LATEST_OS_FOLDER=latest/release/linux
elif [[ "$OSTYPE" == "darwin"* ]]; then
    OS_FOLDER=$RELEASE_FOLDER/mac
    LATEST_OS_FOLDER=latest/release/mac
else
    echo "Error, OS:$OSTYPE not supported"
    exit 1
fi

mkdir -p $OS_FOLDER
mkdir -p $LATEST_OS_FOLDER

tar -cvzf $OS_FOLDER/$BEAM_NODE.tar.gz --directory=$HOME/build/beam-mw/beam/beam $BEAM_NODE $BEAM_NODE.cfg
cp -f $OS_FOLDER/$BEAM_NODE.tar.gz $LATEST_OS_FOLDER/$BEAM_NODE.tar.gz
git add $OS_FOLDER/$BEAM_NODE.tar.gz $LATEST_OS_FOLDER/$BEAM_NODE.tar.gz

tar -cvzf $OS_FOLDER/$BEAM_WALLET.tar.gz --directory=$HOME/build/beam-mw/beam/wallet $BEAM_WALLET $BEAM_WALLET.cfg
cp -f $OS_FOLDER/$BEAM_WALLET.tar.gz $LATEST_OS_FOLDER/$BEAM_WALLET.tar.gz
git add $OS_FOLDER/$BEAM_WALLET.tar.gz $LATEST_OS_FOLDER/$BEAM_WALLET.tar.gz

if [[ "$OSTYPE" == "linux"* ]]; then

	cp -f "$HOME/build/beam-mw/beam/BeamWallet-0.0.1-Linux.deb" "$OS_FOLDER/Beam Wallet.deb"
	cp -f "$OS_FOLDER/Beam Wallet.deb" "$LATEST_OS_FOLDER/Beam Wallet.deb"
	git add "$OS_FOLDER/Beam Wallet.deb" "$LATEST_OS_FOLDER/Beam Wallet.deb"

elif [[ "$OSTYPE" == "darwin"* ]]; then

	cp -f "$HOME/build/beam-mw/beam/BeamWallet-0.0.1-Darwin.dmg" "$OS_FOLDER/Beam Wallet.dmg"
	cp -f "$OS_FOLDER/Beam Wallet.dmg" "$LATEST_OS_FOLDER/Beam Wallet.dmg"
	git add "$OS_FOLDER/Beam Wallet.dmg" "$LATEST_OS_FOLDER/Beam Wallet.dmg"

fi

git commit -m "Travis build $TRAVIS_BUILD_NUMBER on $OSTYPE"
git push $REPO master

echo "Done"
