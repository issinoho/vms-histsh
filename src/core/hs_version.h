/* hs_version.h - the one place the release version is set. tools/kit.sh reads it
   for the PCSI kit (KIT.ENV) and MAKE_KIT.COM checks HISTSH.EXE reports it, so they
   can't drift apart (as vms-steward's st_version.h).

   HS_VERSION is semantic: major.minor.patch. HS_PCSI_VERSION is the same in PCSI
   form: T for a field test (V for a full release), T<major>.<minor>-<patch>. */
#ifndef HS_VERSION_H
#define HS_VERSION_H

#define HS_VERSION      "0.2.0"
#define HS_PCSI_VERSION "T0.2-0"

#endif
