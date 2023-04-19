const IN_NODE = false;
/**
 * We call refreshStreams at the end of every update method, but refreshStreams
 * won't work until initializeStreams is called. So when INITIALIZED is false,
 * refreshStreams is a no-op.
 * @private
 */
let INITIALIZED = false;
// These can't be used until they are initialized by initializeStreams.
const ttyout_ops = {};
const ttyerr_ops = {};
const isattys = {};
/**
 * This is called at the end of loadPyodide to set up the streams. If
 * loadPyodide has been given stdin, stdout, stderr arguments they are provided
 * here. Otherwise, we set the default behaviors. This also fills in the global
 * state in this file.
 * @param stdin
 * @param stdout
 * @param stderr
 * @private
 */
API.initializeStreams = function (stdin, stdout, stderr) {
  setStdin({ stdin: stdin });
  if (stdout) {
    setStdout({ batched: stdout });
  } else {
    setDefaultStdout();
  }
  if (stderr) {
    setStderr({ batched: stderr });
  } else {
    setDefaultStderr();
  }
  // 5.0 and 6.0 are the device numbers that Emscripten uses (see library_fs.js).
  // These haven't changed in ~10 years. If we used different ones nothing would
  // break.
  var ttyout_dev = FS.makedev(5, 0);
  var ttyerr_dev = FS.makedev(6, 0);
  TTY.register(ttyout_dev, ttyout_ops);
  TTY.register(ttyerr_dev, ttyerr_ops);
  INITIALIZED = true;
  refreshStreams();
};
const textencoder = new TextEncoder();
const textdecoder = new TextDecoder();

function refreshStreams() {
  if (!INITIALIZED) {
    return;
  }
  FS.unlink("/dev/stdin");
  FS.unlink("/dev/stdout");
  FS.unlink("/dev/stderr");
  if (isattys.stdin) {
    FS.symlink("/dev/tty", "/dev/stdin");
  } else {
    FS.createDevice("/dev", "stdin", ttyout_ops.get_char);
  }
  if (isattys.stdout) {
    FS.symlink("/dev/tty", "/dev/stdout");
  } else {
    FS.createDevice(
      "/dev",
      "stdout",
      null,
      ttyout_ops.put_char.bind(undefined, undefined)
    );
  }
  if (isattys.stderr) {
    FS.symlink("/dev/tty", "/dev/stderr");
  } else {
    FS.createDevice(
      "/dev",
      "stderr",
      null,
      ttyerr_ops.put_char.bind(undefined, undefined)
    );
  }
  // Refresh std streams so they use our new versions
  FS.closeStream(0 /* stdin */);
  FS.closeStream(1 /* stdout */);
  FS.closeStream(2 /* stderr */);
  FS.open("/dev/stdin", 0 /* write only */);
  FS.open("/dev/stdout", 1 /* read only */);
  FS.open("/dev/stderr", 1 /* read only */);
}
/**
 * Sets the default stdin. If in node, stdin will read from `process.stdin`
 * and isatty(stdin) will be set to tty.isatty(process.stdin.fd).
 * If in a browser, this calls setStdinError.
 */
function setDefaultStdin() {
  if (IN_NODE) {
    var BUFSIZE_1 = 256;
    var buf_1 = Buffer.alloc(BUFSIZE_1);
    var fs_1 = require("fs");
    var tty = require("tty");
    var stdin = function () {
      var bytesRead;
      try {
        bytesRead = fs_1.readSync(process.stdin.fd, buf_1, 0, BUFSIZE_1, -1);
      } catch (e) {
        // Platform differences: on Windows, reading EOF throws an exception,
        // but on other OSes, reading EOF returns 0. Uniformize behavior by
        // catching the EOF exception and returning 0.
        if (e.toString().includes("EOF")) {
          bytesRead = 0;
        } else {
          throw e;
        }
      }
      if (bytesRead === 0) {
        return null;
      }
      return buf_1.subarray(0, bytesRead);
    };
    var isatty = tty.isatty(process.stdin.fd);
    setStdin({ stdin: stdin, isatty: isatty });
  } else {
    setStdinError();
  }
}
/**
 * Sets isatty(stdin) to false and makes reading from stdin always set an EIO
 * error.
 */
