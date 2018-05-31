#!/bin/bash

pwd

mkdir $HOME/beam-builds

tar -czvf $HOME/beam.tar.gz ./README.md

cd $HOME

git config --global user.email "vadim@beam-mw.com"
git config --global user.name "Vadim Grigoruk"
git clone https://${GITHUB_API_KEY}@github.com/beam-mw/beam-builds
cd beam-builds

cp $HOME/beam.tar.gz ./beam.tar.gz

git add ./beam.tar.gz
git commit -m "Travis build $TRAVIS_BUILD_NUMBER"
git push https://${GITHUB_API_KEY}@github.com/beam-mw/beam-builds master

echo "Done\n"

# setup_git()
# {
#     git config --global user.email "vadim@beam-mw.com"
#     git config --global user.name "Vadim Grigoruk"
# }

# commit_build()
# {
#     # create folders for the build

#     APP=beam
#     DATE_FOLDER=nightly/$(date +%Y.%m.%d)
#     RELEASE_FOLDER=$DATE_FOLDER/release

#     if [[ "$OSTYPE" == "linux"* ]]; then
#         OS_FOLDER=$RELEASE_FOLDER/linux
#     elif [[ "$OSTYPE" == "darwin"* ]]; then
#         OS_FOLDER=$RELEASE_FOLDER/mac
#     else
#         echo "Error, OS:$OSTYPE not supported"
#         exit 1
#     fi

#     # checkout to 'nightly-builds' branch and add 
#     git fetch
#     git checkout nightly-builds
#     git pull

#     mkdir -p $OS_FOLDER
#     # compress the build

#     tar -czf $OS_FOLDER/$APP.tar.gz ./README.md 
#     #--directory=$APP beam.cpp

#     git add -f $OS_FOLDER/$APP.tar.gz
#     git commit --message "Travis build: $DATE_FOLDER on $OSTYPE"
#     git push
# }

# setup_git
# commit_build
