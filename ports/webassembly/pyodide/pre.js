const Hiwire = {};
const Tests = {};
const API = {};
Module.hiwire = Hiwire;
API.fatal_error = function (e) {
  console.warn("fatal error!");
  throw e;
};
Module.API = API;
const getTypeTag = (x) => Object.prototype.toString.call(x);

const _whitespace_only_re = /^[ \t]+$/gm;
const _leading_whitespace_re = /(^[ \t]*)(?:[^ \t\n])/gm;
/*
Remove any common leading whitespace from every line in `text`.

This can be used to make triple-quoted strings line up with the left
edge of the display, while still presenting them in the source code
in indented form.

Note that tabs and spaces are both treated as whitespace, but they
are not equal: the lines "  hello" and "\\thello" are
considered to have no common leading whitespace.

Entirely blank lines are normalized to a newline character.
*/
API.dedent = function (text) {
  // Look for the longest leading string of spaces and tabs common to
  // all lines.
  let margin;
  text = text.replaceAll(_whitespace_only_re, "");
  for (let [_, indent] of text.matchAll(_leading_whitespace_re)) {
    if (margin === undefined) {
      margin = indent;
      continue;
    }
    if (indent.startsWith(margin)) {
      // Current line more deeply indented than previous winner:
      // no change (previous winner is still on top).
      continue;
    }
    if (margin.startsWith(indent)) {
      // Current line consistent with and no deeper than previous winner:
      // it's the new winner.
      margin = indent;
      continue;
    }
    // Find the largest common whitespace between current line and previous
    // winner.
    for (let i = 0; i < margin.length; i++) {
      if (margin[i] != indent[i]) {
        margin = margin.slice(0, i);
        break;
      }
    }
  }
  if (margin) {
    text = text.replaceAll(RegExp("^" + margin, "mg"), "");
  }
  return text;
};
