#!/bin/sh
# $Id: dump.sh 22454 2019-08-31 19:53:40Z yeti-dn $
# An extremely simple Gwyddion plug-in example in shell.  Saves and loads
# the data exactly as got from plugin-proxy.
# Written by Yeti <yeti@gwyddion.net>.
# Public domain.
case "$1" in
    register)
    # Plug-in info.
    echo "dump"
    echo "Plug-in proxy format dump (.dump)"
    echo "*.dump"
    echo "load save"
    ;;

    load)
    dumpfile="$2"
    externfile="$3"
    cat "$externfile" >"$dumpfile"
    ;;

    save)
    dumpfile="$2"
    externfile="$3"
    cat "$dumpfile" >"$externfile"
    ;;

    *)
    echo "*** Error: plug-in has to be called from Gwyddion plugin-proxy." 1>&2
    exit 1
    ;;
esac

