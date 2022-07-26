#!/bin/sh
# $Id: yellow.sh 22454 2019-08-31 19:53:40Z yeti-dn $
# An extremely simple Gwyddion plug-in example in shell.  Demonstrates data
# and metadata not outputted are retained from the original data.
# Written by Yeti <yeti@gwyddion.net>.
# Public domain.
case "$1" in
    register)
    # Plug-in info.
    echo "yellow"
    echo "/_Test/Set Palette to _Yellow (shell)"
    echo "noninteractive with_defaults"
    ;;

    run)
    run=$2
    # We don't need to read the input when the output doesn't depend on it.
    echo "/0/base/palette=Yellow" >$3
    ;;

    *)
    echo "*** Error: plug-in has to be called from Gwyddion plugin-proxy." 1>&2
    exit 1
    ;;
esac
