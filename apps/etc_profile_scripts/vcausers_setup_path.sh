#!/bin/bash

# add /usr/sbin to PATH if not already there and user is in vcausers group
! grep -qE '(^|:)/usr/sbin(:|$)' <<< "$PATH" \
  && groups | grep -q vcausers \
  && export PATH="$PATH:/usr/sbin"
true
