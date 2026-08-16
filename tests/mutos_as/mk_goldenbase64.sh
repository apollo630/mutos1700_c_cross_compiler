#!/bin/bash

for file in *.o.golden; do
    [ -e "$file" ] || continue
    base64 "$file" > "${file}_base64.txt"
done
