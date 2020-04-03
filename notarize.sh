#!/usr/bin/env bash
set -e

# Notarize the dmg
notarize_dmg() {(
  echo "Uploading $BEAM_WALLET_UI_IN to notarization service"
  uploadRes=$(xcrun altool --notarize-app --primary-bundle-id "com.mw.beam.beamwallet" --username "$MACOS_NOTARIZE_USER" --password "$MACOS_NOTARIZE_PASS" --file "$BEAM_WALLET_UI_IN" --verbose 2>&1)
  echo "Result: $uploadRes"
  uuid=$(echo "$uploadRes" | grep 'RequestUUID' | awk '{ print $3 }')
  if [ "$uuid" = "" ]; then
    echo "Uploading to notarization service error"
    exit 1
  else
    echo "Successfully uploaded to notarization service, polling for result: $uuid"
  fi
  sleep 15
  while :
  do
    fullstatus=$(xcrun altool --notarization-info "$uuid" --username "$MACOS_NOTARIZE_USER" --password "$MACOS_NOTARIZE_PASS" --verbose 2>&1)
    status=$(echo "$fullstatus" | grep 'Status\:' | awk '{ print $2 }')
    if [ "$status" = "success" ]; then
      xcrun stapler staple "$BEAM_WALLET_UI_IN"
      echo "Notarization success"
      return
    elif [ "$status" = "in" ]; then
      echo "Notarization still in progress, sleeping for 15 seconds and trying again"
      sleep 15
    else
      echo "Notarization failed fullstatus below"
      echo "$fullstatus"
      exit 1
    fi
  done
)}

notarize_dmg
