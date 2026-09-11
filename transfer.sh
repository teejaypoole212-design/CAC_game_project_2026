#!/bin/bash

DEST="/mnt/hdd1/nas_share"

# Make sure the destination exists
if [ ! -d "$DEST" ]; then
    echo "Error: Destination $DEST does not exist"
    exit 1
fi

# Copy only .h and .cpp files from the current directory
count=0
for f in *.h *.cpp; do
    # Skip if the glob didn't match anything
    [ -e "$f" ] || continue

    cp -v "$f" "$DEST/"
    ((count++))
done

echo "Done. Copied $count file(s) to $DEST"
