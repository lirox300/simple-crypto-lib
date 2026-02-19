import sys
import ctypes
import os


def main():
    if len(sys.argv) != 5:
        sys.exit(1)

    lib_path = sys.argv[1]
    key_char = sys.argv[2]
    src_file = sys.argv[3]
    dst_file = sys.argv[4]

    if len(key_char) != 1:
        sys.exit(1)

    try:
        lib = ctypes.CDLL(os.path.abspath(lib_path))
    except OSError:
        sys.exit(1)

    lib.cezare_key.argtypes = [ctypes.c_char]
    lib.cezare.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int]

    lib.cezare_key(key_char.encode("utf-8"))

    try:
        with open(src_file, "rb") as f:
            src_data = f.read()
    except IOError:
        sys.exit(1)

    length = len(src_data)

    if length == 0:
        try:
            with open(dst_file, "wb") as f:
                pass
        except IOError:
            pass
        sys.exit(0)

    dst_buffer = ctypes.create_string_buffer(length)

    lib.cezare(src_data, dst_buffer, length)

    try:
        with open(dst_file, "wb") as f:
            f.write(dst_buffer.raw)
    except IOError:
        sys.exit(1)


if __name__ == "__main__":
    main()