function setStdinError() {
  isattys.stdin = false;
  var get_char = function () {
    throw 0;
  };
  ttyout_ops.get_char = get_char;
  ttyerr_ops.get_char = get_char;
  refreshStreams();
}
/**
 * Set a stdin handler.
 *
 * The stdin handler is called with zero arguments whenever stdin is read and
 * the current input buffer is exhausted. It should return one of:
 *
 * - :js:data:`null` or :js:data:`undefined`: these are interpreted as end of file.
 * - a number
 * - a string
 * - an :js:class:`ArrayBuffer` or :js:class:`TypedArray` with
 *   :js:data:`~TypedArray.BYTES_PER_ELEMENT` equal to 1.
 *
 * If a number is returned, it is interpreted as a single character code. The
 * number should be between 0 and 255.
 *
 * If a string is returned, a new line is appended if one is not present and the
 * resulting string is turned into a :js:class:`Uint8Array` using
 * :js:class:`TextEncoder`.
 *
 * Returning a buffer is more efficient and allows returning partial lines of
 * text.
 *
 * @param options.stdin The stdin handler.
 * @param options.error If this is set to ``true``, attempts to read from stdin
 * will always set an IO error.
 * @param options.isatty Should :py:func:`isatty(stdin) <os.isatty>` be ``true``
 * or ``false`` (default ``false``).
 * @param options.autoEOF Insert an EOF automatically after each string or
 * buffer? (default ``true``).
 */
function setStdin(options) {
  if (options === void 0) {
    options = {};
  }
  if (options.stdin && options.error) {
    throw new TypeError(
      "Both a stdin handler provided and the error argument was set"
    );
  }
  if (options.error) {
    setStdinError();
    return;
  }
  if (options.stdin) {
    var autoEOF = options.autoEOF;
    autoEOF = autoEOF === undefined ? true : autoEOF;
    isattys.stdin = !!options.isatty;
    var get_char = make_get_char(options.stdin, autoEOF);
    ttyout_ops.get_char = get_char;
    ttyerr_ops.get_char = get_char;
    refreshStreams();
    return;
  }
  setDefaultStdin();
}
API.setStdin = setStdin;

/**
 * If in node, sets stdout to write directly to process.stdout and sets isatty(stdout)
 * to tty.isatty(process.stdout.fd).
 * If in a browser, sets stdout to write to console.log and sets isatty(stdout) to false.
 */
function setDefaultStdout() {
  if (IN_NODE) {
    var tty = require("tty");
    var raw = function (x) {
      return process.stdout.write(Buffer.from([x]));
    };
    var isatty = tty.isatty(process.stdout.fd);
    setStdout({ raw: raw, isatty: isatty });
  } else {
    setStdout({
      batched: function (x) {
        return console.log(x);
      },
    });
  }
}
/**
 * Sets the standard out handler. A batched handler or a raw handler can be
 * provided (both not both). If neither is provided, we restore the default
 * handler.
 *
 * @param options.batched A batched handler is called with a string whenever a
 * newline character is written is written or stdout is flushed. In the former
 * case, the received line will end with a newline, in the latter case it will
 * not.
 * @param options.raw A raw handler is called with the handler is called with a
 * `number` for each byte of the output to stdout.
 * @param options.isatty Should :py:func:`isatty(stdout) <os.isatty>` return
 * ``true`` or ``false``. Can only be set to ``true`` if a raw handler is
 * provided (default ``false``).
 */
function setStdout(options) {
  if (options === void 0) {
    options = {};
  }
  if (options.raw && options.batched) {
    throw new TypeError("Both a batched handler and a raw handler provided");
  }
  if (!options.raw && options.isatty) {
    throw new TypeError(
      "Cannot set isatty to true unless a raw handler is provided"
    );
  }
  if (options.raw) {
    isattys.stdout = !!options.isatty;
    Object.assign(ttyout_ops, make_unbatched_put_char(options.raw));
    refreshStreams();
    return;
  }
  if (options.batched) {
    isattys.stdout = false;
    Object.assign(ttyout_ops, make_batched_put_char(options.batched));
    refreshStreams();
    return;
  }
  setDefaultStdout();
}
API.setStdout = setStdout;

/**
 * If in node, sets stderr to write directly to process.stderr and sets isatty(stderr)
 * to tty.isatty(process.stderr.fd).
 * If in a browser, sets stderr to write to console.warn and sets isatty(stderr) to false.
 */
