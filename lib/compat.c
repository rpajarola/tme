/* lib/compat.c - keeps libcompat from being empty: */

/* on hosts that need none of the replacement functions, libcompat
   would otherwise have no members, and some ar(1)s, like macOS's,
   refuse to make an empty archive: */
int _tme_compat_placeholder;
