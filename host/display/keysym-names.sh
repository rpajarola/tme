#! /bin/sh

# host/display/keysym-names.sh - makes keysym-names.h, the table of X
# keysym names, from the X11 keysym headers:
#
#   sh keysym-names.sh /usr/include/X11 > keysym-names.h
#
# the table has the names in keysymdef.h and XF86keysym.h, the same
# keysyms that GDK knows.  like GDK, the names from XF86keysym.h don't
# have the XF86 prefix, unless they would then be the same as a name
# from keysymdef.h.  when several names have the same value,
# the name for the value is the first one in the headers, except for
# the names below, which are the ones that GDK 3 gives for their
# values.  keeping those makes keyboards map the same as they did
# when the display keyboard support used GDK:

x11=${1-/usr/include/X11}
tmp=${TMPDIR-/tmp}/keysym-names.$$

prefer="Hangul_SingleCandidate Hangul_MultipleCandidate
Hangul_PreviousCandidate Henkan Page_Up Page_Down Arabic_switch
KP_Page_Up dead_perispomeni Eth Ooblique Thorn ooblique kappa
Greek_IOTAdiaeresis Greek_LAMBDA Greek_lambda hebrew_kuf hebrew_taf"
prefer=`echo $prefer`

for header in keysymdef.h XF86keysym.h; do
    if test ! -f "$x11/$header"; then
	echo "$0: $x11/$header not found" 1>&2
	exit 1
    fi
done

# list each name with its value in decimal, whether it is preferred,
# and its position in the headers.  _EVDEVK(n) in XF86keysym.h is
# 0x10081000 + n:
cat "$x11/keysymdef.h" "$x11/XF86keysym.h" \
| awk -v prefer="$prefer" '
function hex(s,  i, c, v) {
  v = 0
  s = tolower(s)
  sub(/^0x/, "", s)
  for (i = 1; i <= length(s); i++) {
    c = index("0123456789abcdef", substr(s, i, 1))
    if (c == 0) {
      return -1
    }
    v = v * 16 + c - 1
  }
  return v
}
BEGIN {
  n = split(prefer, names)
  for (i = 1; i <= n; i++) {
    preferred[names[i]] = 1
  }
}
$1 == "#define" && $2 ~ /^(XK|XF86XK)_/ {
  name = $2
  if (sub(/^XF86XK_/, "", name)) {
    if (name in keysymdef) {
      name = "XF86" name
    }
  }
  else {
    sub(/^XK_/, "", name)
    keysymdef[name] = 1
  }
  value = $3
  if (value ~ /^_EVDEVK\(0x[0-9A-Fa-f]+\)$/) {
    gsub(/^_EVDEVK\(|\)$/, "", value)
    value = hex("10081000") + hex(value)
  }
  else {
    value = hex(value)
  }
  if (value < 0) {
    print "keysym-names.sh: bad value for " $2 ": " $3 > "/dev/stderr"
    exit 1
  }
  printf "%d %d %d %s\n", value, (name in preferred ? 0 : 1), NR, name
}' \
| LC_ALL=C sort -n -k1,1 -k2,2 -k3,3 \
| awk '{ printf "%d %s 0x%x\n", NR - 1, $4, $1 }' > "$tmp"

cat <<EOF
/* host/display/keysym-names.h - X keysym names: */

/* this file was made by keysym-names.sh from keysymdef.h and
   XF86keysym.h.  do not edit it. */

/* the names, in order of value, with the name to use for a value
   first: */
static const struct _tme_keysym_name _tme_keysym_names[] = {
EOF
awk '{ printf "  { %s, \"%s\" },\n", $3, $2 }' "$tmp"
cat <<EOF
};

/* the indices of the names above, in order of name: */
static const unsigned short _tme_keysym_names_by_name[] = {
EOF
LC_ALL=C sort -k2,2 "$tmp" | awk '{ printf "  %d,\n", $1 }'
echo "};"
rm -f "$tmp"