function setDefaultStderr() {
  if (IN_NODE) {
    var tty = require("tty");
    var raw = function (x) {
      return process.stderr.write(Buffer.from([x]));
    };
    var isatty = tty.isatty(process.stderr.fd);
    setStderr({ raw: raw, isatty: isatty });
  } else {
    setStderr({
      batched: function (x) {
        return console.warn(x);
      },
    });
  }
}
/**
 * Sets the standard error handler. A batched handler or a raw handler can be
 * provided (both not both). If neither is provided, we restore the default
 * handler.
 *
 * @param options.batched A batched handler is called with a string whenever a
 * newline character is written is written or stderr is flushed. In the former
 * case, the received line will end with a newline, in the latter case it will
 * not. isatty(stderr) is set to false (when using a batched handler, stderr is
 * buffered so it is impossible to make a tty with it).
 * @param options.raw A raw handler is called with the handler is called with a
 * `number` for each byte of the output to stderr.
 * @param options.isatty Should :py:func:`isatty(stderr) <os.isatty>` return
 * ``true`` or ``false``. Can only be set to ``true`` if a raw handler is
 * provided (default ``false``).
 */
function setStderr(options) {
  if (options === void 0) {
    options = {};
  }
  if (options.raw && options.batched) {
    throw new TypeError("Both a batched handler and a raw handler provided");
  }
  if (!options.raw && options.isatty) {
    throw new TypeError(
      "Cannot set isatty to true unless a raw handler is provided"
    );
  }
  if (options.raw) {
    isattys.stderr = !!options.isatty;
    Object.assign(ttyerr_ops, make_unbatched_put_char(options.raw));
    refreshStreams();
    return;
  }
  if (options.batched) {
    isattys.stderr = false;
    Object.assign(ttyerr_ops, make_batched_put_char(options.batched));
    refreshStreams();
    return;
  }
  setDefaultStderr();
}
API.setStderr = setStderr;


function make_get_char(infunc, autoEOF) {
  var index = 0;
  var buf = new Uint8Array(0);
  var insertEOF = false;
  // get_char has 3 particular return values:
  // a.) the next character represented as an integer
  // b.) undefined to signal that no data is currently available
  // c.) null to signal an EOF
  return function get_char() {
    try {
      if (index >= buf.length) {
        if (insertEOF) {
          insertEOF = false;
          return null;
        }
        var input = infunc();
        if (input === undefined || input === null) {
          return null;
        }
        if (typeof input === "number") {
          return input;
        } else if (typeof input === "string") {
          if (!input.endsWith("\n")) {
            input += "\n";
          }
          buf = textencoder.encode(input);
        } else if (ArrayBuffer.isView(input)) {
          if (input.BYTES_PER_ELEMENT !== 1) {
            throw new Error("Expected BYTES_PER_ELEMENT to be 1");
          }
          buf = input;
        } else if (
          Object.prototype.toString.call(input) === "[object ArrayBuffer]"
        ) {
          buf = new Uint8Array(input);
        } else {
          throw new Error(
            "Expected result to be undefined, null, string, array buffer, or array buffer view"
          );
        }
        if (buf.length === 0) {
          return null;
        }
        if (autoEOF) {
          insertEOF = true;
        }
        index = 0;
      }
      return buf[index++];
    } catch (e) {
      // emscripten will catch this and set an IOError which is unhelpful for
      // debugging.
      console.error("Error thrown in stdin:");
      console.error(e);
      throw e;
    }
  };
}
function make_unbatched_put_char(out) {
  return {
    put_char: function (tty, val) {
      out(val);
    },
    fsync: function () {},
  };
}
function make_batched_put_char(out) {
  var output = [];
  return {
    // get_char has 3 particular return values:
    // a.) the next character represented as an integer
    // b.) undefined to signal that no data is currently available
    // c.) null to signal an EOF,
    put_char: function (tty, val) {
      if (val === null || val === 10 /* charCode('\n') */) {
        out(textdecoder.decode(new Uint8Array(output)));
        output = [];
      } else {
        if (val !== 0) {
          output.push(val); // val == 0 would cut text output off in the middle.
        }
      }
    },
    fsync: function (tty) {
      if (output && output.length > 0) {
        out(textdecoder.decode(new Uint8Array(output)));
        output = [];
      }
    },
  };
}
