#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Run as root on a freshly built module so the kernel will load it: sign it
# with the enrolled akmods Machine Owner Key (Secure Boot) and label it for
# SELinux. Used by install-system-boot.sh and system-start.sh.
set -euo pipefail
export PATH=/usr/bin:/usr/sbin
[[ $# == 2 ]] || { echo "Usage: $0 MODULE KERNEL-RELEASE" >&2; exit 2; }
module=$1
release=$2
build=/lib/modules/$release/build
key=/etc/pki/akmods/private/private_key.priv
cert=/etc/pki/akmods/certs/public_key.der
if [[ -r $key && -r $cert ]]; then
    hash=$(sed -n 's/^CONFIG_MODULE_SIG_HASH="\(.*\)"$/\1/p' "$build/.config")
    "$build/scripts/sign-file" "${hash:-sha256}" "$key" "$cert" "$module"
elif [[ $(cat /sys/module/module/parameters/sig_enforce 2>/dev/null) == Y ||
        $(cat /sys/kernel/security/lockdown 2>/dev/null) == *'[integrity]'* ||
        $(cat /sys/kernel/security/lockdown 2>/dev/null) == *'[confidentiality]'* ]]; then
    echo "Secure Boot requires signed modules, but no akmods key was found at $key." >&2
    echo 'Create and enroll one: sudo kmodgenca -a && sudo mokutil --import /etc/pki/akmods/certs/public_key.der' >&2
    exit 1
fi
# SELinux only lets insmod (kmod_t) load files labelled modules_object_t.
if selinuxenabled 2>/dev/null; then
    chcon -t modules_object_t -- "$module"
fi
