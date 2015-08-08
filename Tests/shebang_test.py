import codecs
import os
import subprocess
import sys
import tempfile
import unittest
import win32file

class Test_shebang(unittest.TestCase):
    def _write(self, name, line, encoding):
        fn = os.path.join(tempfile.gettempdir(), name)
        f = win32file.open(fn, writable=True, delete_on_close=True, encoding=encoding)
        f.writelines([line + '\r\n', '\r\n'])
        f.flush()
        return f
    
    def _run(self, args):
        os.environ["PYLAUNCHER_NOLAUNCH"] = "1"
        os.environ["PYLAUNCHER_VERBOSE"] = "1"
        try:
            out = subprocess.check_output(
                [self._python] + args,
                stderr=subprocess.STDOUT,
            )
        finally:
            del os.environ["PYLAUNCHER_NOLAUNCH"]
            del os.environ["PYLAUNCHER_VERBOSE"]
        res = out.decode('utf-8')
        print(res)
        return res

    def __init__(self, methodName = 'runTest'):
        super().__init__(methodName)
        self._python = os.path.abspath(os.path.join(os.path.split(__file__)[0], '..', 'Debug', 'python.exe'))
        self.version = "{0[0]}.{0[1]}{1}".format(sys.version_info, '-32' if sys.maxsize < 2**32 else '')

    def test_ascii_version_shebang(self):
        with self._write("plain-ascii.py", "#! python" + self.version, 'ascii') as f:
            out = self._run([f.path])
            last_line = out.splitlines()[-1]
            self.assertEqual(
                "Selected: {} {}".format(sys.executable, f.path),
                last_line
            )

    def test_utf8_version_shebang(self):
        with self._write("plain-utf8.py", "#! python" + self.version, 'utf-8-sig') as f:
            out = self._run([f.path])
            last_line = out.splitlines()[-1]
            self.assertEqual(
                "Selected: {} {}".format(sys.executable, f.path),
                last_line
            )

    def test_utf16_version_shebang(self):
        with self._write("plain-utf16.py", "#! python" + self.version, 'utf-16') as f:
            out = self._run([f.path])
            last_line = out.splitlines()[-1]
            self.assertEqual(
                "Selected: {} {}".format(sys.executable, f.path),
                last_line
            )

    def test_ignore_shebang(self):
        with self._write("ignore.py", "#! python2.7 ignored", 'ascii') as f:
            out = self._run([self.version, f.path, "notignored"])
            last_line = out.splitlines()[-1]
            self.assertEqual(
                "Selected: {} {} {}".format(sys.executable, f.path, "notignored"),
                last_line
            )

    def test_invalid_filename(self):
        out = self._run(["CON1"])
        last_line = out.splitlines()[-1]
        self.assertTrue(' CON1' in last_line)

    def test_cmdline_shebang(self):
        cmd = '"C:\\Program Files\\IronPython\\ipy.exe" -X:Frames'
        with self._write("ironpython.py", "#! " + cmd, 'utf-8') as f:
            out = self._run([f.path])
            last_line = out.splitlines()[-1]
            self.assertEqual(
                "Selected: {} {}".format(cmd, f.path),
                last_line
            )

if __name__ == '__main__':
    unittest.main()
